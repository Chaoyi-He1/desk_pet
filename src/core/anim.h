#pragma once

namespace pet {

enum class Anim { Idle = 0, Blink, Walk, Drag, Fall, React, Sleep, Count };

// Lower-case name; also the sub-directory name for frame sequences under assets/.
inline const char* animName(Anim a) {
  switch (a) {
    case Anim::Idle: return "idle";
    case Anim::Blink: return "blink";
    case Anim::Walk: return "walk";
    case Anim::Drag: return "drag";
    case Anim::Fall: return "fall";
    case Anim::React: return "react";
    case Anim::Sleep: return "sleep";
    default: return "idle";
  }
}

}  // namespace pet
