// Behaviour state machine. Platform independent; the shells feed it input events and
// time, and render whatever Frame it returns.
#pragma once
#include <random>
#include <string>
#include <vector>

#include "core/anim.h"

namespace pet {

// Things the shell may want to react to (e.g. by showing a line).
enum class PetEvent { None, TapBody, TapHead, TapSpecial, Woke };

struct BrainConfig {
  int walkSpeed = 40;                                    // px/s
  int sleepAfterSec = 180;                               // user idle time before sleeping
  int fps[(int)Anim::Count] = {2, 8, 8, 4, 8, 6, 1, 8};  // per Anim
  // Frame count of each variant, per Anim. Empty = the animation is not available
  // (Blink / Land / React are then skipped). Body taps play React variant 0.
  std::vector<int> variants[(int)Anim::Count];
  int reactHead = 1;                                     // React variant for head pats
  int reactSpecial = -1;                                 // "special touch" variant, -1: none
  double specialChance = 0.25;                           // share of body taps that are special
  int spriteW = 96, spriteH = 144;                       // on-screen canvas size
  int groundInset = 0;                                   // px from canvas bottom up to the feet
  double headFraction = 0.22;                            // clicks in this top part of the canvas hit the head
  int idleMinMs = 4000, idleMaxMs = 12000;               // delay between idle events
  int dragThresholdPx = 4;
  double gravity = 1500;                                 // px/s^2
  bool canWalk = true;                                   // false: stays where it is put (paintings)
  bool constrainDragToWorkArea = true;                   // multi-display shells select the work area on drop

  void setFrames(Anim a, int n) { variants[(int)a] = {n}; }
};

struct Frame {
  Anim anim = Anim::Idle;
  int variant = 0;
  int index = 0;
  bool facingLeft = false;
  int x = 0, y = 0;          // window (canvas) top-left in screen coordinates
  bool visible = true;
  bool dirty = true;         // image or position changed since the previous Frame
  PetEvent event = PetEvent::None;
  int nextTickMs = 500;      // 0: stop the animation timer
};

class Brain {
public:
  Brain(const BrainConfig& cfg, unsigned seed);

  // Horizontal range the pet may occupy and the y coordinate of the ground (work-area bottom).
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
  int groundTop() const { return groundY_ - cfg_.spriteH + cfg_.groundInset; }

private:
  void enter(Anim a, int variant = -1);
  bool available(Anim a) const { return !cfg_.variants[(int)a].empty(); }
  int frames() const;
  void scheduleIdleEvent();
  bool chooseWalkTarget();
  int clampX(double x) const;
  int clampY(double y) const;
  void advanceAnimation(int dtMs);

  BrainConfig cfg_;
  std::mt19937 rng_;

  Anim anim_ = Anim::Idle;
  int variant_ = 0;
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
  PetEvent pendingEvent_ = PetEvent::None;

  bool hasLast_ = false;
  Frame last_;
};

}  // namespace pet
