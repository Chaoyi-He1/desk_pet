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

bool clickableIn(HWND w, POINT pt) {
  if (!w) return false;
  // A program that is not responding would block the accessibility calls: skip it.
  if (IsHungAppWindow(w)) return false;
  DWORD_PTR answer = 0;
  if (!SendMessageTimeoutW(w, WM_NULL, 0, 0, SMTO_ABORTIFHUNG | SMTO_BLOCK, 100, &answer)) return false;
  IAccessible* acc = nullptr;
  if (FAILED(AccessibleObjectFromWindow(w, (DWORD)OBJID_CLIENT, IID_IAccessible, (void**)&acc)) || !acc) return false;
  long childId = CHILDID_SELF;
  for (int depth = 0; depth < 16; ++depth) {  // down to the deepest object at the point
    VARIANT hit;
    VariantInit(&hit);
    if (FAILED(acc->accHitTest(pt.x, pt.y, &hit))) break;
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
  return found;
}

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
  EnterCriticalSection(&lock_);
  quit_ = true;
  LeaveCriticalSection(&lock_);
  WakeConditionVariable(&wake_);
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
    bool clickable = clickableIn(windowBelowAt(pet, pt), pt);
    EnterCriticalSection(&self->lock_);
    bool quit = self->quit_;
    LeaveCriticalSection(&self->lock_);
    if (!quit) PostMessageW(self->notify_, self->msg_, token, clickable ? 1 : 0);
  }
  CoUninitialize();
  delete self;  // abandoned: nobody else holds it any more
  return 0;
}

}  // namespace petwin
