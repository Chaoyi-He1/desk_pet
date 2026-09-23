// Pet images for the Win32 shell.
//
// A skin is either a PNG painting (poses are generated from it with GDI+ at load time)
// or a folder of pre-rendered chibi frames with a meta.ini (see tools/spine/render38.py).
// Every frame is a premultiplied 32-bit top-down DIB cropped to its visible pixels, so a
// frame switch only selects another HBITMAP and moves the layered window.
#pragma once
#include <windows.h>

#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "core/anim.h"

namespace petwin {

struct SpriteFrame {
  HBITMAP bmp = nullptr;
  int offX = 0, offY = 0;  // position of the bitmap inside the canvas
  int w = 0, h = 0;        // bitmap size
};

class SpriteSet {
public:
  SpriteSet() = default;
  ~SpriteSet();
  SpriteSet(const SpriteSet&) = delete;
  SpriteSet& operator=(const SpriteSet&) = delete;

  // `path`: a .png painting or an animated-skin folder. `height` is the requested size
  // (painting height; for chibis 80% of it is the character height). Without
  // `exactHeight` small pictures are not blown up beyond 2x.
  bool load(const std::wstring& path, int height, bool exactHeight, std::wstring* err);

  bool animated() const { return animated_; }
  int width() const { return cw_; }        // canvas size (the Brain's sprite size)
  int height() const { return ch_; }
  int size() const { return size_; }       // the size value this set was built for
  int headTop() const { return headTop_; }  // canvas y of the top of the head
  int groundInset() const { return groundInset_; }
  double headFraction() const { return headFraction_; }
  int fps() const { return fps_; }         // 0: use the configured per-state fps
  std::vector<int> variants(pet::Anim a) const;

  SpriteFrame get(pet::Anim a, int variant, int index, bool mirrored);

private:
  bool loadPainting(const std::wstring& png, int height, bool exact, std::wstring* err);
  bool loadAnimated(const std::wstring& dir, int height, bool exact, std::wstring* err);
  SpriteFrame decode(const std::wstring& png);
  SpriteFrame mirror(const SpriteFrame& f);
  HBITMAP newDib(int w, int h, void** bits);
  void evictExcept(pet::Anim keep);

  bool animated_ = false;
  int cw_ = 0, ch_ = 0, size_ = 0, headTop_ = 0, groundInset_ = 0, fps_ = 0;
  double headFraction_ = 0.22;
  double scale_ = 1;  // animated: source px -> screen px

  // painting: frames_[anim][variant][index]; animated: paths_[anim][variant][index]
  std::vector<std::vector<SpriteFrame>> frames_[(int)pet::Anim::Count];
  std::vector<std::vector<std::wstring>> paths_[(int)pet::Anim::Count];
  std::map<std::tuple<int, int, int, bool>, SpriteFrame> cache_;  // animated, decoded lazily
  std::map<HBITMAP, SpriteFrame> mirrors_;                        // painting mirrors
  std::vector<HBITMAP> owned_;
  int lastAnim_ = -1;
  ULONG_PTR gdiplus_ = 0;
};

}  // namespace petwin
