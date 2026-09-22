// macOS sprite loading: decodes the base picture (or frame sequences) with ImageIO,
// scales it once for the display's backing scale, and keeps the RGBA buffer of the base
// picture for per-pixel hit testing. Poses are applied at run time as CALayer transforms.
#pragma once
#import <CoreGraphics/CoreGraphics.h>

#include <string>
#include <vector>

#include "core/anim.h"
#include "core/pose.h"

namespace petmac {

struct MacFrame {
  CGImageRef image = nullptr;  // not retained by the caller
  pet::Pose pose;              // identity for sequence frames
};

class SpriteSet {
public:
  SpriteSet() = default;
  ~SpriteSet();
  SpriteSet(const SpriteSet&) = delete;
  SpriteSet& operator=(const SpriteSet&) = delete;

  // `height` in points; `scale` is the backing scale factor (1 or 2).
  bool load(const std::string& assetsDir, int height, double scale, std::string* err);

  // Canvas (window) size in points and the picture placement inside it.
  int width() const { return cw_; }
  int height() const { return ch_; }
  int picW() const { return W_; }
  int picH() const { return H_; }
  int padTop() const { return padTop_; }
  int padBottom() const { return padBottom_; }
  int frameCount(pet::Anim a) const { return static_cast<int>(frames_[(int)a].size()); }
  MacFrame get(pet::Anim a, int index) const;

  // (x, y) in canvas points, origin bottom-left (AppKit view coordinates).
  bool hitTest(double x, double y, bool mirrored) const;

private:
  CGImageRef loadScaled(const std::string& path, std::vector<unsigned char>* pixels);

  std::vector<MacFrame> frames_[(int)pet::Anim::Count];
  std::vector<CGImageRef> owned_;
  std::vector<unsigned char> basePixels_;  // RGBA, pxW_ x pxH_
  int pxW_ = 0, pxH_ = 0;
  double scale_ = 1;
  int W_ = 0, H_ = 0, cw_ = 0, ch_ = 0, padTop_ = 0, padBottom_ = 4;
};

}  // namespace petmac
