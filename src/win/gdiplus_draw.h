// GDI+ helpers shared by the layered windows drawn from the game's sprites (bubble, chat box).
#pragma once
#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <string>

namespace petwin {

// The PNG at `path`, or nullptr when it is missing or unreadable.
inline Gdiplus::Bitmap* loadPng(const std::wstring& path) {
  Gdiplus::Bitmap* b = new Gdiplus::Bitmap(path.c_str());
  if (b->GetLastStatus() != Gdiplus::Ok || b->GetWidth() == 0) {
    delete b;
    return nullptr;
  }
  return b;
}

// Draws `src` stretched into `dst` keeping its corners: capX / capY pixels of the source
// edges are copied as they are, the rest is stretched.
inline void drawNine(Gdiplus::Graphics& g, Gdiplus::Bitmap& src, const RECT& dst, int capX, int capY) {
  using namespace Gdiplus;
  ImageAttributes ia;
  ia.SetWrapMode(WrapModeTileFlipXY);  // no seams where the slices meet
  int sw = (int)src.GetWidth(), sh = (int)src.GetHeight();
  int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
  capX = (std::min)(capX, (std::min)(sw / 2, dw / 2));
  capY = (std::min)(capY, (std::min)(sh / 2, dh / 2));
  int sx[4] = {0, capX, sw - capX, sw}, sy[4] = {0, capY, sh - capY, sh};
  int dx[4] = {dst.left, dst.left + capX, dst.right - capX, dst.right};
  int dy[4] = {dst.top, dst.top + capY, dst.bottom - capY, dst.bottom};
  for (int j = 0; j < 3; ++j)
    for (int i = 0; i < 3; ++i) {
      int w = dx[i + 1] - dx[i], h = dy[j + 1] - dy[j];
      if (w <= 0 || h <= 0) continue;
      g.DrawImage(&src, Rect(dx[i], dy[j], w, h), sx[i], sy[j], sx[i + 1] - sx[i], sy[j + 1] - sy[j], UnitPixel, &ia);
    }
}

}  // namespace petwin
