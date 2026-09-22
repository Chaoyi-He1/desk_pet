// Single-image mode: each animation frame is a small transform of the base picture.
#pragma once
#include "core/anim.h"

namespace pet {

struct Pose {
  int dx = 0, dy = 0;          // pixel offset of the picture inside the window
  double angleDeg = 0;         // rotation around the bottom-center, clockwise positive
  double scaleX = 1, scaleY = 1;  // scale around the bottom-center
  double brightness = 1;       // 0..1 multiplier

  bool isIdentity() const {
    return dx == 0 && dy == 0 && angleDeg == 0 && scaleX == 1 && scaleY == 1 && brightness == 1;
  }
  bool operator==(const Pose& o) const {
    return dx == o.dx && dy == o.dy && angleDeg == o.angleDeg && scaleX == o.scaleX &&
           scaleY == o.scaleY && brightness == o.brightness;
  }
  bool operator!=(const Pose& o) const { return !(*this == o); }
};

int poseFrameCount(Anim a);
Pose poseFor(Anim a, int index);  // index is taken modulo poseFrameCount(a)

}  // namespace pet
