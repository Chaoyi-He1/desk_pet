// BelfastPet — Win32 shell.
//
// A layered, non-activating tool window shows one pre-rendered frame at a time. A
// single timer runs at the current animation's frame rate; a second slow timer watches
// the foreground window (fullscreen → hide) and the user's idle time (→ sleep).
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>

#include <cstdint>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "core/brain.h"
#include "core/ini.h"
#include "core/screen.h"
#include "win/bubble.h"
#include "win/resource.h"
#include "win/sprites.h"

namespace {

using pet::Anim;

const wchar_t* kClass = L"BelfastPetWindow";
const wchar_t* kMutex = L"Local\\BelfastPet.SingleInstance";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunName = L"BelfastPet";
const UINT_PTR ID_ANIM = 1, ID_WATCH = 2, ID_DRAG = 3;
const UINT WM_TRAY = WM_APP + 1;
enum { IDM_TOGGLE = 100, IDM_AUTOSTART, IDM_HIDE_FS, IDM_OPEN_ASSETS, IDM_EXIT };

struct App {
  HINSTANCE hinst = nullptr;
  HWND hwnd = nullptr;
  std::unique_ptr<pet::Brain> brain;
  petwin::SpriteSet sprites;
  petwin::Bubble bubble;
  std::wstring assetsDir;

  bool hideOnFullscreen = true;
  int bubbleMs = 3000;
  bool mirrorLeft = true;
  int startX = -1;

  ULONGLONG lastTick = 0;
  int timerMs = -1;
  bool userHidden = false;
  bool fsHidden = false;
  HBITMAP shownSrc = nullptr;
  bool shownMirrored = false;
  pet::Frame last;
  NOTIFYICONDATAW nid = {};
  HICON icon = nullptr;
};

App* g_app = nullptr;

// ---------- small helpers ----------

std::wstring exeDir() {
  wchar_t buf[MAX_PATH];
  GetModuleFileNameW(nullptr, buf, MAX_PATH);
  std::wstring s(buf);
  size_t p = s.find_last_of(L"\\/");
  return p == std::wstring::npos ? L"." : s.substr(0, p);
}

std::string readFile(const std::wstring& path) {
  std::ifstream in(path.c_str(), std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  std::string s = ss.str();
  if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF)
    s.erase(0, 3);
  return s;
}

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}

std::vector<std::string> loadLines(const std::wstring& path) {
  std::vector<std::string> out;
  std::istringstream in(readFile(path));
  std::string line;
  while (std::getline(in, line)) {
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) line.pop_back();
    size_t a = line.find_first_not_of(" \t");
    if (a == std::string::npos || line[a] == '#') continue;
    out.push_back(line.substr(a));
  }
  if (out.empty()) out = {"\xE6\x8C\x87\xE6\x8C\xA5\xE5\xAE\x98\xEF\xBC\x8C\xE6\x9C\x89\xE4\xBB\x80\xE4\xB9\x88\xE5\x90\xA9\xE5\x92\x90\xE5\x90\x97\xEF\xBC\x9F"};  // 指挥官，有什么吩咐吗？
  return out;
}

RECT workArea(HWND hwnd) {
  HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO mi = {sizeof(mi)};
  if (GetMonitorInfoW(mon, &mi)) return mi.rcWork;
  RECT r;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &r, 0);
  return r;
}

bool isShellWindow(HWND h) {
  wchar_t cls[64] = {};
  GetClassNameW(h, cls, 64);
  std::wstring c(cls);
  return c == L"Progman" || c == L"WorkerW" || c == L"Shell_TrayWnd" || c == L"Shell_SecondaryTrayWnd";
}

bool foregroundIsFullscreen(const App& app) {
  HWND fg = GetForegroundWindow();
  if (!fg || fg == app.hwnd || fg == app.bubble.hwnd() || IsIconic(fg) || isShellWindow(fg)) return false;
  RECT r;
  if (!GetWindowRect(fg, &r)) return false;
  HMONITOR mon = MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST);
  MONITORINFO mi = {sizeof(mi)};
  if (!GetMonitorInfoW(mon, &mi)) return false;
  return pet::coversMonitor({r.left, r.top, r.right, r.bottom},
                            {mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right, mi.rcMonitor.bottom});
}

double userIdleSeconds() {
  LASTINPUTINFO li = {sizeof(li)};
  if (!GetLastInputInfo(&li)) return 0;
  return (GetTickCount() - li.dwTime) / 1000.0;
}

bool autostartEnabled() {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &k) != ERROR_SUCCESS) return false;
  DWORD type = 0, size = 0;
  bool on = RegQueryValueExW(k, kRunName, nullptr, &type, nullptr, &size) == ERROR_SUCCESS;
  RegCloseKey(k);
  return on;
}

void setAutostart(bool on) {
  HKEY k;
  if (RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS)
    return;
  if (on) {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(buf) + L"\"";
    RegSetValueExW(k, kRunName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                   (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    RegDeleteValueW(k, kRunName);
  }
  RegCloseKey(k);
}

// Lower our scheduling priority and opt into Windows' power throttling ("efficiency
// mode") when available, so a game in the foreground is never contending with us.
void lowerProcessPriority() {
  SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
  struct PetPowerThrottling { ULONG Version; ULONG ControlMask; ULONG StateMask; };
  typedef BOOL(WINAPI * SetProcInfo)(HANDLE, int, LPVOID, DWORD);
  HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
  auto fn = k32 ? reinterpret_cast<SetProcInfo>(GetProcAddress(k32, "SetProcessInformation")) : nullptr;
  if (fn) {
    PetPowerThrottling t = {1, 0x1u /*EXECUTION_SPEED*/, 0x1u};
    fn(GetCurrentProcess(), 4 /*ProcessPowerThrottling*/, &t, sizeof(t));
  }
}

void enableDpiAwareness() {
  typedef BOOL(WINAPI * SetCtx)(HANDLE);
  HMODULE u32 = GetModuleHandleW(L"user32.dll");
  auto fn = u32 ? reinterpret_cast<SetCtx>(GetProcAddress(u32, "SetProcessDpiAwarenessContext")) : nullptr;
  if (fn && fn(reinterpret_cast<HANDLE>(-4) /*PER_MONITOR_AWARE_V2*/)) return;
  SetProcessDPIAware();
}

// ---------- rendering ----------

void applyFrame(App& app, const pet::Frame& f) {
  if (!f.visible) {
    ShowWindow(app.hwnd, SW_HIDE);
    app.bubble.hide();
    app.shownSrc = nullptr;
    return;
  }
  petwin::SpriteFrame sf = app.sprites.get(f.anim, f.index, f.facingLeft && app.mirrorLeft);
  int wx = f.x + sf.offX, wy = f.y + sf.offY;
  int w = app.sprites.width(), h = app.sprites.height();
  bool sameContent = sf.src == app.shownSrc && sf.mirrored == app.shownMirrored;
  if (sameContent && IsWindowVisible(app.hwnd)) {
    SetWindowPos(app.hwnd, nullptr, wx, wy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  } else {
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HGDIOBJ old = SelectObject(mem, sf.bmp);
    POINT pos = {wx, wy}, src = {0, 0};
    SIZE size = {w, h};
    BLENDFUNCTION bf = {AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    UpdateLayeredWindow(app.hwnd, screen, &pos, &size, mem, &src, 0, &bf, ULW_ALPHA);
    SelectObject(mem, old);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
    app.shownSrc = sf.src;
    app.shownMirrored = sf.mirrored;
    if (!IsWindowVisible(app.hwnd)) ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
  }
  int ax = f.x + w / 2, ay = f.y + app.sprites.padTop();
  if (!f.say.empty()) app.bubble.show(widen(f.say), ax, ay, app.bubbleMs);
  else app.bubble.moveTo(ax, ay);
}

void setTimerMs(App& app, int ms) {
  if (ms == app.timerMs) return;
  app.timerMs = ms;
  if (ms <= 0) KillTimer(app.hwnd, ID_ANIM);
  else SetTimer(app.hwnd, ID_ANIM, (UINT)ms, nullptr);
}

void step(App& app, int dtMs) {
  pet::Frame f = app.brain->tick(dtMs);
  if (f.dirty || !f.say.empty()) applyFrame(app, f);
  app.last = f;
  setTimerMs(app, f.nextTickMs);
}

void updateGround(App& app) {
  RECT w = workArea(app.hwnd);
  app.brain->setWorkTop(w.top);
  app.brain->setGround(w.left, w.right, w.bottom);
}

void updateHidden(App& app) {
  bool hide = app.userHidden || app.fsHidden;
  app.brain->setHidden(hide);
  if (!hide) updateGround(app);
  app.lastTick = GetTickCount64();
  step(app, 0);
}

// ---------- menu / tray ----------

void showMenu(App& app) {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, IDM_TOGGLE, app.userHidden ? L"显示(&S)" : L"隐藏(&H)");
  AppendMenuW(m, MF_STRING | (app.hideOnFullscreen ? MF_CHECKED : 0), IDM_HIDE_FS, L"全屏时自动隐藏(&F)");
  AppendMenuW(m, MF_STRING | (autostartEnabled() ? MF_CHECKED : 0), IDM_AUTOSTART, L"开机自动启动(&A)");
  AppendMenuW(m, MF_STRING, IDM_OPEN_ASSETS, L"打开素材文件夹(&O)");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, IDM_EXIT, L"退出(&X)");
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(app.hwnd);  // required for the menu to dismiss properly
  int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_NONOTIFY, pt.x, pt.y, 0, app.hwnd, nullptr);
  PostMessageW(app.hwnd, WM_NULL, 0, 0);
  DestroyMenu(m);
  switch (cmd) {
    case IDM_TOGGLE:
      app.userHidden = !app.userHidden;
      updateHidden(app);
      break;
    case IDM_HIDE_FS:
      app.hideOnFullscreen = !app.hideOnFullscreen;
      if (!app.hideOnFullscreen && app.fsHidden) { app.fsHidden = false; updateHidden(app); }
      break;
    case IDM_AUTOSTART:
      setAutostart(!autostartEnabled());
      break;
    case IDM_OPEN_ASSETS:
      ShellExecuteW(nullptr, L"open", app.assetsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case IDM_EXIT:
      DestroyWindow(app.hwnd);
      break;
  }
}

void addTray(App& app) {
  app.icon = (HICON)LoadImageW(nullptr, (app.assetsDir + L"\\icon.ico").c_str(), IMAGE_ICON, 16, 16, LR_LOADFROMFILE);
  if (!app.icon) app.icon = LoadIconW(app.hinst, MAKEINTRESOURCEW(IDI_APP));
  app.nid.cbSize = sizeof(app.nid);
  app.nid.hWnd = app.hwnd;
  app.nid.uID = 1;
  app.nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
  app.nid.uCallbackMessage = WM_TRAY;
  app.nid.hIcon = app.icon;
  lstrcpynW(app.nid.szTip, L"BelfastPet — 双击显示/隐藏，右键菜单", ARRAYSIZE(app.nid.szTip));
  Shell_NotifyIconW(NIM_ADD, &app.nid);
}

// ---------- window procedure ----------

LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  App* app = g_app;
  if (!app || app->hwnd != h) return DefWindowProcW(h, msg, wp, lp);
  switch (msg) {
    case WM_TIMER:
      if (wp == ID_ANIM) {
        ULONGLONG now = GetTickCount64();
        int dt = (int)(now - app->lastTick);
        app->lastTick = now;
        if (dt > 1000) dt = 1000;
        step(*app, dt);
      } else if (wp == ID_DRAG) {
        // A non-activating window cannot fully capture the mouse, so poll the button
        // state while dragging; this also catches a release outside the window.
        POINT p;
        GetCursorPos(&p);
        if (GetAsyncKeyState(VK_LBUTTON) & 0x8000) {
          app->brain->move(p.x, p.y);
        } else {
          KillTimer(h, ID_DRAG);
          if (GetCapture() == h) ReleaseCapture();
          app->brain->release();
        }
        step(*app, 0);
      } else if (wp == ID_WATCH) {
        bool fs = app->hideOnFullscreen && foregroundIsFullscreen(*app);
        if (fs != app->fsHidden) {
          app->fsHidden = fs;
          updateHidden(*app);
        } else if (!app->userHidden && !app->fsHidden) {
          app->brain->setUserIdleSeconds(userIdleSeconds());
          if (app->timerMs <= 0) app->lastTick = GetTickCount64();
          step(*app, 0);
        }
      }
      return 0;
    case WM_LBUTTONDOWN: {
      SetCapture(h);
      POINT p;
      GetCursorPos(&p);
      app->brain->press(p.x, p.y);
      SetTimer(h, ID_DRAG, 16, nullptr);
      return 0;
    }
    case WM_MOUSEMOVE:
      if (GetCapture() == h) {
        POINT p;
        GetCursorPos(&p);
        app->brain->move(p.x, p.y);
        step(*app, 0);
      }
      return 0;
    case WM_LBUTTONUP:
      KillTimer(h, ID_DRAG);
      if (GetCapture() == h) ReleaseCapture();
      app->brain->release();
      step(*app, 0);
      return 0;
    case WM_RBUTTONUP:
      showMenu(*app);
      return 0;
    case WM_TRAY:
      if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_CONTEXTMENU) showMenu(*app);
      else if (LOWORD(lp) == WM_LBUTTONDBLCLK) { app->userHidden = !app->userHidden; updateHidden(*app); }
      return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
      updateGround(*app);
      step(*app, 0);
      return 0;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_DESTROY:
      Shell_NotifyIconW(NIM_DELETE, &app->nid);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hinst, HINSTANCE, PWSTR, int) {
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutex);
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    CloseHandle(mutex);
    return 0;
  }
  lowerProcessPriority();
  enableDpiAwareness();

  App app;
  g_app = &app;
  app.hinst = hinst;
  app.assetsDir = exeDir() + L"\\assets";

  pet::Ini ini = pet::Ini::parse(readFile(app.assetsDir + L"\\config.ini"));
  int height = ini.getInt("general", "height", 320);
  app.mirrorLeft = ini.getInt("general", "mirror_left", 1) != 0;
  app.hideOnFullscreen = ini.getInt("general", "hide_on_fullscreen", 1) != 0;
  app.bubbleMs = ini.getInt("general", "bubble_ms", 3000);
  app.startX = ini.getInt("general", "start_x", -1);

  std::wstring err;
  if (!app.sprites.load(app.assetsDir, height, &err)) {
    MessageBoxW(nullptr, err.c_str(), L"BelfastPet", MB_OK | MB_ICONWARNING);
    return 1;
  }

  pet::BrainConfig cfg;
  cfg.spriteW = app.sprites.width();
  cfg.spriteH = app.sprites.height();
  cfg.walkSpeed = ini.getInt("general", "walk_speed", 40);
  cfg.sleepAfterSec = ini.getInt("general", "sleep_after", 180);
  cfg.idleMinMs = ini.getInt("general", "idle_min_ms", 4000);
  cfg.idleMaxMs = ini.getInt("general", "idle_max_ms", 12000);
  for (int i = 0; i < (int)Anim::Count; ++i) {
    Anim a = (Anim)i;
    cfg.fps[i] = ini.getInt("fps", pet::animName(a), cfg.fps[i]);
    cfg.frameCount[i] = app.sprites.frameCount(a);
  }
  app.brain.reset(new pet::Brain(cfg, loadLines(app.assetsDir + L"\\lines.txt"), (unsigned)GetTickCount()));

  WNDCLASSW wc = {};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = hinst;
  wc.lpszClassName = kClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  app.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass,
                             L"BelfastPet", WS_POPUP, 0, 0, cfg.spriteW, cfg.spriteH, nullptr, nullptr, hinst, nullptr);
  if (!app.hwnd) {
    MessageBoxW(nullptr, L"窗口创建失败。", L"BelfastPet", MB_OK | MB_ICONERROR);
    return 1;
  }
  app.bubble.create(hinst);

  RECT work = workArea(app.hwnd);
  app.brain->setWorkTop(work.top);
  app.brain->setGround(work.left, work.right, work.bottom);
  int x0 = app.startX >= 0 ? app.startX : work.right - cfg.spriteW - 24;
  app.brain->setPosition(x0, work.bottom - cfg.spriteH);

  addTray(app);
  app.lastTick = GetTickCount64();
  step(app, 0);
  SetTimer(app.hwnd, ID_WATCH, 2000, nullptr);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (app.icon) DestroyIcon(app.icon);
  CloseHandle(mutex);
  return 0;
}
