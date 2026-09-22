#include "core/pose.h"

namespace pet {
namespace {

struct Table { int count; Pose poses[4]; };

Pose P(int dx, int dy, double ang, double sx, double sy, double br = 1.0) {
  Pose p; p.dx = dx; p.dy = dy; p.angleDeg = ang; p.scaleX = sx; p.scaleY = sy; p.brightness = br;
  return p;
}

const Table& table(Anim a) {
  static const Table kIdle  {2, {P(0, 0, 0, 1, 1),        P(0, 2, 0, 1, 1)}};
  static const Table kBlink {2, {P(0, 0, 0, 1, 0.985),    P(0, 0, 0, 1, 1)}};
  static const Table kWalk  {4, {P(0, -3, -2.5, 1, 1),    P(0, 0, 0, 1, 1),
                                 P(0, -3, 2.5, 1, 1),     P(0, 0, 0, 1, 1)}};
  static const Table kDrag  {2, {P(0, 0, 6, 1, 1),        P(0, 0, 4, 1, 1)}};
  static const Table kFall  {2, {P(0, 0, 3, 1, 1.03),     P(0, 0, -3, 1, 1.03)}};
  static const Table kReact {4, {P(0, 0, 0, 1.04, 0.94),  P(0, 0, 0, 0.98, 1.05),
                                 P(0, 0, 0, 1, 0.98),     P(0, 0, 0, 1, 1)}};
  static const Table kSleep {2, {P(0, 3, 0, 1, 1, 0.6),   P(0, 4, 0, 1, 1, 0.6)}};
  switch (a) {
    case Anim::Blink: return kBlink;
    case Anim::Walk: return kWalk;
    case Anim::Drag: return kDrag;
    case Anim::Fall: return kFall;
    case Anim::React: return kReact;
    case Anim::Sleep: return kSleep;
    default: return kIdle;
  }
}

}  // namespace

int poseFrameCount(Anim a) { return table(a).count; }

Pose poseFor(Anim a, int index) {
  const Table& t = table(a);
  int i = index % t.count;
  if (i < 0) i += t.count;
  return t.poses[i];
}

}  // namespace pet
