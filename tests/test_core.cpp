#include "minitest.h"
#include <algorithm>
#include "core/ini.h"
#include "core/screen.h"
#include "core/pose.h"
#include "core/brain.h"
#include "core/skin.h"
#include "core/voice.h"
#include "core/chat.h"
#include "core/frames.h"
#include "core/yield.h"
#include "core/avatar.h"
#include "core/clicks.h"
#include <cmath>
#include <chrono>
#include <cstring>

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

TEST(monitor_selection_handles_left_right_above_and_below) {
  std::vector<Rect> monitors{{0, 0, 1920, 1080}, {-1080, -100, 0, 1820},
                            {1920, 0, 3840, 1080}, {0, -1080, 1920, 0},
                            {0, 1080, 1920, 2160}};
  CHECK_EQ(monitorAtPoint(monitors, 100, 100), 0);
  CHECK_EQ(monitorAtPoint(monitors, -500, 100), 1);
  CHECK_EQ(monitorAtPoint(monitors, 2500, 100), 2);
  CHECK_EQ(monitorAtPoint(monitors, 100, -500), 3);
  CHECK_EQ(monitorAtPoint(monitors, 100, 1500), 4);
  CHECK_EQ(monitorAtPoint(monitors, 1920, 100), 2);  // shared boundary
  CHECK_EQ(monitorAtPoint(monitors, 0, 100), 0);
}

TEST(monitor_selection_handles_gaps_and_disconnected_displays) {
  std::vector<Rect> monitors{{0, 0, 1000, 800}, {1300, 300, 3220, 1380}};
  CHECK_EQ(monitorAtPoint(monitors, 1100, 400), 0);
  CHECK_EQ(monitorAtPoint(monitors, 1200, 400), 1);
  CHECK_EQ(monitorAtPoint(monitors, 1800, -20), 1);
  monitors.resize(1);
  CHECK_EQ(monitorAtPoint(monitors, 2400, 800), 0);
  CHECK_EQ(monitorAtPoint({}, 0, 0), -1);
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
  CHECK_EQ(poseFrameCount(Anim::Land), 3);
  CHECK(poseFor(Anim::Land, 2).isIdentity());
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
  CHECK(std::string(animName(Anim::Land)) == "land");
}

// ---------- skin ----------
TEST(skin_display_height_caps_upscale_at_2x) {
  CHECK_EQ(displayHeight(320, 1200), 320);   // big picture: config height wins
  CHECK_EQ(displayHeight(320, 150), 300);    // chibi: at most 2x native
  CHECK_EQ(displayHeight(320, 160), 320);
  CHECK_EQ(displayHeight(320, 10), 32);      // never below 32
  CHECK_EQ(displayHeight(0, 1200), 32);
}

TEST(skin_clamp_and_step_height) {
  CHECK_EQ(clampHeight(50), kMinHeight);
  CHECK_EQ(clampHeight(5000), kMaxHeight);
  CHECK_EQ(clampHeight(320), 320);

  CHECK(stepHeight(320, 1) > 320);
  CHECK(stepHeight(320, -1) < 320);
  CHECK_EQ(stepHeight(320, 1), 345);          // 8% of 320 = 25
  CHECK_EQ(stepHeight(320, 0), 320);
  CHECK(stepHeight(320, 3) > stepHeight(320, 1));
  CHECK_EQ(stepHeight(kMaxHeight, 1), kMaxHeight);   // clamped, no runaway
  CHECK_EQ(stepHeight(kMinHeight, -1), kMinHeight);
  CHECK(stepHeight(100, -1) >= kMinHeight);
  // every step must change the size by at least 8 px
  CHECK(stepHeight(96, 1) - 96 >= 8);
  // stepping up then down repeatedly stays inside the range
  int h = 320;
  for (int i = 0; i < 50; ++i) h = stepHeight(h, 1);
  CHECK_EQ(h, kMaxHeight);
  for (int i = 0; i < 80; ++i) h = stepHeight(h, -1);
  CHECK_EQ(h, kMinHeight);
}

TEST(skin_clamp_height_to_screen) {
  CHECK_EQ(clampHeightToScreen(320, 900), 320);
  CHECK_EQ(clampHeightToScreen(1200, 900), 876);      // 900 - 24 margin
  CHECK_EQ(clampHeightToScreen(9999, 0), kMaxHeight); // unknown screen: absolute limit only
  CHECK_EQ(clampHeightToScreen(50, 900), kMinHeight);
  CHECK_EQ(clampHeightToScreen(500, 100), kMinHeight); // tiny screen never goes below the floor
}

TEST(skin_height_presets_are_sorted_and_in_range) {
  const std::vector<int>& p = heightPresets();
  CHECK(!p.empty());
  for (size_t i = 0; i < p.size(); ++i) {
    CHECK(p[i] >= kMinHeight && p[i] <= kMaxHeight);
    if (i) CHECK(p[i] > p[i - 1]);
  }
}

TEST(skin_display_name_strips_order_prefix) {
  CHECK(skinDisplayName("01-改造.png") == "改造");
  CHECK(skinDisplayName("Q版-01-改造.png") == "Q版 改造");
  CHECK(skinDisplayName("04-Serene Steel 礼服.PNG") == "Serene Steel 礼服");
  CHECK(skinDisplayName("belfast.png") == "belfast");
  CHECK(skinDisplayName("11-改造 无舰装 手工抠图.png") == "改造 无舰装 手工抠图");
}

// ---------- avatar (the chat box's face crop) ----------
// A premultiplied BGRA buffer whose alpha is set from a 0/1 picture.
struct AlphaPic {
  int w, h;
  std::vector<uint8_t> px;
  AlphaPic(int w_, int h_) : w(w_), h(h_), px((size_t)w_ * h_ * 4, 0) {}
  void fillRows(int y0, int y1, int x0, int x1) {
    for (int y = y0; y < y1; ++y)
      for (int x = x0; x < x1; ++x) px[((size_t)y * w + x) * 4 + 3] = 255;
  }
  AlphaMask mask() const { return AlphaMask{w, h, px.data() + 3, (size_t)w * 4, 4}; }
};

TEST(avatar_chibi_face_is_a_third_down_the_character) {
  AlphaPic pic(100, 200);
  pic.fillRows(0, 200, 0, 100);  // all opaque: the centre of a row is its middle
  // the character stands from y=10 (head top) to y=190 (ground): 180 tall
  FaceBox f = faceBox(pic.mask(), false, 10, 190, nullptr);
  CHECK(std::fabs(f.side - 180 * 0.48) < 0.01);
  CHECK(std::fabs((f.y + f.side / 2) - (10 + 180 * 0.33)) < 0.01);
  CHECK(std::fabs((f.x + f.side / 2) - 49.5) < 0.01);
}

TEST(avatar_chibi_centres_on_the_longest_run_of_the_face_row) {
  AlphaPic pic(200, 200);
  pic.fillRows(0, 200, 120, 180);  // the head is at x 120..180
  pic.fillRows(0, 200, 10, 30);    // a prop to the left: shorter run, ignored
  FaceBox f = faceBox(pic.mask(), false, 0, 200, nullptr);
  CHECK(std::fabs((f.x + f.side / 2) - 149.5) < 0.01);
}

TEST(avatar_painting_bust_starts_below_thin_strands) {
  AlphaPic pic(200, 200);
  pic.fillRows(0, 10, 69, 71);     // a 2 px strand above the head
  pic.fillRows(10, 50, 50, 90);    // the head: 40 wide, centred on 70
  pic.fillRows(50, 200, 40, 100);  // the body: 60 wide, centred on 70
  FaceBox f = faceBox(pic.mask(), true, 0, 0, nullptr);
  CHECK(std::fabs(f.side - 200 * 0.40) < 0.01);  // 40% of the figure
  CHECK(std::fabs(f.y - 10) < 0.01);              // from the head's top, not the strand's
  CHECK(std::fabs((f.x + f.side / 2) - 69.5) < 0.6);
}

TEST(avatar_hint_is_fractions_of_the_opaque_box) {
  AlphaPic pic(300, 300);
  pic.fillRows(100, 300, 50, 250);  // the figure: box x 50..250, y 100..300 (200 x 200)
  double hint[3] = {0.25, 0.5, 0.3};  // centre at a quarter across, half down; side 30% of the height
  FaceBox f = faceBox(pic.mask(), true, 0, 0, hint);
  CHECK(std::fabs(f.side - 60) < 0.01);
  CHECK(std::fabs((f.x + f.side / 2) - 100) < 0.01);
  CHECK(std::fabs((f.y + f.side / 2) - 200) < 0.01);
}

TEST(avatar_empty_picture_gives_a_box_anyway) {
  AlphaPic pic(64, 64);
  FaceBox f = faceBox(pic.mask(), true, 0, 0, nullptr);
  CHECK(f.side > 0);
}

// ---------- brain ----------
static BrainConfig testCfg() {
  BrainConfig c;
  c.spriteW = 32; c.spriteH = 48;
  for (int i = 0; i < (int)Anim::Count; ++i) c.setFrames((Anim)i, 2);
  c.walkSpeed = 100;
  c.sleepAfterSec = 10;
  c.tapActionChance = 0;  // body taps stay plain unless a test turns actions on
  c.headActionChance = 0;  // and head pats too
  return c;
}
static Brain makeBrain(unsigned seed = 1, BrainConfig c = testCfg()) {
  Brain b(c, seed);
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
  CHECK(f.event == PetEvent::None);
}

TEST(brain_click_body_reacts_and_reports_tap) {
  BrainConfig c = testCfg();
  c.specialChance = 0;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 590);  // 38 px below the top of a 48 px canvas: body
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::React);
  CHECK_EQ(f.variant, 0);
  CHECK(f.event == PetEvent::TapBody);
  CHECK_EQ(f.nextTickMs, 1000 / 6);
  f = b.tick(0);
  CHECK(f.event == PetEvent::None);  // an event is reported once
  int n = 0;
  while (f.anim == Anim::React && n < 20) { f = b.tick(200); ++n; }
  CHECK(f.anim == Anim::Idle);
}

TEST(brain_click_head_uses_head_variant) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {2, 5};  // body, head pat
  c.headFraction = 0.3;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 555);  // 3 px below the canvas top
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::React);
  CHECK_EQ(f.variant, 1);
  CHECK(f.event == PetEvent::TapHead);
  // the head variant has 5 frames at 6 fps: still reacting after 4 frames
  for (int i = 0; i < 4; ++i) f = b.tick(1000 / 6);
  CHECK(f.anim == Anim::React);
  CHECK_EQ(f.index, 4);
}

TEST(brain_special_touch_plays_its_variant) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {2, 3, 7};  // body, head pat, special touch
  c.reactSpecial = 2;
  c.specialChance = 1;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 590);  // body
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::React);
  CHECK_EQ(f.variant, 2);
  CHECK(f.event == PetEvent::TapSpecial);
  b.tick(10000);
  b.press(110, 555);  // head pats are never special
  b.release();
  f = b.tick(0);
  CHECK_EQ(f.variant, 1);
  CHECK(f.event == PetEvent::TapHead);
}

TEST(brain_special_touch_without_motion_keeps_body_variant) {
  BrainConfig c = testCfg();
  c.specialChance = 1;  // reactSpecial stays -1
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 590);
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::React);
  CHECK_EQ(f.variant, 0);
  CHECK(f.event == PetEvent::TapSpecial);  // still picks the special-touch line
}

TEST(brain_body_tap_can_play_a_random_action) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {2, 3};  // body, head pat
  c.variants[(int)Anim::Blink] = {3, 4, 5};
  c.specialChance = 0;
  c.tapActionChance = 1;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 590);  // body
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Blink);
  CHECK(f.event == PetEvent::TapBody);  // still the touch line
  b.tick(10000);
  b.press(110, 555);  // head pats have their own chance, 0 here
  b.release();
  f = b.tick(0);
  CHECK(f.anim == Anim::React);
  CHECK_EQ(f.variant, 1);
  CHECK(f.event == PetEvent::TapHead);
}

TEST(brain_head_pat_can_play_a_random_action) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {2, 3};  // body, head pat
  c.variants[(int)Anim::Blink] = {3, 4, 5};
  c.headActionChance = 1;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 555);  // head
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Blink);
  CHECK(f.event == PetEvent::TapHead);  // still the head-pat line
}

TEST(brain_head_pat_action_needs_action_frames) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {2, 3};
  c.variants[(int)Anim::Blink].clear();
  c.headActionChance = 1;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 555);
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::React);  // the head pat
  CHECK_EQ(f.variant, 1);
  CHECK(f.event == PetEvent::TapHead);
}

TEST(brain_head_pat_shares) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {1, 1, 1};
  c.reactSpecial = 2;
  c.specialChance = 1;  // body only: a head pat is never special
  c.headActionChance = 0.30;
  Brain b = makeBrain(7, c);
  b.tick(0);
  int action = 0, pat = 0;
  for (int i = 0; i < 400; ++i) {
    b.press(110, 555);
    b.release();
    Frame f = b.tick(0);
    CHECK(f.event == PetEvent::TapHead);
    if (f.anim == Anim::Blink) ++action;
    else if (f.anim == Anim::React && f.variant == 1) ++pat;
    b.tick(5000);
  }
  CHECK(action > 80 && action < 160);  // about 30%
  CHECK_EQ(action + pat, 400);         // the rest are head pats
}

TEST(brain_tap_action_picks_any_of_the_actions) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink] = {1, 1, 1};
  c.specialChance = 0;
  c.tapActionChance = 1;
  Brain b = makeBrain(3, c);
  b.tick(0);
  bool seen[3] = {false, false, false};
  for (int i = 0; i < 60; ++i) {
    b.press(110, 590);
    b.release();
    Frame f = b.tick(0);
    CHECK(f.anim == Anim::Blink);
    seen[f.variant % 3] = true;
    b.tick(5000);
  }
  CHECK(seen[0] && seen[1] && seen[2]);
}

TEST(brain_tap_action_needs_action_frames) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink].clear();
  c.specialChance = 0;
  c.tapActionChance = 1;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 590);
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::React);  // the plain touch
  CHECK_EQ(f.variant, 0);
  CHECK(f.event == PetEvent::TapBody);
}

TEST(brain_body_tap_shares) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {1, 1, 1};
  c.reactSpecial = 2;
  c.specialChance = 0.35;
  c.tapActionChance = 0.30;
  Brain b = makeBrain(7, c);
  b.tick(0);
  int special = 0, action = 0, plain = 0;
  for (int i = 0; i < 400; ++i) {
    b.press(110, 590);
    b.release();
    Frame f = b.tick(0);
    if (f.event == PetEvent::TapSpecial) ++special;
    else if (f.anim == Anim::Blink) ++action;
    else ++plain;
    b.tick(5000);
  }
  CHECK(special > 100 && special < 180);  // about 35%
  CHECK(action > 80 && action < 160);     // about 30%
  CHECK(plain > 100 && plain < 180);      // the rest
}

TEST(brain_click_without_react_frames_still_reports) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React].clear();
  c.specialChance = 0;
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 590);
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Idle);
  CHECK(f.event == PetEvent::TapBody);
}

TEST(brain_drag_then_fall_then_land) {
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
  CHECK(f.event == PetEvent::None);  // a drag is not a tap
  int n = 0;
  while (f.anim == Anim::Fall && n < 200) { f = b.tick(33); ++n; }
  CHECK(f.anim == Anim::Land);
  CHECK_EQ(f.y, 552);
  CHECK_EQ(f.x, 120);
  n = 0;
  while (f.anim == Anim::Land && n < 50) { f = b.tick(125); ++n; }
  CHECK(f.anim == Anim::Idle);
}

TEST(brain_fall_without_land_frames_goes_idle) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Land].clear();
  Brain b = makeBrain(1, c);
  b.tick(0);
  b.press(110, 570);
  b.move(130, 300);
  b.release();
  Frame f = b.tick(0);
  int n = 0;
  while (f.anim == Anim::Fall && n < 200) { f = b.tick(33); ++n; }
  CHECK(f.anim == Anim::Idle);
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
  CHECK_EQ(f.y, -16);  // up to a third of the canvas may go above the top edge
  b.move(5000, 5000);
  f = b.tick(0);
  CHECK_EQ(f.x, 768);
  CHECK_EQ(f.y, 552);
}

TEST(brain_cross_display_drag_preserves_grab_offset_and_lands_on_target) {
  struct Drop { Rect work; int x, y; };
  const Drop drops[] = {
    {{-1080, 40, 0, 1920}, -500, 300},
    {{800, 0, 1600, 900}, 1200, 200},
    {{0, -1080, 1920, 0}, 500, -700},
    {{0, 600, 1920, 1680}, 500, 1000}
  };
  for (const Drop& drop : drops) {
    BrainConfig c = testCfg();
    c.constrainDragToWorkArea = false;
    Brain b = makeBrain(1, c);
    b.press(110, 570);  // grab offset (10, 18)
    b.move(drop.x, drop.y);
    Frame f = b.tick(0);
    CHECK(f.anim == Anim::Drag);
    CHECK_EQ(f.x, drop.x - 10);
    CHECK_EQ(f.y, drop.y - 18);
    // While held, animation ticks must not snap back to the previous monitor.
    f = b.tick(200);
    CHECK_EQ(f.x, drop.x - 10);
    CHECK_EQ(f.y, drop.y - 18);
    b.setWorkTop(drop.work.top);
    b.setGround(drop.work.left, drop.work.right, drop.work.bottom);
    b.release();
    f = b.tick(0);
    CHECK(f.anim == Anim::Fall);
    CHECK(f.event == PetEvent::None);
    for (int i = 0; f.anim == Anim::Fall && i < 200; ++i) f = b.tick(33);
    CHECK(f.anim == Anim::Land);
    CHECK_EQ(f.x, drop.x - 10);
    CHECK_EQ(f.y, drop.work.bottom - c.spriteH);
    // Idle walking remains on the selected screen.
    for (int i = 0; i < 300; ++i) {
      f = b.tick(100);
      CHECK(f.x >= drop.work.left && f.x <= drop.work.right - c.spriteW);
    }
  }
}

TEST(brain_cross_display_drop_clamps_to_visible_work_area) {
  BrainConfig c = testCfg();
  c.constrainDragToWorkArea = false;
  Brain b = makeBrain(1, c);
  b.press(110, 570);
  b.move(-20, 3000);
  b.setWorkTop(40);
  b.setGround(-1017, 0, 1957);  // left display, excluding its Dock
  b.release();
  Frame f = b.tick(0);
  CHECK_EQ(f.x, -32);
  CHECK_EQ(f.y, 1909);
  CHECK(f.anim == Anim::Idle);
  b.press(-20, 1920);
  b.move(-2000, -5000);
  b.release();
  f = b.tick(0);
  CHECK_EQ(f.x, -1017);
  CHECK_EQ(f.y, 24);  // workTop minus one third of the canvas
}

TEST(brain_cross_display_drag_keeps_click_threshold) {
  BrainConfig c = testCfg();
  c.constrainDragToWorkArea = false;
  c.specialChance = 0;
  Brain b = makeBrain(1, c);
  b.press(110, 570);
  b.move(112, 571);
  Frame f = b.tick(0);
  CHECK_EQ(f.x, 100);
  CHECK_EQ(f.y, 552);
  CHECK(f.anim == Anim::Idle);
  b.release();
  CHECK(b.tick(0).event == PetEvent::TapBody);
}

// ---------- several displays ----------
// The shells hand the brain every display's work area; she stands on the one under her feet.

// Two displays side by side share one ground: she walks from one onto the other and back.
TEST(brain_floors_walks_onto_the_neighbouring_display) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink].clear();  // walks only
  Brain b(c, 2);
  b.setFloors({{0, 0, 800, 600}, {800, 0, 1600, 600}});
  b.setPosition(100, 0);
  Frame f = b.tick(0);
  CHECK_EQ(f.y, 552);
  bool sawRightDisplay = false, sawLeftAgain = false;
  for (int i = 0; i < 20000 && !(sawRightDisplay && sawLeftAgain); ++i) {
    f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
    CHECK_EQ(f.y, 552);
    CHECK(f.anim == Anim::Idle || f.anim == Anim::Walk);
    CHECK(f.x >= 0 && f.x <= 1600 - 32);
    if (f.x + 16 >= 800) sawRightDisplay = true;
    else if (sawRightDisplay) sawLeftAgain = true;
  }
  CHECK(sawRightDisplay);
  CHECK(sawLeftAgain);
}

// A game takes the left display while she is walking towards it: the walk ends at the edge
// of the display she still has, her feet never over the game.
TEST(brain_floors_a_walk_stops_at_the_edge_when_its_display_is_taken_away) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink].clear();  // walks only
  Brain b(c, 2);
  std::vector<pet::Rect> both = {{0, 0, 800, 600}, {800, 0, 1600, 600}};
  b.setFloors(both);
  b.setPosition(1200, 0);
  Frame f = b.tick(0);
  int leftwardWalks = 0;
  for (int i = 0; i < 200000 && leftwardWalks < 30; ++i) {
    f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
    if (f.anim != Anim::Walk || !f.facingLeft || f.x + 16 < 800) continue;
    ++leftwardWalks;  // heading left on the right display: maybe for the left one
    b.setFloors({both[1]});
    for (int j = 0; j < 20000 && f.anim == Anim::Walk; ++j) {
      f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
      CHECK(f.x >= 800);
      if (f.x < 800) return;
    }
    b.setFloors(both);
  }
  CHECK_EQ(leftwardWalks, 30);
}

// A slightly lower neighbour: she walks off the edge, falls and lands on its ground;
// coming back she just steps up.
TEST(brain_floors_drops_onto_a_lower_display_and_steps_back_up) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink].clear();
  Brain b(c, 2);
  b.setFloors({{0, 0, 800, 600}, {800, 0, 1600, 620}});
  b.setPosition(100, 0);
  Frame f = b.tick(0);
  bool fell = false, landedLow = false, steppedUp = false;
  for (int i = 0; i < 40000 && !(landedLow && steppedUp); ++i) {
    Frame p = f;
    f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
    CHECK(f.anim != Anim::Drag);
    if (f.anim == Anim::Fall) { fell = true; CHECK(f.x + 16 >= 800); }
    if (f.anim == Anim::Land) { landedLow = true; CHECK_EQ(f.y, 572); CHECK(f.x + 16 >= 800); }
    if (f.x + 16 < 800) {
      CHECK_EQ(f.y, 552);
      if (p.y == 572) steppedUp = true;
    } else if (f.anim != Anim::Fall) {
      CHECK_EQ(f.y, 572);
    }
  }
  CHECK(fell);
  CHECK(landedLow);
  CHECK(steppedUp);
}

// Displays stacked above or below hers cannot be walked onto.
TEST(brain_floors_never_walks_onto_a_stacked_display) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink].clear();
  Brain b(c, 2);
  b.setFloors({{0, 0, 800, 600}, {0, 600, 800, 1200}, {0, -1200, 800, -600}});
  b.setPosition(100, 0);
  Frame f = b.tick(0);
  for (int i = 0; i < 20000; ++i) {
    f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
    CHECK_EQ(f.y, 552);
    CHECK(f.x >= 0 && f.x <= 768);
  }
}

// A neighbour whose ground is much lower or higher than hers is not walkable either way.
TEST(brain_floors_too_big_a_step_is_not_walkable) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink].clear();
  Brain b(c, 2);
  b.setFloors({{0, 0, 800, 600}, {800, 0, 1600, 700}, {-800, 0, 0, 500}});
  b.setPosition(100, 0);
  Frame f = b.tick(0);
  for (int i = 0; i < 20000; ++i) {
    f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
    CHECK_EQ(f.y, 552);
    CHECK(f.x >= 0 && f.x <= 768);
  }
}

// Dropped over another display, she falls to that display's ground.
TEST(brain_floors_drop_lands_on_the_display_under_the_cursor) {
  BrainConfig c = testCfg();
  c.constrainDragToWorkArea = false;
  Brain b(c, 1);
  b.setFloors({{0, 0, 800, 600}, {800, 0, 1600, 700}, {0, 600, 800, 1200}});
  b.setPosition(100, 0);
  b.tick(0);
  b.press(110, 570);  // grab offset (10, 18)
  b.move(1210, 300);  // over the right display
  b.release();
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Fall);
  for (int i = 0; f.anim == Anim::Fall && i < 200; ++i) f = b.tick(33);
  CHECK(f.anim == Anim::Land);
  CHECK_EQ(f.x, 1200);
  CHECK_EQ(f.y, 652);
  for (int i = 0; f.anim == Anim::Land && i < 50; ++i) f = b.tick(125);
  b.press(1210, 670);
  b.move(410, 900);  // over the display below the first one
  b.release();
  f = b.tick(0);
  CHECK(f.anim == Anim::Fall);
  for (int i = 0; f.anim == Anim::Fall && i < 200; ++i) f = b.tick(33);
  CHECK(f.anim == Anim::Land);
  CHECK_EQ(f.x, 400);
  CHECK_EQ(f.y, 1152);
}

// A display that goes away: she moves onto the nearest remaining one.
TEST(brain_floors_removed_display_moves_her_to_the_nearest) {
  Brain b(testCfg(), 1);
  b.setFloors({{0, 0, 800, 600}, {800, 0, 1600, 700}});
  b.setPosition(1200, 0);
  Frame f = b.tick(0);
  CHECK_EQ(f.y, 652);
  b.setFloors({{0, 0, 800, 600}});
  f = b.tick(0);
  CHECK_EQ(f.x, 768);
  CHECK_EQ(f.y, 552);
}

TEST(brain_ground_inset_lowers_canvas) {
  BrainConfig c = testCfg();
  c.groundInset = 10;  // feet are 10 px above the canvas bottom
  Brain b(c, 1);
  b.setPosition(100, 0);
  b.setGround(0, 800, 600);  // snaps a standing pet onto the ground
  Frame f = b.tick(0);
  CHECK_EQ(b.groundTop(), 562);
  CHECK_EQ(f.y, 562);
}

TEST(brain_sleep_and_wake_reports_woke) {
  Brain b = makeBrain();
  b.tick(0);
  b.setUserIdleSeconds(11);
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Sleep);
  CHECK_EQ(f.nextTickMs, 1000);
  b.setUserIdleSeconds(0);
  f = b.tick(0);
  CHECK(f.anim == Anim::Idle);
  CHECK(f.event == PetEvent::Woke);
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

TEST(brain_walk_stays_in_bounds_and_uses_all_actions) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink] = {2, 3, 4};  // three random idle actions
  Brain b = makeBrain(1, c);
  Frame f = b.tick(0);
  bool sawWalk = false, sawLeft = false, sawRight = false;
  bool sawVariant[3] = {false, false, false};
  long long t = 0;
  while (t < 900000) {
    int dt = f.nextTickMs > 0 ? f.nextTickMs : 500;
    f = b.tick(dt);
    t += dt;
    CHECK(f.x >= 0 && f.x <= 768);
    CHECK_EQ(f.y, 552);
    if (f.anim == Anim::Walk) { sawWalk = true; if (f.facingLeft) sawLeft = true; else sawRight = true; }
    if (f.anim == Anim::Blink) {
      CHECK(f.variant >= 0 && f.variant < 3);
      CHECK(f.index < 2 + f.variant);
      sawVariant[f.variant] = true;
    }
  }
  CHECK(sawWalk);
  CHECK(sawLeft && sawRight);
  CHECK(sawVariant[0] && sawVariant[1] && sawVariant[2]);
}

TEST(brain_stationary_never_walks) {
  BrainConfig c = testCfg();
  c.canWalk = false;
  Brain b = makeBrain(9, c);
  Frame f = b.tick(0);
  bool sawAction = false;
  for (int i = 0; i < 5000; ++i) {
    f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
    CHECK(f.anim != Anim::Walk);
    CHECK_EQ(f.x, 100);
    sawAction |= f.anim == Anim::Blink;
  }
  CHECK(sawAction);
}

TEST(brain_without_actions_only_walks) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::Blink].clear();
  Brain b = makeBrain(3, c);
  Frame f = b.tick(0);
  for (int i = 0; i < 3000; ++i) {
    f = b.tick(f.nextTickMs ? f.nextTickMs : 500);
    CHECK(f.anim != Anim::Blink);
  }
}

TEST(brain_missing_required_states_fall_back_to_idle_frames) {
  BrainConfig c;
  c.variants[(int)Anim::Idle] = {7};
  Brain b(c, 1);
  b.setGround(0, 800, 600);
  b.tick(0);
  b.press(10, 10);
  b.move(200, 100);
  Frame f = b.tick(0);
  CHECK(f.anim == Anim::Drag);
  for (int i = 0; i < 6; ++i) f = b.tick(250);
  CHECK_EQ(f.index, 6);  // uses idle's 7 frames
}

TEST(brain_no_dirty_without_change) {
  Brain b = makeBrain();
  b.tick(0);
  Frame f = b.tick(0);
  CHECK(!f.dirty);
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
    if (fa.x != fb.x || fa.anim != fb.anim || fa.variant != fb.variant) { CHECK(false); break; }
  }
}

// ---------- voice ----------
static const char* kTsv =
    "skin\tkey\tindex\toath\tzh\tjp\n"
    "02\tlogin\t1\t0\t欢迎回来\tおかえり\n"
    "02\tlogin\t2\t1\t誓约的欢迎\t\n"
    "02\ttouch\t1\t0\t请不要乱碰\t\n"
    "02\ttouch2\t1\t0\t特殊触摸\t\n"
    "01\ttouch\t1\t0\t改造的触摸\t\n"
    "01\tmain\t1\t0\t主界面一\t\n"
    "01\tmain\t2\t0\t主界面二\t\n"
    "02\theadtouch\t1\t0\t摸头\t\n"
    "bad row without enough columns\n";

TEST(voice_parse_tsv) {
  VoiceBank vb = VoiceBank::parseTsv(kTsv);
  CHECK_EQ(vb.size(), (size_t)8);
}

TEST(voice_pick_prefers_skin_then_fallback) {
  VoiceBank vb = VoiceBank::parseTsv(kTsv);
  std::mt19937 rng(1);
  CHECK(vb.pick("01", "02", {"touch"}, false, rng) == "改造的触摸");
  CHECK(vb.pick("09", "02", {"touch"}, false, rng) == "请不要乱碰");   // unknown skin -> fallback
  CHECK(vb.pick("01", "02", {"login"}, false, rng) == "欢迎回来");     // skin lacks key -> fallback
  CHECK(vb.pick("01", "02", {"nokey", "headtouch"}, false, rng) == "摸头");  // next key
  CHECK(vb.pick("01", "02", {"nokey"}, false, rng) == "");
}

TEST(voice_oath_lines_only_when_allowed) {
  VoiceBank vb = VoiceBank::parseTsv(kTsv);
  std::mt19937 rng(3);
  for (int i = 0; i < 20; ++i) CHECK(vb.pick("02", "02", {"login"}, false, rng) == "欢迎回来");
  bool sawOath = false;
  for (int i = 0; i < 40; ++i) sawOath |= vb.pick("02", "02", {"login"}, true, rng) == "誓约的欢迎";
  CHECK(sawOath);
}

TEST(voice_avoids_immediate_repeat) {
  VoiceBank vb = VoiceBank::parseTsv(kTsv);
  std::mt19937 rng(5);
  std::string prev = vb.pick("01", "02", {"main"}, false, rng);
  for (int i = 0; i < 30; ++i) {
    std::string cur = vb.pick("01", "02", {"main"}, false, rng);
    CHECK(cur != prev);
    prev = cur;
  }
}

TEST(voice_samples_prefer_skin_and_are_distinct) {
  VoiceBank vb = VoiceBank::parseTsv(kTsv);
  std::mt19937 rng(2);
  std::vector<std::string> s = vb.samples("01", "02", 3, false, rng);
  CHECK_EQ(s.size(), (size_t)3);
  CHECK(std::find(s.begin(), s.end(), "主界面一") != s.end());
  CHECK(std::find(s.begin(), s.end(), "誓约的欢迎") == s.end());
  std::vector<std::string> all = vb.samples("01", "02", 50, true, rng);
  CHECK(std::find(all.begin(), all.end(), "誓约的欢迎") != all.end());
  CHECK(std::find(all.begin(), all.end(), "摸头") == all.end());  // headtouch is not an everyday scene
}

TEST(voice_plain_lines_match_any_scene) {
  VoiceBank vb = VoiceBank::parsePlain("# comment\n  第一句  \n\n第二句\n");
  CHECK_EQ(vb.size(), (size_t)2);
  std::mt19937 rng(1);
  std::string s = vb.pick("05", "01", {"headtouch"}, false, rng);
  CHECK(s == "第一句" || s == "第二句");
}

TEST(voice_scene_keys) {
  std::mt19937 rng(1);
  CHECK(sceneKeys(Scene::Login, rng)[0] == "login");
  CHECK(sceneKeys(Scene::TapHead, rng)[0] == "headtouch");
  CHECK(sceneKeys(Scene::Home, rng)[0] == "home");
  CHECK(sceneKeys(Scene::TapBody, rng)[0] == "touch");
  CHECK(sceneKeys(Scene::TapSpecial, rng)[0] == "touch2");
  CHECK(sceneKeys(Scene::TapSpecial, rng)[1] == "touch");
}

TEST(voice_bubble_duration_counts_characters) {
  CHECK_EQ(bubbleDurationMs("短", 3000), 3000);
  CHECK_EQ(bubbleDurationMs(std::string(60, 'a'), 3000), 1200 + 110 * 60);
  // 20 CJK characters are 60 bytes but 20 code points
  std::string cjk;
  for (int i = 0; i < 20; ++i) cjk += "字";
  CHECK_EQ(bubbleDurationMs(cjk, 1000), 1200 + 110 * 20);
  CHECK_EQ(bubbleDurationMs(std::string(500, 'a'), 3000), 10000);
}

TEST(reply_hold_lets_lines_through_without_a_reply) {
  ReplyHold h;
  CHECK(h.routineMayShow(0));
  CHECK(h.routineMayShow(123456));
}

TEST(reply_hold_keeps_lines_quiet_while_the_reply_is_up) {
  ReplyHold h;
  h.replyShown(1000, 5000);
  CHECK(!h.routineMayShow(1000));
  CHECK(!h.routineMayShow(5999));
  CHECK(h.routineMayShow(6000));  // the bubble has timed out
}

TEST(reply_hold_follows_the_latest_reply) {
  ReplyHold h;
  h.replyShown(1000, 5000);
  h.replyShown(3000, 5000);  // a second reply restarts the bubble's time
  CHECK(!h.routineMayShow(7999));
  CHECK(h.routineMayShow(8000));
  ReplyHold s;
  s.replyShown(1000, 10000);
  s.replyShown(2000, 3000);  // a short one (an error note) replaces the long one and its time
  CHECK(!s.routineMayShow(4999));
  CHECK(s.routineMayShow(5000));
}

TEST(skin_outfit_name_strips_kind) {
  CHECK(skinOutfitName("L2D-03-彩云之玫瑰") == "彩云之玫瑰");
  CHECK(skinOutfitName("动态-01-改造") == "改造");
  CHECK(skinOutfitName("Q版-02-默认女仆装") == "默认女仆装");
  CHECK(skinOutfitName("05-优雅而高贵的从者.png") == "优雅而高贵的从者");
}

TEST(skin_number_from_names) {
  CHECK(skinNumber("01-改造.png") == "01");
  CHECK(skinNumber("Q版-07-White Seaside Melody") == "07");
  CHECK(skinNumber("belfast.png") == "");
  CHECK(skinNumber("12") == "12");
}

// ---------- chat ----------
TEST(chat_config_and_endpoint) {
  ChatConfig c = ChatConfig::parse("[chat]\nbase_url=https://api.deepseek.com/v1/\napi_key=sk-x\nmodel=deepseek-chat\nmax_turns=3\n");
  CHECK(c.ready());
  CHECK(c.endpoint() == "https://api.deepseek.com/v1/chat/completions");
  CHECK(c.model == "deepseek-chat");
  CHECK_EQ(c.maxTurns, 3);
  CHECK_EQ(ChatConfig::parse("[chat]\n").maxTurns, 3);  // the default keeps only a few lines
  CHECK(!ChatConfig::parse("[chat]\napi_key=\n").ready());
  CHECK(!ChatConfig::parse(chatIniTemplate()).ready());  // template ships without a key
}

TEST(chat_template_is_claude) {
  ChatConfig t = ChatConfig::parse(std::string(chatIniTemplate()) + "api_key=sk-ant-x\n");
  CHECK(t.ready() && t.anthropicApi);
  CHECK(t.endpoint() == "https://api.anthropic.com/v1/messages");
  CHECK(t.model == "claude-opus-5-5" && t.reasoningEffort == "low");
}

TEST(chat_modelhub_responses_config) {
  ChatConfig c = ChatConfig::parse(
      "[chat]\nbase_url=https://aidp.example.net/api/modelhub/online/\napi_key=k+1/2\nmodel=gpt-6-astra\n"
      "api=responses\nauth=ak\nreasoning_effort=medium\n");
  CHECK(c.ready() && c.responsesApi && c.keyInQuery);
  CHECK(c.reasoningEffort == "medium");
  CHECK(c.endpoint() == "https://aidp.example.net/api/modelhub/online/responses");
  CHECK(c.requestUrl() == "https://aidp.example.net/api/modelhub/online/responses?ak=k%2B1%2F2");
  CHECK(c.headers().empty());  // the key is never sent twice
  // a base copied with a dialect suffix is taken as the channel root
  CHECK(ChatConfig::parse("[chat]\nbase_url=https://h/x/responses\napi=responses\n").endpoint() == "https://h/x/responses");
  CHECK(ChatConfig::parse("[chat]\nbase_url=https://h/x/v2/crawl\napi=responses\n").endpoint() == "https://h/x/responses");
  ChatConfig d = ChatConfig::parse("[chat]\nbase_url=https://api.deepseek.com/v1\napi_key=sk-x\n");
  CHECK(!d.responsesApi && !d.keyInQuery && d.reasoningEffort.empty());
  CHECK(d.requestUrl() == "https://api.deepseek.com/v1/chat/completions");
  CHECK(d.headers() == (std::vector<std::pair<std::string, std::string>>{{"Authorization", "Bearer sk-x"}}));
}

TEST(chat_anthropic_config) {
  ChatConfig c = ChatConfig::parse("[chat]\napi=anthropic\napi_key=sk-ant-x\n");
  CHECK(c.ready() && c.anthropicApi && !c.responsesApi);
  CHECK(c.baseUrl == "https://api.anthropic.com");  // defaults for this API
  CHECK(c.model == "claude-opus-5-5");
  CHECK(c.reasoningEffort == "low");
  CHECK(c.endpoint() == "https://api.anthropic.com/v1/messages");
  CHECK(c.requestUrl() == c.endpoint());  // the key never goes into the address
  std::vector<std::pair<std::string, std::string>> want = {
      {"x-api-key", "sk-ant-x"},
      {"anthropic-version", "2026-10-21"},
      {"anthropic-beta", "server-side-fallback-2026-07-01"}};
  CHECK(c.headers() == want);
  // what the user sets wins; a base that already names the version or the path is not repeated
  ChatConfig d = ChatConfig::parse(
      "[chat]\napi=anthropic\napi_key=k\nmodel=claude-haiku-5-5\nreasoning_effort=medium\nbase_url=https://proxy.example/v1/\n");
  CHECK(d.model == "claude-haiku-5-5" && d.reasoningEffort == "medium");
  CHECK(d.endpoint() == "https://proxy.example/v1/messages");
  CHECK(ChatConfig::parse("[chat]\napi=anthropic\nbase_url=https://p/v1/messages\n").endpoint() == "https://p/v1/messages");
  CHECK(ChatConfig::parse("[chat]\napi=anthropic\nbase_url=https://p\n").endpoint() == "https://p/v1/messages");
}

TEST(chat_anthropic_request_body) {
  ChatConfig c = ChatConfig::parse("[chat]\napi=anthropic\napi_key=k\n");
  ChatSession s;
  s.reset("你是贝尔法斯特");
  s.accept("早", "早安，指挥官", 8);
  std::string b = s.requestBody(c, "红茶");
  CHECK(b.find("\"model\":\"claude-opus-5-5\"") != std::string::npos);
  CHECK(b.find("\"max_tokens\":16000") != std::string::npos);
  CHECK(b.find("\"system\":[{\"type\":\"text\",\"text\":\"你是贝尔法斯特\",\"cache_control\":{\"type\":\"ephemeral\"}}]") !=
        std::string::npos);  // the persona is the cached prefix
  CHECK(b.find("\"messages\":[{\"role\":\"user\",\"content\":\"早\"},{\"role\":\"assistant\",\"content\":\"早安，指挥官\"},"
               "{\"role\":\"user\",\"content\":\"红茶\"}]") != std::string::npos);
  CHECK(b.find("\"output_config\":{\"effort\":\"low\"}") != std::string::npos);
  CHECK(b.find("\"fallbacks\":\"default\"") != std::string::npos);
  CHECK(b.find("temperature") == std::string::npos);  // Opus 5.5 rejects sampling parameters
  CHECK(b.find("\"role\":\"system\"") == std::string::npos);  // the system prompt is top-level here
  ChatConfig d = ChatConfig::parse("[chat]\napi=anthropic\napi_key=k\nmax_tokens=2048\nfallbacks=0\n");
  CHECK_EQ(d.maxTokens, 2048);
  CHECK(!d.fallbacks && d.headers().size() == 2);  // no beta header once the user turned the fallback off
  b = s.requestBody(d, "红茶");
  CHECK(b.find("\"max_tokens\":2048") != std::string::npos && b.find("fallbacks") == std::string::npos);
}

TEST(chat_parse_anthropic_reply) {
  std::string r, e;
  const char* ok =
      "{\"id\":\"msg_1\",\"type\":\"message\",\"role\":\"assistant\",\"model\":\"claude-opus-5-5\",\"content\":["
      "{\"type\":\"thinking\",\"thinking\":\"\",\"signature\":\"abc\"},"
      "{\"type\":\"text\",\"text\":\"指挥官，\"},{\"type\":\"text\",\"text\":\"红茶来了。\"}],"
      "\"stop_reason\":\"end_turn\",\"usage\":{\"input_tokens\":10,\"output_tokens\":5}}";
  CHECK(parseChatReply(ok, &r, &e));
  CHECK(r == "指挥官，红茶来了。");
  CHECK(!parseChatReply("{\"type\":\"message\",\"content\":[],\"stop_reason\":\"refusal\","
                        "\"stop_details\":{\"type\":\"refusal\",\"category\":\"cyber\"}}", &r, &e));
  CHECK(e == "她没有回答这个问题");
  CHECK(!parseChatReply("{\"type\":\"message\",\"content\":[{\"type\":\"thinking\",\"thinking\":\"\"}],"
                        "\"stop_reason\":\"max_tokens\"}", &r, &e));
  CHECK(e == "回复不完整：max_tokens");
  CHECK(!parseChatReply("{\"type\":\"error\",\"error\":{\"type\":\"authentication_error\",\"message\":\"invalid x-api-key\"}}",
                        &r, &e));
  CHECK(e == "invalid x-api-key");
  // declined half-way: the partial text is not her answer
  CHECK(!parseChatReply("{\"type\":\"message\",\"content\":[{\"type\":\"text\",\"text\":\"指挥官，我想\"}],\"stop_reason\":\"refusal\"}",
                        &r, &e));
  CHECK(e == "她没有回答这个问题");
  // only whitespace is no reply (an empty assistant turn would make every later request fail)
  CHECK(!parseChatReply("{\"type\":\"message\",\"content\":[{\"type\":\"text\",\"text\":\"\\n\\n \"}],\"stop_reason\":\"end_turn\"}",
                        &r, &e));
  CHECK(e == "回复里没有文字");
}

TEST(chat_history_never_stores_empty_turns) {
  ChatSession s;
  s.reset("p");
  s.accept("你好", "", 8);
  s.accept("  ", "嗯", 8);
  CHECK_EQ(s.turns(), (size_t)0);
  s.accept("你好", "指挥官", 8);
  CHECK_EQ(s.turns(), (size_t)1);
}

TEST(chat_anthropic_fallback_only_where_supported) {
  for (const char* m : {"claude-opus-5-5", "claude-opus-5", "claude-fable-5-1", "claude-sonnet-5-5"}) {
    ChatConfig c = ChatConfig::parse(std::string("[chat]\napi=anthropic\napi_key=k\nmodel=") + m + "\n");
    ChatSession s;
    s.reset("p");
    CHECK(s.requestBody(c, "hi").find("\"fallbacks\":\"default\"") != std::string::npos);
    CHECK(c.headers().size() == 3);
  }
  // Haiku 5.5 has no server-side fallback, and older models do not know the parameter
  for (const char* m : {"claude-haiku-5-5", "claude-sonnet-4-6"}) {
    ChatConfig c = ChatConfig::parse(std::string("[chat]\napi=anthropic\napi_key=k\nmodel=") + m + "\n");
    ChatSession s;
    s.reset("p");
    CHECK(s.requestBody(c, "hi").find("fallbacks") == std::string::npos);
    std::vector<std::pair<std::string, std::string>> want = {{"x-api-key", "k"}, {"anthropic-version", "2026-10-21"}};
    CHECK(c.headers() == want);
  }
  // an empty reasoning_effort sends none (for models without effort)
  ChatConfig n = ChatConfig::parse("[chat]\napi=anthropic\napi_key=k\nreasoning_effort=\n");
  ChatSession s;
  s.reset("p");
  CHECK(n.reasoningEffort.empty() && s.requestBody(n, "hi").find("output_config") == std::string::npos);
}

TEST(chat_ini_wants_template) {
  CHECK(chatIniWantsTemplate(""));
  // the old OpenAI template nobody filled in: replaced, so the menu leads to Claude
  CHECK(chatIniWantsTemplate("[chat]\nbase_url=https://api.openai.com/v1\napi_key=\nmodel=gpt-4o-mini\n"));
  CHECK(!chatIniWantsTemplate(chatIniTemplate()));  // already the new one, key still to be filled in
  CHECK(!chatIniWantsTemplate("[chat]\nbase_url=https://api.deepseek.com/v1\napi_key=sk-x\n"));  // in use
  CHECK(!chatIniWantsTemplate("[chat]\napi=responses\nauth=ak\napi_key=\n"));  // set up by hand
}

TEST(chat_responses_request_body) {
  ChatConfig c;
  c.model = "gpt-6-astra";
  c.responsesApi = true;
  c.reasoningEffort = "medium";
  ChatSession s;
  s.reset("你是贝尔法斯特");
  s.accept("早", "早安，指挥官", 8);
  std::string b = s.requestBody(c, "红茶");
  CHECK(b.find("\"model\":\"gpt-6-astra\"") != std::string::npos);
  CHECK(b.find("\"input\":[{\"role\":\"system\",\"content\":[{\"type\":\"input_text\",\"text\":\"你是贝尔法斯特\"}]}") !=
        std::string::npos);
  CHECK(b.find("{\"role\":\"assistant\",\"content\":[{\"type\":\"output_text\",\"text\":\"早安，指挥官\"}]}") !=
        std::string::npos);
  CHECK(b.find("{\"role\":\"user\",\"content\":[{\"type\":\"input_text\",\"text\":\"红茶\"}]}]") != std::string::npos);
  CHECK(b.find("\"reasoning\":{\"effort\":\"medium\"}") != std::string::npos);
  CHECK(b.find("temperature") == std::string::npos && b.find("messages") == std::string::npos);
  CHECK(b.find("\"max_output_tokens\":" + std::to_string(80 * 3 + 64 + 4096)) != std::string::npos);
}

TEST(chat_parse_responses_reply) {
  std::string r, e;
  const char* resp =
      "{\"id\":\"resp_1\",\"object\":\"response\",\"status\":\"completed\",\"output\":["
      "{\"type\":\"reasoning\",\"id\":\"rs_1\",\"summary\":[],\"encrypted_content\":\"xx\"},"
      "{\"type\":\"message\",\"role\":\"assistant\",\"content\":["
      "{\"type\":\"output_text\",\"text\":\"指挥官，\",\"annotations\":[]},"
      "{\"type\":\"output_text\",\"text\":\"红茶来了。\"}]}],\"usage\":{\"output_tokens\":9}}";
  CHECK(parseChatReply(resp, &r, &e));
  CHECK(r == "指挥官，红茶来了。");
  CHECK(parseChatReply("{\"output_text\":\"好的\",\"output\":[]}", &r, &e));
  CHECK(r == "好的");
  CHECK(!parseChatReply("{\"status\":\"incomplete\",\"incomplete_details\":{\"reason\":\"max_output_tokens\"},"
                        "\"output\":[{\"type\":\"reasoning\",\"summary\":[]}]}", &r, &e));
  CHECK(e == "回复不完整：max_output_tokens");
  CHECK(!parseChatReply("{\"error\":{\"code\":\"-1016\",\"message\":\"invalid target region\"}}", &r, &e));
  CHECK(e == "invalid target region");
}

TEST(chat_json_quote) {
  CHECK(jsonQuote("a\"b\\c\n") == "\"a\\\"b\\\\c\\n\"");
  CHECK(jsonQuote("指挥官") == "\"指挥官\"");
  CHECK(jsonQuote(std::string(1, '\x01')) == "\"\\u0001\"");
}

TEST(chat_request_body_keeps_bounded_history) {
  ChatConfig c;
  c.model = "m";
  ChatSession s;
  s.reset("你是贝尔法斯特");
  std::string b = s.requestBody(c, "你好");
  CHECK(b.find("\"model\":\"m\"") != std::string::npos);
  CHECK(b.find("{\"role\":\"system\",\"content\":\"你是贝尔法斯特\"}") != std::string::npos);
  CHECK(b.find("{\"role\":\"user\",\"content\":\"你好\"}]}") != std::string::npos);
  for (int i = 0; i < 5; ++i) s.accept("q" + std::to_string(i), "a" + std::to_string(i), 2);
  CHECK_EQ(s.turns(), (size_t)2);
  b = s.requestBody(c, "next");
  CHECK(b.find("\"q2\"") == std::string::npos);
  CHECK(b.find("\"q3\"") != std::string::npos && b.find("\"a4\"") != std::string::npos);
  CHECK(b.find("\"q3\"") < b.find("\"a3\"") && b.find("\"a3\"") < b.find("\"q4\""));
}

TEST(chat_parse_reply) {
  std::string r, e;
  const char* ok = "{\"id\":\"x\",\"object\":\"chat.completion\",\"choices\":[{\"index\":0,"
                   "\"message\":{\"role\":\"assistant\",\"content\":\"\\u6307\\u6325\\u5b98\\uff0c\\n红茶\\\"好\\\"了 \\ud83d\\ude0a\"},"
                   "\"finish_reason\":\"stop\"}],\"usage\":{\"total_tokens\":5}}";
  CHECK(parseChatReply(ok, &r, &e));
  CHECK(r == "指挥官，\n红茶\"好\"了 \xF0\x9F\x98\x8A");
  CHECK(!parseChatReply("{\"error\":{\"message\":\"Invalid API key\",\"type\":\"auth\"}}", &r, &e));
  CHECK(e == "Invalid API key");
  CHECK(!parseChatReply("", &r, &e));
  CHECK(!parseChatReply("<html>502</html>", &r, &e));
}

TEST(chat_clip_and_prompt) {
  CHECK(clipReply("  你好  \n", 10) == "你好");
  CHECK(clipReply("一二三四五", 3) == "一二三…");
  CHECK(clipReply("一二三", 3) == "一二三");
  std::string p = chatSystemPrompt("贝尔法斯特", "改造", {"欢迎回来"}, 60);
  CHECK(p.find("贝尔法斯特") != std::string::npos && p.find("改造") != std::string::npos);
  CHECK(p.find("60") != std::string::npos && p.find("- 欢迎回来") != std::string::npos);
}

TEST(chat_key_from_credentials_and_fallbacks) {
  CHECK(ChatConfig::keyFromCredentials("[default]\nanthropic_api_key = sk-ant-abc\n") == "sk-ant-abc");
  CHECK(ChatConfig::keyFromCredentials("; comment\n[default]\napi_key=sk-2\n") == "sk-2");
  CHECK(ChatConfig::keyFromCredentials("anthropic_api_key=sk-3\n") == "sk-3");  // no section header
  CHECK(ChatConfig::keyFromCredentials("").empty());
  ChatConfig c = ChatConfig::parse("[chat]\napi=anthropic\napi_key=\n");
  CHECK(!c.ready());
  c.useKeyIfEmpty("");
  CHECK(!c.ready());
  c.useKeyIfEmpty("sk-file");
  CHECK(c.ready() && c.apiKey == "sk-file");
  c.useKeyIfEmpty("sk-env");  // the first source that has a key wins
  CHECK(c.apiKey == "sk-file");
}

TEST(chat_persona_prompt_groups_all_outfits) {
  VoiceBank vb = VoiceBank::parseTsv(
      "skin\tkey\tindex\toath\tzh\tjp\n"
      "02\tprofile\t1\t0\t贵安，我是贝尔法斯特\t\n"
      "02\tlogin\t1\t0\t欢迎回来，指挥官\t\n"
      "02\tlogin\t2\t1\t欢迎回来，我的指挥官\t\n"
      "02\tmain\t1\t0\t要来一杯红茶吗\t\n"
      "02\tfeeling5\t1\t0\t守护在您身边是我的幸福\t\n"
      "02\tcouple_encourage\t1\t0\t（爱丁堡）姐姐，一起加油吧\t\n"
      "03\tdesc\t1\t0\t这一身就这么让您吃惊吗\t\n"
      "03\thome\t1\t0\t请用，这是糕点\t\n"
      "10\tpropose\t1\t0\t这下麻烦了呢\t\n");
  Persona p;
  p.shipName = "贝尔法斯特";
  p.outfit = "彩云之玫瑰";
  p.voiceSkin = "03";
  p.skinNames = {{"02", "默认女仆装"}, {"03", "彩云之玫瑰"}, {"10", "克拉达的誓约"}};
  p.voices = &vb;
  p.maxReplyChars = 60;
  std::string s = chatPersonaPrompt(p);
  CHECK(s.find("舰船少女贝尔法斯特") != std::string::npos);
  CHECK(s.find("【角色自述】\n贵安，我是贝尔法斯特") != std::string::npos);
  size_t cur = s.find("【衣装「彩云之玫瑰」】（你现在穿着这套）"), def = s.find("【衣装「默认女仆装」】");
  CHECK(cur != std::string::npos && def != std::string::npos && cur < def);  // the current outfit comes first
  CHECK(s.find("- 衣装介绍：这一身就这么让您吃惊吗") != std::string::npos);
  CHECK(s.find("- 回港：请用，这是糕点") != std::string::npos);
  CHECK(s.find("- 登录：欢迎回来，指挥官") != std::string::npos);
  CHECK(s.find("- 主界面闲聊：要来一杯红茶吗") != std::string::npos);
  CHECK(s.find("- 好感度·爱：守护在您身边是我的幸福") != std::string::npos);
  CHECK(s.find("- 鼓励同伴：（爱丁堡）姐姐，一起加油吧") != std::string::npos);
  CHECK(s.find("【衣装「克拉达的誓约」】\n- 誓约：这下麻烦了呢") != std::string::npos);
  CHECK(s.find("欢迎回来，我的指挥官") == std::string::npos);  // post-oath line stays out before the oath
  CHECK(s.find("贵安，我是贝尔法斯特") == s.rfind("贵安，我是贝尔法斯特"));  // profile is not repeated under the outfit
  CHECK(s.find("你现在穿着「彩云之玫瑰」。") != std::string::npos && s.find("缔结了誓约") == std::string::npos);
  CHECK(s.find("不超过60字") != std::string::npos);
  CHECK(s.find("不要原样背诵") != std::string::npos);
  CHECK(s.find("只回应指挥官刚说的这一句") != std::string::npos);  // earlier turns are context, not a topic
  p.oath = true;
  p.voiceSkin = "10";
  p.outfit = "克拉达的誓约";
  s = chatPersonaPrompt(p);
  CHECK(s.find("- 登录：欢迎回来，我的指挥官") != std::string::npos);
  CHECK(s.find("你现在穿着「克拉达的誓约」。你已经和指挥官缔结了誓约。") != std::string::npos);
  CHECK(s.find("【衣装「克拉达的誓约」】（你现在穿着这套）") < s.find("【衣装「默认女仆装」】"));
  // generic lines (assets/lines.txt) have no outfit and no scene
  VoiceBank plain = VoiceBank::parsePlain("指挥官，有什么吩咐吗？\n");
  Persona q;
  q.shipName = "信浓";
  q.outfit = "默认";
  q.voices = &plain;
  s = chatPersonaPrompt(q);
  CHECK(s.find("【台词】\n- 指挥官，有什么吩咐吗？") != std::string::npos);
}

TEST(chat_persona_skin_names_follow_voice_tables) {
  Persona p;
  Ini ini = Ini::parse("[ship]\nname=贝尔法斯特\nvoice_11=01\n");
  p.addSkinName("01-改造.png", ini);
  p.addSkinName("Q版-01-改造", ini);
  p.addSkinName("11-改造 无舰装.png", ini);  // borrows table 01: already named
  p.addSkinName("02-默认女仆装.png", ini);
  p.addSkinName("Q版-12-小贝法", ini);
  p.addSkinName("belfast.png", ini);  // no number: ignored
  CHECK_EQ(p.skinNames.size(), (size_t)3);
  CHECK(p.skinNames[0].first == "01" && p.skinNames[0].second == "改造");
  CHECK(p.skinNames[1].first == "02" && p.skinNames[1].second == "默认女仆装");
  CHECK(p.skinNames[2].first == "12" && p.skinNames[2].second == "小贝法");
}

TEST(chat_study_prompt_and_cache) {
  VoiceBank vb = VoiceBank::parseTsv(
      "skin\tkey\tindex\toath\tzh\tjp\n"
      "02\tprofile\t1\t0\t贵安，我是贝尔法斯特\t\n"
      "02\tlogin\t1\t0\t欢迎回来，指挥官\t\n"
      "02\tlogin\t2\t1\t欢迎回来，我的指挥官\t\n"
      "03\tdesc\t1\t0\t这一身就这么让您吃惊吗\t\n"
      "03\tmain\t1\t0\t请用，这是糕点\t\n");
  Persona p;
  p.shipName = "贝尔法斯特";
  p.outfit = "彩云之玫瑰";
  p.voiceSkin = "03";
  p.skinNames = {{"02", "默认女仆装"}, {"03", "彩云之玫瑰"}};
  p.voices = &vb;
  std::string u = chatStudyUserPrompt(p);
  CHECK(u.find("贝尔法斯特在游戏里的全部官方台词") != std::string::npos);
  CHECK(u.find("【角色自述】\n贵安，我是贝尔法斯特") != std::string::npos);
  CHECK(u.find("【衣装「默认女仆装」】") < u.find("【衣装「彩云之玫瑰」】"));  // listed in order, not the outfit she wears first
  CHECK(u.find("你现在穿着") == std::string::npos);  // the study does not depend on the outfit
  CHECK(u.find("- 登录（誓约后）：欢迎回来，我的指挥官") != std::string::npos);  // post-oath lines are in, labelled
  CHECK(u.find("- 衣装介绍：这一身就这么让您吃惊吗") != std::string::npos);
  CHECK(u.find("不是一套衣装一个性格") != std::string::npos && u.find("600字") != std::string::npos);
  CHECK(std::string(chatStudySystemPrompt()).find("角色设定师") != std::string::npos);
  std::string key = personaStudyKey(p);
  CHECK_EQ(key.size(), (size_t)16);
  Persona q = p;
  q.outfit = "默认女仆装";
  q.voiceSkin = "02";
  q.oath = true;
  CHECK(personaStudyKey(q) == key);  // the outfit and the oath do not change what was studied
  VoiceBank vb2 = VoiceBank::parseTsv("skin\tkey\tindex\toath\tzh\tjp\n02\tlogin\t1\t0\t欢迎回来，指挥官！\t\n");
  q.voices = &vb2;
  CHECK(personaStudyKey(q) != key);  // a changed line does
  // the cache file round-trips and refuses another key or an empty profile
  std::string text = personaCacheText(key, "1. 性格：温柔\n2. 说话方式：敬语");
  std::string out;
  CHECK(personaCacheRead(text, key, &out) && out == "1. 性格：温柔\n2. 说话方式：敬语");
  CHECK(!personaCacheRead(text, "0000000000000000", &out));
  CHECK(!personaCacheRead("", key, &out));
  CHECK(!personaCacheRead(personaCacheText(key, "  \n"), key, &out));
}

TEST(chat_persona_prompt_with_profile) {
  VoiceBank vb = VoiceBank::parseTsv(
      "skin\tkey\tindex\toath\tzh\tjp\n"
      "02\tlogin\t1\t0\t欢迎回来，指挥官\t\n"
      "02\tlogin\t2\t1\t欢迎回来，我的指挥官\t\n"
      "02\tmain\t1\t0\t要来一杯红茶吗\t\n"
      "02\ttouch\t1\t0\t贸然触碰女士可不是绅士作为\t\n"
      "02\thome\t1\t0\t茶点已经准备好了\t\n"
      "02\tdetail\t1\t0\t指挥官，有什么吩咐吗\t\n"
      "02\tmail\t1\t0\t有新邮件来了\t\n"
      "03\tdesc\t1\t0\t这一身就这么让您吃惊吗\t\n"
      "03\tlogin\t1\t0\t旗袍登录\t\n"
      "03\thome\t1\t0\t请用，这是糕点\t\n");
  Persona p;
  p.shipName = "贝尔法斯特";
  p.outfit = "彩云之玫瑰";
  p.voiceSkin = "03";
  p.skinNames = {{"02", "默认女仆装"}, {"03", "彩云之玫瑰"}};
  p.voices = &vb;
  p.maxReplyChars = 60;
  std::string s = chatPersonaPrompt(p, "1. 性格：温柔而有主见");
  CHECK(s.find("【角色档案】") != std::string::npos && s.find("1. 性格：温柔而有主见") != std::string::npos);
  CHECK(s.find("你现在穿着「彩云之玫瑰」。这套衣装的介绍：这一身就这么让您吃惊吗") != std::string::npos);
  CHECK(s.find("【语气参考】") != std::string::npos);
  CHECK(s.find("- 登录：旗袍登录") < s.find("- 登录：欢迎回来，指挥官"));  // the outfit she wears comes first
  CHECK(s.find("- 回港：请用，这是糕点") != std::string::npos && s.find("- 触摸：贸然触碰") != std::string::npos);
  CHECK(s.find("有新邮件") == std::string::npos);  // a reminder of her voice, not every line
  CHECK(s.find("【衣装「") == std::string::npos);  // no grouped dump once there is a profile
  CHECK(s.find("欢迎回来，我的指挥官") == std::string::npos);  // post-oath line stays out before the oath
  CHECK(s.find("按角色档案里的性格") != std::string::npos && s.find("不超过60字") != std::string::npos);
  // without a profile her lines stand in for it, grouped, the outfit she wears first
  std::string t = chatPersonaPrompt(p);
  CHECK(t.find("【角色档案】") == std::string::npos);
  CHECK(t.find("【衣装「彩云之玫瑰」】（你现在穿着这套）") < t.find("【衣装「默认女仆装」】"));
  CHECK(t.find("- 有新邮件：有新邮件来了") != std::string::npos);
  CHECK(t.find("这套衣装的介绍") == std::string::npos);
}

// ---------- yield ----------
TEST(yield_cells_floor_and_center) {
  CHECK(cellAt(0, 0) == (Cell{0, 0}));
  CHECK(cellAt(15, 31) == (Cell{0, 1}));
  CHECK(cellAt(-1, -16) == (Cell{-1, -1}));
  CHECK(cellAt(-17, 16) == (Cell{-2, 1}));
  int x, y;
  cellCenter({-1, 2}, &x, &y);
  CHECK_EQ(x, -8);
  CHECK_EQ(y, 40);
}

TEST(yield_cache_expiry_and_capacity) {
  CellCache c(60000, 3);
  CHECK(c.get({1, 1}, 0) == CellState::Unknown);
  c.put({1, 1}, true, 1000);
  c.put({2, 1}, false, 2000);
  CHECK(c.get({1, 1}, 60999) == CellState::Clickable);
  CHECK(c.get({1, 1}, 61000) == CellState::Unknown);  // one minute later
  CHECK(c.get({2, 1}, 3000) == CellState::Plain);
  c.put({3, 1}, false, 3000);
  c.put({4, 1}, false, 4000);  // full: the oldest ({1, 1}) goes
  CHECK_EQ(c.size(), (size_t)3);
  CHECK(c.get({1, 1}, 4000) == CellState::Unknown);
  CHECK(c.get({4, 1}, 4000) == CellState::Plain);
  c.clear();
  CHECK_EQ(c.size(), (size_t)0);
}

TEST(yield_cache_stale_cells_in_order) {
  CellCache c;
  c.put({0, 0}, false, 0);
  c.put({1, 0}, true, 0);
  std::vector<Cell> want = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
  std::vector<Cell> s = c.stale(want, 1000, 10);
  CHECK_EQ(s.size(), (size_t)2);
  CHECK(s[0] == (Cell{2, 0}) && s[1] == (Cell{3, 0}));
  CHECK_EQ(c.stale(want, 1000, 1).size(), (size_t)1);
  CHECK_EQ(c.stale(want, 70000, 10).size(), (size_t)4);  // all expired
}

TEST(yield_footprint_uses_opaque_centers) {
  // a 40x20 window at (100, 50), opaque only left of x = 120
  auto opaque = [](int x, int) { return x < 120; };
  std::vector<Cell> f = footprint(100, 50, 140, 70, opaque);
  CHECK_EQ(f.size(), (size_t)1);  // centers x 104/120/136, y 56 (72 is outside)
  CHECK(f[0] == (Cell{6, 3}));
  CHECK(footprint(0, 0, 0, 10, opaque).empty());
}

TEST(yield_window_signature_changes) {
  std::vector<WindowInfo> a = {{7, 0, 0, 100, 100}, {9, 10, 10, 50, 50}};
  std::vector<WindowInfo> b = a;
  CHECK(windowSignature(a) == windowSignature(b));
  b[1].left = 11;  // moved
  CHECK(windowSignature(a) != windowSignature(b));
  std::vector<WindowInfo> c = {a[1], a[0]};  // stacking order swapped
  CHECK(windowSignature(a) != windowSignature(c));
  CHECK(windowSignature({}) != windowSignature({a[0]}));
}

TEST(yield_probe_queue_hover_first_one_at_a_time) {
  ProbeQueue q;
  ProbeJob j;
  CHECK(!q.next(&j));
  q.requestScan({{0, 0}, {1, 0}});
  q.requestHover({5, 5}, 85, 90);
  CHECK(q.next(&j));
  CHECK(j.hover && j.cell == (Cell{5, 5}) && j.x == 85 && j.y == 90);
  CHECK(!q.next(&j));  // busy
  q.done();
  CHECK(q.next(&j));
  CHECK(!j.hover && j.cell == (Cell{0, 0}) && j.x == 8 && j.y == 8);  // scans probe the cell center
  CHECK(q.scanning());
  q.requestScan({{9, 9}});  // ignored: a scan is still going
  q.done();
  CHECK(q.next(&j));
  CHECK(j.cell == (Cell{1, 0}));
  q.done();
  CHECK(!q.scanning());
  CHECK(!q.next(&j));
  q.requestScan({{9, 9}});
  CHECK(q.next(&j) && j.cell == (Cell{9, 9}));
}

TEST(yield_hover_gate_once_per_cell_until_expiry) {
  HoverProbeGate g(60000);
  CHECK(g.due({1, 1}, 0));
  g.mark({1, 1}, 1000);
  CHECK(!g.due({1, 1}, 2000));
  CHECK(g.due({2, 1}, 2000));
  CHECK(g.due({1, 1}, 61000));  // its cache entry has expired: check again
  g.reset();                     // the cache was cleared: check again
  CHECK(g.due({1, 1}, 3000));
  g.retryAfter({3, 1}, 5000, 1000);  // could not ask: try again in a second, not at once
  CHECK(!g.due({3, 1}, 5500));
  CHECK(g.due({3, 1}, 6000));
}

TEST(yield_probe_queue_same_hover_cell_once) {
  ProbeQueue q;
  ProbeJob j;
  q.requestHover({5, 5}, 85, 90);
  CHECK(q.next(&j));
  q.requestHover({5, 5}, 86, 91);  // that cell is being checked right now: nothing new
  q.done();
  CHECK(!q.next(&j));
  q.requestHover({6, 5}, 100, 90);
  q.requestHover({7, 5}, 120, 90);  // the newer cell replaces the older request
  CHECK(q.next(&j) && j.cell == (Cell{7, 5}));
  q.done();
  CHECK(!q.next(&j));
}

TEST(yield_probe_queue_clear_drops_everything_pending) {
  ProbeQueue q;
  ProbeJob j;
  q.requestScan({{0, 0}, {1, 0}});
  q.requestHover({5, 5}, 85, 90);
  q.clear();
  CHECK(!q.next(&j));
  CHECK(!q.scanning());
}

TEST(yield_controller_decisions) {
  YieldController y(200);
  YieldInput in;
  in.overPet = true;
  in.cell = CellState::Clickable;
  CHECK(y.update(in, 0));  // over a button: step aside
  in.cell = CellState::Plain;
  CHECK(y.update(in, 100));   // still inside the restore delay
  CHECK(!y.update(in, 200));  // back after 200 ms
  in.cell = CellState::Unknown;
  CHECK(!y.update(in, 300));  // not knowing is no reason
  in.cell = CellState::Clickable;
  CHECK(y.update(in, 400));
  in.modifier = true;
  CHECK(!y.update(in, 410));  // Option / Alt: back at once
  in.modifier = false;
  in.pressed = true;
  CHECK(!y.update(in, 420));  // pressing or dragging her: never
  in.pressed = false;
  in.clickThrough = true;
  CHECK(!y.update(in, 430));
  in.clickThrough = false;
  in.enabled = false;
  CHECK(!y.update(in, 440));
  in.enabled = true;
  in.visible = false;
  CHECK(!y.update(in, 450));
  in.visible = true;
  CHECK(y.update(in, 460));
  in.overPet = false;
  CHECK(y.update(in, 500));   // cursor left her: the delay applies too
  CHECK(!y.update(in, 660));
}

static ElementFacts facts(ElementKind k, int w, int h, bool own = false) {
  ElementFacts e;
  e.kind = k;
  e.ownAction = own;
  e.hasSize = w > 0;
  e.w = w;
  e.h = h;
  return e;
}

TEST(yield_judge_element_sizes_measured_on_mac) {
  // Real controls (Chrome toolbar, Dock, Lark toolbar, Claude sidebar) are clickable by role.
  CHECK(judgeElement(facts(ElementKind::Control, 26, 26)) == Judgement::Clickable);
  CHECK(judgeElement(facts(ElementKind::Control, 38, 50)) == Judgement::Clickable);
  CHECK(judgeElement(facts(ElementKind::Control, 600, 32)) == Judgement::Clickable);  // a wide row button
  CHECK(judgeElement(facts(ElementKind::Control, 0, 0)) == Judgement::Clickable);     // size unknown
  CHECK(judgeElement(facts(ElementKind::Control, 700, 400)) == Judgement::Plain);     // an invisible full-pane overlay
  // Text inputs: composers and fields yes; a whole Terminal or editor no.
  CHECK(judgeElement(facts(ElementKind::Text, 492, 20)) == Judgement::Clickable);
  CHECK(judgeElement(facts(ElementKind::Text, 980, 250)) == Judgement::Clickable);
  CHECK(judgeElement(facts(ElementKind::Text, 1511, 797)) == Judgement::Plain);
  CHECK(judgeElement(facts(ElementKind::Text, 0, 0)) == Judgement::Plain);
  // A group with its own press action: small click targets yes (Claude's 496x24 composer row,
  // a 212x20 sidebar label); the 614x731 chat feed with a click handler is not a control, look higher.
  CHECK(judgeElement(facts(ElementKind::Other, 496, 24, true)) == Judgement::Clickable);
  CHECK(judgeElement(facts(ElementKind::Other, 212, 20, true)) == Judgement::Clickable);
  CHECK(judgeElement(facts(ElementKind::Other, 600, 200, true)) == Judgement::Clickable);  // the limits
  CHECK(judgeElement(facts(ElementKind::Other, 614, 731, true)) == Judgement::Climb);
  CHECK(judgeElement(facts(ElementKind::Other, 1500, 120, true)) == Judgement::Climb);
  CHECK(judgeElement(facts(ElementKind::Other, 300, 220, true)) == Judgement::Climb);
  CHECK(judgeElement(facts(ElementKind::Other, 0, 0, true)) == Judgement::Climb);
  CHECK(judgeElement(facts(ElementKind::Other, 18, 18)) == Judgement::Climb);  // an icon image: its button decides
  // Windows, web areas, scroll areas, lists: nothing above them is a control.
  CHECK(judgeElement(facts(ElementKind::Container, 20, 20, true)) == Judgement::Plain);
}

TEST(yield_control_rects_cover_gaps_between_icons) {
  ControlRects r(60000, 3);
  const int64_t lark = 7, other = 8;
  CHECK(!r.within(1260, 874, 10, lark, 0));
  r.add({1252, 861, 1278, 887}, lark, 0);  // a 26x26 icon button in Lark's toolbar
  CHECK(r.within(1260, 874, 10, lark, 0));
  CHECK(r.within(1285, 874, 10, lark, 0));   // in the 12-pt gap to the next icon
  CHECK(!r.within(1289, 874, 10, lark, 0));  // more than 10 away
  CHECK(r.within(1260, 851, 10, lark, 0));   // margin above
  CHECK(!r.within(1260, 850, 10, lark, 0));
  CHECK(!r.within(1260, 874, 10, other, 0));  // another window covers it there now
  CHECK(r.within(1260, 874, 10, lark, 59999));
  CHECK(!r.within(1260, 874, 10, lark, 60000));  // expired after a minute
  // the same rectangle again refreshes it instead of adding a copy
  r.add({1252, 861, 1278, 887}, lark, 1000);
  r.add({1252, 861, 1278, 887}, lark, 2000);
  CHECK_EQ(r.size(), (size_t)1);
  CHECK(r.within(1260, 874, 10, lark, 61999));
  // full: the oldest goes
  r.add({0, 0, 10, 10}, lark, 3000);
  r.add({100, 0, 110, 10}, lark, 4000);
  r.add({200, 0, 210, 10}, lark, 5000);
  CHECK_EQ(r.size(), (size_t)3);
  CHECK(!r.within(1260, 874, 10, lark, 5000));
  CHECK(r.within(5, 5, 0, lark, 5000));
  // a live check that finds no control inside a rectangle drops it; a miss in the margin does not,
  // nor one in a window that covers part of it
  r.forgetAt(105, 15, lark);
  CHECK(r.within(105, 5, 0, lark, 5000));
  r.forgetAt(105, 5, other);
  CHECK(r.within(105, 5, 0, lark, 5000));
  r.forgetAt(105, 5, lark);
  CHECK(!r.within(105, 5, 0, lark, 5000));
  CHECK(r.within(205, 5, 0, lark, 5000));
  r.clear();
  CHECK_EQ(r.size(), (size_t)0);
}

TEST(yield_control_at_finds_the_taskbar_button_under_the_point) {
  // The Windows 11 taskbar as UI Automation lists it: Start, the app buttons, the tray. The stretch
  // between the last app button and the tray is empty, and so is the one left of Start.
  std::vector<Rect> taskbar = {{518, 1032, 563, 1080}, {831, 1032, 875, 1080}, {875, 1032, 919, 1080},
                               {1609, 1032, 1641, 1080}, {1908, 1032, 1920, 1080}};
  CHECK(controlAt(taskbar, 540, 1050) == &taskbar[0]);
  CHECK(controlAt(taskbar, 831, 1032) == &taskbar[1]);  // left and top edges inclusive
  CHECK(controlAt(taskbar, 875, 1050) == &taskbar[2]);  // right edge exclusive: the next button's
  CHECK(controlAt(taskbar, 1919, 1079) == &taskbar[4]);
  CHECK(controlAt(taskbar, 1500, 1050) == nullptr);  // the empty stretch
  CHECK(controlAt(taskbar, 300, 1050) == nullptr);
  CHECK(controlAt(taskbar, 540, 1031) == nullptr);  // above the taskbar
  CHECK(controlAt(std::vector<Rect>(), 540, 1050) == nullptr);
}

// ---------- frames ----------
static const unsigned char kLz4In[] = {59,97,98,99,3,0,255,29,88,89,90,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,7,1,0,255,23,80,7,7,101,110,100};

static std::string lz4Expected() {
  std::string s = "abcabcabcabcabcabcXYZ";
  for (int i = 0; i < 40; ++i) s += (char)i;
  s += std::string(300, (char)7);
  return s + "end";
}

TEST(frames_lz4_roundtrip_from_python_encoder) {
  std::string want = lz4Expected();
  std::vector<uint8_t> out(want.size());
  long n = lz4Decompress(kLz4In, sizeof kLz4In, out.data(), out.size());
  CHECK_EQ(n, (long)want.size());
  CHECK(std::memcmp(out.data(), want.data(), want.size()) == 0);
  // too small an output buffer and truncated input are rejected, not overrun
  std::vector<uint8_t> small(want.size() - 1);
  CHECK_EQ(lz4Decompress(kLz4In, sizeof kLz4In, small.data(), small.size()), -1L);
  CHECK(lz4Decompress(kLz4In, 20, out.data(), out.size()) != (long)want.size());
}

static std::vector<uint8_t> makeBpf(int W, int H, int x0, int y0, int w, int h, const std::vector<uint32_t>& pal,
                                    const std::vector<uint8_t>& idx) {
  // literal-only LZ4 block: token 0xF0 + length bytes, then the data
  std::vector<uint8_t> lz;
  size_t n = idx.size();
  if (n >= 15) {
    lz.push_back(0xF0);
    size_t r = n - 15;
    while (r >= 255) { lz.push_back(255); r -= 255; }
    lz.push_back((uint8_t)r);
  } else {
    lz.push_back((uint8_t)(n << 4));
  }
  lz.insert(lz.end(), idx.begin(), idx.end());
  std::vector<uint8_t> b = {'B', 'P', 'F', '1'};
  auto u16 = [&](unsigned v) { b.push_back(v & 255); b.push_back(v >> 8); };
  auto u32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) b.push_back((v >> (8 * i)) & 255); };
  u16(W); u16(H); u16(x0); u16(y0); u16(w); u16(h); u16((unsigned)pal.size());
  for (uint32_t c : pal) u32(c);
  u32((uint32_t)lz.size());
  b.insert(b.end(), lz.begin(), lz.end());
  return b;
}

TEST(frames_bpf_parse_and_expand) {
  std::vector<uint32_t> pal = {0x00000000u, 0xFF0000FFu, 0x80404000u};
  std::vector<uint8_t> idx = {1, 2, 0, 2, 1, 1};  // 3x2 box
  std::vector<uint8_t> file = makeBpf(5, 4, 1, 1, 3, 2, pal, idx);
  BpfFrame f;
  std::string err;
  CHECK(parseBpf(file.data(), file.size(), &f, &err));
  CHECK_EQ(f.width, 5);
  CHECK_EQ(f.w, 3);
  std::vector<uint32_t> canvas(5 * 4, 0xDEADBEEFu);
  expandBpf(f, (uint8_t*)canvas.data(), 5 * 4);
  CHECK_EQ(canvas[0], 0u);                // cleared outside the box
  CHECK_EQ(canvas[1 * 5 + 1], 0xFF0000FFu);
  CHECK_EQ(canvas[1 * 5 + 2], 0x80404000u);
  CHECK_EQ(canvas[2 * 5 + 3], 0xFF0000FFu);
  CHECK_EQ(canvas[3 * 5 + 4], 0u);
  // corrupt inputs
  std::vector<uint8_t> bad = file;
  bad[0] = 'X';
  CHECK(!parseBpf(bad.data(), bad.size(), &f, &err));
  bad = file;
  bad[bad.size() - 1] = 9;  // index 9 is outside the 3-colour palette
  CHECK(!parseBpf(bad.data(), bad.size(), &f, &err));
  CHECK(!parseBpf(file.data(), file.size() - 3, &f, &err));
}

TEST(frames_downscale_averages_areas) {
  // 4x2 -> 2x1: each output pixel averages a 2x2 block per channel
  std::vector<uint32_t> src = {0x00000000u, 0x40404040u, 0xFFFFFFFFu, 0xFFFFFFFFu,
                               0x80808080u, 0xC0C0C0C0u, 0x00000000u, 0x00000000u};
  uint32_t dst[2];
  downscaleBGRA(src.data(), 4, 2, 4, dst, 2, 1, 2);
  CHECK_EQ(dst[0], 0x60606060u);  // (0+64+128+192)/4 = 96
  CHECK_EQ(dst[1], 0x80808080u);  // (255+255+0+0)/4 = 127.5 -> 128
  // 3 -> 2 uses fractional weights: [a, a/2+b/2... ] check constant image stays constant
  std::vector<uint32_t> flat(9 * 7, 0x7F3F1F0Fu);
  std::vector<uint32_t> out(4 * 3);
  downscaleBGRA(flat.data(), 9, 7, 9, out.data(), 4, 3, 4);
  for (uint32_t v : out) CHECK_EQ(v, 0x7F3F1F0Fu);
}

TEST(frames_downscale_speed_is_reasonable) {
  const int sw = 969, sh = 731, dw = 424, dh = 320;
  std::vector<uint32_t> src((size_t)sw * sh, 0x80402010u), dst((size_t)dw * dh);
  double ms = 1e9;  // best of 5: a busy CI box's scheduling hiccups must not count
  for (int i = 0; i < 5; ++i) {
    auto t0 = std::chrono::steady_clock::now();
    downscaleBGRA(src.data(), sw, sh, sw, dst.data(), dw, dh, dw);
    double t = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (t < ms) ms = t;
  }
  std::printf("  downscale 969x731 -> 424x320: %.2f ms (best of 5)\n", ms);
  CHECK(ms < 40);  // generous: -O0 debug builds of the tests are slow
}

MINITEST_MAIN

// ---------- fullscreen refuge: which display she lives on while programs fill others ----------

namespace {
std::vector<pet::Rect> twoDisplays() { return {{0, 0, 1920, 1080}, {1920, 0, 3840, 1080}}; }
}  // namespace

TEST(refuge_hides_when_the_only_display_is_covered) {
  pet::FullscreenRefuge r;
  pet::FullscreenRefuge::Result a = r.tick({{0, 0, 1920, 1080}}, {true}, 100, 936, 96, 144);
  CHECK(a.action == pet::FullscreenRefuge::Action::Hide);
  CHECK(!r.away());
}

TEST(refuge_stays_on_a_free_display) {
  pet::FullscreenRefuge r;
  pet::FullscreenRefuge::Result a = r.tick(twoDisplays(), {false, true}, 480, 936, 96, 144);
  CHECK(a.action == pet::FullscreenRefuge::Action::Stay);
  CHECK(!r.away());
}

TEST(refuge_moves_to_the_free_display_keeping_her_relative_spot) {
  pet::FullscreenRefuge r;
  // Her centre sits a quarter of the way across display 0: the same quarter of display 1, on its floor.
  pet::FullscreenRefuge::Result a = r.tick(twoDisplays(), {true, false}, 432, 936, 96, 144);
  CHECK(a.action == pet::FullscreenRefuge::Action::Move);
  CHECK(a.x + 48 == 1920 + 480);
  CHECK(a.y == 1080 - 144);
  CHECK(r.away());
}

TEST(refuge_moves_back_home_when_her_display_frees_up) {
  pet::FullscreenRefuge r;
  r.tick(twoDisplays(), {true, false}, 432, 936, 96, 144);
  pet::FullscreenRefuge::Result still = r.tick(twoDisplays(), {true, false}, 2352, 936, 96, 144);  // home still covered
  CHECK(still.action == pet::FullscreenRefuge::Action::Stay);
  pet::FullscreenRefuge::Result back = r.tick(twoDisplays(), {false, false}, 2352, 936, 96, 144);
  CHECK(back.action == pet::FullscreenRefuge::Action::Move);
  CHECK(back.x == 432 && back.y == 936);
  CHECK(!r.away());
}

TEST(refuge_hides_when_every_display_is_covered_then_comes_back_where_it_can) {
  pet::FullscreenRefuge r;
  pet::FullscreenRefuge::Result a = r.tick(twoDisplays(), {true, true}, 432, 936, 96, 144);
  CHECK(a.action == pet::FullscreenRefuge::Action::Hide);
  pet::FullscreenRefuge::Result b = r.tick(twoDisplays(), {true, false}, 432, 936, 96, 144);  // display 1 freed up first
  CHECK(b.action == pet::FullscreenRefuge::Action::Move && b.x >= 1920);
  pet::FullscreenRefuge::Result c = r.tick(twoDisplays(), {false, false}, b.x, b.y, 96, 144);
  CHECK(c.action == pet::FullscreenRefuge::Action::Move && c.x == 432 && c.y == 936);
}

TEST(refuge_stays_where_the_user_dragged_her) {
  pet::FullscreenRefuge r;
  r.tick(twoDisplays(), {true, false}, 432, 936, 96, 144);
  r.userMoved();  // dragged somewhere on display 1: that is her place now
  CHECK(!r.away());
  pet::FullscreenRefuge::Result a = r.tick(twoDisplays(), {false, false}, 3000, 936, 96, 144);
  CHECK(a.action == pet::FullscreenRefuge::Action::Stay);
}

TEST(refuge_picks_the_nearest_free_display) {
  std::vector<pet::Rect> three = {{0, 0, 1920, 1080}, {1920, 0, 3840, 1080}, {3840, 0, 5760, 1080}};
  pet::FullscreenRefuge r;
  pet::FullscreenRefuge::Result a = r.tick(three, {false, true, false}, 2000, 936, 96, 144);  // left part of the middle one
  CHECK(a.action == pet::FullscreenRefuge::Action::Move && a.x < 1920);
}

TEST(refuge_move_keeps_her_inside_a_narrow_display) {
  pet::FullscreenRefuge r;
  pet::FullscreenRefuge::Result a =
      r.tick({{0, 0, 1920, 1080}, {1920, 0, 2560, 1080}}, {true, false}, 1800, 936, 200, 144);
  CHECK(a.action == pet::FullscreenRefuge::Action::Move);
  CHECK(a.x >= 1920 && a.x + 200 <= 2560);
}

TEST(free_floors_leave_out_the_covered_displays) {
  // A game fills display 0: she may walk on display 1 only, so she cannot wander back over the game.
  std::vector<pet::Rect> floors = {{0, 0, 1920, 1080}, {1920, 0, 3840, 1080}};
  std::vector<pet::Rect> f = pet::freeFloors(floors, {true, false});
  CHECK_EQ(f.size(), (size_t)1);
  CHECK(f.size() == 1 && f[0].left == 1920);
  CHECK_EQ(pet::freeFloors(floors, {false, false}).size(), (size_t)2);
  CHECK_EQ(pet::freeFloors(floors, {true, true}).size(), (size_t)2);  // none free: she is hidden, keep them all
  CHECK_EQ(pet::freeFloors(floors, {}).size(), (size_t)2);            // nothing known yet
  CHECK_EQ(pet::freeFloors(floors, {true}).size(), (size_t)2);        // the lists do not match (a display just changed)
}

TEST(brain_low_power_halves_the_frame_rate) {
  Brain b = makeBrain();
  CHECK_EQ(b.tick(0).nextTickMs, 500);  // Idle at 2 fps
  b.setLowPower(true);
  CHECK_EQ(b.tick(0).nextTickMs, 1000);
  b.setLowPower(false);
  CHECK_EQ(b.tick(0).nextTickMs, 500);
}

TEST(brain_low_power_keeps_at_least_one_frame_per_second) {
  BrainConfig c = testCfg();
  c.fps[(int)Anim::Idle] = 1;
  Brain b = makeBrain(1, c);
  b.setLowPower(true);
  CHECK_EQ(b.tick(0).nextTickMs, 1000);
}

TEST(brain_low_power_advances_frames_at_the_slower_pace) {
  Brain b = makeBrain();  // Idle: 2 frames at 2 fps, so 1000 ms per frame in low power
  b.setLowPower(true);
  CHECK_EQ(b.tick(600).index, 0);
  CHECK_EQ(b.tick(500).index, 1);
}

// ---------- ClickCounter (three quick clicks open the chat) ----------
TEST(clicks_three_quick_clicks_in_one_spot_count_up_to_three) {
  ClickCounter c(500, 8);
  CHECK_EQ(c.click(100, 100, 1000), 1);
  CHECK_EQ(c.click(103, 100, 1300), 2);
  CHECK_EQ(c.click(100, 104, 1600), 3);
}

TEST(clicks_a_gap_longer_than_the_interval_starts_a_new_count) {
  ClickCounter c(500, 8);
  c.click(100, 100, 1000);
  CHECK_EQ(c.click(100, 100, 1500), 2);  // exactly the interval still counts
  CHECK_EQ(c.click(100, 100, 2001), 1);  // 501 ms after the previous click
  CHECK_EQ(c.click(100, 100, 2200), 2);
}

TEST(clicks_far_from_the_previous_one_start_a_new_count) {
  ClickCounter c(500, 8);
  c.click(100, 100, 1000);
  CHECK_EQ(c.click(120, 100, 1200), 1);  // 20 px to the right
  CHECK_EQ(c.click(120, 130, 1400), 1);  // 30 px down
  CHECK_EQ(c.click(120, 130, 1600), 2);
}

TEST(clicks_reset_by_a_drag_starts_a_new_count) {
  ClickCounter c(500, 8);
  c.click(100, 100, 1000);
  c.click(100, 100, 1200);
  c.reset();
  CHECK_EQ(c.click(100, 100, 1400), 1);
}

TEST(clicks_the_click_after_a_third_starts_a_new_count) {
  ClickCounter c(500, 8);
  c.click(100, 100, 1000);
  c.click(100, 100, 1200);
  CHECK_EQ(c.click(100, 100, 1400), 3);
  CHECK_EQ(c.click(100, 100, 1600), 1);
  CHECK_EQ(c.click(100, 100, 1800), 2);
}
