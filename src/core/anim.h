#pragma once

namespace pet {

enum class Anim { Idle = 0, Blink, Walk, Drag, Fall, React, Sleep, Land, Count };

// Lower-case name; also the folder name of a frame sequence inside an animated skin.
inline const char* animName(Anim a) {
  switch (a) {
    case Anim::Idle: return "idle";
    case Anim::Blink: return "blink";
    case Anim::Walk: return "walk";
    case Anim::Drag: return "drag";
    case Anim::Fall: return "fall";
    case Anim::React: return "react";
    case Anim::Sleep: return "sleep";
    case Anim::Land: return "land";
    default: return "idle";
  }
}

// One-shot animations play once and return to Idle.
inline bool isOneShot(Anim a) { return a == Anim::Blink || a == Anim::React || a == Anim::Land; }

}  // namespace pet
