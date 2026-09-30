// Accessibility queries for "yield to controls" on Windows: what lies behind the pet.
#pragma once
#include <windows.h>

#include <vector>

#include "core/yield.h"

namespace petwin {

// Other programs' visible top-level windows below `pet` that intersect `region`, front to back.
std::vector<pet::WindowInfo> windowsBelow(HWND pet, const RECT& region);
// The topmost other program's window below `pet` that contains `pt`; nullptr if none.
HWND windowBelowAt(HWND pet, POINT pt);
// True when the accessible object of `w` at `pt`, or one of its three nearest containers, is
// a clickable control. Hit-tests inside `w` itself, so the pet on top does not get in the way.
// Needs COM on the calling thread.
bool clickableIn(HWND w, POINT pt);

// A background thread with COM that runs one query at a time. The result is posted to
// `notify` as message `msg`: wParam = token, lParam = 1 when clickable. A query into a
// program that stops responding can block for a long time, so the owner never deletes a
// ProbeThread: abandon() tells it to stop, and the thread frees itself once its current
// query returns (a stuck one is simply replaced by a new ProbeThread).
class ProbeThread {
public:
  static ProbeThread* start(HWND notify, UINT msg);
  void submit(HWND pet, POINT pt, WPARAM token);
  void abandon();  // the object must not be used afterwards

private:
  ProbeThread(HWND notify, UINT msg);
  ~ProbeThread();
  ProbeThread(const ProbeThread&) = delete;
  ProbeThread& operator=(const ProbeThread&) = delete;
  static DWORD WINAPI main(LPVOID self);
  HWND notify_;
  UINT msg_;
  CRITICAL_SECTION lock_;
  CONDITION_VARIABLE wake_;
  bool has_ = false, quit_ = false;
  HWND pet_ = nullptr;
  POINT pt_{};
  WPARAM token_ = 0;
  HANDLE thread_ = nullptr;
};

}  // namespace petwin
