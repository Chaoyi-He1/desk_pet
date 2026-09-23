#include "mac/sprites.h"

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>
#import <IOSurface/IOSurface.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <sstream>

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
    if (ext && ![[n.pathExtension lowercaseString] isEqualToString:@(ext)]) continue;
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

}  // namespace

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
    }
  }
  if (paths_[(int)Anim::Idle].empty()) {
    if (err) *err = "动画形象缺少 idle 帧：\n" + dir;
    return false;
  }
  int size = exact ? pet::clampHeight(height) : height;
  s_ = std::min(2.0, 0.8 * size / charH);
  size_ = size;
  cw_ = std::max(1, (int)std::lround(srcW_ * s_));
  ch_ = std::max(1, (int)std::lround(srcH_ * s_));
  headTop_ = (int)std::lround(meta.getInt("sequence", "head_top", 0) * s_);
  groundInset_ = (int)std::lround(meta.getInt("sequence", "ground", 0) * s_);
  headFraction_ = meta.getDouble("sequence", "head_fraction", 0.45);
  fps_ = meta.getInt("sequence", "fps", 10);
  return true;
}

const SpriteSet::Cached* SpriteSet::decode(int a, int v, int i) {
  auto key = std::make_tuple(a, v, i);
  auto it = cache_.find(key);
  if (it != cache_.end()) return &it->second;
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

MacFrame SpriteSet::get(Anim a, int variant, int index) {
  MacFrame f;
  if (!animated_) {
    f.image = base_;
    f.pose = pet::poseFor(a, index);
    return f;
  }
  int ai = paths_[(int)a].empty() ? (int)Anim::Idle : (int)a;
  if (ai != lastAnim_) {  // keep idle and walk (most of the time) and the current state
    for (auto it = cache_.begin(); it != cache_.end();) {
      int k = std::get<0>(it->first);
      if (k != (int)Anim::Idle && k != (int)Anim::Walk && k != ai) it = cache_.erase(it);
      else ++it;
    }
    lastAnim_ = ai;
  }
  int v = (int)((size_t)variant % paths_[ai].size());
  int i = (int)((size_t)index % paths_[ai][v].size());
  const Cached* c = decode(ai, v, i);
  if (!c) return f;
  auto key = std::make_tuple(ai, v, i);
  if (!surf_[0]) {
    NSDictionary* props = @{
      (id)kIOSurfaceWidth : @(srcW_), (id)kIOSurfaceHeight : @(srcH_), (id)kIOSurfaceBytesPerElement : @4,
      (id)kIOSurfacePixelFormat : @((uint32_t)'BGRA')
    };
    for (IOSurfaceRef& sf : surf_) sf = IOSurfaceCreate((__bridge CFDictionaryRef)props);
  }
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
  const Cached* c = decode(ai, v, i);
  if (!c) return false;
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
