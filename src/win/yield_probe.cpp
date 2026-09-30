#include "win/yield_probe.h"

#include <dwmapi.h>
#include <oleacc.h>

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

bool roleClickable(long role, long state) {
  switch (role) {
    case ROLE_SYSTEM_PUSHBUTTON:
    case ROLE_SYSTEM_LINK:
    case ROLE_SYSTEM_CHECKBUTTON:
    case ROLE_SYSTEM_RADIOBUTTON:
    case ROLE_SYSTEM_COMBOBOX:
    case ROLE_SYSTEM_BUTTONMENU:
    case ROLE_SYSTEM_BUTTONDROPDOWN:
    case ROLE_SYSTEM_SPLITBUTTON:
    case ROLE_SYSTEM_MENUITEM:
    case ROLE_SYSTEM_PAGETAB:
    case ROLE_SYSTEM_LISTITEM:
    case ROLE_SYSTEM_OUTLINEITEM:
    case ROLE_SYSTEM_SLIDER:
      return true;
    case ROLE_SYSTEM_TEXT:
      return !(state & STATE_SYSTEM_READONLY);  // an input box
    default:
      return false;
  }
}

bool itemClickable(IAccessible* acc, long childId) {
  VARIANT child;
  VariantInit(&child);
  child.vt = VT_I4;
  child.lVal = childId;
  long state = 0;
  VARIANT v;
  VariantInit(&v);
  if (SUCCEEDED(acc->get_accState(child, &v)) && v.vt == VT_I4) state = v.lVal;
  VariantClear(&v);
  bool hit = false;
  if (SUCCEEDED(acc->get_accRole(child, &v)) && v.vt == VT_I4) hit = roleClickable(v.lVal, state);
  VariantClear(&v);
  if (!hit) {
    BSTR action = nullptr;
    if (SUCCEEDED(acc->get_accDefaultAction(child, &action)) && action) {
      hit = SysStringLen(action) > 0;
      SysFreeString(action);
    }
  }
  return hit;
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

Probe clickableIn(HWND w, POINT pt) {
  if (!w) return Probe::Plain;  // nothing but the desktop background
  // A program that is not responding (or busy right now) would block the accessibility
  // calls: do not ask it, and do not take that for an answer either.
  if (IsHungAppWindow(w)) return Probe::Unknown;
  DWORD_PTR answer = 0;
  if (!SendMessageTimeoutW(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &answer)) return Probe::Unknown;
  IAccessible* acc = nullptr;
  if (FAILED(AccessibleObjectFromWindow(w, (DWORD)OBJID_CLIENT, IID_IAccessible, (void**)&acc)) || !acc)
    return Probe::Unknown;
  long childId = CHILDID_SELF;
  for (int depth = 0; depth < 16; ++depth) {  // down to the deepest object at the point
    VARIANT hit;
    VariantInit(&hit);
    HRESULT hr = acc->accHitTest(pt.x, pt.y, &hit);
    if (FAILED(hr)) {
      // The program rejected or dropped the call (e.g. Office while editing a cell): no answer.
      if (depth == 0 && HRESULT_FACILITY(hr) == FACILITY_RPC) {
        acc->Release();
        return Probe::Unknown;
      }
      break;
    }
    if (hit.vt == VT_DISPATCH && hit.pdispVal) {
      IAccessible* inner = nullptr;
      HRESULT hr = hit.pdispVal->QueryInterface(IID_IAccessible, (void**)&inner);
      VariantClear(&hit);
      if (FAILED(hr) || !inner) break;
      if (inner == acc) {
        inner->Release();
        break;
      }
      acc->Release();
      acc = inner;
      childId = CHILDID_SELF;
      continue;
    }
    if (hit.vt == VT_I4) childId = hit.lVal;  // a simple element inside this object
    VariantClear(&hit);
    break;
  }
  bool found = itemClickable(acc, childId);
  for (int up = 0; up < 3 && !found; ++up) {  // then up to three containers
    if (childId != CHILDID_SELF) {
      childId = CHILDID_SELF;
      found = itemClickable(acc, childId);
      continue;
    }
    IDispatch* pd = nullptr;
    if (FAILED(acc->get_accParent(&pd)) || !pd) break;
    IAccessible* parent = nullptr;
    HRESULT hr = pd->QueryInterface(IID_IAccessible, (void**)&parent);
    pd->Release();
    if (FAILED(hr) || !parent) break;
    acc->Release();
    acc = parent;
    found = itemClickable(acc, CHILDID_SELF);
  }
  acc->Release();
  return found ? Probe::Clickable : Probe::Plain;
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
    Probe result = clickableIn(windowBelowAt(pet, pt), pt);
    EnterCriticalSection(&self->lock_);
    bool quit = self->quit_;
    LeaveCriticalSection(&self->lock_);
    if (!quit) PostMessageW(self->notify_, self->msg_, token, (LPARAM)result);
  }
  CoUninitialize();
  delete self;  // abandoned: nobody else holds it any more
  InterlockedDecrement(&g_stuck);
  return 0;
}

}  // namespace petwin
