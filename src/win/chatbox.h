// The chat input box in the look of the game's JUUS chat: a borderless per-pixel layered
// window drawn from the game's UI sprites (assets/ui/juus, see tools/extract_ui.py) with her
// avatar, the white input bar, a voice button and a send button. The text box itself is a
// plain EDIT control in a small white popup laid over the bar, because a per-pixel layered
// window cannot host controls. Without the sprites the shell keeps its plain titled window.
#pragma once
#include <windows.h>

#include <functional>
#include <string>

namespace Gdiplus {
class Bitmap;
}

namespace petwin {

class ChatBox {
public:
  ChatBox() = default;
  ~ChatBox();
  ChatBox(const ChatBox&) = delete;
  ChatBox& operator=(const ChatBox&) = delete;

  std::function<void()> onSend;   // the send button: the caller reads and clears the EDIT
  std::function<void()> onVoice;  // the voice button

  // Loads assets/ui/juus/*.png and creates the windows, hidden. False when the sprites are
  // missing: nothing is created and the caller uses its plain box.
  bool create(HINSTANCE hinst, const std::wstring& assetsDir);
  // Her face for the avatar, cut from her idle frame (a premultiplied top-down DIB; offY is its
  // row in the canvas) by pet::faceBox: a chibi standing from canvas row headTop down to
  // canvasH - groundInset, or a painting; `hint` as in faceBox (3 values or nullptr). Copied.
  void setAvatar(HBITMAP frame, int offY, int canvasH, int headTop, int groundInset, bool painting, const double* hint);
  void setBusy(bool busy);  // greys the send button while a request is out

  HWND hwnd() const { return art_; }   // the window to position, show and hide
  HWND edit() const { return edit_; }  // the EDIT control (in its own popup over the bar)

private:
  static LRESULT CALLBACK artProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK hostProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT onArt(HWND, UINT, WPARAM, LPARAM);
  void render();
  void placeHost();

  HWND art_ = nullptr, host_ = nullptr, edit_ = nullptr;
  HBITMAP dib_ = nullptr;
  void* bits_ = nullptr;
  HBRUSH white_ = nullptr;
  ULONG_PTR gdiplus_ = 0;
  Gdiplus::Bitmap* bar_ = nullptr;
  Gdiplus::Bitmap* pill_ = nullptr;
  Gdiplus::Bitmap* send_ = nullptr;
  Gdiplus::Bitmap* avatarBg_ = nullptr;
  Gdiplus::Bitmap* face_ = nullptr;
  bool busy_ = false;
};

}  // namespace petwin
