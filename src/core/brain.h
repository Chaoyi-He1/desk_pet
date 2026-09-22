// Behaviour state machine. Platform independent; the shells feed it input events and
// time, and render whatever Frame it returns.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "core/anim.h"

namespace pet {

struct BrainConfig {
  int walkSpeed = 40;                                  // px/s
  int sleepAfterSec = 180;                             // user idle time before sleeping
  int fps[(int)Anim::Count] = {2, 8, 8, 4, 8, 6, 1};   // per Anim
  int frameCount[(int)Anim::Count] = {1, 1, 1, 1, 1, 1, 1};
  int spriteW = 96, spriteH = 144;                     // on-screen size of one frame
  int idleMinMs = 4000, idleMaxMs = 12000;             // delay between idle events
  int dragThresholdPx = 4;
  double gravity = 1500;                               // px/s^2
};

struct Frame {
  Anim anim = Anim::Idle;
  int index = 0;
  bool facingLeft = false;
  int x = 0, y = 0;          // window top-left in screen coordinates
  bool visible = true;
  bool dirty = true;         // image or position changed since the previous Frame
  std::string say;           // non-empty: show this line in a bubble
  int nextTickMs = 500;      // 0: stop the animation timer
};

class Brain {
public:
  Brain(const BrainConfig& cfg, std::vector<std::string> lines, unsigned seed);

  // Horizontal range the pet may occupy and the y coordinate of its feet.
  void setGround(int workLeft, int workRight, int groundY);
  void setWorkTop(int top);
  void setPosition(int x, int y);
  void setUserIdleSeconds(double s);
  void setHidden(bool hidden);

  // Left mouse button, screen coordinates.
  void press(int mx, int my);
  void move(int mx, int my);
  void release();

  // Advance time by dtMs (0 = recompute output only) and return what to draw.
  Frame tick(int dtMs);
  Anim anim() const { return anim_; }

private:
  void enter(Anim a);
  int frames(Anim a) const;
  void scheduleIdleEvent();
  bool chooseWalkTarget();
  int clampX(double x) const;
  int clampY(double y) const;
  int groundTop() const { return groundY_ - cfg_.spriteH; }
  void advanceAnimation(int dtMs);

  BrainConfig cfg_;
  std::vector<std::string> lines_;
  std::mt19937 rng_;

  Anim anim_ = Anim::Idle;
  int index_ = 0;
  int frameAccMs_ = 0;
  bool facingLeft_ = false;
  double x_ = 0, y_ = 0;

  int workLeft_ = 0, workRight_ = 1920, workTop_ = 0, groundY_ = 1080;
  int idleEventMs_ = 0;
  int targetX_ = 0;
  double vy_ = 0;
  bool hidden_ = false;
  bool sleepy_ = false;

  bool pressed_ = false, dragging_ = false;
  int pressX_ = 0, pressY_ = 0, offX_ = 0, offY_ = 0;
  std::string pendingSay_;

  bool hasLast_ = false;
  Frame last_;
};

}  // namespace pet
