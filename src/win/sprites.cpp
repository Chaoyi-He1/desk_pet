#include "win/sprites.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>

#include "core/pose.h"
#include "core/skin.h"

namespace petwin {
namespace {

using pet::Anim;
using pet::Pose;

std::vector<std::wstring> listPngs(const std::wstring& dir) {
  std::vector<std::wstring> out;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*.png").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) out.push_back(dir + L"\\" + fd.cFileName);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end());
  return out;
}

// Key that identifies a distinct rendering (translation is applied as a window offset).
struct RenderKey {
  double angle, sx, sy, br;
  bool operator<(const RenderKey& o) const {
    if (angle != o.angle) return angle < o.angle;
    if (sx != o.sx) return sx < o.sx;
    if (sy != o.sy) return sy < o.sy;
    return br < o.br;
  }
};

bool translationOnly(const Pose& p) { return p.angleDeg == 0 && p.scaleX == 1 && p.scaleY == 1; }

}  // namespace

SpriteSet::~SpriteSet() {
  for (auto& o : owned_) DeleteObject(o.bmp);
  if (scratch_) DeleteObject(scratch_);
}

HBITMAP SpriteSet::newDib(void** bits) {
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = cw_;
  bi.bmiHeader.biHeight = -ch_;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  HBITMAP bmp = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, bits, nullptr, 0);
  if (bmp) owned_.push_back({bmp, *bits});
  return bmp;
}

void* SpriteSet::bitsOf(HBITMAP bmp) const {
  for (auto& o : owned_) if (o.bmp == bmp) return o.bits;
  return nullptr;
}

bool SpriteSet::load(const std::wstring& assetsDir, const std::wstring& imagePath, int height, bool exactHeight,
                     std::wstring* err) {
  using namespace Gdiplus;
  GdiplusStartupInput gsi;
  ULONG_PTR token = 0;
  if (GdiplusStartup(&token, &gsi, nullptr) != Ok) {
    if (err) *err = L"GDI+ 初始化失败。";
    return false;
  }
  bool ok = false;
  {
    std::unique_ptr<Bitmap> base;
    {
      std::unique_ptr<Bitmap> raw(new Bitmap(imagePath.c_str()));
      if (raw->GetLastStatus() == Ok && raw->GetWidth() > 0 && raw->GetHeight() > 0) {
        base.reset(raw->Clone(0, 0, raw->GetWidth(), raw->GetHeight(), PixelFormat32bppPARGB));
      }
    }
    std::vector<std::wstring> idleSeq = listPngs(assetsDir + L"\\idle");
    if (!base && idleSeq.empty()) {
      if (err) *err = L"没有找到形象文件：\n" + imagePath + L"\n\n请把透明背景的立绘 PNG 放到 assets\\skins 目录后重新启动。";
      GdiplusShutdown(token);
      return false;
    }

    // Reference picture size: the base picture, or the first idle frame.
    double refW, refH;
    if (base) { refW = base->GetWidth(); refH = base->GetHeight(); }
    else {
      Bitmap first(idleSeq[0].c_str());
      refW = first.GetWidth(); refH = first.GetHeight();
      if (refW <= 0 || refH <= 0) { refW = 1; refH = 2; }
    }
    const int H = exactHeight ? pet::clampHeight(height) : pet::displayHeight(height, static_cast<int>(refH));
    picH_ = H;
    const int W = (std::max)(1, static_cast<int>(std::lround(refW * H / refH)));
    const int padX = static_cast<int>(std::ceil(0.105 * H + 0.02 * W)) + 2;
    padTop_ = static_cast<int>(std::ceil(0.05 * H)) + 3;
    const int padBottom = 4;
    cw_ = W + 2 * padX;
    ch_ = H + padTop_ + padBottom;
    const float ax = cw_ / 2.0f, ay = static_cast<float>(padTop_ + H);  // bottom-center anchor

    // Renders `img` scaled to W x H with the given pose transform into a new DIB.
    auto render = [&](Bitmap& img, const Pose& p) -> HBITMAP {
      Bitmap canvas(cw_, ch_, PixelFormat32bppPARGB);
      Graphics g(&canvas);
      g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
      g.SetPixelOffsetMode(PixelOffsetModeHalf);
      g.SetCompositingQuality(CompositingQualityHighQuality);
      g.TranslateTransform(ax, ay);
      g.RotateTransform(static_cast<float>(p.angleDeg));
      g.ScaleTransform(static_cast<float>(p.scaleX), static_cast<float>(p.scaleY));
      g.TranslateTransform(-W / 2.0f, -static_cast<float>(H));
      ImageAttributes attrs;
      if (p.brightness != 1.0) {
        float b = static_cast<float>(p.brightness);
        ColorMatrix cm = {{{b, 0, 0, 0, 0}, {0, b, 0, 0, 0}, {0, 0, b, 0, 0}, {0, 0, 0, 1, 0}, {0, 0, 0, 0, 1}}};
        attrs.SetColorMatrix(&cm);
      }
      g.DrawImage(&img, Rect(0, 0, W, H), 0, 0, img.GetWidth(), img.GetHeight(), UnitPixel, &attrs);

      void* bits = nullptr;
      HBITMAP dib = newDib(&bits);
      if (!dib) return nullptr;
      BitmapData bd;
      Rect all(0, 0, cw_, ch_);
      if (canvas.LockBits(&all, ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Ok) {
        for (int y = 0; y < ch_; ++y)
          std::memcpy(static_cast<char*>(bits) + y * cw_ * 4,
                      static_cast<char*>(bd.Scan0) + y * bd.Stride, cw_ * 4);
        canvas.UnlockBits(&bd);
      }
      return dib;
    };

    std::map<RenderKey, HBITMAP> cache;
    Pose identity;
    for (int ai = 0; ai < (int)Anim::Count; ++ai) {
      Anim a = static_cast<Anim>(ai);
      std::vector<std::wstring> seq = listPngs(assetsDir + L"\\" + std::wstring(pet::animName(a), pet::animName(a) + std::strlen(pet::animName(a))));
      std::vector<SpriteFrame>& out = frames_[ai];
      if (!seq.empty()) {
        for (auto& path : seq) {
          Bitmap img(path.c_str());
          if (img.GetLastStatus() != Ok) continue;
          HBITMAP b = render(img, identity);
          if (b) { SpriteFrame f; f.bmp = b; out.push_back(f); }
        }
      }
      if (out.empty() && base) {
        int n = pet::poseFrameCount(a);
        for (int i = 0; i < n; ++i) {
          Pose p = pet::poseFor(a, i);
          RenderKey key{p.angleDeg, p.scaleX, p.scaleY, p.brightness};
          SpriteFrame f;
          if (translationOnly(p)) { f.offX = p.dx; f.offY = p.dy; }
          auto it = cache.find(key);
          if (it == cache.end()) {
            Pose rp = p;
            if (translationOnly(p)) { rp.dx = rp.dy = 0; }
            it = cache.emplace(key, render(*base, rp)).first;
          }
          f.bmp = it->second;
          if (f.bmp) out.push_back(f);
        }
      }
    }
    // Any animation still without frames borrows idle's.
    for (int ai = 0; ai < (int)Anim::Count; ++ai)
      if (frames_[ai].empty()) frames_[ai] = frames_[(int)Anim::Idle];
    ok = !frames_[(int)Anim::Idle].empty();
    if (!ok && err) *err = L"形象文件无法解码，请确认它是有效的 PNG。";
  }
  GdiplusShutdown(token);
  if (ok) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = cw_;
    bi.bmiHeader.biHeight = -ch_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    scratch_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &scratchBits_, nullptr, 0);
  }
  return ok;
}

SpriteFrame SpriteSet::get(Anim a, int index, bool mirrored) {
  const std::vector<SpriteFrame>& v = frames_[(int)a];
  if (v.empty()) return {};
  SpriteFrame f = v[static_cast<size_t>(index) % v.size()];
  f.src = f.bmp;
  if (!mirrored || !scratch_) return f;
  f.mirrored = true;
  if (scratchSrc_ != f.bmp) {
    GdiFlush();
    const uint32_t* src = static_cast<const uint32_t*>(bitsOf(f.bmp));
    uint32_t* dst = static_cast<uint32_t*>(scratchBits_);
    if (src && dst) {
      for (int y = 0; y < ch_; ++y) {
        const uint32_t* s = src + y * cw_;
        uint32_t* d = dst + y * cw_;
        for (int x = 0; x < cw_; ++x) d[x] = s[cw_ - 1 - x];
      }
    }
    scratchSrc_ = f.bmp;
  }
  f.bmp = scratch_;
  f.offX = -f.offX;
  return f;
}

}  // namespace petwin
