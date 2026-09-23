#include "minitest.h"
#include "core/ini.h"
#include "core/screen.h"
#include "core/pose.h"
#include "core/brain.h"
#include "core/skin.h"
#include "core/voice.h"

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

// ---------- brain ----------
static BrainConfig testCfg() {
  BrainConfig c;
  c.spriteW = 32; c.spriteH = 48;
  for (int i = 0; i < (int)Anim::Count; ++i) c.setFrames((Anim)i, 2);
  c.walkSpeed = 100;
  c.sleepAfterSec = 10;
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
  Brain b = makeBrain();
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

TEST(brain_click_without_react_frames_still_reports) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React].clear();
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
  int special = 0;
  for (int i = 0; i < 1000; ++i) special += sceneKeys(Scene::TapBody, rng)[0] == "touch2";
  CHECK(special > 150 && special < 350);  // about 25%
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

TEST(skin_number_from_names) {
  CHECK(skinNumber("01-改造.png") == "01");
  CHECK(skinNumber("Q版-07-White Seaside Melody") == "07");
  CHECK(skinNumber("belfast.png") == "");
  CHECK(skinNumber("12") == "12");
}

MINITEST_MAIN
