#include "core/brain.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

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
  floors_.clear();
  floor_ = -1;
  workLeft_ = workLeft;
  workRight_ = workRight;
  groundY_ = groundY;
  x_ = clampX(x_);
  if (anim_ != Anim::Drag && anim_ != Anim::Fall) y_ = groundTop();
}

void Brain::setFloors(const std::vector<Rect>& floors) {
  floors_ = floors;
  if (floors_.empty()) return;
  selectFloor(false);
  x_ = clampX(x_);
  if (anim_ != Anim::Drag && anim_ != Anim::Fall) y_ = groundTop();
}

// The display under her feet becomes her floor. `falling`: the first display at or below
// the feet (she is being dropped onto it); otherwise the one whose ground is nearest to
// them (a display stacked under hers is not hers while she walks about). Nothing spanning
// the feet's x: the nearest display.
void Brain::selectFloor(bool falling) {
  if (floors_.empty()) return;
  int fx = static_cast<int>(std::lround(x_)) + cfg_.spriteW / 2;
  int fy = static_cast<int>(std::lround(y_)) + cfg_.spriteH - cfg_.groundInset;
  int best = -1;
  for (size_t i = 0; i < floors_.size(); ++i) {
    const Rect& r = floors_[i];
    if (fx < r.left || fx >= r.right) continue;
    if (best < 0) { best = static_cast<int>(i); continue; }
    const Rect& b = floors_[best];
    bool better;
    if (falling) {
      bool rBelow = r.bottom >= fy, bBelow = b.bottom >= fy;
      better = rBelow != bBelow ? rBelow : rBelow ? r.bottom < b.bottom : r.bottom > b.bottom;
    } else {
      better = std::abs(r.bottom - fy) < std::abs(b.bottom - fy);
    }
    if (better) best = static_cast<int>(i);
  }
  if (best < 0) best = monitorAtPoint(floors_, fx, fy);
  floor_ = best;
  const Rect& r = floors_[static_cast<size_t>(best)];
  workLeft_ = r.left;
  workRight_ = r.right;
  workTop_ = r.top;
  groundY_ = r.bottom;
}

// The x range she may walk over: her display and, following on, every display beside it
// (touching or overlapping in x, overlapping in y) whose ground is within half her height
// of the one before. Displays stacked above or below are left to drags.
void Brain::walkRange(int* lo, int* hi) const {
  *lo = workLeft_;
  *hi = workRight_;
  if (floors_.empty() || floor_ < 0) return;
  std::vector<bool> seen(floors_.size(), false);
  std::vector<int> queue{floor_};
  seen[static_cast<size_t>(floor_)] = true;
  while (!queue.empty()) {
    const Rect a = floors_[static_cast<size_t>(queue.back())];
    queue.pop_back();
    *lo = std::min(*lo, a.left);
    *hi = std::max(*hi, a.right);
    for (size_t j = 0; j < floors_.size(); ++j) {
      const Rect& b = floors_[j];
      bool beside = b.left <= a.right && b.right >= a.left && b.top < a.bottom && b.bottom > a.top;
      if (seen[j] || !beside || std::abs(b.bottom - a.bottom) > cfg_.spriteH / 2) continue;
      seen[j] = true;
      queue.push_back(static_cast<int>(j));
    }
  }
}

void Brain::setWorkTop(int top) { workTop_ = top; }

void Brain::setPosition(int x, int y) {
  x_ = x;
  y_ = y;
  selectFloor(false);
  x_ = clampX(x_);
  // Standing, she is on the ground: a caller's y only places her among stacked displays.
  y_ = anim_ == Anim::Drag || anim_ == Anim::Fall ? clampY(y_) : groundTop();
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
    // The shell may have selected a different monitor while dragging (or, given every
    // display, she picks the one under her feet). Only now constrain the pet to that
    // monitor, then fall to its own floor.
    selectFloor(true);
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
  // Plain click: the top part of the canvas is the head. Body taps split three ways: the
  // "special touch" (its line, and its motion when there is one), one of the random actions
  // (with the plain touch line), or the plain touch.
  bool head = offY_ < cfg_.headFraction * cfg_.spriteH;
  double u = head ? 1.0 : std::uniform_real_distribution<double>(0.0, 1.0)(rng_);
  bool special = u < cfg_.specialChance;
  bool action = !special && u < cfg_.specialChance + cfg_.tapActionChance && available(Anim::Blink);
  pendingEvent_ = head ? PetEvent::TapHead : special ? PetEvent::TapSpecial : PetEvent::TapBody;
  if (action) {
    enter(Anim::Blink);
  } else if (available(Anim::React)) {
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
          if (!floors_.empty()) {
            int was = floor_;
            selectFloor(false);
            if (floor_ != was) {
              if (groundTop() > y_) {  // walked off the edge of a higher display
                vy_ = 0;
                enter(Anim::Fall);
              } else {
                y_ = groundTop();  // a small step up
              }
            }
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
  int lo, hi;
  walkRange(&lo, &hi);
  hi -= cfg_.spriteW;
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
