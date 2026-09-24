// Pet images for the Win32 shell.
//
// A skin is either a PNG painting (poses are generated from it with GDI+ at load time)
// or a folder of pre-rendered chibi frames with a meta.ini (see tools/spine/render38.py).
// Every frame is a premultiplied 32-bit top-down DIB cropped to its visible pixels, so a
// frame switch only selects another HBITMAP and moves the layered window.
#pragma once
#include <windows.h>

#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "core/anim.h"

namespace petwin {

struct SpriteFrame {
  uint64_t id = 0;         // unique per bitmap content (handles can be reused after DeleteObject)
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
  bool walks() const { return walks_; }    // false: stationary (paintings)
  int idleMinMs() const { return idleMinMs_; }  // 0: use the configured value
  int idleMaxMs() const { return idleMaxMs_; }
  std::vector<int> variants(pet::Anim a) const;
  // Index of the variant loaded from folder `name` (e.g. L"react_head"), -1 if none.
  int variantIndex(pet::Anim a, const std::wstring& name) const;

  SpriteFrame get(pet::Anim a, int variant, int index, bool mirrored);

private:
  bool loadPainting(const std::wstring& png, int height, bool exact, std::wstring* err);
  bool loadAnimated(const std::wstring& dir, int height, bool exact, std::wstring* err);
  SpriteFrame decode(const std::wstring& path);
  SpriteFrame cropToDib(const uint8_t* px, size_t stride);
  SpriteFrame mirror(const SpriteFrame& f);
  HBITMAP newDib(int w, int h, void** bits);

  bool animated_ = false;
  int cw_ = 0, ch_ = 0, size_ = 0, headTop_ = 0, groundInset_ = 0, fps_ = 0;
  double headFraction_ = 0.22;
  bool walks_ = true;
  bool stream_ = false;
  uint64_t serial_ = 0;
  int idleMinMs_ = 0, idleMaxMs_ = 0;
  double scale_ = 1;  // animated: source px -> screen px

  // painting: frames_[anim][variant][index]; animated: paths_[anim][variant][index]
  std::vector<std::vector<SpriteFrame>> frames_[(int)pet::Anim::Count];
  std::vector<std::vector<std::wstring>> paths_[(int)pet::Anim::Count];
  std::vector<std::wstring> names_[(int)pet::Anim::Count];  // folder of each variant
  std::map<std::tuple<int, int, int, bool>, SpriteFrame> cache_;  // animated idle/walk, decoded lazily
  SpriteFrame transient_;                                         // animated, any other state: one frame
  std::tuple<int, int, int, bool> transientKey_{-1, -1, -1, false};
  std::map<HBITMAP, SpriteFrame> mirrors_;                        // painting mirrors
  std::vector<HBITMAP> owned_;
  ULONG_PTR gdiplus_ = 0;
};

}  // namespace petwin
