#include "core/avatar.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace pet {
namespace {

const int kOpaque = 24;  // alpha above this counts as her

inline bool on(const AlphaMask& m, int x, int y) { return m.alpha[(size_t)y * m.stride + (size_t)x * m.step] > kOpaque; }

// Centre of the longest run of opaque pixels in row y; -1 when the row is empty.
double longestRunCentre(const AlphaMask& m, int y) {
  int bestStart = -1, bestLen = 0, start = -1;
  for (int x = 0; x <= m.w; ++x) {
    bool o = x < m.w && on(m, x, y);
    if (o && start < 0) start = x;
    if (!o && start >= 0) {
      if (x - start > bestLen) {
        bestLen = x - start;
        bestStart = start;
      }
      start = -1;
    }
  }
  return bestLen ? bestStart + (bestLen - 1) / 2.0 : -1;
}

}  // namespace

FaceBox faceBox(const AlphaMask& m, bool painting, double headTopY, double groundY, const double* hint) {
  FaceBox out{m.w / 4.0, 0, std::max(1.0, m.w / 2.0)};
  if (!m.alpha || m.w <= 0 || m.h <= 0) return out;

  // Opaque pixels per row, and the opaque box.
  std::vector<int> counts(m.h, 0);
  int x0 = m.w, x1 = -1, y0 = -1, y1 = -1;
  for (int y = 0; y < m.h; ++y) {
    for (int x = 0; x < m.w; ++x) {
      if (!on(m, x, y)) continue;
      ++counts[y];
      x0 = std::min(x0, x);
      x1 = std::max(x1, x);
    }
    if (counts[y]) {
      if (y0 < 0) y0 = y;
      y1 = y;
    }
  }
  if (y0 < 0) return out;  // nothing visible

  if (hint) {
    double bw = x1 + 1 - x0, bh = y1 + 1 - y0;
    double side = std::max(1.0, hint[2] * bh);
    out.side = side;
    out.x = x0 + hint[0] * bw - side / 2;
    out.y = y0 + hint[1] * bh - side / 2;
    return out;
  }

  if (!painting && groundY > headTopY + 4) {
    double hc = groundY - headTopY;
    double cy = headTopY + 0.33 * hc, side = 0.48 * hc;
    int row = std::min(m.h - 1, std::max(0, (int)std::floor(cy)));
    double cx = longestRunCentre(m, row);
    if (cx < 0) cx = m.w / 2.0;
    out.side = side;
    out.x = cx - side / 2;
    out.y = cy - side / 2;
    return out;
  }

  // A bust shot.
  int hp = y1 - y0 + 1;
  double thresh = 0.06 * hp;
  int head = y0;
  for (int y = y0; y + 6 <= y1; ++y) {
    int lo = counts[y];
    for (int k = 1; k < 6; ++k) lo = std::min(lo, counts[y + k]);
    if (lo >= thresh) {
      head = y;
      break;
    }
  }
  int last = std::min(y1, (int)(head + 0.22 * hp));
  double sumX = 0;
  long n = 0;
  for (int y = head; y <= last; ++y)
    for (int x = x0; x <= x1; ++x)
      if (on(m, x, y)) {
        sumX += x;
        ++n;
      }
  double cx = n ? sumX / n : m.w / 2.0;
  double side = 0.40 * hp;
  out.side = side;
  out.x = cx - side / 2;
  out.y = head + 0.20 * hp - side / 2;
  return out;
}

}  // namespace pet
