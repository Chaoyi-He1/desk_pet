#include "win/chatbox.h"

#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>

#include "core/avatar.h"

namespace petwin {
namespace {

// Layout in pixels (the box is not DPI-scaled, like the bubble).
const int kW = 470, kH = 56;
const RECT kAvatar = {2, 4, 50, 52};                          // the white disc
const RECT kFace = {5, 7, 47, 49};                            // her face inside it
const RECT kBar = {56, 6, kW - 2, 50};                        // the input bar
const RECT kVoice = {kW - 110, 13, kW - 54, 43};              // the 语音 pill
const RECT kSend = {kW - 48, 14, kW - 16, 42};                // the paper plane
const RECT kEdit = {kBar.left + 16, 15, kVoice.left - 10, 41};  // the EDIT's popup over the bar
const Gdiplus::Color kBlue(255, 118, 198, 254);               // the player's bubble colour in JUUS
const Gdiplus::Color kGrey(255, 190, 194, 204);
const Gdiplus::Color kInk(255, 70, 80, 100);

bool inRect(const RECT& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }

Gdiplus::Bitmap* loadPng(const std::wstring& path) {
  Gdiplus::Bitmap* b = new Gdiplus::Bitmap(path.c_str());
  if (b->GetLastStatus() != Gdiplus::Ok || b->GetWidth() == 0) {
    delete b;
    return nullptr;
  }
  return b;
}

// Draws `src` stretched into `dst` keeping its corners: capX / capY pixels of the source
// edges are copied as they are, the rest is stretched.
void drawNine(Gdiplus::Graphics& g, Gdiplus::Bitmap& src, const RECT& dst, int capX, int capY) {
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

// Draws `src` with every pixel's colour replaced by `tint`, keeping its alpha.
void drawTinted(Gdiplus::Graphics& g, Gdiplus::Bitmap& src, const RECT& dst, Gdiplus::Color tint) {
  using namespace Gdiplus;
  ColorMatrix cm = {{{0, 0, 0, 0, 0},
                     {0, 0, 0, 0, 0},
                     {0, 0, 0, 0, 0},
                     {0, 0, 0, 1, 0},
                     {tint.GetR() / 255.0f, tint.GetG() / 255.0f, tint.GetB() / 255.0f, 0, 1}}};
  ImageAttributes ia;
  ia.SetColorMatrix(&cm);
  g.DrawImage(&src, Rect(dst.left, dst.top, dst.right - dst.left, dst.bottom - dst.top), 0, 0, (int)src.GetWidth(),
              (int)src.GetHeight(), UnitPixel, &ia);
}

}  // namespace

ChatBox::~ChatBox() {
  if (art_) DestroyWindow(art_);  // owns host_ and the EDIT
  if (dib_) DeleteObject(dib_);
  if (white_) DeleteObject(white_);
  delete bar_;
  delete pill_;
  delete send_;
  delete avatarBg_;
  delete face_;
  if (gdiplus_) Gdiplus::GdiplusShutdown(gdiplus_);
}

bool ChatBox::create(HINSTANCE hinst, const std::wstring& assetsDir) {
  Gdiplus::GdiplusStartupInput gsi;
  if (!gdiplus_ && Gdiplus::GdiplusStartup(&gdiplus_, &gsi, nullptr) != Gdiplus::Ok) return false;
  std::wstring dir = assetsDir + L"/ui/juus/";
  bar_ = loadPng(dir + L"bottom_bar.png");
  pill_ = loadPng(dir + L"reply_bg.png");
  send_ = loadPng(dir + L"send.png");
  avatarBg_ = loadPng(dir + L"avatar_bg.png");
  if (!bar_ || !pill_ || !send_ || !avatarBg_) return false;

  white_ = CreateSolidBrush(RGB(255, 255, 255));
  WNDCLASSW wc = {};
  wc.lpfnWndProc = artProc;
  wc.hInstance = hinst;
  wc.lpszClassName = L"BelfastPetChat";
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  art_ = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, L"和她聊天", WS_POPUP, 0, 0,
                         kW, kH, nullptr, nullptr, hinst, this);
  if (!art_) return false;

  WNDCLASSW hc = {};
  hc.lpfnWndProc = hostProc;
  hc.hInstance = hinst;
  hc.lpszClassName = L"BelfastPetChatEdit";
  hc.hCursor = LoadCursorW(nullptr, IDC_IBEAM);
  hc.hbrBackground = white_;
  RegisterClassW(&hc);
  int ew = kEdit.right - kEdit.left, eh = kEdit.bottom - kEdit.top;
  host_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, hc.lpszClassName, L"", WS_POPUP, 0, 0, ew, eh, art_, nullptr,
                          hinst, this);
  if (!host_) return false;
  edit_ = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 2, 3, ew - 4, eh - 5, host_, nullptr,
                          hinst, nullptr);
  if (!edit_) return false;

  BITMAPINFO bi = {};
  bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
  bi.bmiHeader.biWidth = kW;
  bi.bmiHeader.biHeight = -kH;  // top-down
  bi.bmiHeader.biPlanes = 1;
  bi.bmiHeader.biBitCount = 32;
  dib_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits_, nullptr, 0);
  if (!dib_) return false;
  render();
  return true;
}

void ChatBox::setAvatar(HBITMAP frame, int offY, int canvasH, int headTop, int groundInset, bool painting,
                        const double* hint) {
  using namespace Gdiplus;
  delete face_;
  face_ = nullptr;
  DIBSECTION ds = {};
  if (!frame || GetObjectW(frame, sizeof(ds), &ds) != sizeof(ds) || !ds.dsBm.bmBits || ds.dsBm.bmBitsPixel != 32) {
    render();
    return;
  }
  int fw = ds.dsBm.bmWidth, fh = ds.dsBm.bmHeight;
  pet::AlphaMask mask{fw, fh, (const uint8_t*)ds.dsBm.bmBits + 3, (size_t)ds.dsBm.bmWidthBytes, 4};
  pet::FaceBox box = pet::faceBox(mask, painting, headTop - offY, canvasH - groundInset - offY, hint);
  Bitmap src(fw, fh, ds.dsBm.bmWidthBytes, PixelFormat32bppPARGB, (BYTE*)ds.dsBm.bmBits);
  double sx = box.x, sy = box.y, side = box.side;
  int D = kFace.right - kFace.left;
  double f = D / side;
  face_ = new Bitmap(D, D, PixelFormat32bppPARGB);
  Graphics g(face_);
  g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
  g.SetPixelOffsetMode(PixelOffsetModeHalf);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  GraphicsPath disc;
  disc.AddEllipse(0, 0, D, D);
  g.SetClip(&disc);
  g.DrawImage(&src, RectF((REAL)(-sx * f), (REAL)(-sy * f), (REAL)(fw * f), (REAL)(fh * f)));
  render();
}

void ChatBox::setBusy(bool busy) {
  if (busy_ == busy) return;
  busy_ = busy;
  render();
}

void ChatBox::render() {
  using namespace Gdiplus;
  if (!dib_ || !art_) return;
  Bitmap canvas(kW, kH, kW * 4, PixelFormat32bppPARGB, (BYTE*)bits_);
  {
    Graphics g(&canvas);
    g.Clear(Color(0, 0, 0, 0));
    g.SetInterpolationMode(InterpolationModeHighQualityBilinear);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetTextRenderingHint(TextRenderingHintAntiAlias);
    drawNine(g, *bar_, kBar, 16, 14);
    // Solid white under the text box: its popup window is white and lies exactly there.
    SolidBrush white(Color(255, 255, 255, 255));
    g.FillRectangle(&white, Rect(kEdit.left - 2, kEdit.top - 2, kEdit.right - kEdit.left + 4, kEdit.bottom - kEdit.top + 4));
    drawNine(g, *pill_, kVoice, 30, 30);
    Font font(L"Microsoft YaHei UI", 14, FontStyleRegular, UnitPixel);
    StringFormat sf;
    sf.SetAlignment(StringAlignmentCenter);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush ink(kInk);
    g.DrawString(L"语音", -1, &font, RectF((REAL)kVoice.left, (REAL)kVoice.top - 1, (REAL)(kVoice.right - kVoice.left),
                                          (REAL)(kVoice.bottom - kVoice.top)),
                 &sf, &ink);
    drawTinted(g, *send_, kSend, busy_ ? kGrey : kBlue);
    g.DrawImage(avatarBg_, Rect(kAvatar.left, kAvatar.top, kAvatar.right - kAvatar.left, kAvatar.bottom - kAvatar.top));
    if (face_) g.DrawImage(face_, Rect(kFace.left, kFace.top, kFace.right - kFace.left, kFace.bottom - kFace.top));
  }
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HGDIOBJ old = SelectObject(mem, dib_);
  POINT src = {0, 0};
  SIZE size = {kW, kH};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
  UpdateLayeredWindow(art_, screen, nullptr, &size, mem, &src, 0, &bf, ULW_ALPHA);
  SelectObject(mem, old);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
}

void ChatBox::placeHost() {
  if (!art_ || !host_) return;
  RECT r;
  GetWindowRect(art_, &r);
  SetWindowPos(host_, nullptr, r.left + kEdit.left, r.top + kEdit.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

LRESULT CALLBACK ChatBox::artProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
    return DefWindowProcW(h, msg, wp, lp);
  }
  ChatBox* self = (ChatBox*)GetWindowLongPtrW(h, GWLP_USERDATA);
  return self ? self->onArt(h, msg, wp, lp) : DefWindowProcW(h, msg, wp, lp);
}

LRESULT ChatBox::onArt(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  switch (msg) {
    case WM_CLOSE:
      ShowWindow(h, SW_HIDE);
      return 0;
    case WM_SHOWWINDOW:
      if (host_) {
        if (wp) placeHost();
        ShowWindow(host_, wp ? SW_SHOWNA : SW_HIDE);
      }
      break;
    case WM_WINDOWPOSCHANGED: {  // SetWindowPos(SWP_SHOWWINDOW) sends no WM_SHOWWINDOW
      placeHost();
      UINT flags = ((const WINDOWPOS*)lp)->flags;
      if (host_ && (flags & SWP_SHOWWINDOW)) ShowWindow(host_, SW_SHOWNA);
      if (host_ && (flags & SWP_HIDEWINDOW)) ShowWindow(host_, SW_HIDE);
      break;
    }
    case WM_ACTIVATE:
      if (LOWORD(wp) != WA_INACTIVE && edit_) SetFocus(edit_);
      break;
    case WM_SETCURSOR: {
      POINT p;
      GetCursorPos(&p);
      ScreenToClient(h, &p);
      if (inRect(kSend, p) || inRect(kVoice, p)) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;
    }
    case WM_LBUTTONDOWN: {
      POINT p = {(short)LOWORD(lp), (short)HIWORD(lp)};
      if (inRect(kSend, p)) {
        if (onSend) onSend();
      } else if (inRect(kVoice, p)) {
        if (onVoice) onVoice();
      } else {  // anywhere else on the box drags it, like a title bar would
        ReleaseCapture();
        SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
      }
      return 0;
    }
    default:
      break;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

LRESULT CALLBACK ChatBox::hostProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_NCCREATE) {
    SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
    return DefWindowProcW(h, msg, wp, lp);
  }
  ChatBox* self = (ChatBox*)GetWindowLongPtrW(h, GWLP_USERDATA);
  switch (msg) {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {  // the EDIT asks as a static while disabled
      HDC dc = (HDC)wp;
      SetBkColor(dc, RGB(255, 255, 255));
      SetTextColor(dc, msg == WM_CTLCOLORSTATIC ? RGB(150, 154, 164) : RGB(24, 24, 32));
      return (LRESULT)(self ? self->white_ : GetStockObject(WHITE_BRUSH));
    }
    case WM_ACTIVATE:
      if (LOWORD(wp) != WA_INACTIVE && self && self->edit_) SetFocus(self->edit_);
      break;
    case WM_CLOSE:
      if (self && self->art_) ShowWindow(self->art_, SW_HIDE);
      return 0;
    default:
      break;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace petwin
