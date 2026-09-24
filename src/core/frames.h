// Fast frame format for big animated paintings (dynamic paintings, Live2D).
//
// A PNG frame costs several milliseconds to decode and convert; at 8 fps that is ~5% of a
// core for a pet that should be idle. ".bpf" frames store the same 256-colour picture as
// a palette (premultiplied BGRA) plus LZ4-compressed palette indices of the visible box,
// which decodes ~10x faster. Written by tools/make_bpf.py. Layout (little-endian):
//   "BPF1" u16 width u16 height u16 x0 u16 y0 u16 w u16 h u16 paletteCount
//   paletteCount x u32 (B,G,R,A bytes, premultiplied)  u32 compressedSize  LZ4 block
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace pet {

// Decompresses one raw LZ4 block. Returns the number of bytes written, or -1 if the
// input is malformed or would overflow dst.
long lz4Decompress(const uint8_t* src, size_t srcLen, uint8_t* dst, size_t dstCap);

struct BpfFrame {
  int width = 0, height = 0;    // canvas
  int x0 = 0, y0 = 0, w = 0, h = 0;  // visible box, rows top-down
  std::vector<uint32_t> palette;     // premultiplied BGRA as little-endian u32 (0xAARRGGBB)
  std::vector<uint8_t> index;        // w*h
};

bool parseBpf(const uint8_t* data, size_t n, BpfFrame* out, std::string* err = nullptr);

// Expands the frame into a canvas-sized premultiplied BGRA buffer (rows top-down,
// `stride` bytes apart). Pixels outside the visible box are cleared.
void expandBpf(const BpfFrame& f, uint8_t* dst, size_t stride);

// Area-average downscale of a premultiplied BGRA image (used where the compositor cannot
// scale, e.g. Windows layered windows). dw <= sw and dh <= sh; larger sizes are clamped
// to a plain copy of the overlapping region.
void downscaleBGRA(const uint32_t* src, int sw, int sh, int srcStridePx, uint32_t* dst, int dw, int dh,
                   int dstStridePx);

}  // namespace pet
