#include "core/brain.h"

#include <algorithm>
#include <cmath>

namespace pet {

Brain::Brain(const BrainConfig& cfg, std::vector<std::string> lines, unsigned seed)
    : cfg_(cfg), lines_(std::move(lines)), rng_(seed) {
  for (int& n : cfg_.frameCount) n = std::max(n, 1);
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
  x_ = clampX(mx - offX_);
  y_ = clampY(my - offY_);
}

void Brain::release() {
  if (!pressed_) return;
  pressed_ = false;
  if (dragging_) {
    dragging_ = false;
    if (y_ < groundTop()) {
      vy_ = 0;
      enter(Anim::Fall);
    } else {
      y_ = groundTop();
      enter(Anim::Idle);
    }
    return;
  }
  // Plain click.
  if (!lines_.empty()) {
    std::uniform_int_distribution<size_t> pick(0, lines_.size() - 1);
    pendingSay_ = lines_[pick(rng_)];
  }
  enter(Anim::React);
}

Frame Brain::tick(int dtMs) {
  if (!hidden_) {
    bool restful = anim_ == Anim::Idle || anim_ == Anim::Blink || anim_ == Anim::Walk;
    if (sleepy_ && restful) enter(Anim::Sleep);
    else if (!sleepy_ && anim_ == Anim::Sleep) enter(Anim::Idle);

    if (dtMs > 0) {
      Anim before = anim_;
      switch (anim_) {
        case Anim::Idle:
          idleEventMs_ -= dtMs;
          if (idleEventMs_ <= 0) {
            std::uniform_real_distribution<double> u(0.0, 1.0);
            if (u(rng_) < 0.4 || !chooseWalkTarget()) enter(Anim::Blink);
            else enter(Anim::Walk);
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
            enter(Anim::Idle);
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
  f.index = index_;
  f.facingLeft = facingLeft_;
  f.x = static_cast<int>(std::lround(x_));
  f.y = static_cast<int>(std::lround(y_));
  f.visible = !hidden_;
  f.say = std::move(pendingSay_);
  pendingSay_.clear();
  if (hidden_) f.nextTickMs = 0;
  else if (anim_ == Anim::Fall) f.nextTickMs = 33;
  else f.nextTickMs = 1000 / cfg_.fps[(int)anim_];
  f.dirty = !hasLast_ || f.anim != last_.anim || f.index != last_.index ||
            f.facingLeft != last_.facingLeft || f.x != last_.x || f.y != last_.y ||
            f.visible != last_.visible;
  last_ = f;
  hasLast_ = true;
  return f;
}

void Brain::advanceAnimation(int dtMs) {
  frameAccMs_ += dtMs;
  int period = 1000 / cfg_.fps[(int)anim_];
  while (frameAccMs_ >= period) {
    frameAccMs_ -= period;
    if (++index_ >= frames(anim_)) {
      index_ = 0;
      if (anim_ == Anim::Blink || anim_ == Anim::React) {
        enter(Anim::Idle);
        break;
      }
    }
  }
}

void Brain::enter(Anim a) {
  anim_ = a;
  index_ = 0;
  frameAccMs_ = 0;
  if (a == Anim::Idle) scheduleIdleEvent();
}

int Brain::frames(Anim a) const { return cfg_.frameCount[(int)a]; }

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
  return static_cast<int>(std::lround(std::min<double>(std::max<double>(y, workTop_), hi)));
}

}  // namespace pet
