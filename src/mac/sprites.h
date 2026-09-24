// macOS pet images.
//
// Paintings: the picture is decoded and scaled once; poses become CALayer transforms,
// so the compositor does the drawing. Animated chibis: frames are decoded lazily with
// ImageIO, cropped to their visible pixels, and only idle plus the current state stay
// in memory, as 8-bit palette indices (the frames are 256-colour PNGs), and the current
// frame is expanded to BGRA only when shown; the layer scales it.
#pragma once
#import <CoreGraphics/CoreGraphics.h>
#import <IOSurface/IOSurface.h>

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "core/anim.h"
#include "core/pose.h"

namespace petmac {

struct MacFrame {
  CGImageRef image = nullptr;      // paintings: the picture (owned by the SpriteSet)
  pet::Pose pose;                  // paintings only
  IOSurfaceRef surface = nullptr;  // animated: the whole canvas, current frame drawn in
};

class SpriteSet {
public:
  SpriteSet() = default;
  ~SpriteSet();
  SpriteSet(const SpriteSet&) = delete;
  SpriteSet& operator=(const SpriteSet&) = delete;

  // `path`: a .png painting or an animated-skin folder; see the Windows SpriteSet.
  bool load(const std::string& path, int height, bool exactHeight, double backingScale, std::string* err);

  bool animated() const { return animated_; }
  int width() const { return cw_; }  // canvas, points
  int height() const { return ch_; }
  int size() const { return size_; }
  int picW() const { return W_; }    // paintings: picture size inside the canvas
  int picH() const { return H_; }
  int padBottom() const { return padBottom_; }
  int headTop() const { return headTop_; }
  int groundInset() const { return groundInset_; }
  double headFraction() const { return headFraction_; }
  int fps() const { return fps_; }
  bool walks() const { return walks_; }
  int idleMinMs() const { return idleMinMs_; }
  int idleMaxMs() const { return idleMaxMs_; }
  std::vector<int> variants(pet::Anim a) const;
  // Index of the variant loaded from folder `name` (e.g. "react_head"), -1 if none.
  int variantIndex(pet::Anim a, const std::string& name) const;

  MacFrame get(pet::Anim a, int variant, int index);
  // (x, y) in canvas points, origin bottom-left.
  bool hitTest(double x, double y, bool mirrored, pet::Anim a, int variant, int index);

private:
  struct Cached {
    int x0 = 0, y0 = 0, w = 0, h = 0;  // crop in source pixels (y down)
    std::vector<unsigned char> index;  // w*h palette indices (empty if the frame has >256 colours)
    std::vector<uint32_t> palette;     // premultiplied BGRA
    std::vector<uint32_t> bgra;        // full-colour fallback
    unsigned char alphaAt(int x, int y) const {
      uint32_t c = index.empty() ? bgra[(size_t)y * w + x] : palette[index[(size_t)y * w + x]];
      return (unsigned char)(c >> 24);
    }
  };
  bool loadPainting(const std::string& png, int height, bool exact, double scale, std::string* err);
  bool loadAnimated(const std::string& dir, int height, bool exact, std::string* err);
  const Cached* decode(int a, int v, int i);
  void blitFile(const std::string& png);  // stream a frame straight into the next surface
  void ensureSurfaces();
  bool copyPixels(CGImageRef img, unsigned char* dst, size_t dstStride);

  bool animated_ = false;
  int cw_ = 0, ch_ = 0, size_ = 0, W_ = 0, H_ = 0, padBottom_ = 4, headTop_ = 0, groundInset_ = 0, fps_ = 0;
  double headFraction_ = 0.22;
  bool walks_ = true;
  bool stream_ = false;  // animated paintings: every frame is decoded from disk, nothing cached
  int idleMinMs_ = 0, idleMaxMs_ = 0;

  // painting
  CGImageRef base_ = nullptr;
  std::vector<unsigned char> basePixels_;  // RGBA, pxW_ x pxH_
  int pxW_ = 0, pxH_ = 0;
  double scale_ = 1;
  // animated
  int srcW_ = 0, srcH_ = 0;
  double s_ = 1;  // source px -> points
  std::vector<std::vector<std::string>> paths_[(int)pet::Anim::Count];
  std::vector<std::string> names_[(int)pet::Anim::Count];  // folder of each variant
  std::map<std::tuple<int, int, int>, Cached> cache_;
  std::vector<unsigned char> scratch_;  // full-frame decode buffer
  // Animated frames are expanded into one of two canvas-sized IOSurfaces (alternating,
  // so Core Animation always gets a surface it is not showing); no per-frame images.
  IOSurfaceRef surf_[2] = {nullptr, nullptr};
  CGRect used_[2] = {CGRectNull, CGRectNull};  // pixels last written, to clear next time
  int surfIdx_ = 0;
  std::tuple<int, int, int> currentKey_{-1, -1, -1};
};

}  // namespace petmac
