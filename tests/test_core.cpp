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

TEST(brain_special_touch_share) {
  BrainConfig c = testCfg();
  c.variants[(int)Anim::React] = {1, 1, 1};
  c.reactSpecial = 2;
  Brain b = makeBrain(7, c);
  b.tick(0);
  int special = 0;
  for (int i = 0; i < 400; ++i) {
    b.press(110, 590);
    b.release();
    special += b.tick(0).event == PetEvent::TapSpecial;
    b.tick(5000);
  }
  CHECK(special > 60 && special < 140);  // about 25%
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
  CHECK(!ChatConfig::parse("[chat]\napi_key=\n").ready());
  CHECK(!ChatConfig::parse(chatIniTemplate()).ready());  // template ships without a key
}

TEST(chat_modelhub_responses_config) {
  ChatConfig c = ChatConfig::parse(
      "[chat]\nbase_url=https://aidp.example.net/api/modelhub/online/\napi_key=k+1/2\nmodel=gpt-6-astra\n"
      "api=responses\nauth=ak\nreasoning_effort=medium\n");
  CHECK(c.ready() && c.responsesApi && c.keyInQuery);
  CHECK(c.reasoningEffort == "medium");
  CHECK(c.endpoint() == "https://aidp.example.net/api/modelhub/online/responses");
  CHECK(c.requestUrl() == "https://aidp.example.net/api/modelhub/online/responses?ak=k%2B1%2F2");
  CHECK(c.authorization().empty());  // the key is never sent twice
  // a base copied with a dialect suffix is taken as the channel root
  CHECK(ChatConfig::parse("[chat]\nbase_url=https://h/x/responses\napi=responses\n").endpoint() == "https://h/x/responses");
  CHECK(ChatConfig::parse("[chat]\nbase_url=https://h/x/v2/crawl\napi=responses\n").endpoint() == "https://h/x/responses");
  ChatConfig d = ChatConfig::parse("[chat]\nbase_url=https://api.deepseek.com/v1\napi_key=sk-x\n");
  CHECK(!d.responsesApi && !d.keyInQuery && d.reasoningEffort.empty());
  CHECK(d.requestUrl() == "https://api.deepseek.com/v1/chat/completions");
  CHECK(d.authorization() == "Bearer sk-x");
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
  auto t0 = std::chrono::steady_clock::now();
  for (int i = 0; i < 5; ++i) downscaleBGRA(src.data(), sw, sh, sw, dst.data(), dw, dh, dw);
  double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / 5;
  std::printf("  downscale 969x731 -> 424x320: %.2f ms\n", ms);
  CHECK(ms < 40);  // generous: -O0 debug builds of the tests are slow
}

MINITEST_MAIN
