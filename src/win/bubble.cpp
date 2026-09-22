#include "win/bubble.h"

#include <cstdint>

namespace petwin {
namespace {
const wchar_t* kClass = L"BelfastPetBubble";
const UINT_PTR kTimer = 1;
const int kPad = 10, kTail = 8, kMaxText = 240, kRadius = 12;
}  // namespace

LRESULT CALLBACK Bubble::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Bubble* self = reinterpret_cast<Bubble*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (msg == WM_TIMER && self) { self->hide(); return 0; }
  return DefWindowProcW(h, msg, wp, lp);
}

bool Bubble::create(HINSTANCE hinst) {
  WNDCLASSW wc = {};
  wc.lpfnWndProc = proc;
  wc.hInstance = hinst;
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                          kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
  if (!hwnd_) return false;
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  font_ = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
  return true;
}

void Bubble::render(const std::wstring& text) {
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HGDIOBJ oldFont = SelectObject(mem, font_);

  RECT tr = {0, 0, kMaxText, 0};
  DrawTextW(mem, text.c_str(), -1, &tr, DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
  int tw = tr.right - tr.left, th = tr.bottom - tr.top;
  w_ = tw + 2 * kPad + 2;
  h_ = th + 2 * kPad + kTail + 2;

  if (dib_) DeleteObject(dib_);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w_;
  bi.bmiHeader.biHeight = -h_;
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  dib_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  HGDIOBJ oldBmp = SelectObject(mem, dib_);

  // Everything drawn uses non-zero colours, so untouched (zero) pixels stay transparent.
  HBRUSH fill = CreateSolidBrush(RGB(254, 254, 254));
  HPEN border = CreatePen(PS_SOLID, 1, RGB(60, 64, 84));
  HGDIOBJ oldBrush = SelectObject(mem, fill);
  HGDIOBJ oldPen = SelectObject(mem, border);
  RoundRect(mem, 0, 0, w_, h_ - kTail, kRadius, kRadius);
  POINT tail[3] = {{w_ / 2 - kTail, h_ - kTail - 1}, {w_ / 2 + kTail, h_ - kTail - 1}, {w_ / 2, h_ - 1}};
  Polygon(mem, tail, 3);
  // Hide the border segment behind the tail.
  HPEN erase = CreatePen(PS_SOLID, 1, RGB(254, 254, 254));
  SelectObject(mem, erase);
  MoveToEx(mem, w_ / 2 - kTail + 1, h_ - kTail - 1, nullptr);
  LineTo(mem, w_ / 2 + kTail, h_ - kTail - 1);

  SetBkMode(mem, TRANSPARENT);
  SetTextColor(mem, RGB(24, 24, 32));
  RECT rt = {kPad + 1, kPad + 1, kPad + 1 + tw, kPad + 1 + th};
  DrawTextW(mem, text.c_str(), -1, &rt, DT_WORDBREAK | DT_NOPREFIX);

  SelectObject(mem, oldPen);
  SelectObject(mem, oldBrush);
  SelectObject(mem, oldFont);
  SelectObject(mem, oldBmp);
  DeleteObject(erase);
  DeleteObject(border);
  DeleteObject(fill);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);

  GdiFlush();
  uint32_t* px = static_cast<uint32_t*>(bits);
  for (int i = 0; i < w_ * h_; ++i) px[i] = px[i] ? (px[i] | 0xFF000000u) : 0;
}

void Bubble::place() {
  RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  POINT p = {anchorX_, anchorY_};
  HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  if (GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
  int x = anchorX_ - w_ / 2, y = anchorY_ - h_;
  if (x < work.left) x = work.left;
  if (x + w_ > work.right) x = work.right - w_;
  if (y < work.top) y = anchorY_ + 8;

  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HGDIOBJ old = SelectObject(mem, dib_);
  POINT pos = {x, y}, src = {0, 0};
  SIZE size = {w_, h_};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(hwnd_, screen, &pos, &size, mem, &src, 0, &bf, ULW_ALPHA);
  SelectObject(mem, old);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
}

void Bubble::show(const std::wstring& text, int anchorX, int anchorY, int durationMs) {
  if (!hwnd_) return;
  render(text);
  anchorX_ = anchorX;
  anchorY_ = anchorY;
  place();
  ShowWindow(hwnd_, SW_SHOWNOACTIVATE);
  visible_ = true;
  SetTimer(hwnd_, kTimer, durationMs > 0 ? durationMs : 3000, nullptr);
}

void Bubble::moveTo(int anchorX, int anchorY) {
  if (!visible_) return;
  anchorX_ = anchorX;
  anchorY_ = anchorY;
  place();
}

void Bubble::hide() {
  if (!hwnd_) return;
  KillTimer(hwnd_, kTimer);
  ShowWindow(hwnd_, SW_HIDE);
  visible_ = false;
}

}  // namespace petwin
