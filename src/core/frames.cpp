#include "core/frames.h"

#include <algorithm>
#include <cstring>

namespace pet {

long lz4Decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap) {
  const uint8_t* ip = src;
  const uint8_t* const iend = src + srcLen;
  uint8_t* op = dst;
  uint8_t* const oend = dst + dstCap;
  while (ip < iend) {
    unsigned token = *ip++;
    size_t lit = token >> 4;
    if (lit == 15) {
      unsigned b;
      do {
        if (ip >= iend) return -1;
        b = *ip++;
        lit += b;
      } while (b == 255);
    }
    if ((size_t)(iend - ip) < lit || (size_t)(oend - op) < lit) return -1;
    std::memcpy(op, ip, lit);
    ip += lit;
    op += lit;
    if (ip >= iend) break;  // the last sequence has literals only
    if (iend - ip < 2) return -1;
    size_t off = ip[0] | (ip[1] << 8);
    ip += 2;
    if (off == 0 || off > (size_t)(op - dst)) return -1;
    size_t len = token & 15;
    if (len == 15) {
      unsigned b;
      do {
        if (ip >= iend) return -1;
        b = *ip++;
        len += b;
      } while (b == 255);
    }
    len += 4;
    if ((size_t)(oend - op) < len) return -1;
    const uint8_t* match = op - off;
    if (off >= len) {
      std::memcpy(op, match, len);
      op += len;
    } else {
      for (size_t i = 0; i < len; ++i) *op++ = match[i];  // overlapping copy
    }
  }
  return (long)(op - dst);
}

static unsigned rd16(const uint8_t* p) { return p[0] | (p[1] << 8); }
static uint32_t rd32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool parseBpf(const uint8_t* data, size_t n, BpfFrame* out, std::string* err) {
  auto fail = [&](const char* why) {
    if (err) *err = why;
    return false;
  };
  if (n < 18 || std::memcmp(data, "BPF1", 4) != 0) return fail("not a BPF1 frame");
  BpfFrame f;
  f.width = rd16(data + 4);
  f.height = rd16(data + 6);
  f.x0 = rd16(data + 8);
  f.y0 = rd16(data + 10);
  f.w = rd16(data + 12);
  f.h = rd16(data + 14);
  unsigned pc = rd16(data + 16);
  if (pc == 0 || pc > 256) return fail("bad palette size");
  if (f.x0 + f.w > f.width || f.y0 + f.h > f.height) return fail("box outside canvas");
  size_t pos = 18;
  if (n < pos + pc * 4 + 4) return fail("truncated palette");
  f.palette.resize(pc);
  for (unsigned i = 0; i < pc; ++i) f.palette[i] = rd32(data + pos + i * 4);
  pos += pc * 4;
  size_t clen = rd32(data + pos);
  pos += 4;
  if (n < pos + clen) return fail("truncated data");
  f.index.resize((size_t)f.w * f.h);
  long got = lz4Decompress(data + pos, clen, f.index.data(), f.index.size());
  if (got != (long)f.index.size()) return fail("bad LZ4 data");
  for (uint8_t v : f.index)
    if (v >= pc) return fail("index outside palette");
  *out = std::move(f);
  return true;
}

void expandBpf(const BpfFrame& f, uint8_t* dst, size_t stride) {
  for (int y = 0; y < f.height; ++y) {
    uint32_t* row = (uint32_t*)(dst + (size_t)y * stride);
    if (y < f.y0 || y >= f.y0 + f.h) {
      std::memset(row, 0, (size_t)f.width * 4);
      continue;
    }
    std::memset(row, 0, (size_t)f.x0 * 4);
    const uint8_t* ix = f.index.data() + (size_t)(y - f.y0) * f.w;
    uint32_t* p = row + f.x0;
    for (int x = 0; x < f.w; ++x) p[x] = f.palette[ix[x]];
    std::memset(row + f.x0 + f.w, 0, (size_t)(f.width - f.x0 - f.w) * 4);
  }
}

void downscaleBGRA(const uint32_t* src, int sw, int sh, int sstride, uint32_t* dst, int dw, int dh, int dstride) {
  if (dw <= 0 || dh <= 0 || sw <= 0 || sh <= 0) return;
  if (dw > sw || dh > sh) {  // not a downscale: copy what fits
    for (int y = 0; y < dh; ++y)
      for (int x = 0; x < dw; ++x) dst[(size_t)y * dstride + x] = (x < sw && y < sh) ? src[(size_t)y * sstride + x] : 0;
    return;
  }
  // Separable box filter with fractional edge weights, 16.16 fixed point.
  // Horizontal pass into a temporary buffer of dw x sh (4 channels, 32-bit sums).
  std::vector<uint32_t> tmp((size_t)dw * sh * 4);
  const uint64_t xscale = ((uint64_t)sw << 16) / dw;  // source px per dest px, 16.16
  for (int y = 0; y < sh; ++y) {
    const uint8_t* s = (const uint8_t*)(src + (size_t)y * sstride);
    uint32_t* t = &tmp[(size_t)y * dw * 4];
    for (int x = 0; x < dw; ++x) {
      uint64_t a = (uint64_t)x * sw * 65536 / dw, b = (uint64_t)(x + 1) * sw * 65536 / dw;  // span in 16.16
      uint64_t acc[4] = {0, 0, 0, 0};
      for (uint64_t p = a >> 16; (p << 16) < b; ++p) {
        uint64_t lo = std::max(a, p << 16), hi = std::min(b, (p + 1) << 16);
        uint64_t wgt = hi - lo;
        const uint8_t* px = s + p * 4;
        for (int c = 0; c < 4; ++c) acc[c] += px[c] * wgt;
      }
      for (int c = 0; c < 4; ++c) t[x * 4 + c] = (uint32_t)((acc[c] * 256 + xscale / 2) / xscale);  // 8.8 fixed
    }
  }
  const uint64_t yscale = ((uint64_t)sh << 16) / dh;
  for (int y = 0; y < dh; ++y) {
    uint64_t a = (uint64_t)y * sh * 65536 / dh, b = (uint64_t)(y + 1) * sh * 65536 / dh;
    uint8_t* d = (uint8_t*)(dst + (size_t)y * dstride);
    for (int x = 0; x < dw; ++x) {
      uint64_t acc[4] = {0, 0, 0, 0};
      for (uint64_t p = a >> 16; (p << 16) < b; ++p) {
        uint64_t lo = std::max(a, p << 16), hi = std::min(b, (p + 1) << 16);
        uint64_t wgt = hi - lo;
        const uint32_t* t = &tmp[((size_t)p * dw + x) * 4];
        for (int c = 0; c < 4; ++c) acc[c] += t[c] * wgt;
      }
      for (int c = 0; c < 4; ++c) {
        uint64_t v = (acc[c] + yscale * 128) / (yscale * 256);
        d[x * 4 + c] = (uint8_t)std::min<uint64_t>(v, 255);
      }
    }
  }
}

}  // namespace pet
