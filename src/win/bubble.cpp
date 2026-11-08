#include "win/bubble.h"

#include <cmath>

#include "win/gdiplus_draw.h"

namespace petwin {
namespace {
const wchar_t* kClass = L"BelfastPetBubble";
const UINT_PTR kTimer = 1;
// The game's box holds 28 px text at 1080p; ours holds 15 px text, so its sprites are drawn
// at this scale and the paddings are the game's at the same scale.
const float kScale = 0.55f;
const int kPadX = 20, kPadY = 14, kMaxText = 240;
const int kGap = 6;                // between the box and the top of her head
const int kCapX = 12, kCapY = 14;  // word_bg's 9-slice borders (22-27 px) at kScale
const int kRadius = 12;            // the plain box's corners without the sprite
const int kPinX = 6, kPinY = 5;    // the marks' top-left corner
const int kWaveRight = 12, kWaveBottom = 4;
const Gdiplus::Color kBoxColor(102, 28, 30, 33);  // word_bg is this one colour: 40% dark grey
const Gdiplus::Color kTextColor(255, 250, 250, 248);

// The PNG at `path` scaled by kScale, or nullptr.
Gdiplus::Bitmap* loadScaled(const std::wstring& path) {
  using namespace Gdiplus;
  Bitmap* src = loadPng(path);
  if (!src) return nullptr;
  int w = (std::max)(1, (int)std::lround(src->GetWidth() * kScale));
  int h = (std::max)(1, (int)std::lround(src->GetHeight() * kScale));
  Bitmap* out = new Bitmap(w, h, PixelFormat32bppPARGB);
  {
    Graphics g(out);
    g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    ImageAttributes ia;
    ia.SetWrapMode(WrapModeTileFlipXY);  // no dark fringe from outside the picture
    g.DrawImage(src, Rect(0, 0, w, h), 0, 0, (int)src->GetWidth(), (int)src->GetHeight(), UnitPixel, &ia);
  }
  delete src;
  return out;
}
}  // namespace

Bubble::~Bubble() {
  delete box_;
  delete pin_;
  delete wave_;
  if (dib_) DeleteObject(dib_);
  if (gdiplus_) Gdiplus::GdiplusShutdown(gdiplus_);
}

LRESULT CALLBACK Bubble::proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  Bubble* self = reinterpret_cast<Bubble*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  if (msg == WM_TIMER && self) { self->hide(); return 0; }
  return DefWindowProcW(h, msg, wp, lp);
}

bool Bubble::create(HINSTANCE hinst, const std::wstring& assetsDir) {
  Gdiplus::GdiplusStartupInput gsi;
  if (!gdiplus_ && Gdiplus::GdiplusStartup(&gdiplus_, &gsi, nullptr) != Gdiplus::Ok) return false;
  std::wstring dir = assetsDir + L"/ui/main/";
  box_ = loadScaled(dir + L"word_bg.png");
  pin_ = loadScaled(dir + L"word_p.png");
  wave_ = loadScaled(dir + L"word_p2.png");

  WNDCLASSW wc = {};
  wc.lpfnWndProc = proc;
  wc.hInstance = hinst;
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);
  hwnd_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                          kClass, L"", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
  if (!hwnd_) return false;
  SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
  return true;
}

void Bubble::render(const std::wstring& text) {
  using namespace Gdiplus;
  Font font(L"Microsoft YaHei UI", 15, FontStyleRegular, UnitPixel);
  StringFormat sf(StringFormat::GenericTypographic());
  sf.SetAlignment(StringAlignmentCenter);  // the game centres her lines in the box

  RectF bounds;
  {
    Bitmap probe(1, 1, PixelFormat32bppPARGB);
    Graphics mg(&probe);
    mg.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
    mg.MeasureString(text.c_str(), -1, &font, RectF(0, 0, (REAL)kMaxText, 4096), &sf, &bounds);
  }
  int tw = (int)std::ceil(bounds.Width) + 2, th = (int)std::ceil(bounds.Height);
  w_ = tw + 2 * kPadX;
  h_ = th + 2 * kPadY;
  if (pin_ && wave_) {  // room for both marks side by side, like the game's wide box
    int minW = kPinX + (int)pin_->GetWidth() + 12 + (int)wave_->GetWidth() + kWaveRight;
    if (w_ < minW) w_ = minW;
  }

  if (dib_) DeleteObject(dib_);
  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bi.bmiHeader.biWidth = w_;
  bi.bmiHeader.biHeight = -h_;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  bi.bmiHeader.biCompression = BI_RGB;
  void* bits = nullptr;
  dib_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (!dib_) return;

  Bitmap canvas(w_, h_, w_ * 4, PixelFormat32bppPARGB, (BYTE*)bits);
  Graphics g(&canvas);
  g.Clear(Color(0, 0, 0, 0));
  g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);

  if (box_) {
    drawNine(g, *box_, RECT{0, 0, w_, h_}, kCapX, kCapY);
  } else {
    GraphicsPath p;
    int d = 2 * kRadius;
    p.AddArc(0, 0, d, d, 180, 90);
    p.AddArc(w_ - d, 0, d, d, 270, 90);
    p.AddArc(w_ - d, h_ - d, d, d, 0, 90);
    p.AddArc(0, h_ - d, d, d, 90, 90);
    p.CloseFigure();
    SolidBrush fill(kBoxColor);
    g.FillPath(&fill, &p);
  }
  if (pin_) g.DrawImage(pin_, kPinX, kPinY, (int)pin_->GetWidth(), (int)pin_->GetHeight());
  if (wave_)
    g.DrawImage(wave_, w_ - kWaveRight - (int)wave_->GetWidth(), h_ - kWaveBottom - (int)wave_->GetHeight(),
                (int)wave_->GetWidth(), (int)wave_->GetHeight());
  SolidBrush ink(kTextColor);
  g.DrawString(text.c_str(), -1, &font, RectF((REAL)kPadX, (REAL)kPadY, (REAL)(w_ - 2 * kPadX), (REAL)th), &sf, &ink);
  g.Flush(FlushIntentionSync);
}

void Bubble::place() {
  RECT work = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
  POINT p = {anchorX_, anchorY_};
  HMONITOR mon = MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  if (GetMonitorInfoW(mon, &mi)) work = mi.rcWork;
  int x = anchorX_ - w_ / 2, y = anchorY_ - h_ - kGap;
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
  if (!dib_) return;
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
