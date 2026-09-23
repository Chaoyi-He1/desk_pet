/* Textured-triangle rasterizer used by render38.py (loaded through ctypes).
 * Texture and framebuffer are premultiplied RGBA floats. Normal blending:
 * dst = src + dst * (1 - src.a). Bilinear sampling, pixel centres at +0.5. */
#include <math.h>

static inline float edgef(float ax, float ay, float bx, float by, float px, float py) {
  return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static inline void sample(const float *tex, int tw, int th, float u, float v, float out[4]) {
  u -= 0.5f; v -= 0.5f;
  int x0 = (int)floorf(u), y0 = (int)floorf(v);
  float fx = u - x0, fy = v - y0;
  int x1 = x0 + 1, y1 = y0 + 1;
  if (x0 < 0) x0 = 0; if (y0 < 0) y0 = 0; if (x1 < 0) x1 = 0; if (y1 < 0) y1 = 0;
  if (x0 >= tw) x0 = tw - 1; if (x1 >= tw) x1 = tw - 1;
  if (y0 >= th) y0 = th - 1; if (y1 >= th) y1 = th - 1;
  const float *p00 = tex + 4 * (y0 * tw + x0), *p10 = tex + 4 * (y0 * tw + x1);
  const float *p01 = tex + 4 * (y1 * tw + x0), *p11 = tex + 4 * (y1 * tw + x1);
  for (int k = 0; k < 4; ++k) {
    float a = p00[k] + (p10[k] - p00[k]) * fx;
    float b = p01[k] + (p11[k] - p01[k]) * fx;
    out[k] = a + (b - a) * fy;
  }
}

void draw_tris(const float *tex, int tw, int th, float *fb, int fw, int fh,
               const float *xy, const float *uv, const int *tri, int ntri,
               float cr, float cg, float cb, float ca, int additive) {
  for (int t = 0; t < ntri; ++t) {
    int i0 = tri[3 * t], i1 = tri[3 * t + 1], i2 = tri[3 * t + 2];
    float x0 = xy[2 * i0], y0 = xy[2 * i0 + 1], x1 = xy[2 * i1], y1 = xy[2 * i1 + 1];
    float x2 = xy[2 * i2], y2 = xy[2 * i2 + 1];
    float area = edgef(x0, y0, x1, y1, x2, y2);
    if (fabsf(area) < 1e-8f) continue;
    float minx = fminf(x0, fminf(x1, x2)), maxx = fmaxf(x0, fmaxf(x1, x2));
    float miny = fminf(y0, fminf(y1, y2)), maxy = fmaxf(y0, fmaxf(y1, y2));
    int bx0 = (int)floorf(minx), bx1 = (int)ceilf(maxx), by0 = (int)floorf(miny), by1 = (int)ceilf(maxy);
    if (bx0 < 0) bx0 = 0; if (by0 < 0) by0 = 0; if (bx1 > fw) bx1 = fw; if (by1 > fh) by1 = fh;
    float u0 = uv[2 * i0], v0 = uv[2 * i0 + 1], u1 = uv[2 * i1], v1 = uv[2 * i1 + 1];
    float u2 = uv[2 * i2], v2 = uv[2 * i2 + 1];
    float inv = 1.0f / area;
    for (int py = by0; py < by1; ++py) {
      float cy = py + 0.5f;
      for (int px = bx0; px < bx1; ++px) {
        float cx = px + 0.5f;
        float w0 = edgef(x1, y1, x2, y2, cx, cy) * inv;
        float w1 = edgef(x2, y2, x0, y0, cx, cy) * inv;
        float w2 = 1.0f - w0 - w1;
        /* Half-open coverage: a pixel centre exactly on a shared edge belongs to one triangle. */
        if (w0 < 0 || w1 < 0 || w2 <= 0) continue;
        float s[4];
        sample(tex, tw, th, w0 * u0 + w1 * u1 + w2 * u2, w0 * v0 + w1 * v1 + w2 * v2, s);
        float sa = s[3] * ca;
        if (sa <= 0) continue;
        float *d = fb + 4 * (py * fw + px);
        if (additive) { /* light adds colour; alpha accumulates like a normal layer */
          d[0] += s[0] * cr * ca; d[1] += s[1] * cg * ca; d[2] += s[2] * cb * ca;
          d[3] = sa + d[3] * (1.0f - sa);
          continue;
        }
        float k = 1.0f - sa;
        d[0] = s[0] * cr * ca + d[0] * k;
        d[1] = s[1] * cg * ca + d[1] * k;
        d[2] = s[2] * cb * ca + d[2] * k;
        d[3] = sa + d[3] * k;
      }
    }
  }
}
