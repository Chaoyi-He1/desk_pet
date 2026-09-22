#include "mac/sprites.h"

#import <Foundation/Foundation.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>

#include "core/skin.h"

namespace petmac {
namespace {

std::vector<std::string> listPngs(const std::string& dir) {
  std::vector<std::string> out;
  NSArray* names = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:@(dir.c_str()) error:nil];
  for (NSString* n in names)
    if ([[n.pathExtension lowercaseString] isEqualToString:@"png"]) out.push_back(dir + "/" + n.UTF8String);
  std::sort(out.begin(), out.end());
  return out;
}

CGImageRef decode(const std::string& path) {
  NSURL* url = [NSURL fileURLWithPath:@(path.c_str())];
  CGImageSourceRef src = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
  if (!src) return nullptr;
  CGImageRef img = CGImageSourceCreateImageAtIndex(src, 0, nullptr);
  CFRelease(src);
  return img;
}

}  // namespace

SpriteSet::~SpriteSet() {
  for (CGImageRef i : owned_) CGImageRelease(i);
}

// Decodes and scales `path` to pxW_ x pxH_ (fitting the height, centred horizontally).
CGImageRef SpriteSet::loadScaled(const std::string& path, std::vector<unsigned char>* pixels) {
  CGImageRef raw = decode(path);
  if (!raw) return nullptr;
  size_t rw = CGImageGetWidth(raw), rh = CGImageGetHeight(raw);
  std::vector<unsigned char> local;
  std::vector<unsigned char>& buf = pixels ? *pixels : local;
  buf.assign(static_cast<size_t>(pxW_) * pxH_ * 4, 0);
  CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
  CGContextRef ctx = CGBitmapContextCreate(buf.data(), pxW_, pxH_, 8, pxW_ * 4, cs,
                                           kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
  CGContextSetInterpolationQuality(ctx, kCGInterpolationHigh);
  double s = static_cast<double>(pxH_) / rh;
  double dw = rw * s;
  CGContextDrawImage(ctx, CGRectMake((pxW_ - dw) / 2, 0, dw, pxH_), raw);
  CGImageRef img = CGBitmapContextCreateImage(ctx);
  CGContextRelease(ctx);
  CGColorSpaceRelease(cs);
  CGImageRelease(raw);
  if (img) owned_.push_back(img);
  return img;
}

bool SpriteSet::load(const std::string& assetsDir, const std::string& imagePath, int height, bool exactHeight,
                     double scale, std::string* err) {
  using pet::Anim;
  scale_ = scale;
  const std::string& basePath = imagePath;
  CGImageRef probe = decode(basePath);
  std::vector<std::string> idleSeq = listPngs(assetsDir + "/idle");
  if (!probe && idleSeq.empty()) {
    if (err) *err = "没有找到形象文件：\n" + basePath + "\n\n请把透明背景的立绘 PNG 放到 assets/skins 目录后重新启动。";
    return false;
  }
  double refW = 1, refH = 2;
  if (probe) { refW = CGImageGetWidth(probe); refH = CGImageGetHeight(probe); CGImageRelease(probe); }
  else if (CGImageRef f = decode(idleSeq[0])) { refW = CGImageGetWidth(f); refH = CGImageGetHeight(f); CGImageRelease(f); }

  H_ = exactHeight ? pet::clampHeight(height) : pet::displayHeight(height, static_cast<int>(refH));
  W_ = std::max(1, static_cast<int>(std::lround(refW * H_ / refH)));
  int padX = static_cast<int>(std::ceil(0.105 * H_ + 0.02 * W_)) + 2;
  padTop_ = static_cast<int>(std::ceil(0.05 * H_)) + 3;
  padBottom_ = 4;
  cw_ = W_ + 2 * padX;
  ch_ = H_ + padTop_ + padBottom_;
  pxW_ = static_cast<int>(std::lround(W_ * scale_));
  pxH_ = static_cast<int>(std::lround(H_ * scale_));

  CGImageRef base = nullptr;
  if ([[NSFileManager defaultManager] fileExistsAtPath:@(basePath.c_str())]) base = loadScaled(basePath, &basePixels_);

  for (int ai = 0; ai < (int)Anim::Count; ++ai) {
    Anim a = static_cast<Anim>(ai);
    std::vector<MacFrame>& out = frames_[ai];
    for (const std::string& p : listPngs(assetsDir + "/" + pet::animName(a))) {
      std::vector<unsigned char>* keep = (base == nullptr && basePixels_.empty()) ? &basePixels_ : nullptr;
      CGImageRef img = loadScaled(p, keep);
      if (img) out.push_back({img, pet::Pose()});
    }
    if (out.empty() && base) {
      int n = pet::poseFrameCount(a);
      for (int i = 0; i < n; ++i) out.push_back({base, pet::poseFor(a, i)});
    }
  }
  for (int ai = 0; ai < (int)Anim::Count; ++ai)
    if (frames_[ai].empty()) frames_[ai] = frames_[(int)Anim::Idle];
  if (frames_[(int)Anim::Idle].empty()) {
    if (err) *err = "形象文件无法解码，请确认它是有效的 PNG。";
    return false;
  }
  return true;
}

MacFrame SpriteSet::get(pet::Anim a, int index) const {
  const std::vector<MacFrame>& v = frames_[(int)a];
  if (v.empty()) return {};
  return v[static_cast<size_t>(index) % v.size()];
}

bool SpriteSet::hitTest(double x, double y, bool mirrored) const {
  if (basePixels_.empty()) return true;
  double px = x - (cw_ - W_) / 2.0;
  double py = y - padBottom_;  // from the picture's bottom edge, upwards
  if (px < 0 || py < 0 || px >= W_ || py >= H_) return false;
  if (mirrored) px = W_ - 1 - px;
  int ix = std::min(pxW_ - 1, std::max(0, static_cast<int>(px * scale_)));
  int iy = std::min(pxH_ - 1, std::max(0, static_cast<int>((H_ - 1 - py) * scale_)));
  // Be forgiving near edges: hit when any pixel in a small neighbourhood is opaque.
  for (int dy = -2; dy <= 2; ++dy)
    for (int dx = -2; dx <= 2; ++dx) {
      int sx = std::min(pxW_ - 1, std::max(0, ix + dx)), sy = std::min(pxH_ - 1, std::max(0, iy + dy));
      if (basePixels_[(static_cast<size_t>(sy) * pxW_ + sx) * 4 + 3] > 24) return true;
    }
  return false;
}

}  // namespace petmac
