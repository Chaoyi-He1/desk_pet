// Loads the base picture (or frame sequences) and pre-renders every pose into a
// premultiplied 32-bit top-down DIB. After loading, GDI+ is shut down; at run time a
// frame switch only selects a different HBITMAP.
#pragma once
#include <windows.h>

#include <string>
#include <vector>

#include "core/anim.h"

namespace petwin {

struct SpriteFrame {
  HBITMAP bmp = nullptr;   // bitmap to present (may be the shared mirror scratch)
  HBITMAP src = nullptr;   // underlying un-mirrored bitmap; (src, mirrored) identifies the content
  bool mirrored = false;
  int offX = 0, offY = 0;  // window offset for translation-only poses
};

class SpriteSet {
public:
  SpriteSet() = default;
  ~SpriteSet();
  SpriteSet(const SpriteSet&) = delete;
  SpriteSet& operator=(const SpriteSet&) = delete;

  // `height` is the on-screen height of the picture in pixels. On failure returns
  // false and fills *err with a message for the user.
  bool load(const std::wstring& assetsDir, int height, std::wstring* err);

  int width() const { return cw_; }    // canvas (window) size, same for all frames
  int height() const { return ch_; }
  int padTop() const { return padTop_; }
  int frameCount(pet::Anim a) const { return static_cast<int>(frames_[(int)a].size()); }

  // Mirrored frames are produced on demand into a scratch DIB (one row-reverse pass,
  // only when the requested source changes).
  SpriteFrame get(pet::Anim a, int index, bool mirrored);

private:
  struct Owned { HBITMAP bmp; void* bits; };
  HBITMAP newDib(void** bits);
  void* bitsOf(HBITMAP bmp) const;

  std::vector<SpriteFrame> frames_[(int)pet::Anim::Count];
  std::vector<Owned> owned_;
  HBITMAP scratch_ = nullptr;
  void* scratchBits_ = nullptr;
  HBITMAP scratchSrc_ = nullptr;
  int cw_ = 0, ch_ = 0, padTop_ = 0;
};

}  // namespace petwin
