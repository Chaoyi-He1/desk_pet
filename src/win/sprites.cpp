#include "win/sprites.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <memory>
#include <sstream>

#include "core/frames.h"
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

// A file in a cloud-synced folder (Dropbox, OneDrive...) can be "online-only": a placeholder
// whose content is downloaded the first time it is read, a network round trip per frame.
// Reading one on the UI thread freezes her, so such frames are not decoded until the
// Hydrator has read them (she keeps showing the frame she has).
bool isLocalFile(const std::wstring& path) {
  DWORD a = GetFileAttributesW(path.c_str());
  if (a == INVALID_FILE_ATTRIBUTES) return true;  // missing or unreadable: let the decoder report it
  const DWORD remote = FILE_ATTRIBUTE_OFFLINE | 0x40000u /*RECALL_ON_OPEN*/ | 0x400000u /*RECALL_ON_DATA_ACCESS*/;
  return (a & remote) == 0;
}

// Reads the whole file and drops the data: the sync client fetches it and the file is local.
void readThrough(const std::wstring& path) {
  HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (h == INVALID_HANDLE_VALUE) return;
  std::vector<char> buf(1 << 16);
  DWORD got = 0;
  while (ReadFile(h, buf.data(), (DWORD)buf.size(), &got, nullptr) && got) {
  }
  CloseHandle(h);
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

// Reads a skin's frame files once on its own thread, in the order she is likely to need them,
// so online-only placeholders are local before the UI thread asks for them; a frame the UI
// thread wants right now goes first. The SpriteSet abandons it when it goes away, and the
// object is freed by whichever of the two (SpriteSet, thread) lets go last.
class SpriteSet::Hydrator {
public:
  static Hydrator* start(std::vector<std::wstring> files) {
    Hydrator* h = new Hydrator(std::move(files));
    HANDLE t = CreateThread(nullptr, 0, &Hydrator::main, h, 0, nullptr);
    if (!t) {
      delete h;
      return nullptr;
    }
    CloseHandle(t);
    return h;
  }

  void want(const std::wstring& path) {  // UI thread: this one next
    AcquireSRWLockExclusive(&lock_);
    bool queued = false;
    for (const std::wstring& p : urgent_)
      if (p == path) queued = true;
    if (!queued) urgent_.push_back(path);
    ReleaseSRWLockExclusive(&lock_);
    WakeConditionVariable(&wake_);
  }

  void abandon() {  // UI thread: the SpriteSet is gone; never touched again by it
    AcquireSRWLockExclusive(&lock_);
    quit_ = true;
    ReleaseSRWLockExclusive(&lock_);
    WakeConditionVariable(&wake_);
    release();
  }

private:
  explicit Hydrator(std::vector<std::wstring> files) : files_(std::move(files)) {}
  ~Hydrator() = default;

  void release() {
    if (InterlockedDecrement(&refs_) == 0) delete this;
  }

  static DWORD WINAPI main(LPVOID p) {
    Hydrator* self = static_cast<Hydrator*>(p);
    for (;;) {
      AcquireSRWLockExclusive(&self->lock_);
      while (!self->quit_ && self->urgent_.empty() && self->next_ >= self->files_.size())
        SleepConditionVariableSRW(&self->wake_, &self->lock_, INFINITE, 0);
      if (self->quit_) {
        ReleaseSRWLockExclusive(&self->lock_);
        break;
      }
      std::wstring path;
      if (!self->urgent_.empty()) {  // the newest request is the frame closest to being shown
        path = self->urgent_.back();
        self->urgent_.pop_back();
      } else {
        path = self->files_[self->next_++];
      }
      ReleaseSRWLockExclusive(&self->lock_);
      if (!isLocalFile(path)) readThrough(path);
    }
    self->release();
    return 0;
  }

  volatile LONG refs_ = 2;  // the SpriteSet and the thread
  SRWLOCK lock_ = SRWLOCK_INIT;
  CONDITION_VARIABLE wake_ = CONDITION_VARIABLE_INIT;
  std::vector<std::wstring> files_, urgent_;  // files_: in order of need; urgent_: asked for by the UI thread
  size_t next_ = 0;
  bool quit_ = false;
};

SpriteSet::~SpriteSet() {
  if (hydrator_) hydrator_->abandon();
  for (auto& kv : cache_) DeleteObject(kv.second.bmp);
  if (transient_.bmp) DeleteObject(transient_.bmp);
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

int SpriteSet::variantIndex(Anim a, const std::wstring& name) const {
  const auto& n = names_[(int)a];
  for (size_t i = 0; i < n.size(); ++i)
    if (n[i] == name) return (int)i;
  return -1;
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
    f.id = ++serial_;
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
      std::vector<std::wstring> files = listDir(dir + L"\\" + vf, false, L"*.bpf");  // fast frames first
      if (files.empty()) files = listDir(dir + L"\\" + vf, false, L"*.png");
      if (files.empty()) continue;
      for (auto& f : files) f = dir + L"\\" + vf + L"\\" + f;
      paths_[ai].push_back(files);
      names_[ai].push_back(vf);
    }
  }
  if (paths_[(int)Anim::Idle].empty()) {
    if (err) *err = L"动画形象缺少 idle 帧：\n" + dir;
    return false;
  }
  // "Size" for chibis: the character is 80% of it, so paintings and chibis look alike;
  // animated paintings (Live2D, dynamic paintings) use size_ratio=1: the picture height.
  int size = exact ? pet::clampHeight(height) : height;
  scale_ = (std::min)(2.0, meta.getDouble("sequence", "size_ratio", 0.8) * size / charH);
  walks_ = meta.getInt("sequence", "walk", 1) != 0;
  // Painting-sized frames (dynamic paintings, Live2D) would need 100+ MB cached: decode each
  // one when shown instead (.bpf frames decode in ~1 ms plus the scale).
  stream_ = meta.get("sequence", "kind", "") == "painting";
  idleMinMs_ = meta.getInt("sequence", "idle_min_ms", 0);
  idleMaxMs_ = meta.getInt("sequence", "idle_max_ms", 0);
  size_ = size;
  cw_ = (std::max)(1, (int)std::lround(srcW * scale_));
  ch_ = (std::max)(1, (int)std::lround(srcH * scale_));
  headTop_ = (int)std::lround(meta.getInt("sequence", "head_top", 0) * scale_);
  groundInset_ = (int)std::lround(meta.getInt("sequence", "ground", 0) * scale_);
  headFraction_ = meta.getDouble("sequence", "head_fraction", 0.45);
  fps_ = meta.getInt("sequence", "fps", 10);
  // Make the frame files local in the background (a cloud-synced folder keeps them online-only
  // until something reads them): what she shows most of the time first, then the rest.
  std::vector<std::wstring> files;
  for (Anim a : {Anim::Idle, Anim::Walk, Anim::Blink, Anim::React, Anim::Drag, Anim::Fall, Anim::Land, Anim::Sleep})
    for (auto& list : paths_[(int)a]) files.insert(files.end(), list.begin(), list.end());
  hydrator_ = Hydrator::start(std::move(files));
  return true;
}

// Crops a display-size premultiplied BGRA canvas to its visible pixels as a DIB.
SpriteFrame SpriteSet::cropToDib(const uint8_t* px, size_t stride) {
  SpriteFrame f;
  int x0, y0, x1, y1;
  if (!alphaBox((const uint32_t*)px, cw_, ch_, (int)(stride / 4), &x0, &y0, &x1, &y1)) { x0 = y0 = 0; x1 = y1 = 0; }
  f.offX = x0;
  f.offY = y0;
  f.w = x1 - x0 + 1;
  f.h = y1 - y0 + 1;
  void* bits = nullptr;
  f.bmp = newDib(f.w, f.h, &bits);
  f.id = ++serial_;
  if (f.bmp)
    for (int y = 0; y < f.h; ++y)
      std::memcpy((char*)bits + (size_t)y * f.w * 4, px + (size_t)(y0 + y) * stride + (size_t)x0 * 4, (size_t)f.w * 4);
  return f;
}

SpriteFrame SpriteSet::decode(const std::wstring& path) {
  using namespace Gdiplus;
  SpriteFrame f;
  if (!isLocalFile(path)) {  // online-only: fetched in the background; she keeps the frame she has
    if (hydrator_) hydrator_->want(path);
    return f;
  }
  bool bpf = path.size() > 4 && _wcsicmp(path.c_str() + path.size() - 4, L".bpf") == 0;
  if (bpf) {
    // Palette + LZ4 frame: expand at source size, then scale to the display size.
    std::ifstream in(path.c_str(), std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    pet::BpfFrame bf;
    if (data.empty() || !pet::parseBpf(data.data(), data.size(), &bf)) return f;
    std::vector<uint32_t> src((size_t)bf.width * bf.height);
    pet::expandBpf(bf, (uint8_t*)src.data(), (size_t)bf.width * 4);
    if (cw_ <= bf.width && ch_ <= bf.height) {
      std::vector<uint32_t> dst((size_t)cw_ * ch_);
      pet::downscaleBGRA(src.data(), bf.width, bf.height, bf.width, dst.data(), cw_, ch_, cw_);
      return cropToDib((const uint8_t*)dst.data(), (size_t)cw_ * 4);
    }
    // enlarging: let GDI+ interpolate
    Bitmap srcBmp(bf.width, bf.height, bf.width * 4, PixelFormat32bppPARGB, (BYTE*)src.data());
    Bitmap canvas(cw_, ch_, PixelFormat32bppPARGB);
    {
      Graphics g(&canvas);
      g.SetInterpolationMode(InterpolationModeBilinear);
      g.SetPixelOffsetMode(PixelOffsetModeHalf);
      g.DrawImage(&srcBmp, Rect(0, 0, cw_, ch_), 0, 0, bf.width, bf.height, UnitPixel);
    }
    BitmapData bd;
    Rect all(0, 0, cw_, ch_);
    if (canvas.LockBits(&all, ImageLockModeRead, PixelFormat32bppPARGB, &bd) != Ok) return f;
    f = cropToDib((const uint8_t*)bd.Scan0, (size_t)bd.Stride);
    canvas.UnlockBits(&bd);
    return f;
  }
  Bitmap src(path.c_str());
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
  f = cropToDib((const uint8_t*)bd.Scan0, (size_t)bd.Stride);
  canvas.UnlockBits(&bd);
  return f;
}

SpriteFrame SpriteSet::mirror(const SpriteFrame& f) {
  SpriteFrame m = f;
  void* dst = nullptr;
  m.bmp = newDib(f.w, f.h, &dst);
  m.id = ++serial_;
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
  auto& vars = paths_[ai];
  int v = (int)((size_t)variant % vars.size());
  int i = (int)((size_t)index % vars[v].size());
  auto key = std::make_tuple(ai, v, i, mirrored);
  // Idle and walk are on screen most of the time and stay decoded; any other state
  // (reactions, random actions, sleep...) is decoded frame by frame and not kept.
  if (stream_ || (ai != (int)Anim::Idle && ai != (int)Anim::Walk)) {
    if (key == transientKey_ && transient_.bmp) return transient_;
    SpriteFrame plain = decode(vars[v][i]);
    if (!plain.bmp) return plain;  // not available (yet): the shell keeps the frame on screen, which stays valid
    SpriteFrame f = plain;
    if (mirrored) {
      SpriteFrame m = mirror(plain);
      if (m.bmp != plain.bmp) {
        DeleteObject(plain.bmp);
        f = m;
      }
    }
    if (transient_.bmp) DeleteObject(transient_.bmp);
    transient_ = f;
    transientKey_ = key;
    return f;
  }
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
