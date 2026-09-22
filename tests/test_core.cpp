#include "minitest.h"
#include "core/ini.h"
#include "core/screen.h"
#include "core/pose.h"
#include "core/brain.h"

using namespace pet;

// ---------- Ini ----------
TEST(ini_parses_sections_keys_comments) {
  Ini ini = Ini::parse("[a]\nx=1 ; c\n# c\n  y = hello  \n[b]\nx=2\nz=1.5\n");
  CHECK_EQ(ini.getInt("a", "x", 0), 1);
  CHECK(ini.get("a", "y", "") == "hello");
  CHECK_EQ(ini.getInt("b", "x", 0), 2);
  CHECK_EQ(ini.getInt("a", "zz", 7), 7);
  CHECK_EQ(ini.getInt("a", "y", 9), 9);  // non-numeric -> default
  CHECK(ini.getDouble("b", "z", 0) == 1.5);
  CHECK(ini.has("a", "x"));
  CHECK(!ini.has("c", "x"));
  CHECK_EQ(Ini::parse("").getInt("g", "k", 3), 3);
}

TEST(ini_keys_case_insensitive_and_inline_comment_trimmed) {
  Ini ini = Ini::parse("[General]\nHeight = 320 ; px\n");
  CHECK_EQ(ini.getInt("general", "height", 0), 320);
}

// ---------- screen ----------
TEST(covers_monitor) {
  Rect mon{0, 0, 1920, 1080};
  CHECK(coversMonitor(Rect{0, 0, 1920, 1080}, mon));
  CHECK(coversMonitor(Rect{0, 0, 1920, 1079}, mon));      // within tolerance
  CHECK(!coversMonitor(Rect{0, 0, 1920, 1000}, mon));
  CHECK(coversMonitor(Rect{-1, -1, 1921, 1081}, mon));    // overshoot is fine
  CHECK(!coversMonitor(Rect{100, 0, 2020, 1080}, mon));
}

// ---------- pose ----------
TEST(pose_table) {
  CHECK_EQ(poseFrameCount(Anim::Idle), 2);
  CHECK_EQ(poseFrameCount(Anim::Blink), 2);
  CHECK_EQ(poseFrameCount(Anim::Walk), 4);
  CHECK_EQ(poseFrameCount(Anim::Drag), 2);
  CHECK_EQ(poseFrameCount(Anim::Fall), 2);
  CHECK_EQ(poseFrameCount(Anim::React), 4);
  CHECK_EQ(poseFrameCount(Anim::Sleep), 2);
  CHECK(poseFor(Anim::Idle, 0).isIdentity());
  CHECK(!poseFor(Anim::Idle, 1).isIdentity());
  CHECK(poseFor(Anim::Walk, 0).angleDeg < 0);
  CHECK(poseFor(Anim::Walk, 2).angleDeg > 0);
  CHECK(poseFor(Anim::Sleep, 0).brightness < 1.0);
  CHECK(poseFor(Anim::Idle, 5) == poseFor(Anim::Idle, 1));
}

TEST(anim_names_match_asset_dirs) {
  CHECK(std::string(animName(Anim::Idle)) == "idle");
  CHECK(std::string(animName(Anim::Blink)) == "blink");
  CHECK(std::string(animName(Anim::Walk)) == "walk");
  CHECK(std::string(animName(Anim::Drag)) == "drag");
  CHECK(std::string(animName(Anim::Fall)) == "fall");
  CHECK(std::string(animName(Anim::React)) == "react");
  CHECK(std::string(animName(Anim::Sleep)) == "sleep");
}

// ---------- brain ----------
static BrainConfig testCfg() {
  BrainConfig c;
  c.spriteW = 32; c.spriteH = 48;
  for (int i = 0; i < (int)Anim::Count; ++i) c.frameCount[i] = 2;
  c.walkSpeed = 100;
  c.sleepAfterSec = 10;
  return c;
}
static Brain makeBrain(unsigned seed = 1) {
  Brain b(testCfg(), {"line one", "line two"}, seed);
  b.setGround(0, 800, 600);
  b.setPosition(100, 552);
  return b;
}

TEST(brain_initial) {
  Brain b = makeBrain();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Idle);
  CHECK_EQ(f.y, 552);
  CHECK_EQ(f.x, 100);
  CHECK(f.visible);
  CHECK_EQ(f.nextTickMs, 500);
  CHECK(f.dirty);  // first frame must be drawn
}

TEST(brain_click_reacts_with_line) {
  Brain b = makeBrain();
  b.tick(0);
  b.press(110, 570);
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::React);
  CHECK(!f.say.empty());
  CHECK_EQ(f.nextTickMs, 1000 / 6);
  int n = 0;
  while (f.anim == Anim::React && n < 20) { f = b.tick(200); ++n; }
  CHECK(f.anim == Anim::Idle);
  CHECK(f.say.empty());
}

TEST(brain_drag_then_fall) {
  Brain b = makeBrain();
  b.tick(0);
  b.press(110, 570);
  b.move(112, 571);          // below threshold: still not dragging
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Idle);
  b.move(130, 400);
  f = b.tick(0);
  CHECK(f.anim == Anim::Drag);
  CHECK_EQ(f.x, 120);
  CHECK_EQ(f.y, 382);
  b.release();
  f = b.tick(0);
  CHECK(f.anim == Anim::Fall);
  CHECK_EQ(f.nextTickMs, 33);
  int n = 0;
  while (f.anim == Anim::Fall && n < 200) { f = b.tick(33); ++n; }
  CHECK(f.anim == Anim::Idle);
  CHECK_EQ(f.y, 552);
  CHECK_EQ(f.x, 120);
}

TEST(brain_drag_release_on_ground_no_fall) {
  Brain b = makeBrain();
  b.tick(0);
  b.press(110, 570);
  b.move(300, 570);
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Idle);
  CHECK_EQ(f.y, 552);
}

TEST(brain_drag_clamped_to_work_area) {
  Brain b = makeBrain();
  b.tick(0);
  b.press(110, 570);
  b.move(-500, -500);
  Frame f = b.tick(0);
  CHECK_EQ(f.x, 0);
  CHECK_EQ(f.y, 0);
  b.move(5000, 5000);
  f = b.tick(0);
  CHECK_EQ(f.x, 768);
  CHECK_EQ(f.y, 552);
}

TEST(brain_sleep_and_wake) {
  Brain b = makeBrain();
  b.tick(0);
  b.setUserIdleSeconds(11);
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Sleep);
  CHECK_EQ(f.nextTickMs, 1000);
  b.setUserIdleSeconds(0);
  f = b.tick(0);
  CHECK(f.anim == Anim::Idle);
}

TEST(brain_hidden_stops_timer) {
  Brain b = makeBrain();
  b.tick(0);
  b.setHidden(true);
  Frame f = b.tick(0);
  CHECK(!f.visible);
  CHECK_EQ(f.nextTickMs, 0);
  b.setHidden(false);
  f = b.tick(0);
  CHECK(f.visible);
  CHECK(f.nextTickMs > 0);
  CHECK(f.dirty);
}

TEST(brain_walk_stays_in_bounds) {
  Brain b = makeBrain(1);
  Frame f = b.tick(0);
  bool sawWalk = false, sawBlink = false, sawLeft = false, sawRight = false;
  long long t = 0;
  while (t < 600000) {
    int dt = f.nextTickMs > 0 ? f.nextTickMs : 500;
    f = b.tick(dt);
    t += dt;
    CHECK(f.x >= 0 && f.x <= 768);
    CHECK_EQ(f.y, 552);
    CHECK(f.index >= 0 && f.index < 2);
    if (f.anim == Anim::Walk) { sawWalk = true; if (f.facingLeft) sawLeft = true; else sawRight = true; }
    if (f.anim == Anim::Blink) sawBlink = true;
  }
  CHECK(sawWalk);
  CHECK(sawBlink);
  CHECK(sawLeft && sawRight);
}

TEST(brain_no_dirty_without_change) {
  Brain b = makeBrain();
  b.tick(0);
  Frame f = b.tick(0);
  CHECK(!f.dirty);
  CHECK(f.say.empty());
}

TEST(brain_frame_advances_at_fps) {
  Brain b = makeBrain();
  Frame f = b.tick(0);
  CHECK_EQ(f.index, 0);
  f = b.tick(499);
  CHECK_EQ(f.index, 0);
  f = b.tick(1);
  CHECK_EQ(f.index, 1);
  CHECK(f.dirty);
}

TEST(brain_seeded_is_deterministic) {
  Brain a = makeBrain(7), b = makeBrain(7);
  Frame fa = a.tick(0), fb = b.tick(0);
  for (int i = 0; i < 2000; ++i) {
    fa = a.tick(fa.nextTickMs ? fa.nextTickMs : 500);
    fb = b.tick(fb.nextTickMs ? fb.nextTickMs : 500);
    if (fa.x != fb.x || fa.anim != fb.anim) { CHECK(false); break; }
  }
}

MINITEST_MAIN
