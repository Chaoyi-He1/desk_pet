// Speech bubble: a separate click-through layered window in the look of the game's main
// screen, where her lines show in a dark translucent box. Drawn with GDI+.
#pragma once
#include <windows.h>

#include <string>

namespace Gdiplus {
class Bitmap;
}

namespace petwin {

class Bubble {
public:
  ~Bubble();
  // `assetsDir` holds ui/main/word_bg.png (the box), word_p.png and word_p2.png (its faint
  // marks); without them the box is drawn plain in the same colour.
  bool create(HINSTANCE hinst, const std::wstring& assetsDir);
  // Shows `text` centred above the point (anchorX, anchorY) for durationMs.
  void show(const std::wstring& text, int anchorX, int anchorY, int durationMs);
  void moveTo(int anchorX, int anchorY);
  void hide();
  bool visible() const { return visible_; }
  HWND hwnd() const { return hwnd_; }

private:
  static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
  void render(const std::wstring& text);
  void place();

  HWND hwnd_ = nullptr;
  HBITMAP dib_ = nullptr;
  ULONG_PTR gdiplus_ = 0;
  Gdiplus::Bitmap* box_ = nullptr;   // word_bg, scaled to the bubble's text size
  Gdiplus::Bitmap* pin_ = nullptr;   // the marks at the top left
  Gdiplus::Bitmap* wave_ = nullptr;  // the voice wave at the bottom right
  int w_ = 0, h_ = 0;
  int anchorX_ = 0, anchorY_ = 0;
  bool visible_ = false;
};

}  // namespace petwin
