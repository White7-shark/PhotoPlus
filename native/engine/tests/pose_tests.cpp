// Pose assistant. What a person would notice:
//  - a landmark outside the photo, or hidden, is not trusted (the model
//    guesses positions for them);
//  - the same pose scores the same wherever the person stands and however
//    big they are; matching it scores high and says "perfect";
//  - a different arm, a step too far or too close, standing too far to one
//    side, a body or head turned: each gives the right advice, framing first;
//  - a hand that is briefly hidden is held for a moment (guidance stays
//    steady), and after a while the app asks to show it;
//  - a mirrored pose matches its mirror image; stillness is measured.
#include <cmath>
#include <cstdio>
#include <limits>

#include "photoplus/pp_engine.h"
#include "test_harness.hpp"

namespace {

constexpr float kPi = 3.14159265f;

// A simple skeleton: shoulder centre (cx, cy) in normalised coordinates,
// shoulder width `sw` (share of the image height), limb directions
// (radians, 0 = image right, +pi/2 = down), facing the camera (the
// person's left on the image's right).
struct Body {
  float cx = 0.5f, cy = 0.35f, sw = 0.12f, aspect = 0.75f;
  float leftUpper = kPi / 2 - 0.15f, leftFore = kPi / 2 - 0.1f;    // arms hanging
  float rightUpper = kPi / 2 + 0.15f, rightFore = kPi / 2 + 0.1f;
  float leftThigh = kPi / 2, leftShin = kPi / 2, rightThigh = kPi / 2, rightShin = kPi / 2;
  float headTilt = 0;      // radians
  float yaw = 0;           // body turn (radians): the shoulders narrow and the left one moves back (+)
  float visibility = 0.98f, presence = 0.98f;
};

pp_pose make(const Body& b) {
  pp_pose p{};
  p.aspect = b.aspect;
  p.valid = 1;
  // Work in square units (x * aspect), convert back at the end.
  float X[PP_POSE_LANDMARKS] = {}, Y[PP_POSE_LANDMARKS] = {}, Z[PP_POSE_LANDMARKS] = {};
  const float cx = b.cx * b.aspect, cy = b.cy, s = b.sw;
  auto put = [&](int i, float x, float y, float z = 0) { X[i] = x, Y[i] = y, Z[i] = z; };
  const float half = s / 2 * std::cos(b.yaw), depth = s / 2 * std::sin(b.yaw);
  put(11, cx + half, cy, depth);  // left shoulder (image right)
  put(12, cx - half, cy, -depth);
  auto limb = [&](int from, int to, float ang, float length) {
    put(to, X[from] + std::cos(ang) * length, Y[from] + std::sin(ang) * length);
  };
  limb(11, 13, b.leftUpper, 0.82f * s);
  limb(13, 15, b.leftFore, 0.68f * s);
  limb(12, 14, b.rightUpper, 0.82f * s);
  limb(14, 16, b.rightFore, 0.68f * s);
  put(23, cx + 0.36f * s, cy + 1.25f * s);
  put(24, cx - 0.36f * s, cy + 1.25f * s);
  limb(23, 25, b.leftThigh, 1.2f * s);
  limb(25, 27, b.leftShin, 1.15f * s);
  limb(24, 26, b.rightThigh, 1.2f * s);
  limb(26, 28, b.rightShin, 1.15f * s);
  // Feet and hands near their joints.
  for (int i : {29, 31}) put(i, X[27] + (i == 31 ? 0.1f : 0) * s, Y[27] + 0.1f * s);
  for (int i : {30, 32}) put(i, X[28] - (i == 32 ? 0.1f : 0) * s, Y[28] + 0.1f * s);
  for (int i : {17, 19, 21}) put(i, X[15] + 0.05f * s, Y[15] + 0.1f * s);
  for (int i : {18, 20, 22}) put(i, X[16] - 0.05f * s, Y[16] + 0.1f * s);
  // Head: centred above the shoulders, tilted by headTilt.
  const float hx = cx, hy = cy - 0.75f * s;
  auto head = [&](int i, float dx, float dy) {
    const float c = std::cos(b.headTilt), sn = std::sin(b.headTilt);
    put(i, hx + dx * c - dy * sn, hy + dx * sn + dy * c);
  };
  head(0, 0, 0);
  head(1, 0.06f * s, -0.08f * s), head(2, 0.1f * s, -0.08f * s), head(3, 0.15f * s, -0.08f * s);
  head(4, -0.06f * s, -0.08f * s), head(5, -0.1f * s, -0.08f * s), head(6, -0.15f * s, -0.08f * s);
  head(7, 0.21f * s, -0.04f * s), head(8, -0.21f * s, -0.04f * s);
  head(9, 0.06f * s, 0.12f * s), head(10, -0.06f * s, 0.12f * s);
  for (int i = 0; i < PP_POSE_LANDMARKS; ++i) {
    p.points[i] = pp_pose_point{X[i] / b.aspect, Y[i], Z[i] / b.aspect, b.visibility, b.presence};
  }
  return p;
}

pp_pose_match_result matchOf(const pp_pose& ref, const pp_pose& live, bool framing = true) {
  pp_pose_match_params mp{};
  mp.framing = framing ? 1 : 0;
  mp.strictness = 0.5f;
  pp_pose_match_result r{};
  pp_pose_match(&ref, &live, &mp, &r);
  return r;
}

bool advises(const pp_pose_match_result& r, int part, int action) {
  for (int i = 0; i < r.advice_count; ++i) {
    if ((part < 0 || r.advice[i].part == part) && r.advice[i].action == action) return true;
  }
  return false;
}

void printAdvice(const char* tag, const pp_pose_match_result& r) {
  std::printf("    %s: score %.0f (pose %.0f, framing %.0f, coverage %.2f) advice", tag, r.score, r.pose_score, r.framing_score,
              r.coverage);
  for (int i = 0; i < r.advice_count; ++i) std::printf(" [%d:%d %.2f]", r.advice[i].part, r.advice[i].action, r.advice[i].amount);
  std::printf("\n");
}

}  // namespace

TEST(pose_reliability_ignores_guessed_and_hidden_landmarks) {
  pp_pose p = make(Body{});
  pp_pose_description d{};
  ASSERT_OK(pp_pose_describe(&p, &d));
  EXPECT_TRUE(d.reliability[0] > 0.95f);
  // A landmark the model guessed outside the picture (high visibility, low
  // presence, as the real model reports for an elbow below the frame).
  p.points[13] = pp_pose_point{0.7f, 1.04f, 0, 0.93f, 0.35f};
  // Hidden: in the picture, low visibility.
  p.points[16].visibility = 0.1f;
  // Off the image entirely, confidently reported.
  p.points[27].y = 1.3f;
  // Nonsense.
  p.points[29].x = std::numeric_limits<float>::quiet_NaN();
  ASSERT_OK(pp_pose_describe(&p, &d));
  EXPECT_TRUE(d.reliability[13] < 0.05f);
  EXPECT_TRUE(d.reliability[16] < 0.05f);
  EXPECT_TRUE(d.reliability[27] < 0.01f);
  EXPECT_EQ(d.reliability[29], 0.0f);
  EXPECT_TRUE(d.bone_reliability[PP_POSE_LEFT_UPPER_ARM] < 0.05f);
}

TEST(pose_description_does_not_depend_on_position_or_size) {
  Body a;
  a.rightUpper = -0.3f, a.rightFore = -1.2f;  // right arm raised
  Body b = a;
  b.cx = 0.3f, b.cy = 0.42f, b.sw = 0.07f;
  pp_pose pa = make(a), pb = make(b);
  pp_pose_description da{}, db{};
  ASSERT_OK(pp_pose_describe(&pa, &da));
  ASSERT_OK(pp_pose_describe(&pb, &db));
  EXPECT_EQ(da.valid, 1);
  EXPECT_EQ(da.anchor_from_shoulders, 1);
  EXPECT_NEAR(da.scale, 0.12, 0.012);  // the shoulder width, from every visible bone
  EXPECT_NEAR(db.scale, 0.07, 0.007);
  EXPECT_NEAR(da.anchor_x, 0.5, 1e-4);
  float worst = 0;
  for (int i = 0; i < PP_POSE_LANDMARKS; ++i) {
    worst = std::max(worst, std::fabs(da.nx[i] - db.nx[i]) + std::fabs(da.ny[i] - db.ny[i]));
  }
  for (int k = 0; k < PP_POSE_BONES; ++k) worst = std::max(worst, std::fabs(da.direction[k] - db.direction[k]));
  std::printf("    worst difference %.4f\n", worst);
  EXPECT_TRUE(worst < 0.05f);
  EXPECT_NEAR(da.direction[PP_POSE_TORSO], kPi / 2, 0.01);
  EXPECT_NEAR(da.direction[PP_POSE_SHOULDER_LINE], 0.0, 0.01);
  EXPECT_EQ(da.shot, PP_POSE_SHOT_FULL_BODY);
  // A portrait: hips and legs out of the picture.
  Body portrait;
  portrait.cy = 0.8f, portrait.sw = 0.45f, portrait.aspect = 0.8f;
  pp_pose pp = make(portrait);
  for (int i = 23; i < PP_POSE_LANDMARKS; ++i) pp.points[i].presence = 0.02f;
  pp_pose_description dp{};
  ASSERT_OK(pp_pose_describe(&pp, &dp));
  EXPECT_EQ(dp.valid, 1);
  EXPECT_EQ(dp.shot, PP_POSE_SHOT_HEAD_AND_SHOULDERS);
  EXPECT_NEAR(dp.scale, 0.45, 0.06);
}

TEST(pose_match_scores_the_same_pose_high_and_says_perfect) {
  Body ref;
  ref.leftUpper = -0.4f, ref.leftFore = -kPi / 2;  // a wave
  const pp_pose r = make(ref);
  const auto same = matchOf(r, r);
  printAdvice("same", same);
  EXPECT_TRUE(same.score > 97);
  EXPECT_NEAR(same.coverage, 1.0, 0.01);
  EXPECT_EQ(same.advice_count, 1);
  EXPECT_EQ(same.advice[0].action, PP_POSE_PERFECT);
  // Elsewhere in the frame and smaller: the pose itself still matches.
  Body moved = ref;
  moved.cx = 0.35f, moved.sw = 0.09f;
  const auto m = matchOf(r, make(moved), false);
  EXPECT_TRUE(m.pose_score > 97);
}

TEST(pose_match_advises_the_arm_and_the_framing) {
  Body ref;
  ref.leftUpper = -0.4f, ref.leftFore = -kPi / 2;  // left hand raised (image right)
  const pp_pose r = make(ref);
  // Arm down: lower score, "left hand up".
  const auto down = matchOf(r, make(Body{}));
  printAdvice("arm down", down);
  EXPECT_TRUE(down.score < 65);  // one clearly wrong arm shows plainly
  EXPECT_TRUE(down.bone_score[PP_POSE_LEFT_FOREARM] < 0.2f);
  EXPECT_TRUE(down.bone_score[PP_POSE_RIGHT_FOREARM] > 0.9f);  // the other arm already matches
  EXPECT_TRUE(advises(down, PP_POSE_PART_LEFT_HAND, PP_POSE_UP));
  // Standing too far to the image's right: move left, first.
  Body right = ref;
  right.cx = 0.68f;
  const auto r1 = matchOf(r, make(right));
  printAdvice("too far right", r1);
  EXPECT_EQ(r1.advice[0].action, PP_POSE_MOVE_LEFT);
  EXPECT_NEAR(r1.offset_x, 0.18, 0.01);
  // Too close: step back. Too far: come closer.
  Body close = ref;
  close.sw = 0.17f;
  EXPECT_TRUE(advises(matchOf(r, make(close)), PP_POSE_PART_BODY, PP_POSE_BACK));
  Body far = ref;
  far.sw = 0.08f;
  EXPECT_TRUE(advises(matchOf(r, make(far)), PP_POSE_PART_BODY, PP_POSE_CLOSER));
  // Framing ignored: none of that.
  const auto noFrame = matchOf(r, make(right), false);
  EXPECT_TRUE(!advises(noFrame, -1, PP_POSE_MOVE_LEFT));
  // Head tilted the wrong way, body turned.
  Body tilt = ref;
  tilt.headTilt = -0.35f;
  EXPECT_TRUE(advises(matchOf(r, make(tilt)), PP_POSE_PART_HEAD, PP_POSE_TILT_RIGHT));
  Body turned = ref;
  turned.yaw = 0.7f;
  const auto t = matchOf(r, make(turned));
  printAdvice("turned", t);
  EXPECT_TRUE(advises(t, PP_POSE_PART_BODY, PP_POSE_TURN_LEFT));
}

TEST(pose_hidden_parts_lower_coverage_not_the_score_and_are_asked_for) {
  Body ref;
  ref.leftUpper = -0.4f, ref.leftFore = -kPi / 2;
  const pp_pose r = make(ref);
  pp_pose live = make(ref);
  for (int i : {13, 15, 17, 19, 21}) live.points[i].visibility = 0.05f;  // left arm hidden
  const auto m = matchOf(r, live);
  printAdvice("arm hidden", m);
  EXPECT_TRUE(m.pose_score > 95);  // what can be seen matches
  EXPECT_TRUE(m.coverage < 0.9f);
  EXPECT_TRUE(advises(m, PP_POSE_PART_LEFT_HAND, PP_POSE_SHOW));
  // A half-body reference against a full-body view of the same person is
  // fine; a full-body reference against a portrait asks to step back.
  pp_pose portrait = make(ref);
  for (int i = 23; i < PP_POSE_LANDMARKS; ++i) portrait.points[i].presence = 0.02f;
  EXPECT_TRUE(advises(matchOf(r, portrait), PP_POSE_PART_BODY, PP_POSE_BACK));
  // Legs covered (a table, a crowd) but in the picture: still a full-body
  // shot, so there is no reason to step back.
  pp_pose covered = make(ref);
  for (int i = 23; i < PP_POSE_LANDMARKS; ++i) covered.points[i].visibility = 0.05f;
  pp_pose_description dc{};
  ASSERT_OK(pp_pose_describe(&covered, &dc));
  EXPECT_EQ(dc.shot, PP_POSE_SHOT_FULL_BODY);
  EXPECT_TRUE(!advises(matchOf(r, covered), PP_POSE_PART_BODY, PP_POSE_BACK));
}

TEST(pose_tracker_holds_a_briefly_hidden_hand_then_lets_go) {
  Body ref;
  ref.leftUpper = -0.4f, ref.leftFore = -kPi / 2;
  const pp_pose r = make(ref);
  pp_pose_tracker* t = nullptr;
  ASSERT_OK(pp_pose_tracker_create(&t));
  pp_pose steady{};
  float still = 0;
  int64_t ms = 0;
  for (int k = 0; k < 10; ++k, ms += 33) ASSERT_OK(pp_pose_tracker_update(t, &r, ms, &steady, &still));
  EXPECT_TRUE(still > 0.8f);  // standing still
  // The hand goes behind the head for 300 ms: still matched, nothing asked.
  pp_pose hidden = r;
  for (int i : {15, 17, 19, 21}) hidden.points[i].visibility = 0.05f;
  for (int k = 0; k < 9; ++k, ms += 33) ASSERT_OK(pp_pose_tracker_update(t, &hidden, ms, &steady, &still));
  auto m = matchOf(r, steady);
  printAdvice("held 300 ms", m);
  EXPECT_TRUE(!advises(m, PP_POSE_PART_LEFT_HAND, PP_POSE_SHOW));
  EXPECT_TRUE(m.left_hand_score > 0.9f);
  // Hidden for over a second: let go, and asked for.
  for (int k = 0; k < 30; ++k, ms += 33) ASSERT_OK(pp_pose_tracker_update(t, &hidden, ms, &steady, &still));
  m = matchOf(r, steady);
  printAdvice("hidden 1.3 s", m);
  EXPECT_TRUE(advises(m, PP_POSE_PART_LEFT_HAND, PP_POSE_SHOW));
  // A hand over the shoulders, held for most of the hold: the person is
  // still in shot (head and shoulders), so nobody is told to step back.
  pp_pose shoulders = r;
  for (int i : {11, 12}) shoulders.points[i].visibility = 0.05f;
  for (int k = 0; k < 5; ++k, ms += 33) ASSERT_OK(pp_pose_tracker_update(t, &r, ms, &steady, &still));
  for (int k = 0; k < 20; ++k, ms += 33) {
    ASSERT_OK(pp_pose_tracker_update(t, &shoulders, ms, &steady, &still));
    pp_pose_description ds{};
    ASSERT_OK(pp_pose_describe(&steady, &ds));
    EXPECT_EQ(ds.shot, PP_POSE_SHOT_FULL_BODY);
    EXPECT_TRUE(!advises(matchOf(r, steady), PP_POSE_PART_BODY, PP_POSE_BACK));
  }
  // Nobody for a moment: the last pose fades out; then nobody.
  ASSERT_OK(pp_pose_tracker_update(t, nullptr, ms += 33, &steady, &still));
  EXPECT_EQ(steady.valid, 1);
  ASSERT_OK(pp_pose_tracker_update(t, nullptr, ms += 1000, &steady, &still));
  EXPECT_EQ(steady.valid, 0);
  EXPECT_EQ(still, 0.0f);
  // Moving fast: not still.
  Body moving = ref;
  for (int k = 0; k < 10; ++k, ms += 33) {
    moving.cx = 0.3f + 0.04f * static_cast<float>(k);
    const pp_pose p = make(moving);
    ASSERT_OK(pp_pose_tracker_update(t, &p, ms, &steady, &still));
  }
  std::printf("    moving: stillness %.2f\n", static_cast<double>(still));
  EXPECT_TRUE(still < 0.3f);
  pp_pose_tracker_destroy(t);
}

TEST(pose_mirror_matches_its_mirror_image) {
  Body ref;
  ref.leftUpper = -0.4f, ref.leftFore = -kPi / 2;  // left hand raised, image right
  const pp_pose r = make(ref);
  pp_pose m{};
  ASSERT_OK(pp_pose_mirror(&r, &m));
  // In the mirror the raised hand is the right one, on the image's left.
  EXPECT_TRUE(m.points[16].y < m.points[12].y);
  EXPECT_TRUE(m.points[16].x < 0.5f);
  pp_pose back{};
  ASSERT_OK(pp_pose_mirror(&m, &back));
  for (int i = 0; i < PP_POSE_LANDMARKS; ++i) EXPECT_NEAR(back.points[i].x, r.points[i].x, 1e-5);
  // Mirroring both sides of a comparison changes nothing.
  Body live;
  const pp_pose l = make(live);
  pp_pose ml{};
  ASSERT_OK(pp_pose_mirror(&l, &ml));
  EXPECT_NEAR(matchOf(r, l).score, matchOf(m, ml).score, 0.5);
  // A person doing it with the other hand matches the mirrored reference.
  Body other;
  other.rightUpper = kPi + 0.4f, other.rightFore = -kPi / 2;
  EXPECT_TRUE(matchOf(m, make(other)).score > 90);
}

TEST(pose_rejects_bad_input) {
  pp_pose p = make(Body{});
  pp_pose_description d{};
  EXPECT_EQ(pp_pose_describe(nullptr, &d), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_pose_describe(&p, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_pose bad = p;
  bad.aspect = std::numeric_limits<float>::infinity();
  EXPECT_EQ(pp_pose_describe(&bad, &d), PP_ERROR_INVALID_ARGUMENT);
  pp_pose_match_result r{};
  EXPECT_EQ(pp_pose_match(&p, nullptr, nullptr, &r), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_pose_match(&bad, &p, nullptr, &r), PP_ERROR_INVALID_ARGUMENT);
  // Nobody in view: one piece of advice.
  pp_pose nobody{};
  nobody.aspect = 0.75f;
  ASSERT_OK(pp_pose_match(&p, &nobody, nullptr, &r));
  EXPECT_EQ(r.advice_count, 1);
  EXPECT_EQ(r.advice[0].action, PP_POSE_NO_PERSON);
  // Garbage landmarks never crash and never score.
  pp_pose junk = p;
  for (auto& q : junk.points) q.x = std::numeric_limits<float>::quiet_NaN();
  ASSERT_OK(pp_pose_match(&p, &junk, nullptr, &r));
  EXPECT_EQ(r.score, 0.0f);
  EXPECT_EQ(pp_pose_tracker_create(nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_pose out{};
  EXPECT_EQ(pp_pose_mirror(&bad, &out), PP_ERROR_INVALID_ARGUMENT);
  EXPECT_EQ(pp_pose_mirror(&p, nullptr), PP_ERROR_INVALID_ARGUMENT);
  pp_pose_tracker_destroy(nullptr);
}
