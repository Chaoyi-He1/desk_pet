// BelfastPet — Win32 shell.
//
// A layered, non-activating tool window shows one pre-rendered frame at a time. A
// single timer runs at the current animation's frame rate; a slow timer watches the
// foreground window (fullscreen -> hide) and the user's idle time (-> sleep); an
// optional timer lets the pet say something now and then.
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "core/brain.h"
#include "core/ini.h"
#include "core/screen.h"
#include "core/skin.h"
#include "core/voice.h"
#include "core/chat.h"
#include "win/bubble.h"
#include "win/resource.h"
#include "win/sprites.h"

namespace {

using pet::Anim;

const wchar_t* kClass = L"BelfastPetWindow";
const wchar_t* kMutex = L"Local\\BelfastPet.SingleInstance";
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunName = L"BelfastPet";
const UINT_PTR ID_ANIM = 1, ID_WATCH = 2, ID_DRAG = 3, ID_RESIZE = 4, ID_CHATTER = 5, ID_FADE = 6;
const UINT WM_TRAY = WM_APP + 1;
const UINT WM_CHAT_DONE = WM_APP + 2;  // lParam: ChatResult* from the worker thread
const int kChatterChoices[] = {0, 10, 20, 30, 60};  // minutes; 0 = off
enum {
  IDM_TOGGLE = 100, IDM_AUTOSTART, IDM_HIDE_FS, IDM_OPEN_ASSETS, IDM_EXIT,
  IDM_SIZE_UP, IDM_SIZE_DOWN, IDM_SIZE_RESET, IDM_CLICKTHROUGH,
  IDM_RANDOM_SKIN, IDM_DAILY_RANDOM, IDM_CHAT, IDM_CHAT_SETTINGS,
  IDM_CHATTER_BASE = 300, IDM_SIZE_BASE = 400, IDM_SKIN_BASE = 1000  // + ship * 100 + skin
};

struct Ship {
  std::wstring key;      // folder name under assets\ships
  std::wstring name;     // display name
  std::vector<std::wstring> skins;  // file / folder names under skins\, sorted
  pet::Ini ini;          // ship.ini
};

struct App {
  HINSTANCE hinst = nullptr;
  HWND hwnd = nullptr;
  std::unique_ptr<pet::Brain> brain;
  std::unique_ptr<petwin::SpriteSet> sprites;
  petwin::Bubble bubble;
  std::wstring assetsDir, shipsDir;
  std::vector<Ship> ships;
  int ship = -1;
  std::wstring skin;
  pet::VoiceBank voices;
  pet::VoiceBank fallbackVoices;  // assets\lines.txt
  std::mt19937 rng{GetTickCount()};

  pet::BrainConfig cfgBase;
  int height = 320, configHeight = 320;
  bool userHeight = false;
  // Paintings (static, dynamic, Live2D) and chibis keep separate sizes: [0] painting, [1] chibi.
  int heightK[2] = {320, 320};
  bool userHeightK[2] = {false, false};
  int kind = 0;
  bool dailyRandom = false;
  std::string lastDay;
  // fade in / out (blyy-style), in SourceConstantAlpha steps
  int alpha = 255, fadeDir = 0;
  petwin::SpriteFrame cur;
  int curX = 0, curY = 0;
  uint64_t shownId = 0;
  // chat
  HWND chatWnd = nullptr, chatEdit = nullptr;
  pet::ChatSession chat;
  std::string chatKey;
  bool chatBusy = false;
  int pendingWheel = 0;
  bool hideOnFullscreen = true, mirrorLeft = true, clickThrough = false;
  int bubbleMs = 3000, chatterMin = 20, savedX = -1;
  std::wstring configShip, configSkin;

  ULONGLONG lastTick = 0, hiddenSince = 0;
  int timerMs = -1;
  bool userHidden = false, fsHidden = false;
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

std::string narrow(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
  return s;
}


std::vector<std::wstring> listEntries(const std::wstring& dir, bool dirsOnly) {
  std::vector<std::wstring> out;
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((dir + L"\\*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE) return out;
  do {
    if (fd.cFileName[0] == L'.') continue;
    bool isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    std::wstring n = fd.cFileName;
    bool png = n.size() > 4 && _wcsicmp(n.c_str() + n.size() - 4, L".png") == 0;
    if (dirsOnly ? isDir : (isDir || png)) out.push_back(n);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  std::sort(out.begin(), out.end(), [](const std::wstring& a, const std::wstring& b) {
    return CompareStringW(LOCALE_USER_DEFAULT, NORM_IGNORECASE | SORT_DIGITSASNUMBERS, a.c_str(), -1, b.c_str(), -1) ==
           CSTR_LESS_THAN;
  });
  return out;
}

std::wstring userDataDir() {
  wchar_t buf[MAX_PATH];
  if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, buf))) return exeDir();
  std::wstring dir = std::wstring(buf) + L"\\BelfastPet";
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}

std::wstring settingsFile() { return userDataDir() + L"\\settings.ini"; }

void writeSettings(const App& app) {
  std::ofstream out(settingsFile().c_str(), std::ios::binary | std::ios::trunc);
  if (app.ship >= 0) out << "ship=" << narrow(app.ships[app.ship].key) << "\n";
  out << "skin=" << narrow(app.skin) << "\n";
  if (app.userHeightK[0]) out << "height=" << app.heightK[0] << "\n";
  if (app.userHeightK[1]) out << "height_sd=" << app.heightK[1] << "\n";
  out << "daily_random=" << (app.dailyRandom ? 1 : 0) << "\n";
  out << "last_day=" << app.lastDay << "\n";
  out << "chatter=" << app.chatterMin << "\n";
  out << "clickthrough=" << (app.clickThrough ? 1 : 0) << "\n";
  if (app.brain) out << "x=" << app.last.x << "\n";
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

// Lower our scheduling priority and opt into power throttling ("efficiency mode") when
// available, so a game in the foreground never contends with us.
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

// ---------- ships, skins and lines ----------

void scanShips(App& app) {
  app.ships.clear();
  for (const std::wstring& key : listEntries(app.shipsDir, true)) {
    Ship s;
    s.key = key;
    s.ini = pet::Ini::parse(readFile(app.shipsDir + L"\\" + key + L"\\ship.ini"));
    s.name = widen(s.ini.get("ship", "name", narrow(key)));
    s.skins = listEntries(app.shipsDir + L"\\" + key + L"\\skins", false);
    if (!s.skins.empty()) app.ships.push_back(s);
  }
}

int shipIndex(const App& app, const std::wstring& key) {
  for (size_t i = 0; i < app.ships.size(); ++i)
    if (app.ships[i].key == key) return (int)i;
  return -1;
}

std::wstring skinPath(const App& app, int ship, const std::wstring& skin) {
  return app.shipsDir + L"\\" + app.ships[ship].key + L"\\skins\\" + skin;
}

// 0: painting (static PNG, dynamic painting, Live2D), 1: chibi.
int kindOf(const App& app, int ship, const std::wstring& skin) {
  std::wstring path = skinPath(app, ship, skin);
  DWORD a = GetFileAttributesW(path.c_str());
  if (a == INVALID_FILE_ATTRIBUTES || !(a & FILE_ATTRIBUTE_DIRECTORY)) return 0;
  pet::Ini meta = pet::Ini::parse(readFile(path + L"\\meta.ini"));
  return meta.get("sequence", "kind", "chibi") == "painting" ? 0 : 1;
}

std::string today() {
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[16];
  wsprintfA(buf, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
  return buf;
}

std::pair<int, std::wstring> randomSkin(App& app, int ship, const std::wstring& skin) {
  std::vector<std::pair<int, std::wstring>> all;
  for (size_t si = 0; si < app.ships.size(); ++si)
    for (const std::wstring& k : app.ships[si].skins)
      if (!((int)si == ship && k == skin)) all.push_back({(int)si, k});
  if (all.empty()) return {ship, skin};
  std::uniform_int_distribution<size_t> d(0, all.size() - 1);
  return all[d(app.rng)];
}

// The voice table a skin uses, and whether it is an oath (wedding) skin.
std::string voiceSkin(const App& app, bool* oath) {
  const Ship& s = app.ships[app.ship];
  std::string num = pet::skinNumber(narrow(app.skin));
  std::string mapped = s.ini.get("ship", "voice_" + num, num);
  std::string oathList = "," + s.ini.get("ship", "oath_skins", "") + ",";
  if (oath) *oath = !num.empty() && oathList.find("," + num + ",") != std::string::npos;
  return mapped;
}

void say(App& app, pet::Scene scene) {
  if (app.ship < 0 || !app.brain || app.last.visible == false) return;
  bool oath = false;
  std::string skin = voiceSkin(app, &oath);
  std::string fallback = app.ships[app.ship].ini.get("ship", "default_voice", "01");
  std::vector<std::string> keys = pet::sceneKeys(scene, app.rng);
  std::string text = app.voices.pick(skin, fallback, keys, oath, app.rng);
  if (text.empty()) text = app.fallbackVoices.pick("", "", keys, false, app.rng);
  if (text.empty()) return;
  int ax = app.last.x + app.sprites->width() / 2, ay = app.last.y + app.sprites->headTop();
  app.bubble.show(widen(text), ax, ay, pet::bubbleDurationMs(text, app.bubbleMs));
}

// ---------- rendering ----------

// Pushes the current frame to the layered window at the current fade alpha.
void present(App& app) {
  if (!app.cur.bmp) return;
  HDC screen = GetDC(nullptr);
  HDC mem = CreateCompatibleDC(screen);
  HGDIOBJ old = SelectObject(mem, app.cur.bmp);
  POINT pos = {app.curX, app.curY}, src = {0, 0};
  SIZE size = {app.cur.w, app.cur.h};
  BLENDFUNCTION bf = {AC_SRC_OVER, 0, (BYTE)app.alpha, AC_SRC_ALPHA};
  UpdateLayeredWindow(app.hwnd, screen, &pos, &size, mem, &src, 0, &bf, ULW_ALPHA);
  SelectObject(mem, old);
  DeleteDC(mem);
  ReleaseDC(nullptr, screen);
  app.shownId = app.cur.id;
}

void startFade(App& app, int dir) {
  app.fadeDir = dir;
  SetTimer(app.hwnd, ID_FADE, 40, nullptr);  // 5 steps of 51 alpha = 200 ms
}

void applyFrame(App& app, const pet::Frame& f) {
  if (!f.visible) {
    app.bubble.hide();
    if (IsWindowVisible(app.hwnd) && app.fadeDir >= 0) startFade(app, -1);
    return;
  }
  petwin::SpriteFrame sf = app.sprites->get(f.anim, f.variant, f.index, f.facingLeft && app.mirrorLeft);
  if (!sf.bmp) return;
  int wx = f.x + sf.offX, wy = f.y + sf.offY;
  bool sameImage = sf.id == app.shownId;
  app.cur = sf;
  app.curX = wx;
  app.curY = wy;
  if (!IsWindowVisible(app.hwnd) || app.fadeDir < 0) {
    if (!IsWindowVisible(app.hwnd)) app.alpha = 0;
    present(app);
    ShowWindow(app.hwnd, SW_SHOWNOACTIVATE);
    startFade(app, +1);
  } else if (sameImage) {
    SetWindowPos(app.hwnd, nullptr, wx, wy, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
  } else {
    present(app);
  }
  app.bubble.moveTo(f.x + app.sprites->width() / 2, f.y + app.sprites->headTop());
}

void setTimerMs(App& app, int ms) {
  if (ms == app.timerMs) return;
  app.timerMs = ms;
  if (ms <= 0) KillTimer(app.hwnd, ID_ANIM);
  else SetTimer(app.hwnd, ID_ANIM, (UINT)ms, nullptr);
}

void step(App& app, int dtMs) {
  pet::Frame f = app.brain->tick(dtMs);
  if (f.dirty) applyFrame(app, f);
  app.last = f;
  setTimerMs(app, f.nextTickMs);
  switch (f.event) {
    case pet::PetEvent::TapBody: say(app, pet::Scene::TapBody); break;
    case pet::PetEvent::TapHead: say(app, pet::Scene::TapHead); break;
    case pet::PetEvent::TapSpecial: say(app, pet::Scene::TapSpecial); break;
    case pet::PetEvent::Woke: say(app, pet::Scene::Home); break;
    default: break;
  }
}

void updateGround(App& app) {
  RECT w = workArea(app.hwnd);
  app.brain->setWorkTop(w.top);
  app.brain->setGround(w.left, w.right, w.bottom);
}

void updateHidden(App& app) {
  bool hide = app.userHidden || app.fsHidden;
  bool wasHidden = !app.last.visible;
  app.brain->setHidden(hide);
  if (!hide) updateGround(app);
  app.lastTick = GetTickCount64();
  step(app, 0);
  if (hide && !wasHidden) app.hiddenSince = GetTickCount64();
  // Back after a long break (e.g. a game session): welcome the commander home.
  if (!hide && wasHidden && app.hiddenSince && GetTickCount64() - app.hiddenSince > 10 * 60 * 1000)
    say(app, pet::Scene::Home);
}

void applyClickThrough(App& app) {
  LONG_PTR ex = GetWindowLongPtrW(app.hwnd, GWL_EXSTYLE);
  ex = app.clickThrough ? (ex | WS_EX_TRANSPARENT) : (ex & ~WS_EX_TRANSPARENT);
  SetWindowLongPtrW(app.hwnd, GWL_EXSTYLE, ex);
}

void applyChatterTimer(App& app) {
  KillTimer(app.hwnd, ID_CHATTER);
  if (app.chatterMin > 0) SetTimer(app.hwnd, ID_CHATTER, (UINT)app.chatterMin * 60u * 1000u, nullptr);
}

// Loads skin `skin` of ship `ship` at the current size. The pet keeps its x position.
bool loadSkin(App& app, int ship, const std::wstring& skin, std::wstring* err) {
  std::unique_ptr<petwin::SpriteSet> next(new petwin::SpriteSet());
  int kind = kindOf(app, ship, skin);
  if (!next->load(skinPath(app, ship, skin), app.heightK[kind], app.userHeightK[kind], err)) return false;
  app.kind = kind;
  app.heightK[kind] = next->size();
  app.height = app.heightK[kind];
  app.userHeight = app.userHeightK[kind];

  int x = app.brain ? app.last.x : app.savedX;
  pet::BrainConfig cfg = app.cfgBase;
  cfg.spriteW = next->width();
  cfg.spriteH = next->height();
  cfg.groundInset = next->groundInset();
  cfg.headFraction = next->headFraction();
  cfg.canWalk = next->animated() && next->walks();  // a painting sliding across the desktop looks wrong
  if (next->idleMinMs() > 0) cfg.idleMinMs = next->idleMinMs();
  if (next->idleMaxMs() > 0) cfg.idleMaxMs = next->idleMaxMs();
  for (int i = 0; i < (int)Anim::Count; ++i) {
    cfg.variants[i] = next->variants((Anim)i);
    if (next->fps() > 0) cfg.fps[i] = next->fps();  // chibi frames were rendered at one rate
  }
  cfg.reactHead = next->variantIndex(Anim::React, L"react_head");
  cfg.reactSpecial = next->variantIndex(Anim::React, L"react_special");

  if (ship != app.ship) {
    app.voices = pet::VoiceBank::parseTsv(readFile(app.shipsDir + L"\\" + app.ships[ship].key + L"\\voices.tsv"));
  }
  app.sprites = std::move(next);
  app.brain.reset(new pet::Brain(cfg, (unsigned)GetTickCount()));
  app.ship = ship;
  app.skin = skin;
  app.shownId = 0;
  app.cur = petwin::SpriteFrame();
  if (app.hwnd) {
    RECT work = workArea(app.hwnd);
    app.brain->setWorkTop(work.top);
    if (x < work.left || x > work.right - 20) x = work.right - cfg.spriteW - 24;
    app.brain->setPosition(x, 0);
    app.brain->setGround(work.left, work.right, work.bottom);
    app.brain->setHidden(app.userHidden || app.fsHidden);
    app.timerMs = -1;
    app.lastTick = GetTickCount64();
    step(app, 0);
  }
  return true;
}

void applyHeight(App& app, int height) {
  RECT work = workArea(app.hwnd);
  int h = pet::clampHeightToScreen(height, work.bottom - work.top);
  int k = app.kind;
  if (h == app.heightK[k] && app.userHeightK[k]) return;
  int prev = app.heightK[k];
  bool prevUser = app.userHeightK[k];
  app.heightK[k] = h;
  app.userHeightK[k] = true;
  std::wstring err;
  if (!loadSkin(app, app.ship, app.skin, &err)) {
    app.heightK[k] = prev;
    app.userHeightK[k] = prevUser;
    return;
  }
  writeSettings(app);
}

// ---------- chat (optional; any OpenAI-compatible endpoint, key in chat.ini) ----------

struct ChatResult {
  std::string user, body;  // body: HTTP response text
  std::string netError;
};

std::wstring chatIniPath() { return userDataDir() + L"\\chat.ini"; }
pet::ChatConfig chatConfig() { return pet::ChatConfig::parse(readFile(chatIniPath())); }

void openChatSettings() {
  std::wstring p = chatIniPath();
  if (readFile(p).empty()) {
    std::ofstream out(p.c_str(), std::ios::binary);
    out << "\xEF\xBB\xBF" << pet::chatIniTemplate();  // BOM so Notepad keeps it UTF-8
  }
  ShellExecuteW(nullptr, L"open", p.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

struct ChatJob {
  HWND notify;
  std::wstring url;
  std::string key, body, user;
};

DWORD WINAPI chatThread(LPVOID param) {
  std::unique_ptr<ChatJob> job((ChatJob*)param);
  ChatResult* res = new ChatResult();
  res->user = job->user;
  URL_COMPONENTS uc = {};
  uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {}, path[2048] = {};
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  HINTERNET ses = nullptr, con = nullptr, req = nullptr;
  if (!WinHttpCrackUrl(job->url.c_str(), 0, 0, &uc)) {
    res->netError = "base_url 无效";
  } else {
    ses = WinHttpOpen(L"BelfastPet/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (ses) WinHttpSetTimeouts(ses, 10000, 10000, 60000, 60000);
    con = ses ? WinHttpConnect(ses, host, uc.nPort, 0) : nullptr;
    req = con ? WinHttpOpenRequest(con, L"POST", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)
              : nullptr;
    std::wstring headers = L"Content-Type: application/json\r\nAuthorization: Bearer " + widen(job->key) + L"\r\n";
    bool ok = req && WinHttpSendRequest(req, headers.c_str(), (DWORD)-1L, (LPVOID)job->body.data(), (DWORD)job->body.size(),
                                        (DWORD)job->body.size(), 0) &&
              WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
      res->netError = "连接失败（错误 " + std::to_string(GetLastError()) + "）";
    } else {
      DWORD avail = 0;
      while (WinHttpQueryDataAvailable(req, &avail) && avail) {
        std::string chunk(avail, '\0');
        DWORD got = 0;
        if (!WinHttpReadData(req, &chunk[0], avail, &got) || got == 0) break;
        res->body.append(chunk.data(), got);
      }
    }
  }
  if (req) WinHttpCloseHandle(req);
  if (con) WinHttpCloseHandle(con);
  if (ses) WinHttpCloseHandle(ses);
  if (!PostMessageW(job->notify, WM_CHAT_DONE, 0, (LPARAM)res)) delete res;
  return 0;
}

void showChatBubble(App& app, const std::string& text) {
  app.bubble.show(widen(text), app.last.x + app.sprites->width() / 2, app.last.y + app.sprites->headTop(),
                  pet::bubbleDurationMs(text, app.bubbleMs) + 2000);
}

void prepareChatSession(App& app) {
  std::string key = narrow(app.ships[app.ship].key + L"/" + app.skin);
  if (key == app.chatKey) return;
  app.chatKey = key;
  bool oath = false;
  std::string vskin = voiceSkin(app, &oath);
  std::string fallback = app.ships[app.ship].ini.get("ship", "default_voice", "01");
  pet::ChatConfig cc = chatConfig();
  std::vector<std::string> samples = app.voices.samples(vskin, fallback, 8, oath, app.rng);
  app.chat.reset(pet::chatSystemPrompt(narrow(app.ships[app.ship].name), pet::skinOutfitName(narrow(app.skin)), samples,
                                       cc.maxReplyChars));
}

void sendChat(App& app, const std::wstring& text) {
  pet::ChatConfig cc = chatConfig();
  if (app.chatBusy || !cc.ready() || text.empty()) return;
  prepareChatSession(app);
  ChatJob* job = new ChatJob();
  job->notify = app.hwnd;
  job->url = widen(cc.endpoint());
  job->key = cc.apiKey;
  job->user = narrow(text);
  job->body = app.chat.requestBody(cc, job->user);
  HANDLE th = CreateThread(nullptr, 0, chatThread, job, 0, nullptr);
  if (!th) {
    delete job;
    return;
  }
  CloseHandle(th);
  app.chatBusy = true;
  if (app.chatEdit) EnableWindow(app.chatEdit, FALSE);
  showChatBubble(app, "……");
}

WNDPROC g_editProc = nullptr;

LRESULT CALLBACK chatEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_KEYDOWN && wp == VK_RETURN) {
    wchar_t buf[1024];
    GetWindowTextW(h, buf, 1024);
    SetWindowTextW(h, L"");
    if (g_app) sendChat(*g_app, buf);
    return 0;
  }
  if (msg == WM_KEYDOWN && wp == VK_ESCAPE) {
    ShowWindow(GetParent(h), SW_HIDE);
    return 0;
  }
  if (msg == WM_CHAR && (wp == VK_RETURN || wp == VK_ESCAPE)) return 0;  // no beep
  return CallWindowProcW(g_editProc, h, msg, wp, lp);
}

LRESULT CALLBACK chatWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_CLOSE) {
    ShowWindow(h, SW_HIDE);
    return 0;
  }
  if (msg == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE && g_app && g_app->chatEdit) SetFocus(g_app->chatEdit);
  return DefWindowProcW(h, msg, wp, lp);
}

void openChat(App& app) {
  if (!chatConfig().ready()) {
    openChatSettings();
    return;
  }
  if (!app.chatWnd) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = chatWndProc;
    wc.hInstance = app.hinst;
    wc.lpszClassName = L"BelfastPetChat";
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);
    RECT r = {0, 0, 340, 36};
    AdjustWindowRectEx(&r, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_TOOLWINDOW | WS_EX_TOPMOST);
    app.chatWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, L"和她聊天", WS_POPUP | WS_CAPTION | WS_SYSMENU,
                                  0, 0, r.right - r.left, r.bottom - r.top, nullptr, nullptr, app.hinst, nullptr);
    app.chatEdit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, 6, 6, 328, 24,
                                   app.chatWnd, nullptr, app.hinst, nullptr);
    HFONT font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    SendMessageW(app.chatEdit, WM_SETFONT, (WPARAM)font, TRUE);
    SendMessageW(app.chatEdit, EM_SETCUEBANNER, TRUE, (LPARAM)L"说点什么，回车发送");
    g_editProc = (WNDPROC)SetWindowLongPtrW(app.chatEdit, GWLP_WNDPROC, (LONG_PTR)chatEditProc);
  }
  RECT wr;
  GetWindowRect(app.chatWnd, &wr);
  int w = wr.right - wr.left, h = wr.bottom - wr.top;
  RECT work = workArea(app.hwnd);
  int x = app.last.x + app.sprites->width() / 2 - w / 2;
  int y = app.last.y + app.sprites->height() + 6;           // just below her
  if (y + h > work.bottom) y = app.last.y + app.sprites->headTop() - h - 70;  // or above the bubble
  x = (std::max)((int)work.left, (std::min)(x, (int)work.right - w));
  y = (std::max)((int)work.top, y);
  SetWindowPos(app.chatWnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
  SetForegroundWindow(app.chatWnd);
  SetFocus(app.chatEdit);
}

// ---------- menu / tray ----------

void showMenu(App& app) {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, IDM_TOGGLE, app.userHidden ? L"显示(&S)" : L"隐藏(&H)");

  HMENU skinMenu = CreatePopupMenu();
  for (size_t si = 0; si < app.ships.size(); ++si) {
    HMENU sub = CreatePopupMenu();
    const Ship& s = app.ships[si];
    for (size_t k = 0; k < s.skins.size() && k < 100; ++k) {
      std::wstring label = widen(pet::skinDisplayName(narrow(s.skins[k])));
      bool on = (int)si == app.ship && s.skins[k] == app.skin;
      AppendMenuW(sub, MF_STRING | (on ? MF_CHECKED : 0), IDM_SKIN_BASE + (UINT)(si * 100 + k), label.c_str());
    }
    AppendMenuW(skinMenu, MF_POPUP | ((int)si == app.ship ? MF_CHECKED : 0), (UINT_PTR)sub, s.name.c_str());
  }
  if (app.ships.empty()) AppendMenuW(skinMenu, MF_STRING | MF_GRAYED, 0, L"(assets\\ships 里没有形象)");
  AppendMenuW(skinMenu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(skinMenu, MF_STRING, IDM_RANDOM_SKIN, L"随机换一套(&R)");
  AppendMenuW(skinMenu, MF_STRING | (app.dailyRandom ? MF_CHECKED : 0), IDM_DAILY_RANDOM, L"每天随机换装(&D)");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)skinMenu, L"切换形象(&K)");

  HMENU sizeMenu = CreatePopupMenu();
  AppendMenuW(sizeMenu, MF_STRING, IDM_SIZE_UP, L"放大\t滚轮上");
  AppendMenuW(sizeMenu, MF_STRING, IDM_SIZE_DOWN, L"缩小\t滚轮下");
  AppendMenuW(sizeMenu, MF_SEPARATOR, 0, nullptr);
  const std::vector<int>& presets = pet::heightPresets();
  for (size_t i = 0; i < presets.size(); ++i) {
    wchar_t label[64];
    wsprintfW(label, L"%d", presets[i]);
    AppendMenuW(sizeMenu, MF_STRING | (presets[i] == app.height ? MF_CHECKED : 0), IDM_SIZE_BASE + (UINT)i, label);
  }
  AppendMenuW(sizeMenu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(sizeMenu, MF_STRING, IDM_SIZE_RESET, L"恢复默认大小");
  wchar_t sizeLabel[64];
  wsprintfW(sizeLabel, L"大小：%d(&Z)", app.height);
  AppendMenuW(m, MF_POPUP, (UINT_PTR)sizeMenu, sizeLabel);

  HMENU chatMenu = CreatePopupMenu();
  for (size_t i = 0; i < sizeof(kChatterChoices) / sizeof(kChatterChoices[0]); ++i) {
    wchar_t label[64];
    if (kChatterChoices[i] == 0) lstrcpyW(label, L"关闭");
    else wsprintfW(label, L"每 %d 分钟", kChatterChoices[i]);
    AppendMenuW(chatMenu, MF_STRING | (kChatterChoices[i] == app.chatterMin ? MF_CHECKED : 0),
                IDM_CHATTER_BASE + (UINT)i, label);
  }
  AppendMenuW(m, MF_POPUP, (UINT_PTR)chatMenu, L"自动说话(&T)");

  AppendMenuW(m, MF_STRING, IDM_CHAT, chatConfig().ready() ? L"和她聊天…(&C)" : L"和她聊天（需先填写 API Key）…(&C)");
  AppendMenuW(m, MF_STRING, IDM_CHAT_SETTINGS, L"聊天设置…(&G)");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (app.clickThrough ? MF_CHECKED : 0), IDM_CLICKTHROUGH, L"鼠标穿透（用托盘图标关闭）(&P)");
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
    case IDM_CLICKTHROUGH:
      app.clickThrough = !app.clickThrough;
      applyClickThrough(app);
      writeSettings(app);
      break;
    case IDM_AUTOSTART:
      setAutostart(!autostartEnabled());
      break;
    case IDM_OPEN_ASSETS:
      ShellExecuteW(nullptr, L"open", app.shipsDir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      break;
    case IDM_SIZE_UP:
      applyHeight(app, pet::stepHeight(app.height, 1));
      break;
    case IDM_SIZE_DOWN:
      applyHeight(app, pet::stepHeight(app.height, -1));
      break;
    case IDM_SIZE_RESET: {
      app.userHeightK[app.kind] = false;
      app.heightK[app.kind] = app.configHeight;
      std::wstring err;
      loadSkin(app, app.ship, app.skin, &err);
      writeSettings(app);
      break;
    }
    case IDM_RANDOM_SKIN: {
      std::pair<int, std::wstring> r = randomSkin(app, app.ship, app.skin);
      std::wstring err;
      if (loadSkin(app, r.first, r.second, &err)) {
        writeSettings(app);
        say(app, pet::Scene::Login);
      }
      break;
    }
    case IDM_DAILY_RANDOM:
      app.dailyRandom = !app.dailyRandom;
      writeSettings(app);
      break;
    case IDM_CHAT:
      openChat(app);
      break;
    case IDM_CHAT_SETTINGS:
      openChatSettings();
      break;
    case IDM_EXIT:
      DestroyWindow(app.hwnd);
      break;
    default:
      if (cmd >= IDM_SKIN_BASE) {
        int si = (cmd - IDM_SKIN_BASE) / 100, k = (cmd - IDM_SKIN_BASE) % 100;
        if (si < (int)app.ships.size() && k < (int)app.ships[si].skins.size()) {
          std::wstring err;
          if (loadSkin(app, si, app.ships[si].skins[k], &err)) writeSettings(app);
          else MessageBoxW(app.hwnd, err.c_str(), L"BelfastPet", MB_OK | MB_ICONWARNING);
        }
      } else if (cmd >= IDM_SIZE_BASE && cmd < IDM_SIZE_BASE + (int)pet::heightPresets().size()) {
        applyHeight(app, pet::heightPresets()[cmd - IDM_SIZE_BASE]);
      } else if (cmd >= IDM_CHATTER_BASE && cmd < IDM_CHATTER_BASE + (int)(sizeof(kChatterChoices) / sizeof(int))) {
        app.chatterMin = kChatterChoices[cmd - IDM_CHATTER_BASE];
        applyChatterTimer(app);
        writeSettings(app);
      }
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
      } else if (wp == ID_FADE) {
        app->alpha = (std::max)(0, (std::min)(255, app->alpha + app->fadeDir * 51));
        if (app->alpha == 0 && app->fadeDir < 0) {
          KillTimer(h, ID_FADE);
          app->fadeDir = 0;
          ShowWindow(h, SW_HIDE);
          app->shownId = 0;
        } else {
          present(*app);
          if ((app->alpha == 255 && app->fadeDir > 0) || app->fadeDir == 0) {
            KillTimer(h, ID_FADE);
            app->fadeDir = 0;
          }
        }
      } else if (wp == ID_RESIZE) {
        KillTimer(h, ID_RESIZE);
        int notches = app->pendingWheel;
        app->pendingWheel = 0;
        if (notches) applyHeight(*app, pet::stepHeight(app->height, notches));
      } else if (wp == ID_CHATTER) {
        // Only chat when someone is around to read it.
        if (app->last.visible && app->brain->anim() != Anim::Sleep && userIdleSeconds() < 120)
          say(*app, pet::Scene::Chatter);
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
    case WM_LBUTTONUP:
      KillTimer(h, ID_DRAG);
      if (GetCapture() == h) ReleaseCapture();
      app->brain->release();
      step(*app, 0);
      return 0;
    case WM_MOUSEWHEEL: {
      // Re-rendering every pose is not free, so collect notches and resize once the wheel
      // stops. (Windows 10+ delivers wheel messages to the hovered window by default.)
      int notches = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
      if (notches == 0) notches = GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 1 : -1;
      app->pendingWheel += notches;
      SetTimer(h, ID_RESIZE, 120, nullptr);
      return 0;
    }
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
    case WM_CHAT_DONE: {
      std::unique_ptr<ChatResult> res((ChatResult*)lp);
      app->chatBusy = false;
      if (app->chatEdit) {
        EnableWindow(app->chatEdit, TRUE);
        if (IsWindowVisible(app->chatWnd)) SetFocus(app->chatEdit);
      }
      pet::ChatConfig cc = chatConfig();
      std::string reply, why;
      if (!res->netError.empty()) {
        showChatBubble(*app, "（" + res->netError + "）");
      } else if (pet::parseChatReply(res->body, &reply, &why)) {
        reply = pet::clipReply(reply, cc.maxReplyChars + 20);
        app->chat.accept(res->user, reply, cc.maxTurns);
        showChatBubble(*app, reply);
      } else {
        showChatBubble(*app, "（" + why + "）");
      }
      return 0;
    }
    case WM_DESTROY:
      writeSettings(*app);
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
  app.shipsDir = app.assetsDir + L"\\ships";

  pet::Ini ini = pet::Ini::parse(readFile(app.assetsDir + L"\\config.ini"));
  app.configHeight = ini.getInt("general", "height", 320);
  app.height = app.configHeight;
  app.mirrorLeft = ini.getInt("general", "mirror_left", 1) != 0;
  app.hideOnFullscreen = ini.getInt("general", "hide_on_fullscreen", 1) != 0;
  app.bubbleMs = ini.getInt("general", "bubble_ms", 3000);
  app.chatterMin = ini.getInt("general", "chatter_minutes", 20);
  app.savedX = ini.getInt("general", "start_x", -1);
  app.configShip = widen(ini.get("general", "ship", "belfast"));
  app.configSkin = widen(ini.get("general", "skin", ""));
  app.cfgBase.walkSpeed = ini.getInt("general", "walk_speed", 40);
  app.cfgBase.sleepAfterSec = ini.getInt("general", "sleep_after", 180);
  app.cfgBase.idleMinMs = ini.getInt("general", "idle_min_ms", 4000);
  app.cfgBase.idleMaxMs = ini.getInt("general", "idle_max_ms", 12000);
  for (int i = 0; i < (int)Anim::Count; ++i)
    app.cfgBase.fps[i] = ini.getInt("fps", pet::animName((Anim)i), app.cfgBase.fps[i]);
  app.fallbackVoices = pet::VoiceBank::parsePlain(readFile(app.assetsDir + L"\\lines.txt"));

  pet::Ini saved = pet::Ini::parse(readFile(settingsFile()));
  app.heightK[0] = app.heightK[1] = app.configHeight;
  if (saved.has("", "height_sd")) {
    app.heightK[1] = pet::clampHeight(saved.getInt("", "height_sd", app.configHeight));
    app.userHeightK[1] = true;
  }
  app.dailyRandom = saved.getInt("", "daily_random", 0) != 0;
  app.lastDay = saved.get("", "last_day", "");
  app.chatterMin = saved.getInt("", "chatter", app.chatterMin);
  app.clickThrough = saved.getInt("", "clickthrough", 0) != 0;
  app.savedX = saved.getInt("", "x", app.savedX);

  scanShips(app);
  if (app.ships.empty()) {
    MessageBoxW(nullptr, (L"没有找到形象。\n\n请先运行 tools/build_ships.py 生成\n" + app.shipsDir).c_str(), L"BelfastPet",
                MB_OK | MB_ICONWARNING);
    return 1;
  }
  // Saved choice -> config.ini -> the first skin of the first ship.
  int ship = shipIndex(app, widen(saved.get("", "ship", "")));
  std::wstring skin = widen(saved.get("", "skin", ""));
  if (ship < 0) { ship = shipIndex(app, app.configShip); skin = app.configSkin; }
  if (ship < 0) ship = 0;
  const Ship& s = app.ships[ship];
  if (std::find(s.skins.begin(), s.skins.end(), skin) == s.skins.end()) skin = s.skins.front();
  if (saved.has("", "height")) {  // older settings had one size: it belonged to the kind shown then
    int k = saved.has("", "height_sd") ? 0 : kindOf(app, ship, skin);
    app.heightK[k] = pet::clampHeight(saved.getInt("", "height", app.configHeight));
    app.userHeightK[k] = true;
  }
  if (app.dailyRandom && app.lastDay != today()) {  // 每天随机换装
    std::pair<int, std::wstring> r = randomSkin(app, ship, skin);
    ship = r.first;
    skin = r.second;
  }
  app.lastDay = today();

  WNDCLASSW wc = {};
  wc.lpfnWndProc = wndProc;
  wc.hInstance = hinst;
  wc.lpszClassName = kClass;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassW(&wc);
  app.hwnd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE, kClass,
                             L"BelfastPet", WS_POPUP, 0, 0, 1, 1, nullptr, nullptr, hinst, nullptr);
  if (!app.hwnd) {
    MessageBoxW(nullptr, L"窗口创建失败。", L"BelfastPet", MB_OK | MB_ICONERROR);
    return 1;
  }
  app.bubble.create(hinst);

  std::wstring err;
  if (!loadSkin(app, ship, skin, &err) && !loadSkin(app, 0, app.ships[0].skins.front(), &err)) {
    MessageBoxW(nullptr, err.c_str(), L"BelfastPet", MB_OK | MB_ICONWARNING);
    return 1;
  }
  applyClickThrough(app);
  addTray(app);
  SetTimer(app.hwnd, ID_WATCH, 2000, nullptr);
  applyChatterTimer(app);
  say(app, pet::Scene::Login);

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0)) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (app.icon) DestroyIcon(app.icon);
  CloseHandle(mutex);
  return 0;
}
