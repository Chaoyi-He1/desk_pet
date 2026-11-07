#include "mac/sprites.h"

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <IOSurface/IOSurface.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <fstream>
#include <iterator>
#include <mutex>
#include <sstream>
#include <thread>
#include <unordered_map>

#include "core/frames.h"
#include "core/ini.h"
#include "core/skin.h"

namespace petmac {
namespace {

using pet::Anim;

std::vector<std::string> listDir(const std::string& dir, bool wantDirs, const char* ext) {
  std::vector<std::string> out;
  NSFileManager* fm = [NSFileManager defaultManager];
  for (NSString* n in [fm contentsOfDirectoryAtPath:@(dir.c_str()) error:nil]) {
    if ([n hasPrefix:@"."]) continue;
    BOOL isDir = NO;
    [fm fileExistsAtPath:[@(dir.c_str()) stringByAppendingPathComponent:n] isDirectory:&isDir];
    if ((bool)isDir != wantDirs) continue;
    if (ext) {  // "png" also accepts the fast .bpf frames
      NSString* e = [n.pathExtension lowercaseString];
      if (![e isEqualToString:@(ext)] && !(strcmp(ext, "png") == 0 && [e isEqualToString:@"bpf"])) continue;
    }
    out.push_back(n.precomposedStringWithCanonicalMapping.UTF8String);
  }
  std::sort(out.begin(), out.end());
  return out;
}

CGImageRef decodeFile(const std::string& path) {
  NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
  CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
  if (!src) return nullptr;
  CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
  CFRelease(src);
  return img;
}

std::string readAll(const std::string& p) {
  std::ifstream in(p, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

#ifndef SF_DATALESS
#define SF_DATALESS 0x40000000
#endif

// An "online-only" file in a cloud-synced folder is a dataless placeholder: the first read
// downloads it. Reading one on the main thread freezes her, so such frames are not decoded
// until the prefetcher has read them (she keeps the frame she has). stat() does not fetch.
bool isLocalFile(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) return true;  // missing or unreadable: let the decoder report it
  return (st.st_flags & SF_DATALESS) == 0;
}

// Reads the whole file and drops the data: the sync client fetches it and the file is local.
void readThrough(const std::string& path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) return;
  std::vector<char> buf(1 << 16);
  while (read(fd, buf.data(), buf.size()) > 0) {
  }
  close(fd);
}

}  // namespace

// The reader threads behind prefetchWant / prefetchFiles / prefetchDirs, one set for the whole
// process (they live as long as it does). A placeholder read is mostly waiting on the network,
// so eight at once finish a skin several times sooner than one after another. Priority 0 is
// what the main thread asked for (newest first), 1 her current skin in the order she needs it,
// 2 the meta.ini of every skin (a switch reads it on the main thread), 3 the rest of her ship,
// 4 the other ships. Same design as the Windows one in win/sprites.cpp.
class Prefetcher {
public:
  static Prefetcher& get() {
    static Prefetcher p;
    return p;
  }

  void want(const std::string& path) {
    {
      std::lock_guard<std::mutex> g(lock_);
      urgent_.erase(std::remove(urgent_.begin(), urgent_.end(), path), urgent_.end());
      urgent_.push_back(path);
      startReaders();
    }
    wake_.notify_one();
  }

  void files(std::vector<std::string> paths, int priority) {
    priority = std::max(1, std::min(priority, kLevels - 1));
    {
      std::lock_guard<std::mutex> g(lock_);
      if (priority == 1) {  // a new current skin: what is left of the previous one can wait
        std::deque<std::string>& prev = queues_[1];
        queues_[kRestOfShip].insert(queues_[kRestOfShip].end(), prev.begin(), prev.end());
        prev.clear();
      }
      queues_[priority].insert(queues_[priority].end(), paths.begin(), paths.end());
      startReaders();
    }
    wake_.notify_all();
  }

  void dirs(std::vector<std::string> skinsDirs, int priority) {
    bool start;
    {
      std::lock_guard<std::mutex> g(lock_);
      for (auto& d : skinsDirs) dirs_.push_back({d, priority});
      start = !listing_;
      listing_ = true;
    }
    if (start) std::thread([this] { listMain(); }).detach();
  }

private:
  static const int kLevels = 5, kMeta = 2, kRestOfShip = 3, kReaders = 8;

  void startReaders() {  // under lock_
    if (readers_) return;
    for (int i = 0; i < kReaders; ++i) {
      std::thread([this] { readMain(); }).detach();
      ++readers_;
    }
  }

  bool next(std::string* path) {  // under lock_
    if (!urgent_.empty()) {
      *path = urgent_.back();
      urgent_.pop_back();
      return true;
    }
    for (int i = 1; i < kLevels; ++i) {
      if (queues_[i].empty()) continue;
      *path = queues_[i].front();
      queues_[i].pop_front();
      return true;
    }
    return false;
  }

  void readMain() {
    for (;;) {
      std::string path;
      {
        std::unique_lock<std::mutex> g(lock_);
        wake_.wait(g, [&] { return next(&path); });
      }
      if (!isLocalFile(path)) readThrough(path);
    }
  }

  // A skin folder: its meta.ini, then its frames, idle and walk first.
  static void listSkin(const std::string& skin, std::vector<std::string>* metas, std::vector<std::string>* frames) {
    metas->push_back(skin + "/meta.ini");
    std::vector<std::string> folders = listDir(skin, true, nullptr);
    auto rank = [](const std::string& f) { return f.rfind("idle", 0) == 0 ? 0 : f.rfind("walk", 0) == 0 ? 1 : 2; };
    std::stable_sort(folders.begin(), folders.end(), [&](const std::string& a, const std::string& b) { return rank(a) < rank(b); });
    for (auto& f : folders)
      for (auto& file : listDir(skin + "/" + f, false, nullptr)) frames->push_back(skin + "/" + f + "/" + file);
  }

  // Lists the queued skins folders (directory listings do not fetch anything) and hands the
  // readers every meta.ini first, then each ship's paintings and frames.
  void listMain() {
    for (;;) {
      std::vector<std::pair<std::string, int>> batch;
      {
        std::lock_guard<std::mutex> g(lock_);
        batch.swap(dirs_);
        if (batch.empty()) listing_ = false;
      }
      if (batch.empty()) break;
      std::vector<std::string> metas;
      std::vector<std::vector<std::string>> frames(batch.size());
      @autoreleasepool {
        for (size_t i = 0; i < batch.size(); ++i) {
          const std::string& dir = batch[i].first;
          for (auto& png : listDir(dir, false, "png")) frames[i].push_back(dir + "/" + png);
          for (auto& sub : listDir(dir, true, nullptr)) listSkin(dir + "/" + sub, &metas, &frames[i]);
        }
      }
      files(std::move(metas), kMeta);
      for (size_t i = 0; i < batch.size(); ++i) files(std::move(frames[i]), batch[i].second);
    }
  }

  std::mutex lock_;
  std::condition_variable wake_;
  std::vector<std::string> urgent_;
  std::deque<std::string> queues_[kLevels];
  std::vector<std::pair<std::string, int>> dirs_;
  int readers_ = 0;
  bool listing_ = false;
};

void prefetchWant(const std::string& path) { Prefetcher::get().want(path); }
void prefetchFiles(std::vector<std::string> files, int priority) { Prefetcher::get().files(std::move(files), priority); }
void prefetchDirs(std::vector<std::string> skinsDirs, int priority) { Prefetcher::get().dirs(std::move(skinsDirs), priority); }

SpriteSet::~SpriteSet() {
  if (base_) CGImageRelease(base_);
  for (IOSurfaceRef sf : surf_)
    if (sf) CFRelease(sf);
}

std::vector<int> SpriteSet::variants(Anim a) const {
  std::vector<int> v;
  if (animated_) {
    for (auto& list : paths_[(int)a]) v.push_back((int)list.size());
  } else {
    v.push_back(pet::poseFrameCount(a));
  }
  return v;
}

int SpriteSet::variantIndex(Anim a, const std::string& name) const {
  const auto& n = names_[(int)a];
  for (size_t i = 0; i < n.size(); ++i)
    if (n[i] == name) return (int)i;
  return -1;
}

bool SpriteSet::load(const std::string& path, int height, bool exactHeight, double backingScale, std::string* err) {
  BOOL isDir = NO;
  [[NSFileManager defaultManager] fileExistsAtPath:@(path.c_str()) isDirectory:&isDir];
  return isDir ? loadAnimated(path, height, exactHeight, err) : loadPainting(path, height, exactHeight, backingScale, err);
}

// ---------------------------------------------------------------- paintings

bool SpriteSet::loadPainting(const std::string& png, int height, bool exact, double scale, std::string* err) {
  scale_ = scale;
  CGImageRef raw = decodeFile(png);
  if (!raw) {
    if (err) *err = "无法读取形象文件：\n" + png;
    return false;
  }
  double refW = CGImageGetWidth(raw), refH = CGImageGetHeight(raw);
  H_ = exact ? pet::clampHeight(height) : pet::displayHeight(height, (int)refH);
  W_ = std::max(1, (int)std::lround(refW * H_ / refH));
  int padX = (int)std::ceil(0.105 * H_ + 0.02 * W_) + 2;
  int padTop = (int)std::ceil(0.05 * H_) + 3;
  padBottom_ = 4;
  cw_ = W_ + 2 * padX;
  ch_ = H_ + padTop + padBottom_;
  size_ = H_;
  headTop_ = padTop;
  headFraction_ = 0.22;
  pxW_ = (int)std::lround(W_ * scale_);
  pxH_ = (int)std::lround(H_ * scale_);
  basePixels_.assign((size_t)pxW_ * pxH_ * 4, 0);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(basePixels_.data(), pxW_, pxH_, 8, pxW_ * 4, cs,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
  CGContextDrawImage(ctx, CGRectMake(0, 0, pxW_, pxH_), raw);
  base_ = CGBitmapContextCreateImage(ctx);
  CGContextRelease(ctx);
  CGColorSpaceRelease(cs);
  CGImageRelease(raw);
  return base_ != nullptr;
}

// ---------------------------------------------------------------- animated

bool SpriteSet::loadAnimated(const std::string& dir, int height, bool exact, std::string* err) {
  animated_ = true;
  pet::Ini meta = pet::Ini::parse(readAll(dir + "/meta.ini"));
  srcW_ = meta.getInt("sequence", "width", 0);
  srcH_ = meta.getInt("sequence", "height", 0);
  int charH = meta.getInt("sequence", "char_height", srcH_);
  if (srcW_ <= 0 || srcH_ <= 0 || charH <= 0) {
    if (err) *err = "动画形象缺少 meta.ini：\n" + dir;
    return false;
  }
  std::vector<std::string> folders = listDir(dir, true, nullptr);
  for (int ai = 0; ai < (int)Anim::Count; ++ai) {
    std::string name = pet::animName((Anim)ai);
    std::vector<std::string> vf;
    for (auto& f : folders)
      if (f == name) vf.insert(vf.begin(), f);
      else if (f.rfind(name + "_", 0) == 0) vf.push_back(f);
    for (auto& v : vf) {
      std::vector<std::string> files = listDir(dir + "/" + v, false, "png");
      if (files.empty()) continue;
      for (auto& f : files) f = dir + "/" + v + "/" + f;
      paths_[ai].push_back(files);
      names_[ai].push_back(v);
    }
  }
  if (paths_[(int)Anim::Idle].empty()) {
    if (err) *err = "动画形象缺少 idle 帧：\n" + dir;
    return false;
  }
  int size = exact ? pet::clampHeight(height) : height;
  s_ = std::min(2.0, meta.getDouble("sequence", "size_ratio", 0.8) * size / charH);
  walks_ = meta.getInt("sequence", "walk", 1) != 0;
  // Painting-sized frames (dynamic paintings, Live2D) would need 50-200 MB cached; decode
  // each one when shown instead (a palette PNG decodes in a few ms at 8 fps).
  stream_ = meta.get("sequence", "kind", "") == "painting";
  idleMinMs_ = meta.getInt("sequence", "idle_min_ms", 0);
  idleMaxMs_ = meta.getInt("sequence", "idle_max_ms", 0);
  size_ = size;
  cw_ = std::max(1, (int)std::lround(srcW_ * s_));
  ch_ = std::max(1, (int)std::lround(srcH_ * s_));
  headTop_ = (int)std::lround(meta.getInt("sequence", "head_top", 0) * s_);
  groundInset_ = (int)std::lround(meta.getInt("sequence", "ground", 0) * s_);
  headFraction_ = meta.getDouble("sequence", "head_fraction", 0.45);
  fps_ = meta.getInt("sequence", "fps", 10);
  // Make the frame files local in the background (a cloud-synced folder keeps them online-only
  // until something reads them): what she shows most of the time first, then the rest.
  std::vector<std::string> files;
  for (Anim a : {Anim::Idle, Anim::Walk, Anim::Blink, Anim::React, Anim::Drag, Anim::Fall, Anim::Land, Anim::Sleep})
    for (auto& list : paths_[(int)a]) files.insert(files.end(), list.begin(), list.end());
  prefetchFiles(std::move(files), 1);
  return true;
}

static bool readBpf(const std::string& path, pet::BpfFrame* f) {
  std::ifstream in(path, std::ios::binary);
  std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  return !data.empty() && pet::parseBpf(data.data(), data.size(), f);
}

static bool isBpf(const std::string& p) { return p.size() > 4 && p.compare(p.size() - 4, 4, ".bpf") == 0; }

const SpriteSet::Cached* SpriteSet::decode(int a, int v, int i) {
  auto key = std::make_tuple(a, v, i);
  auto it = cache_.find(key);
  if (it != cache_.end()) return &it->second;
  if (!isLocalFile(paths_[a][v][i])) {  // online-only: fetched in the background; she keeps the frame she has
    prefetchWant(paths_[a][v][i]);
    return nullptr;
  }
  if (isBpf(paths_[a][v][i])) {  // already palette + indices: no pixel work at all
    pet::BpfFrame f;
    if (!readBpf(paths_[a][v][i], &f) || f.width != srcW_ || f.height != srcH_) return nullptr;
    Cached c;
    c.x0 = f.x0; c.y0 = f.y0; c.w = f.w; c.h = f.h;
    c.index = std::move(f.index);
    c.palette = std::move(f.palette);
    return &cache_.emplace(key, std::move(c)).first->second;
  }
  CGImageRef img = decodeFile(paths_[a][v][i]);
  if (!img) return nullptr;
  const int w = (int)CGImageGetWidth(img), h = (int)CGImageGetHeight(img);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  scratch_.assign((size_t)w * h * 4, 0);
  CGContextRef full = CGBitmapContextCreate(scratch_.data(), w, h, 8, w * 4, cs,
                                            kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
  CGContextDrawImage(full, CGRectMake(0, 0, w, h), img);
  CGContextRelease(full);
  CGColorSpaceRelease(cs);
  CGImageRelease(img);
  const uint32_t* px = (const uint32_t*)scratch_.data();  // rows top-down
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; ++y)
    for (int x = 0; x < w; ++x)
      if ((px[(size_t)y * w + x] >> 24) > 2) {
        x0 = std::min(x0, x); x1 = std::max(x1, x);
        y0 = std::min(y0, y); y1 = std::max(y1, y);
      }
  if (x1 < x0) { x0 = y0 = 0; x1 = y1 = 0; }
  Cached c;
  c.x0 = x0; c.y0 = y0; c.w = x1 - x0 + 1; c.h = y1 - y0 + 1;
  // The frames are 256-colour PNGs: keep one byte per pixel plus a palette.
  std::unordered_map<uint32_t, unsigned char> lut;
  lut.reserve(512);
  uint32_t lastCol = 0x01000000;  // not a premultiplied colour, so never matches
  unsigned char lastIdx = 0;
  c.index.resize((size_t)c.w * c.h);
  bool indexed = true;
  for (int y = 0; y < c.h && indexed; ++y)
    for (int x = 0; x < c.w; ++x) {
      uint32_t col = px[(size_t)(y0 + y) * w + x0 + x];
      if (col == lastCol) {  // runs of one colour are common: skip the hash lookup
        c.index[(size_t)y * c.w + x] = lastIdx;
        continue;
      }
      auto f = lut.find(col);
      if (f == lut.end()) {
        if (lut.size() == 256) { indexed = false; break; }
        f = lut.emplace(col, (unsigned char)lut.size()).first;
        c.palette.push_back(col);
      }
      lastCol = col;
      lastIdx = f->second;
      c.index[(size_t)y * c.w + x] = f->second;
    }
  if (!indexed) {  // more than 256 colours: keep full colour
    c.index.clear();
    c.palette.clear();
    c.bgra.resize((size_t)c.w * c.h);
    for (int y = 0; y < c.h; ++y)
      std::memcpy(&c.bgra[(size_t)y * c.w], &px[(size_t)(y0 + y) * w + x0], (size_t)c.w * 4);
  }
  std::vector<unsigned char>().swap(scratch_);  // decoding is rare: give the memory back
  return &cache_.emplace(key, std::move(c)).first->second;
}

void SpriteSet::ensureSurfaces() {
  if (surf_[0]) return;
  NSDictionary* props = @{
    (id)kIOSurfaceWidth : @(srcW_), (id)kIOSurfaceHeight : @(srcH_), (id)kIOSurfaceBytesPerElement : @4,
    (id)kIOSurfacePixelFormat : @((uint32_t)'BGRA')
  };
  for (IOSurfaceRef& sf : surf_) sf = IOSurfaceCreate((__bridge CFDictionaryRef)props);
}

// Fast path for our frames: 8-bit RGBA (ImageIO expands palette PNGs to it). Copies the
// decoded pixels straight into a premultiplied BGRA buffer, skipping Core Graphics'
// colour matching and blending, which cost ~5x the PNG decode itself.
bool SpriteSet::copyPixels(CGImageRef img, unsigned char* dst, size_t dstStride) {
  const size_t w = CGImageGetWidth(img), h = CGImageGetHeight(img);
  if ((int)w != srcW_ || (int)h != srcH_ || CGImageGetBitsPerComponent(img) != 8 || CGImageGetBitsPerPixel(img) != 32)
    return false;
  CGImageAlphaInfo ai = CGImageGetAlphaInfo(img);
  CGBitmapInfo order = CGImageGetBitmapInfo(img) & kCGBitmapByteOrderMask;
  bool premul = ai == kCGImageAlphaPremultipliedLast;
  if ((ai != kCGImageAlphaLast && !premul) || (order != kCGBitmapByteOrderDefault && order != kCGBitmapByteOrder32Big))
    return false;
  CGColorSpaceModel model = CGColorSpaceGetModel(CGImageGetColorSpace(img));
  if (model != kCGColorSpaceModelRGB) return false;
  CFDataRef data = CGDataProviderCopyData(CGImageGetDataProvider(img));
  if (!data) return false;
  const unsigned char* src = CFDataGetBytePtr(data);
  const size_t srcStride = CGImageGetBytesPerRow(img);
  for (size_t y = 0; y < h; ++y) {
    const unsigned char* s = src + y * srcStride;  // R G B A
    unsigned char* d = dst + y * dstStride;         // B G R A (little-endian ARGB)
    for (size_t x = 0; x < w; ++x, s += 4, d += 4) {
      unsigned a = s[3];
      if (a == 0) continue;  // buffer was cleared
      if (premul || a == 255) {
        d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
      } else {
        d[0] = (unsigned char)((s[2] * a + 127) / 255);
        d[1] = (unsigned char)((s[1] * a + 127) / 255);
        d[2] = (unsigned char)((s[0] * a + 127) / 255);
      }
      d[3] = (unsigned char)a;
    }
  }
  CFRelease(data);
  return true;
}

void SpriteSet::blitFile(const std::string& png) {
  ensureSurfaces();
  if (isBpf(png)) {
    pet::BpfFrame f;
    bool ok = readBpf(png, &f) && f.width == srcW_ && f.height == srcH_;
    surfIdx_ ^= 1;
    IOSurfaceRef sf = surf_[surfIdx_];
    IOSurfaceLock(sf, 0, nullptr);
    uint8_t* base = (uint8_t*)IOSurfaceGetBaseAddress(sf);
    size_t stride = IOSurfaceGetBytesPerRow(sf);
    if (ok) pet::expandBpf(f, base, stride);
    else std::memset(base, 0, stride * (size_t)srcH_);
    IOSurfaceUnlock(sf, 0, nullptr);
    used_[surfIdx_] = CGRectMake(0, 0, srcW_, srcH_);
    return;
  }
  CGImageRef img = decodeFile(png);
  surfIdx_ ^= 1;
  IOSurfaceRef sf = surf_[surfIdx_];
  IOSurfaceLock(sf, 0, nullptr);
  void* base = IOSurfaceGetBaseAddress(sf);
  size_t stride = IOSurfaceGetBytesPerRow(sf);
  std::memset(base, 0, stride * (size_t)srcH_);
  if (img && copyPixels(img, (unsigned char*)base, stride)) {
    CGImageRelease(img);
    img = nullptr;
  }
  if (img) {  // unusual pixel format: let Core Graphics convert it
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
    CGContextRef ctx = CGBitmapContextCreate(base, srcW_, srcH_, 8, stride, cs,
                                             kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
    CGContextDrawImage(ctx, CGRectMake(0, 0, srcW_, srcH_), img);
    CGContextRelease(ctx);
    CGColorSpaceRelease(cs);
    CGImageRelease(img);
  }
  IOSurfaceUnlock(sf, 0, nullptr);
  used_[surfIdx_] = CGRectMake(0, 0, srcW_, srcH_);  // whole surface was rewritten
}

MacFrame SpriteSet::get(Anim a, int variant, int index) {
  MacFrame f;
  if (!animated_) {
    f.image = base_;
    f.pose = pet::poseFor(a, index);
    return f;
  }
  int ai = paths_[(int)a].empty() ? (int)Anim::Idle : (int)a;
  int v = (int)((size_t)variant % paths_[ai].size());
  int i = (int)((size_t)index % paths_[ai][v].size());
  if (stream_ || (ai != (int)Anim::Idle && ai != (int)Anim::Walk)) {
    // Reactions, random actions, sleep...: decoded frame by frame straight into the
    // display surface and not kept, so memory only holds the idle and walk loops.
    auto key = std::make_tuple(ai, v, i);
    if (key != currentKey_) {
      if (!isLocalFile(paths_[ai][v][i])) {  // online-only: fetched in the background; she keeps the frame she has
        prefetchWant(paths_[ai][v][i]);
        return f;
      }
      blitFile(paths_[ai][v][i]);
      currentKey_ = key;
    }
    f.surface = surf_[surfIdx_];
    return f;
  }
  const Cached* c = decode(ai, v, i);
  if (!c) return f;
  auto key = std::make_tuple(ai, v, i);
  ensureSurfaces();

  if (key != currentKey_) {
    surfIdx_ ^= 1;
    IOSurfaceRef sf = surf_[surfIdx_];
    IOSurfaceLock(sf, 0, nullptr);
    unsigned char* base = (unsigned char*)IOSurfaceGetBaseAddress(sf);
    size_t stride = IOSurfaceGetBytesPerRow(sf);
    CGRect old = used_[surfIdx_];  // clear what this surface showed two frames ago
    if (!CGRectIsNull(old))
      for (int y = (int)old.origin.y; y < (int)CGRectGetMaxY(old); ++y)
        std::memset(base + (size_t)y * stride + (size_t)old.origin.x * 4, 0, (size_t)old.size.width * 4);
    for (int y = 0; y < c->h; ++y) {
      uint32_t* row = (uint32_t*)(base + (size_t)(c->y0 + y) * stride) + c->x0;
      if (c->index.empty()) {
        std::memcpy(row, &c->bgra[(size_t)y * c->w], (size_t)c->w * 4);
      } else {
        const unsigned char* ix = &c->index[(size_t)y * c->w];
        for (int x = 0; x < c->w; ++x) row[x] = c->palette[ix[x]];
      }
    }
    IOSurfaceUnlock(sf, 0, nullptr);
    used_[surfIdx_] = CGRectMake(c->x0, c->y0, c->w, c->h);
    currentKey_ = key;
  }
  f.surface = surf_[surfIdx_];
  return f;
}

bool SpriteSet::hitTest(double x, double y, bool mirrored, Anim a, int variant, int index) {
  if (!animated_) {
    if (basePixels_.empty()) return true;
    double px = x - (cw_ - W_) / 2.0;
    double py = y - padBottom_;
    if (px < 0 || py < 0 || px >= W_ || py >= H_) return false;
    if (mirrored) px = W_ - 1 - px;
    int ix = std::min(pxW_ - 1, std::max(0, (int)(px * scale_)));
    int iy = std::min(pxH_ - 1, std::max(0, (int)((H_ - 1 - py) * scale_)));
    for (int dy = -2; dy <= 2; ++dy)
      for (int dx = -2; dx <= 2; ++dx) {
        int sx = std::min(pxW_ - 1, std::max(0, ix + dx)), sy = std::min(pxH_ - 1, std::max(0, iy + dy));
        if (basePixels_[((size_t)sy * pxW_ + sx) * 4 + 3] > 24) return true;
      }
    return false;
  }
  int ai = paths_[(int)a].empty() ? (int)Anim::Idle : (int)a;
  int v = (int)((size_t)variant % paths_[ai].size());
  int i = (int)((size_t)index % paths_[ai][v].size());
  auto key = std::make_tuple(ai, v, i);
  const Cached* c;
  if (stream_ || (ai != (int)Anim::Idle && ai != (int)Anim::Walk)) {  // streamed: not kept in cache_
    if (key != hitKey_) {
      if (!decode(ai, v, i)) return false;
      auto it = cache_.find(key);
      hitFrame_ = std::move(it->second);
      cache_.erase(it);
      hitKey_ = key;
    }
    c = &hitFrame_;
  } else {
    c = decode(ai, v, i);
    if (!c) return false;
  }
  if (mirrored) x = cw_ - x;
  int sx = (int)(x / s_) - c->x0;
  int sy = (int)((ch_ - y) / s_) - c->y0;
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx) {
      int qx = sx + dx, qy = sy + dy;
      if (qx >= 0 && qy >= 0 && qx < c->w && qy < c->h && c->alphaAt(qx, qy) > 24) return true;
    }
  return false;
}

}  // namespace petmac
