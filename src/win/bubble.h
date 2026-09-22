// Speech bubble: a separate click-through layered window drawn with plain GDI.
#pragma once
#include <windows.h>

#include <string>

namespace petwin {

class Bubble {
public:
  bool create(HINSTANCE hinst);
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
  HFONT font_ = nullptr;
  int w_ = 0, h_ = 0;
  int anchorX_ = 0, anchorY_ = 0;
  bool visible_ = false;
};

}  // namespace petwin
