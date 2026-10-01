#include "win/yield_probe.h"

#include <dwmapi.h>
#include <oleacc.h>

#include <cmath>
#include <cwchar>
#include <map>
#include <string>
#include <utility>

namespace petwin {
namespace {

bool candidate(HWND w, DWORD me) {
  if (!IsWindowVisible(w)) return false;
  if (GetWindowLongPtrW(w, GWL_EXSTYLE) & WS_EX_TRANSPARENT) return false;
  BOOL cloaked = FALSE;
  if (SUCCEEDED(DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  return pid != me;
}

// The facts judgeElement needs for one MSAA element; sizes in 96-dpi pixels, clipped to `win`.
// There is no "own action" on Windows: Chromium gives every element under a click handler the
// (localized) default action "click ancestor", so a default action says nothing; only roles count.
pet::ElementFacts factsOf(IAccessible* acc, long childId, double scale, const RECT& win, bool chromium, RECT* where) {
  pet::ElementFacts f;
  VARIANT child;
  VariantInit(&child);
  child.vt = VT_I4;
  child.lVal = childId;
  long role = 0, state = 0;
  VARIANT v;
  VariantInit(&v);
  if (SUCCEEDED(acc->get_accRole(child, &v)) && v.vt == VT_I4) role = v.lVal;
  VariantClear(&v);
  if (SUCCEEDED(acc->get_accState(child, &v)) && v.vt == VT_I4) state = v.lVal;
  VariantClear(&v);
  switch (role) {
    case ROLE_SYSTEM_PUSHBUTTON:
    case ROLE_SYSTEM_LINK:
    case ROLE_SYSTEM_CHECKBUTTON:
    case ROLE_SYSTEM_RADIOBUTTON:
    case ROLE_SYSTEM_COMBOBOX:
    case ROLE_SYSTEM_BUTTONMENU:
    case ROLE_SYSTEM_BUTTONDROPDOWN:
    case ROLE_SYSTEM_BUTTONDROPDOWNGRID:
    case ROLE_SYSTEM_SPLITBUTTON:
    case ROLE_SYSTEM_MENUITEM:
    case ROLE_SYSTEM_PAGETAB:
    case ROLE_SYSTEM_OUTLINEITEM:
    case ROLE_SYSTEM_SLIDER:
    case ROLE_SYSTEM_SPINBUTTON:
      f.kind = pet::ElementKind::Control;
      break;
    case ROLE_SYSTEM_LISTITEM:  // web <li> are read-only; list-box options, desktop and Explorer icons are not
      if (!(state & STATE_SYSTEM_READONLY)) f.kind = pet::ElementKind::Control;
      break;
    case ROLE_SYSTEM_TEXT:  // an input box. In Chromium, <strong>, <code> and <label> are TEXT too, but
                            // not focusable; native edit boxes are focusable only in the active window.
      if (!(state & (STATE_SYSTEM_READONLY | STATE_SYSTEM_UNAVAILABLE)) && (!chromium || (state & STATE_SYSTEM_FOCUSABLE)))
        f.kind = pet::ElementKind::Text;
      break;
    case ROLE_SYSTEM_COLUMNHEADER: {  // a native list's sortable header (web <th> are not clickable)
      BSTR action = nullptr;
      if (!chromium && SUCCEEDED(acc->get_accDefaultAction(child, &action)) && action && SysStringLen(action) > 0)
        f.kind = pet::ElementKind::Control;
      SysFreeString(action);
      if (f.kind == pet::ElementKind::Other) return f;
      break;
    }
    case ROLE_SYSTEM_DOCUMENT:
    case ROLE_SYSTEM_PANE:
    case ROLE_SYSTEM_CLIENT:
    case ROLE_SYSTEM_WINDOW:
    case ROLE_SYSTEM_APPLICATION:
    case ROLE_SYSTEM_LIST:
    case ROLE_SYSTEM_TABLE:
    case ROLE_SYSTEM_OUTLINE:
    case ROLE_SYSTEM_PAGETABLIST:
    case ROLE_SYSTEM_TOOLBAR:
    case ROLE_SYSTEM_MENUBAR:
      f.kind = pet::ElementKind::Container;
      return f;
    default:
      return f;  // Other: climb
  }
  long l = 0, t = 0, w = 0, h = 0;
  RECT r, clipped;
  if (acc->accLocation(&l, &t, &w, &h, child) == S_OK && w > 0 && h > 0) {
    r = {l, t, l + w, t + h};  // physical pixels, not clipped by scrolling containers
    if (IntersectRect(&clipped, &r, &win)) {
      *where = clipped;
      f.hasSize = true;
      f.w = (int)lround((clipped.right - clipped.left) / scale);
      f.h = (int)lround((clipped.bottom - clipped.top) / scale);
    }
  }
  return f;
}

// Down to the deepest accessible object at `pt` under `root`: *out (AddRef'd) and *childId.
// Returns the result of the first hit test.
HRESULT descend(IAccessible* root, POINT pt, IAccessible** out, long* childId) {
  IAccessible* acc = root;
  acc->AddRef();
  *childId = CHILDID_SELF;
  HRESULT first = S_OK;
  for (int depth = 0; depth < 16; ++depth) {
    VARIANT hit;
    VariantInit(&hit);
    HRESULT hr = acc->accHitTest(pt.x, pt.y, &hit);
    if (depth == 0) first = hr;
    if (FAILED(hr)) break;
    if (hit.vt == VT_DISPATCH && hit.pdispVal) {
      IAccessible* inner = nullptr;
      HRESULT qr = hit.pdispVal->QueryInterface(IID_IAccessible, (void**)&inner);
      VariantClear(&hit);
      if (FAILED(qr) || !inner) break;
      if (inner == acc) {
        inner->Release();
        break;
      }
      acc->Release();
      acc = inner;
      *childId = CHILDID_SELF;
      continue;
    }
    if (hit.vt == VT_I4) *childId = hit.lVal;  // a simple element inside this object
    VariantClear(&hit);
    break;
  }
  *out = acc;
  return first;
}

// Whether the element's current location contains `pt` (give or take `slack`). Chromium can answer
// with the element it found last time if it was near, even after it has scrolled away.
bool locatedAt(IAccessible* acc, long childId, POINT pt, int slack) {
  VARIANT child;
  VariantInit(&child);
  child.vt = VT_I4;
  child.lVal = childId;
  long l = 0, t = 0, w = 0, h = 0;
  if (acc->accLocation(&l, &t, &w, &h, child) != S_OK || w <= 0 || h <= 0) return true;  // no location: take it
  return pt.x >= l - slack && pt.x < l + w + slack && pt.y >= t - slack && pt.y < t + h + slack;
}

// Whether Chromium draws what is at `pt`: Chrome, Edge and Electron apps, and WebView2 or CEF
// content inside another program's window (found by walking down the child windows there).
bool chromiumAt(HWND w, POINT pt) {
  for (HWND h = w; h;) {
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, 64);
    if (wcsncmp(cls, L"Chrome_", 7) == 0) return true;  // Chrome_WidgetWin_*, Chrome_RenderWidgetHostHWND
    POINT c = pt;
    if (!ScreenToClient(h, &c)) break;
    HWND k = ChildWindowFromPointEx(h, c, CWP_SKIPINVISIBLE | CWP_SKIPTRANSPARENT);  // sends no messages
    if (!k || k == h) break;
    h = k;
  }
  return false;
}

// The process of `w`, told apart from a later one that reuses its PID by its start time.
std::pair<DWORD, ULONGLONG> processKey(HWND w) {
  DWORD pid = 0;
  GetWindowThreadProcessId(w, &pid);
  ULONGLONG born = 0;
  if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
    FILETIME c, e, k, u;
    if (GetProcessTimes(p, &c, &e, &k, &u)) born = ((ULONGLONG)c.dwHighDateTime << 32) | c.dwLowDateTime;
    CloseHandle(p);
  }
  return {pid, born};
}

// VS Code and its forks (Cursor, Trae, Windsurf...) take an accessibility client for a screen
// reader and switch the editor into screen-reader mode: they are never asked. They ship the
// workbench in resources\app\out\vs next to the program.
bool isCodeEditor(HWND w) {
  static SRWLOCK lock = SRWLOCK_INIT;
  static std::map<std::pair<DWORD, ULONGLONG>, bool> known;
  auto key = processKey(w);
  AcquireSRWLockShared(&lock);
  auto it = known.find(key);
  bool hit = it != known.end(), editor = hit && it->second;
  ReleaseSRWLockShared(&lock);
  if (hit) return editor;
  if (HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, key.first)) {
    wchar_t path[1024];
    DWORD n = 1024;
    if (QueryFullProcessImageNameW(p, 0, path, &n)) {
      std::wstring exe(path, n);
      size_t slash = exe.find_last_of(L"\\/");
      std::wstring dir = exe.substr(0, slash == std::wstring::npos ? 0 : slash);
      std::wstring name = exe.substr(slash == std::wstring::npos ? 0 : slash + 1);
      DWORD a = GetFileAttributesW((dir + L"\\resources\\app\\out\\vs").c_str());
      editor = a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
      for (const wchar_t* editorExe : {L"Code.exe", L"Code - Insiders.exe", L"Cursor.exe", L"Trae.exe",
                                       L"Trae CN.exe", L"Windsurf.exe", L"VSCodium.exe", L"Kiro.exe"})
        if (_wcsicmp(name.c_str(), editorExe) == 0) editor = true;
    }
    CloseHandle(p);
  }
  AcquireSRWLockExclusive(&lock);
  known[key] = editor;
  ReleaseSRWLockExclusive(&lock);
  return editor;
}

// Chromium makes web pages readable only after a call that needs them (get_accDefaultAction
// below), so for 3 s after first asking a process an empty answer means "nothing yet".
bool waking(HWND w) {
  static SRWLOCK lock = SRWLOCK_INIT;
  static std::map<std::pair<DWORD, ULONGLONG>, ULONGLONG> seen;
  auto key = processKey(w);
  ULONGLONG now = GetTickCount64();
  AcquireSRWLockExclusive(&lock);
  auto it = seen.find(key);
  bool r = true;
  if (it == seen.end())
    seen[key] = now;
  else
    r = now - it->second < 3000;
  ReleaseSRWLockExclusive(&lock);
  return r;
}

}  // namespace

std::vector<pet::WindowInfo> windowsBelow(HWND pet, const RECT& region) {
  std::vector<pet::WindowInfo> out;
  DWORD me = GetCurrentProcessId();
  for (HWND w = GetWindow(pet, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) {
    if (!candidate(w, me)) continue;
    RECT r, x;
    if (!GetWindowRect(w, &r) || !IntersectRect(&x, &r, &region)) continue;
    out.push_back({(int64_t)(intptr_t)w, (int)r.left, (int)r.top, (int)r.right, (int)r.bottom});
  }
  return out;
}

HWND windowBelowAt(HWND pet, POINT pt) {
  DWORD me = GetCurrentProcessId();
  for (HWND w = GetWindow(pet, GW_HWNDNEXT); w; w = GetWindow(w, GW_HWNDNEXT)) {
    if (!candidate(w, me)) continue;
    RECT r;
    if (GetWindowRect(w, &r) && PtInRect(&r, pt)) return w;
  }
  return nullptr;
}

double dipScale(HWND w) {
  typedef UINT(WINAPI * GetDpi)(HWND);
  static GetDpi fn = reinterpret_cast<GetDpi>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
  UINT dpi = fn ? fn(w) : 0;
  if (!dpi) {
    HDC dc = GetDC(nullptr);
    dpi = (UINT)GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
  }
  return dpi ? dpi / 96.0 : 1.0;
}

pet::ProbeResult probeIn(HWND w, POINT pt, double scale) {
  pet::ProbeResult out;
  out.window = (int64_t)(intptr_t)w;
  if (!w || isCodeEditor(w)) {  // nothing but the desktop background, or an editor that is never asked
    out.state = pet::CellState::Plain;
    return out;
  }
  POINT local = pt;
  RECT client;
  if (ScreenToClient(w, &local) && GetClientRect(w, &client) && !PtInRect(&client, local)) {
    out.state = pet::CellState::Plain;  // title bar, menu bar, frame: not asked (OBJID_CLIENT does not cover them)
    return out;
  }
  // A program that is not responding (or busy right now) would block the accessibility
  // calls: do not ask it, and do not take that for an answer either.
  if (IsHungAppWindow(w)) return out;
  DWORD_PTR answer = 0;
  if (!SendMessageTimeoutW(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &answer)) return out;
  bool fresh = waking(w);
  IAccessible* root = nullptr;
  if (FAILED(AccessibleObjectFromWindow(w, (DWORD)OBJID_CLIENT, IID_IAccessible, (void**)&root)) || !root) return out;
  IAccessible* acc = nullptr;
  long childId = CHILDID_SELF;
  HRESULT hr = descend(root, pt, &acc, &childId);
  if (FAILED(hr) && HRESULT_FACILITY(hr) == FACILITY_RPC) {  // rejected or dropped (e.g. Office editing a cell)
    acc->Release();
    root->Release();
    return out;
  }
  int slack = (int)lround(2 * scale);
  bool chromium = chromiumAt(w, pt);
  if (chromium || !locatedAt(acc, childId, pt, slack)) {
    // Chromium answers from its last hit (or a rough guess) and refines it in the background, and
    // keeps web pages empty until something asks for a default action: ask, then look again.
    VARIANT child;
    VariantInit(&child);
    child.vt = VT_I4;
    child.lVal = childId;
    BSTR action = nullptr;
    if (SUCCEEDED(acc->get_accDefaultAction(child, &action)) && action) SysFreeString(action);
    acc->Release();
    Sleep(50);
    descend(root, pt, &acc, &childId);
  }
  root->Release();
  if (!locatedAt(acc, childId, pt, slack)) {  // an old answer about an element that has moved
    acc->Release();
    return out;
  }
  RECT win;
  GetWindowRect(w, &win);
  out.state = pet::CellState::Plain;
  for (int level = 0; level < 6; ++level) {  // the element, then up to five containers
    RECT where{};
    pet::ElementFacts f = factsOf(acc, childId, scale, win, chromium, &where);
    pet::Judgement j = pet::judgeElement(f);
    if (j == pet::Judgement::Clickable) {
      out.state = pet::CellState::Clickable;
      out.rect = f.hasSize ? pet::Rect{(int)where.left, (int)where.top, (int)where.right, (int)where.bottom}
                           : pet::Rect{(int)pt.x, (int)pt.y, (int)pt.x + 1, (int)pt.y + 1};
      break;
    }
    if (j == pet::Judgement::Plain) break;
    if (childId != CHILDID_SELF) {  // a simple element: its object next
      childId = CHILDID_SELF;
      continue;
    }
    IDispatch* pd = nullptr;
    if (FAILED(acc->get_accParent(&pd)) || !pd) break;
    IAccessible* parent = nullptr;
    HRESULT qr = pd->QueryInterface(IID_IAccessible, (void**)&parent);
    pd->Release();
    if (FAILED(qr) || !parent) break;
    acc->Release();
    acc = parent;
  }
  acc->Release();
  if (out.state == pet::CellState::Plain && fresh) out.state = pet::CellState::Unknown;  // page not readable yet
  return out;
}

static volatile LONG g_stuck = 0;  // abandoned threads that have not finished yet

long ProbeThread::stuck() { return InterlockedCompareExchange(&g_stuck, 0, 0); }

ProbeThread* ProbeThread::start(HWND notify, UINT msg) {
  ProbeThread* t = new ProbeThread(notify, msg);
  t->thread_ = CreateThread(nullptr, 0, &ProbeThread::main, t, 0, nullptr);
  if (!t->thread_) {
    delete t;
    return nullptr;
  }
  return t;
}

ProbeThread::ProbeThread(HWND notify, UINT msg) : notify_(notify), msg_(msg) {
  InitializeCriticalSection(&lock_);
  InitializeConditionVariable(&wake_);
}

ProbeThread::~ProbeThread() {  // only ever run by the thread itself, or when it never started
  DeleteCriticalSection(&lock_);
  if (thread_) CloseHandle(thread_);
}

void ProbeThread::abandon() {
  InterlockedIncrement(&g_stuck);
  EnterCriticalSection(&lock_);
  quit_ = true;
  WakeConditionVariable(&wake_);  // while holding the lock: once it is released the thread may free itself
  LeaveCriticalSection(&lock_);
}

void ProbeThread::submit(HWND pet, POINT pt, WPARAM token) {
  EnterCriticalSection(&lock_);
  pet_ = pet;
  pt_ = pt;
  token_ = token;
  has_ = true;
  LeaveCriticalSection(&lock_);
  WakeConditionVariable(&wake_);
}

DWORD WINAPI ProbeThread::main(LPVOID p) {
  ProbeThread* self = (ProbeThread*)p;
  CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  for (;;) {
    EnterCriticalSection(&self->lock_);
    while (!self->has_ && !self->quit_) SleepConditionVariableCS(&self->wake_, &self->lock_, INFINITE);
    if (self->quit_) {
      LeaveCriticalSection(&self->lock_);
      break;
    }
    HWND pet = self->pet_;
    POINT pt = self->pt_;
    WPARAM token = self->token_;
    self->has_ = false;
    LeaveCriticalSection(&self->lock_);
    pet::ProbeResult result = probeIn(windowBelowAt(pet, pt), pt, dipScale(pet));
    EnterCriticalSection(&self->lock_);
    bool quit = self->quit_;
    LeaveCriticalSection(&self->lock_);
    if (!quit) {
      pet::ProbeResult* r = new pet::ProbeResult(result);
      if (!PostMessageW(self->notify_, self->msg_, token, (LPARAM)r)) delete r;
    }
  }
  CoUninitialize();
  delete self;  // abandoned: nobody else holds it any more
  InterlockedDecrement(&g_stuck);
  return 0;
}

}  // namespace petwin
