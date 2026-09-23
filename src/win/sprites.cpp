#include "win/sprites.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>

#include "core/ini.h"
#include "core/pose.h"
#include "core/skin.h"

namespace petwin {
namespace {

using pet::Anim;
using pet::Pose;

std::vector<std::wstring> listDir(const std::wstring& dir, bool wantDirs, const wchar_t* pattern) {
  std::vector<std::wstring> out;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\" + pattern).c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (isDir != wantDirs || fd.cFileName[0] == L'.') continue;
    out.push_back(fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end());
  return out;
}

std::wstring widenAscii(const char* s) { return std::wstring(s, s + std::strlen(s)); }

std::string readAll(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

bool isDirectory(const std::wstring& p) {
  DWORD a = GetFileAttributesW(p.c_str());
  return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}

// Opaque bounding box of a premultiplied top-down 32-bit buffer.
bool alphaBox(const uint32_t* px, int w, int h, int stride, int* x0, int* y0, int* x1, int* y1) {
  *x0 = w; *y0 = h; *x1 = -1; *y1 = -1;
  for (int y = 0; y < h; ++y) {
    const uint32_t* row = px + (size_t)y * stride;
    for (int x = 0; x < w; ++x) {
      if ((row[x] >> 24) > 2) {
        if (x < *x0) *x0 = x;
        if (x > *x1) *x1 = x;
        if (y < *y0) *y0 = y;
        if (y > *y1) *y1 = y;
      }
    }
  }
  return *x1 >= *x0;
}

}  // namespace

SpriteSet::~SpriteSet() {
  for (auto& kv : cache_) DeleteObject(kv.second.bmp);
  for (HBITMAP b : owned_) DeleteObject(b);
  if (gdiplus_) Gdiplus::GdiplusShutdown(gdiplus_);
}

HBITMAP SpriteSet::newDib(int w, int h, void** bits) {
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w;
  bi.bmiHeader.biHeight = -h;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  return CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
}

std::vector<int> SpriteSet::variants(Anim a) const {
  std::vector<int> v;
  if (animated_) {
    for (auto& list : paths_[(int)a]) v.push_back((int)list.size());
  } else {
    for (auto& list : frames_[(int)a]) v.push_back((int)list.size());
  }
  return v;
}

bool SpriteSet::load(const std::wstring& path, int height, bool exactHeight, std::wstring* err) {
  Gdiplus::GdiplusStartupInput gsi;
  if (Gdiplus::GdiplusStartup(&gdiplus_, &gsi, nullptr) != Gdiplus::Ok) {
    gdiplus_ = 0;
    if (err) *err = L"GDI+ 初始化失败。";
    return false;
  }
  bool ok = isDirectory(path) ? loadAnimated(path, height, exactHeight, err)
                              : loadPainting(path, height, exactHeight, err);
  if (!animated_ && gdiplus_) {  // paintings are fully rendered: GDI+ is no longer needed
    Gdiplus::GdiplusShutdown(gdiplus_);
    gdiplus_ = 0;
  }
  return ok;
}

// ---------------------------------------------------------------- paintings

bool SpriteSet::loadPainting(const std::wstring& png, int height, bool exact, std::wstring* err) {
  using namespace Gdiplus;
  std::unique_ptr<Bitmap> base;
  {
    std::unique_ptr<Bitmap> raw(new Bitmap(png.c_str()));
    if (raw->GetLastStatus() == Ok && raw->GetWidth() > 0 && raw->GetHeight() > 0)
      base.reset(raw->Clone(0, 0, raw->GetWidth(), raw->GetHeight(), PixelFormat32bppPARGB));
  }
  if (!base) {
    if (err) *err = L"无法读取形象文件：\n" + png;
    return false;
  }
  const double refW = base->GetWidth(), refH = base->GetHeight();
  const int H = exact ? pet::clampHeight(height) : pet::displayHeight(height, (int)refH);
  const int W = (std::max)(1, (int)std::lround(refW * H / refH));
  const int padX = (int)std::ceil(0.105 * H + 0.02 * W) + 2;
  const int padTop = (int)std::ceil(0.05 * H) + 3;
  cw_ = W + 2 * padX;
  ch_ = H + padTop + 4;
  size_ = H;
  headTop_ = padTop;
  groundInset_ = 0;
  headFraction_ = 0.22;
  const float ax = cw_ / 2.0f, ay = (float)(padTop + H);  // poses pivot on the feet

  auto render = [&](const Pose& p) -> SpriteFrame {
    Bitmap canvas(cw_, ch_, PixelFormat32bppPARGB);
    Graphics g(&canvas);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.TranslateTransform(ax, ay);
    g.RotateTransform((float)p.angleDeg);
    g.ScaleTransform((float)p.scaleX, (float)p.scaleY);
    g.TranslateTransform(-W / 2.0f, -(float)H);
    ImageAttributes attrs;
    if (p.brightness != 1.0) {
      float b = (float)p.brightness;
      ColorMatrix cm = {{{b, 0, 0, 0, 0}, {0, b, 0, 0, 0}, {0, 0, b, 0, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 1}}};
      attrs.SetColorMatrix(&cm);
    }
    g.DrawImage(base.get(), Rect(0, 0, W, H), 0, 0, base->GetWidth(), base->GetHeight(), UnitPixel, &attrs);
    SpriteFrame f;
    void* bits = nullptr;
    f.bmp = newDib(cw_, ch_, &bits);
    if (!f.bmp) return f;
    owned_.push_back(f.bmp);
    f.w = cw_;
    f.h = ch_;
    BitmapData bd;
    Rect all(0, 0, cw_, ch_);
    if (canvas.LockBits(&all, ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Ok) {
      for (int y = 0; y < ch_; ++y)
        std::memcpy((char*)bits + (size_t)y * cw_ * 4, (char*)bd.Scan0 + (size_t)y * bd.Stride, (size_t)cw_ * 4);
      canvas.UnlockBits(&bd);
    }
    return f;
  };

  // Identical transforms (ignoring pure translation) share one bitmap.
  struct Key {
    double a, sx, sy, br;
    bool operator<(const Key& o) const { return std::tie(a, sx, sy, br) < std::tie(o.a, o.sx, o.sy, o.br); }
  };
  std::map<Key, SpriteFrame> rendered;
  for (int ai = 0; ai < (int)Anim::Count; ++ai) {
    Anim a = (Anim)ai;
    std::vector<SpriteFrame> list;
    for (int i = 0; i < pet::poseFrameCount(a); ++i) {
      Pose p = pet::poseFor(a, i);
      bool moveOnly = p.angleDeg == 0 && p.scaleX == 1 && p.scaleY == 1;
      Key k{p.angleDeg, p.scaleX, p.scaleY, p.brightness};
      auto it = rendered.find(k);
      if (it == rendered.end()) {
        Pose rp = p;
        if (moveOnly) rp.dx = rp.dy = 0;
        it = rendered.emplace(k, render(rp)).first;
      }
      SpriteFrame f = it->second;
      if (moveOnly) {
        f.offX = p.dx;
        f.offY = p.dy;
      }
      if (f.bmp) list.push_back(f);
    }
    frames_[ai].push_back(list);
  }
  return !frames_[(int)Anim::Idle][0].empty();
}

// ---------------------------------------------------------------- animated chibis

bool SpriteSet::loadAnimated(const std::wstring& dir, int height, bool exact, std::wstring* err) {
  animated_ = true;
  pet::Ini meta = pet::Ini::parse(readAll(dir + L"\\meta.ini"));
  int srcW = meta.getInt("sequence", "width", 0), srcH = meta.getInt("sequence", "height", 0);
  int charH = meta.getInt("sequence", "char_height", srcH);
  if (srcW <= 0 || srcH <= 0 || charH <= 0) {
    if (err) *err = L"动画形象缺少 meta.ini：\n" + dir;
    return false;
  }
  std::vector<std::wstring> folders = listDir(dir, true, L"*");
  for (int ai = 0; ai < (int)Anim::Count; ++ai) {
    std::wstring name = widenAscii(pet::animName((Anim)ai));
    std::vector<std::wstring> vfolders;
    for (auto& f : folders)
      if (f == name) vfolders.insert(vfolders.begin(), f);
      else if (f.size() > name.size() + 1 && f.compare(0, name.size() + 1, name + L"_") == 0) vfolders.push_back(f);
    for (auto& vf : vfolders) {
      std::vector<std::wstring> files = listDir(dir + L"\\" + vf, false, L"*.png");
      if (files.empty()) continue;
      for (auto& f : files) f = dir + L"\\" + vf + L"\\" + f;
      paths_[ai].push_back(files);
    }
  }
  if (paths_[(int)Anim::Idle].empty()) {
    if (err) *err = L"动画形象缺少 idle 帧：\n" + dir;
    return false;
  }
  // "Size" for chibis: the character is 80% of it, so paintings and chibis look alike.
  int size = exact ? pet::clampHeight(height) : height;
  scale_ = (std::min)(2.0, 0.8 * size / charH);
  size_ = size;
  cw_ = (std::max)(1, (int)std::lround(srcW * scale_));
  ch_ = (std::max)(1, (int)std::lround(srcH * scale_));
  headTop_ = (int)std::lround(meta.getInt("sequence", "head_top", 0) * scale_);
  groundInset_ = (int)std::lround(meta.getInt("sequence", "ground", 0) * scale_);
  headFraction_ = meta.getDouble("sequence", "head_fraction", 0.45);
  fps_ = meta.getInt("sequence", "fps", 10);
  return true;
}

SpriteFrame SpriteSet::decode(const std::wstring& png) {
  using namespace Gdiplus;
  SpriteFrame f;
  Bitmap src(png.c_str());
  if (src.GetLastStatus() != Ok) return f;
  Bitmap canvas(cw_, ch_, PixelFormat32bppPARGB);
  {
    Graphics g(&canvas);
    g.SetInterpolationMode(scale_ < 1 ? InterpolationModeHighQualityBicubic : InterpolationModeBilinear);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.DrawImage(&src, Rect(0, 0, cw_, ch_), 0, 0, src.GetWidth(), src.GetHeight(), UnitPixel);
  }
  BitmapData bd;
  Rect all(0, 0, cw_, ch_);
  if (canvas.LockBits(&all, ImageLockModeRead, PixelFormat32bppPARGB, &bd) != Ok) return f;
  int x0, y0, x1, y1;
  const uint32_t* px = (const uint32_t*)bd.Scan0;
  if (!alphaBox(px, cw_, ch_, bd.Stride / 4, &x0, &y0, &x1, &y1)) { x0 = y0 = 0; x1 = y1 = 0; }
  f.offX = x0;
  f.offY = y0;
  f.w = x1 - x0 + 1;
  f.h = y1 - y0 + 1;
  void* bits = nullptr;
  f.bmp = newDib(f.w, f.h, &bits);
  if (f.bmp) {
    for (int y = 0; y < f.h; ++y)
      std::memcpy((char*)bits + (size_t)y * f.w * 4, (const char*)bd.Scan0 + (size_t)(y0 + y) * bd.Stride + (size_t)x0 * 4,
                  (size_t)f.w * 4);
  }
  canvas.UnlockBits(&bd);
  return f;
}

SpriteFrame SpriteSet::mirror(const SpriteFrame& f) {
  SpriteFrame m = f;
  void* dst = nullptr;
  m.bmp = newDib(f.w, f.h, &dst);
  if (!m.bmp) return f;
  DIBSECTION ds;
  GetObject(f.bmp, sizeof(ds), &ds);
  GdiFlush();
  const uint32_t* s = (const uint32_t*)ds.dsBm.bmBits;
  uint32_t* d = (uint32_t*)dst;
  for (int y = 0; y < f.h; ++y)
    for (int x = 0; x < f.w; ++x) d[(size_t)y * f.w + x] = s[(size_t)y * f.w + (f.w - 1 - x)];
  m.offX = cw_ - f.offX - f.w;
  return m;
}

void SpriteSet::evictExcept(Anim keep) {
  for (auto it = cache_.begin(); it != cache_.end();) {
    int a = std::get<0>(it->first);
    if (a != (int)Anim::Idle && a != (int)keep) {
      DeleteObject(it->second.bmp);
      it = cache_.erase(it);
    } else {
      ++it;
    }
  }
}

SpriteFrame SpriteSet::get(Anim a, int variant, int index, bool mirrored) {
  if (!animated_) {
    auto& vars = frames_[(int)a].empty() || frames_[(int)a][0].empty() ? frames_[(int)Anim::Idle] : frames_[(int)a];
    auto& list = vars[(size_t)variant % vars.size()];
    SpriteFrame f = list[(size_t)index % list.size()];
    if (!mirrored) return f;
    auto it = mirrors_.find(f.bmp);
    if (it == mirrors_.end()) {
      SpriteFrame m = mirror(f);
      if (m.bmp != f.bmp) owned_.push_back(m.bmp);
      it = mirrors_.emplace(f.bmp, m).first;
    }
    SpriteFrame m = it->second;
    m.offX = -f.offX;  // translation-only poses keep their offset, mirrored
    m.offY = f.offY;
    return m;
  }
  int ai = paths_[(int)a].empty() ? (int)Anim::Idle : (int)a;
  if (ai != lastAnim_) {
    evictExcept((Anim)ai);  // keep idle (common) and the current state only
    lastAnim_ = ai;
  }
  auto& vars = paths_[ai];
  int v = (int)((size_t)variant % vars.size());
  int i = (int)((size_t)index % vars[v].size());
  auto key = std::make_tuple(ai, v, i, mirrored);
  auto it = cache_.find(key);
  if (it != cache_.end()) return it->second;
  SpriteFrame f;
  if (mirrored) {
    SpriteFrame plain = get((Anim)ai, v, i, false);
    f = plain.bmp ? mirror(plain) : plain;
  } else {
    f = decode(vars[v][i]);
  }
  if (f.bmp) cache_[key] = f;
  return f;
}

}  // namespace petwin
