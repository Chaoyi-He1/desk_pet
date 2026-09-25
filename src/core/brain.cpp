#include "core/brain.h"

#include <algorithm>
#include <cmath>

namespace pet {

Brain::Brain(const BrainConfig& cfg, unsigned seed) : cfg_(cfg), rng_(seed) {
  for (auto& v : cfg_.variants)
    for (int& n : v) n = std::max(n, 1);
  if (cfg_.variants[(int)Anim::Idle].empty()) cfg_.variants[(int)Anim::Idle] = {1};
  // States every shell must be able to show fall back to idle's frames.
  for (Anim a : {Anim::Walk, Anim::Drag, Anim::Fall, Anim::Sleep})
    if (!available(a)) cfg_.variants[(int)a] = cfg_.variants[(int)Anim::Idle];
  for (int& f : cfg_.fps) f = std::max(f, 1);
  scheduleIdleEvent();
}

void Brain::setGround(int workLeft, int workRight, int groundY) {
  workLeft_ = workLeft;
  workRight_ = workRight;
  groundY_ = groundY;
  x_ = clampX(x_);
  if (anim_ != Anim::Drag && anim_ != Anim::Fall) y_ = groundTop();
}

void Brain::setWorkTop(int top) { workTop_ = top; }

void Brain::setPosition(int x, int y) {
  x_ = clampX(x);
  y_ = clampY(y);
}

void Brain::setUserIdleSeconds(double s) { sleepy_ = s >= cfg_.sleepAfterSec; }

void Brain::setHidden(bool hidden) {
  if (hidden == hidden_) return;
  hidden_ = hidden;
  pressed_ = dragging_ = false;
  vy_ = 0;
  y_ = groundTop();
  enter(Anim::Idle);
}

void Brain::press(int mx, int my) {
  if (hidden_) return;
  pressed_ = true;
  dragging_ = false;
  pressX_ = mx;
  pressY_ = my;
  offX_ = mx - static_cast<int>(std::lround(x_));
  offY_ = my - static_cast<int>(std::lround(y_));
}

void Brain::move(int mx, int my) {
  if (!pressed_) return;
  if (!dragging_) {
    double dx = mx - pressX_, dy = my - pressY_;
    if (std::sqrt(dx * dx + dy * dy) <= cfg_.dragThresholdPx) return;
    dragging_ = true;
    vy_ = 0;
    enter(Anim::Drag);
  }
  x_ = cfg_.constrainDragToWorkArea ? clampX(mx - offX_) : mx - offX_;
  y_ = cfg_.constrainDragToWorkArea ? clampY(my - offY_) : my - offY_;
}

void Brain::release() {
  if (!pressed_) return;
  pressed_ = false;
  if (dragging_) {
    dragging_ = false;
    // The shell may have selected a different monitor while dragging. Only now
    // constrain the pet to that monitor, then fall to its own floor.
    x_ = clampX(x_);
    y_ = clampY(y_);
    if (y_ < groundTop()) {
      vy_ = 0;
      enter(Anim::Fall);
    } else {
      y_ = groundTop();
      enter(Anim::Idle);
    }
    return;
  }
  // Plain click: the top part of the canvas is the head.
  // Some body taps are the "special touch": its line, and its motion when there is one.
  bool head = offY_ < cfg_.headFraction * cfg_.spriteH;
  bool special = !head && std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < cfg_.specialChance;
  pendingEvent_ = head ? PetEvent::TapHead : special ? PetEvent::TapSpecial : PetEvent::TapBody;
  if (available(Anim::React)) {
    int variants = static_cast<int>(cfg_.variants[(int)Anim::React].size());
    int v = 0;
    if (head && cfg_.reactHead >= 0 && cfg_.reactHead < variants) v = cfg_.reactHead;
    else if (special && cfg_.reactSpecial >= 0 && cfg_.reactSpecial < variants) v = cfg_.reactSpecial;
    enter(Anim::React, v);
  }
}

Frame Brain::tick(int dtMs) {
  if (!hidden_) {
    bool restful = anim_ == Anim::Idle || anim_ == Anim::Blink || anim_ == Anim::Walk;
    if (sleepy_ && restful) {
      enter(Anim::Sleep);
    } else if (!sleepy_ && anim_ == Anim::Sleep) {
      enter(Anim::Idle);
      pendingEvent_ = PetEvent::Woke;
    }

    if (dtMs > 0) {
      Anim before = anim_;
      switch (anim_) {
        case Anim::Idle:
          idleEventMs_ -= dtMs;
          if (idleEventMs_ <= 0) {
            std::uniform_real_distribution<double> u(0.0, 1.0);
            bool action = available(Anim::Blink) && (!cfg_.canWalk || u(rng_) < 0.4);
            if (!action && cfg_.canWalk && chooseWalkTarget()) enter(Anim::Walk);
            else if (available(Anim::Blink)) enter(Anim::Blink);
            else scheduleIdleEvent();
          }
          break;
        case Anim::Walk: {
          double step = cfg_.walkSpeed * dtMs / 1000.0;
          if (std::fabs(targetX_ - x_) <= step) {
            x_ = targetX_;
            enter(Anim::Idle);
          } else {
            x_ += facingLeft_ ? -step : step;
          }
          break;
        }
        case Anim::Fall:
          vy_ += cfg_.gravity * dtMs / 1000.0;
          y_ += vy_ * dtMs / 1000.0;
          if (y_ >= groundTop()) {
            y_ = groundTop();
            vy_ = 0;
            enter(available(Anim::Land) ? Anim::Land : Anim::Idle);
          }
          break;
        default:
          break;
      }
      // A transition consumes the tick; the new animation starts on its first frame.
      if (anim_ == before) advanceAnimation(dtMs);
    }
  }

  Frame f;
  f.anim = anim_;
  f.variant = variant_;
  f.index = index_;
  f.facingLeft = facingLeft_;
  f.x = static_cast<int>(std::lround(x_));
  f.y = static_cast<int>(std::lround(y_));
  f.visible = !hidden_;
  f.event = pendingEvent_;
  pendingEvent_ = PetEvent::None;
  if (hidden_) f.nextTickMs = 0;
  else if (anim_ == Anim::Fall) f.nextTickMs = 33;
  else f.nextTickMs = 1000 / cfg_.fps[(int)anim_];
  f.dirty = !hasLast_ || f.anim != last_.anim || f.variant != last_.variant || f.index != last_.index ||
            f.facingLeft != last_.facingLeft || f.x != last_.x || f.y != last_.y || f.visible != last_.visible;
  last_ = f;
  hasLast_ = true;
  return f;
}

int Brain::frames() const {
  const std::vector<int>& v = cfg_.variants[(int)anim_];
  if (v.empty()) return 1;
  return v[static_cast<size_t>(variant_) % v.size()];
}

void Brain::advanceAnimation(int dtMs) {
  frameAccMs_ += dtMs;
  int period = 1000 / cfg_.fps[(int)anim_];
  while (frameAccMs_ >= period) {
    frameAccMs_ -= period;
    if (++index_ >= frames()) {
      index_ = 0;
      if (isOneShot(anim_)) {
        enter(Anim::Idle);
        break;
      }
    }
  }
}

void Brain::enter(Anim a, int variant) {
  anim_ = a;
  index_ = 0;
  frameAccMs_ = 0;
  int n = static_cast<int>(cfg_.variants[(int)a].size());
  if (variant >= 0) variant_ = n ? variant % n : 0;
  else if (a == Anim::Blink && n > 1) variant_ = std::uniform_int_distribution<int>(0, n - 1)(rng_);
  else variant_ = 0;
  if (a == Anim::Idle) scheduleIdleEvent();
}

void Brain::scheduleIdleEvent() {
  std::uniform_int_distribution<int> d(cfg_.idleMinMs, std::max(cfg_.idleMinMs, cfg_.idleMaxMs));
  idleEventMs_ = d(rng_);
}

bool Brain::chooseWalkTarget() {
  int lo = workLeft_, hi = workRight_ - cfg_.spriteW;
  const int minDist = 40;
  if (hi - lo < minDist) return false;
  std::uniform_int_distribution<int> d(lo, hi);
  for (int i = 0; i < 10; ++i) {
    int t = d(rng_);
    if (std::fabs(t - x_) >= minDist) {
      targetX_ = t;
      facingLeft_ = t < x_;
      return true;
    }
  }
  return false;
}

int Brain::clampX(double x) const {
  int hi = std::max(workLeft_, workRight_ - cfg_.spriteW);
  return static_cast<int>(std::lround(std::min<double>(std::max<double>(x, workLeft_), hi)));
}

int Brain::clampY(double y) const {
  int hi = std::max(workTop_, groundTop());
  return static_cast<int>(std::lround(std::min<double>(std::max<double>(y, workTop_ - cfg_.spriteH / 3), hi)));
}

}  // namespace pet
