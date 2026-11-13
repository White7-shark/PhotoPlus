#include "photoplus/pose.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace photoplus::pose {

namespace {

constexpr float kPi = 3.14159265358979f;

float smoothstep(float e0, float e1, float x) {
  const float t = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
  return t * t * (3.0f - 2.0f * t);
}

bool finite(const Point& p) {
  return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z) && std::isfinite(p.visibility) &&
         std::isfinite(p.presence) && std::isfinite(p.weight);
}

// Left/right partner of each landmark.
constexpr std::array<int, kLandmarks> kMirror = {
    Nose, RightEyeInner, RightEye, RightEyeOuter, LeftEyeInner, LeftEye, LeftEyeOuter, RightEar, LeftEar, MouthRight,
    MouthLeft, RightShoulder, LeftShoulder, RightElbow, LeftElbow, RightWrist, LeftWrist, RightPinky, LeftPinky,
    RightIndex, LeftIndex, RightThumb, LeftThumb, RightHip, LeftHip, RightKnee, LeftKnee, RightAnkle, LeftAnkle,
    RightHeel, LeftHeel, RightFootIndex, LeftFootIndex,
};

struct V2 {
  float x = 0, y = 0;
};
V2 operator-(V2 a, V2 b) { return {a.x - b.x, a.y - b.y}; }
V2 operator+(V2 a, V2 b) { return {a.x + b.x, a.y + b.y}; }
V2 operator*(V2 a, float k) { return {a.x * k, a.y * k}; }
float len(V2 v) { return std::sqrt(v.x * v.x + v.y * v.y); }

// Square units: across × aspect, down; 1 = the image height.
V2 sq(const Pose& p, int i) { return {p.points[static_cast<size_t>(i)].x * p.aspect, p.points[static_cast<size_t>(i)].y}; }

float wrap(float a) {
  while (a > kPi) a -= 2 * kPi;
  while (a < -kPi) a += 2 * kPi;
  return a;
}

struct BoneDef {
  int a, b;
};
// Torso is drawn from the shoulder centre to the hip centre (-1, -2 below).
constexpr std::array<BoneDef, kBones> kBoneDefs = {{
    {LeftShoulder, LeftElbow}, {RightShoulder, RightElbow}, {LeftElbow, LeftWrist}, {RightElbow, RightWrist},
    {LeftHip, LeftKnee}, {RightHip, RightKnee}, {LeftKnee, LeftAnkle}, {RightKnee, RightAnkle},
    {-1, -2}, {RightShoulder, LeftShoulder}, {RightHip, LeftHip}, {RightEye, LeftEye},
}};

// Typical lengths, in face-on shoulder widths (shoulder joint to shoulder
// joint), for estimating the body's scale from whatever is seen.
struct Typical {
  int a, b;
  float ratio;
};
constexpr std::array<Typical, 10> kTypical = {{
    {LeftShoulder, RightShoulder, 1.0f},
    {LeftEar, RightEar, 0.42f},
    {LeftEyeOuter, RightEyeOuter, 0.30f},
    {LeftShoulder, LeftElbow, 0.82f},
    {RightShoulder, RightElbow, 0.82f},
    {LeftHip, RightHip, 0.72f},
    {LeftHip, LeftKnee, 1.2f},
    {RightHip, RightKnee, 1.2f},
    {LeftKnee, LeftAnkle, 1.15f},
    {RightKnee, RightAnkle, 1.15f},
}};

}  // namespace

namespace {
// Whether a landmark is inside the picture at all, seen or covered: the
// model's presence and the frame edges, not its visibility (nor how long a
// covered one has been held: a hand over the shoulders leaves them in shot).
float inPicture(const Point& p) {
  if (!finite(p)) return 0.0f;
  const float inFrame = smoothstep(-0.03f, 0.01f, p.x) * smoothstep(-0.03f, 0.01f, 1.0f - p.x) *
                        smoothstep(-0.03f, 0.01f, p.y) * smoothstep(-0.03f, 0.01f, 1.0f - p.y);
  return smoothstep(0.4f, 0.75f, p.presence) * inFrame;
}
}  // namespace

float reliability(const Point& p) {
  if (!finite(p)) return 0.0f;
  return std::clamp(p.weight, 0.0f, 1.0f) * inPicture(p) * smoothstep(0.35f, 0.7f, p.visibility);
}

Pose mirror(const Pose& pose) {
  Pose out = pose;
  for (int i = 0; i < kLandmarks; ++i) {
    Point p = pose.points[static_cast<size_t>(kMirror[static_cast<size_t>(i)])];
    p.x = 1.0f - p.x;
    out.points[static_cast<size_t>(i)] = p;
  }
  return out;
}

Description describe(const Pose& pose) {
  Description d;
  if (!pose.valid || !(pose.aspect > 0.05f && pose.aspect < 20.0f)) return d;
  for (int i = 0; i < kLandmarks; ++i) d.reliability[static_cast<size_t>(i)] = reliability(pose.points[static_cast<size_t>(i)]);
  auto rel = [&](int i) { return d.reliability[static_cast<size_t>(i)]; };
  // Scale: each reliable length divided by its typical share; foreshortening
  // only shortens, so an upper value of the estimates is taken.
  std::vector<float> est;
  for (const Typical& t : kTypical) {
    if (std::min(rel(t.a), rel(t.b)) < 0.5f) continue;
    const float l = len(sq(pose, t.a) - sq(pose, t.b));
    if (l > 1e-4f) est.push_back(l / t.ratio);
  }
  if (est.empty()) return d;
  std::sort(est.begin(), est.end());
  d.scale = est[static_cast<size_t>(std::floor(0.75f * static_cast<float>(est.size() - 1) + 0.5f))];
  // Anchor: the shoulder centre, else the face (nose, a typical distance up).
  V2 anchor;
  if (std::min(rel(LeftShoulder), rel(RightShoulder)) >= 0.3f) {
    anchor = (sq(pose, LeftShoulder) + sq(pose, RightShoulder)) * 0.5f;
    d.anchorFromShoulders = true;
  } else if (rel(Nose) >= 0.3f) {
    anchor = sq(pose, Nose) + V2{0.0f, 1.15f * d.scale};
  } else {
    return d;
  }
  d.anchorX = anchor.x / pose.aspect;
  d.anchorY = anchor.y;
  for (int i = 0; i < kLandmarks; ++i) {
    const V2 v = (sq(pose, i) - anchor) * (1.0f / d.scale);
    d.nx[static_cast<size_t>(i)] = v.x;
    d.ny[static_cast<size_t>(i)] = v.y;
  }
  // Bones.
  const V2 shoulderMid = (sq(pose, LeftShoulder) + sq(pose, RightShoulder)) * 0.5f;
  const V2 hipMid = (sq(pose, LeftHip) + sq(pose, RightHip)) * 0.5f;
  for (int b = 0; b < kBones; ++b) {
    const BoneDef& def = kBoneDefs[static_cast<size_t>(b)];
    V2 from, to;
    float r;
    if (b == Torso) {
      from = shoulderMid, to = hipMid;
      r = std::min({rel(LeftShoulder), rel(RightShoulder), rel(LeftHip), rel(RightHip)});
    } else {
      from = sq(pose, def.a), to = sq(pose, def.b);
      r = std::min(rel(def.a), rel(def.b));
    }
    const V2 v = to - from;
    if (len(v) < 0.05f * d.scale) r = 0.0f;  // too short to have a direction
    d.direction[static_cast<size_t>(b)] = std::atan2(v.y, v.x);
    d.boneReliability[static_cast<size_t>(b)] = r;
  }
  auto bend = [&](int a, int b, int c) {
    const V2 u = sq(pose, a) - sq(pose, b), v = sq(pose, c) - sq(pose, b);
    const float lu = len(u), lv = len(v);
    if (lu < 1e-5f || lv < 1e-5f) return 0.0f;
    const float cosA = std::clamp((u.x * v.x + u.y * v.y) / (lu * lv), -1.0f, 1.0f);
    return kPi - std::acos(cosA);
  };
  d.leftElbowBend = bend(LeftShoulder, LeftElbow, LeftWrist);
  d.rightElbowBend = bend(RightShoulder, RightElbow, RightWrist);
  d.leftKneeBend = bend(LeftHip, LeftKnee, LeftAnkle);
  d.rightKneeBend = bend(RightHip, RightKnee, RightAnkle);
  // Body turn. The model's depth for the shoulders is noisy close up (it
  // swings by ±30° in a selfie held still), so the amount comes from how
  // narrow the shoulders look against the torso's height (which turning
  // does not change) and only the direction from depth. Without the hips
  // (a portrait) the turn is a weak hint.
  {
    const Point& l = pose.points[LeftShoulder];
    const Point& r = pose.points[RightShoulder];
    const float dx = (l.x - r.x) * pose.aspect, dz = (l.z - r.z) * pose.aspect;
    const float relShoulders = std::min(rel(LeftShoulder), rel(RightShoulder));
    // Facing the camera, the left shoulder is on the image's right (dx > 0).
    const float fromDepth = std::atan2(dz, std::fabs(dx) > 1e-5f ? dx : 1e-5f);
    const float relHips = std::min(rel(LeftHip), rel(RightHip));
    if (relHips >= 0.5f && relShoulders >= 0.5f) {
      const float torso = len(shoulderMid - hipMid);
      const float faceOn = torso / 1.3f;  // shoulder width facing the camera
      const float amount = std::acos(std::clamp(std::fabs(dx) / std::max(faceOn, 1e-4f), 0.0f, 1.0f));
      d.bodyYaw = fromDepth >= 0 ? amount : -amount;
      d.bodyYawReliability = 0.9f * std::min(relShoulders, relHips);
    } else {
      d.bodyYaw = fromDepth;
      d.bodyYawReliability = 0.2f * relShoulders;
    }
  }
  // Head: turn (nose against the ears' or eyes' centre) and tilt (eye line).
  {
    int a = -1, b = -1;
    if (std::min(rel(LeftEar), rel(RightEar)) >= 0.5f) a = LeftEar, b = RightEar;
    else if (std::min(rel(LeftEyeOuter), rel(RightEyeOuter)) >= 0.5f) a = LeftEyeOuter, b = RightEyeOuter;
    if (a >= 0 && rel(Nose) >= 0.5f) {
      const V2 mid = (sq(pose, a) + sq(pose, b)) * 0.5f;
      const float half = 0.5f * len(sq(pose, a) - sq(pose, b));
      d.headYaw = std::clamp((sq(pose, Nose).x - mid.x) / std::max(half, 1e-4f), -1.5f, 1.5f);
      d.headReliability = std::min({rel(a), rel(b), rel(Nose)});
    }
    d.headRoll = d.direction[HeadTilt];
  }
  // Framing.
  float top = 2, bottom = -1, left = 2, right = -1;
  for (int i = 0; i < kLandmarks; ++i) {
    if (rel(i) < 0.5f) continue;
    const Point& p = pose.points[static_cast<size_t>(i)];
    top = std::min(top, p.y), bottom = std::max(bottom, p.y), left = std::min(left, p.x), right = std::max(right, p.x);
  }
  d.top = top, d.bottom = bottom, d.left = left, d.right = right;
  // The shot is what the picture holds: a hand over the shoulders does not
  // make it a face shot (and is no reason to step back).
  auto either = [&](int a, int b) {
    return std::max(inPicture(pose.points[static_cast<size_t>(a)]), inPicture(pose.points[static_cast<size_t>(b)])) >= 0.5f;
  };
  d.shot = either(LeftAnkle, RightAnkle)     ? FullBody
           : either(LeftKnee, RightKnee)     ? ThreeQuarter
           : either(LeftHip, RightHip)       ? HalfBody
           : either(LeftShoulder, RightShoulder) ? HeadAndShoulders
                                             : Face;
  d.valid = true;
  return d;
}

namespace {

struct Feature {
  float weight, sigma;
};
// Bone comparisons (radians).
constexpr std::array<Feature, kBones> kBoneFeature = {{
    {1.0f, 0.38f}, {1.0f, 0.38f}, {1.0f, 0.38f}, {1.0f, 0.38f},  // arms
    {0.8f, 0.38f}, {0.8f, 0.38f}, {0.8f, 0.38f}, {0.8f, 0.38f},  // legs
    {0.7f, 0.2f},                                                // torso lean
    {0.4f, 0.17f}, {0.3f, 0.17f},                                // shoulder and hip lines
    {0.5f, 0.17f},                                               // head tilt
}};

Part handPart(bool left) { return left ? Part::LeftHand : Part::RightHand; }

}  // namespace

Match match(const Pose& reference, const Pose& live, const MatchParams& params) {
  Match m;
  m.boneScore.fill(-1.0f);
  const Description R = describe(reference);
  const Description L = describe(live);
  if (!L.valid) {
    m.advice.push_back({Part::Body, Action::NoPerson, 0});
    return m;
  }
  if (!R.valid) return m;
  const float k = 1.3f - 0.6f * std::clamp(params.strictness, 0.0f, 1.0f);
  // A geometric average of the per-part agreement, each part's penalty
  // capped: one clearly wrong limb pulls the score down plainly (an average
  // would let the parts that match hide it), small differences stay gentle.
  double logSum = 0, den = 0, refWeight = 0;
  auto add = [&](float weight, float error, float sigma) {
    const float z = std::min(error / sigma, 2.5f);
    logSum += weight * (-0.5 * static_cast<double>(z) * z);
    den += weight;
    return std::exp(-0.5f * z * z);
  };
  struct Candidate {
    Advice advice;
    float priority;
  };
  std::vector<Candidate> pose;
  // Bones.
  for (int b = 0; b < kBones; ++b) {
    const Feature f = kBoneFeature[static_cast<size_t>(b)];
    const float rr = R.boneReliability[static_cast<size_t>(b)], rl = L.boneReliability[static_cast<size_t>(b)];
    refWeight += f.weight * rr;
    if (rr <= 0.0f || rl <= 0.0f) continue;
    const float e = std::fabs(wrap(R.direction[static_cast<size_t>(b)] - L.direction[static_cast<size_t>(b)]));
    m.boneScore[static_cast<size_t>(b)] = add(f.weight * rr * rl, e, f.sigma * k);
  }
  // Hands: where they are relative to the shoulders and face (hand to chin,
  // hand on hip), in body units — when both poses are anchored alike.
  const bool sameAnchor = R.anchorFromShoulders == L.anchorFromShoulders;
  auto joint = [&](int i, float weight, float sigma, float* scoreOut) {
    const float rr = R.reliability[static_cast<size_t>(i)], rl = L.reliability[static_cast<size_t>(i)];
    refWeight += weight * rr;
    if (!sameAnchor || rr <= 0.0f || rl <= 0.0f) return;
    const float dx = R.nx[static_cast<size_t>(i)] - L.nx[static_cast<size_t>(i)];
    const float dy = R.ny[static_cast<size_t>(i)] - L.ny[static_cast<size_t>(i)];
    const float s = add(weight * rr * rl, std::sqrt(dx * dx + dy * dy), sigma * k);
    if (scoreOut != nullptr) *scoreOut = s;
  };
  joint(LeftWrist, 1.2f, 0.45f, &m.leftHandScore);
  joint(RightWrist, 1.2f, 0.45f, &m.rightHandScore);
  // Turn of body and head.
  {
    const float rr = R.bodyYawReliability, rl = L.bodyYawReliability;
    refWeight += 0.6f * rr;
    if (rr > 0 && rl > 0) {
      add(0.6f * rr * rl, std::fabs(wrap(R.bodyYaw - L.bodyYaw)), 0.45f * k);
    }
    const float hr = R.headReliability, hl = L.headReliability;
    refWeight += 0.5f * hr;
    if (hr > 0 && hl > 0) {
      add(0.5f * hr * hl, std::fabs(R.headYaw - L.headYaw), 0.35f * k);
    }
  }
  m.poseScore = den > 1e-6 ? static_cast<float>(100.0 * std::exp(logSum / den)) : 0.0f;
  m.coverage = refWeight > 1e-6 ? static_cast<float>(std::min(1.0, den / refWeight)) : 0.0f;
  // Framing: the same anchor in both, else the face.
  std::vector<Candidate> framing;
  {
    float lx = L.anchorX, ly = L.anchorY, rx = R.anchorX, ry = R.anchorY;
    if (!sameAnchor) {
      lx = live.points[Nose].x, ly = live.points[Nose].y, rx = reference.points[Nose].x, ry = reference.points[Nose].y;
    }
    m.offsetX = lx - rx, m.offsetY = ly - ry;
    m.sizeRatio = L.scale / std::max(R.scale, 1e-4f);
    const float lr = std::log(std::max(m.sizeRatio, 1e-3f));
    m.framingScore = 100.0f * std::exp(-0.5f * ((m.offsetX / 0.08f) * (m.offsetX / 0.08f) +
                                               (m.offsetY / 0.08f) * (m.offsetY / 0.08f) + (lr / 0.18f) * (lr / 0.18f)));
  }
  const float base = params.framing ? 0.8f * m.poseScore + 0.2f * m.framingScore : m.poseScore;
  // What could not be compared counts against the match: a pose whose
  // point is the hands is not matched by a view without hands.
  m.score = std::clamp(base * (0.35f + 0.65f * m.coverage), 0.0f, 100.0f);

  // ---- Advice.
  if (params.framing) {
    // More of the body in the reference than in view: step back.
    if (R.shot > L.shot && L.shot < FullBody && m.sizeRatio > 0.9f) {
      framing.push_back({{Part::Body, Action::Back, static_cast<float>(R.shot - L.shot)}, 10.0f});
    } else if (m.sizeRatio > 1.18f) {
      framing.push_back({{Part::Body, Action::Back, m.sizeRatio - 1.0f}, 9.0f});
    } else if (m.sizeRatio < 0.85f) {
      framing.push_back({{Part::Body, Action::Closer, 1.0f / m.sizeRatio - 1.0f}, 9.0f});
    }
    if (std::fabs(m.offsetX) > 0.07f) {
      framing.push_back({{Part::Body, m.offsetX > 0 ? Action::MoveLeft : Action::MoveRight, std::fabs(m.offsetX)}, 8.0f});
    }
    if (std::fabs(m.offsetY) > 0.07f) {
      // Too low in the frame: aim the camera down (the person rises in it).
      framing.push_back({{Part::Camera, m.offsetY > 0 ? Action::CameraDown : Action::CameraUp, std::fabs(m.offsetY)}, 7.0f});
    }
  }
  // A hand in the reference that cannot be seen now.
  for (bool left : {true, false}) {
    const int w = left ? LeftWrist : RightWrist;
    if (R.reliability[static_cast<size_t>(w)] >= 0.5f && L.reliability[static_cast<size_t>(w)] < 0.2f) {
      pose.push_back({{handPart(left), Action::Show, 1.0f}, 6.0f});
    }
  }
  // Body turn.
  if (R.bodyYawReliability > 0.3f && L.bodyYawReliability > 0.3f) {
    const float e = wrap(R.bodyYaw - L.bodyYaw);
    if (std::fabs(e) > 0.35f) pose.push_back({{Part::Body, e > 0 ? Action::TurnRight : Action::TurnLeft, std::fabs(e)}, 4.0f * std::fabs(e)});
  }
  // Joints to move: where the reference puts them (body units), against now.
  struct JointMove {
    int index;
    Part part;
    float weight;
  };
  const JointMove moves[] = {
      {LeftWrist, Part::LeftHand, 1.2f}, {RightWrist, Part::RightHand, 1.2f},
      {LeftElbow, Part::LeftElbow, 0.6f}, {RightElbow, Part::RightElbow, 0.6f},
      {LeftAnkle, Part::LeftFoot, 0.8f}, {RightAnkle, Part::RightFoot, 0.8f},
      {LeftKnee, Part::LeftKnee, 0.5f}, {RightKnee, Part::RightKnee, 0.5f},
  };
  if (sameAnchor) {
    for (const JointMove& j : moves) {
      const size_t i = static_cast<size_t>(j.index);
      if (R.reliability[i] < 0.5f || L.reliability[i] < 0.3f) continue;
      const float dx = R.nx[i] - L.nx[i], dy = R.ny[i] - L.ny[i];
      const float e = std::sqrt(dx * dx + dy * dy);
      if (e < 0.35f * k) continue;
      Action a;
      float amount;
      if (std::fabs(dy) >= std::fabs(dx)) {
        a = dy < 0 ? Action::Up : Action::Down, amount = std::fabs(dy);
      } else {
        // In or out: toward the body's centre line (the anchor) or away.
        const bool towardCentre = std::fabs(R.nx[i]) < std::fabs(L.nx[i]);
        a = towardCentre ? Action::In : Action::Out, amount = std::fabs(dx);
      }
      pose.push_back({{j.part, a, amount}, j.weight * e * 3.0f});
    }
  }
  // Lean, head tilt and turn, shoulders.
  auto boneDelta = [&](int b) { return wrap(R.direction[static_cast<size_t>(b)] - L.direction[static_cast<size_t>(b)]); };
  if (R.boneReliability[Torso] > 0.3f && L.boneReliability[Torso] > 0.3f) {
    const float e = boneDelta(Torso);
    // The torso points down from the shoulders; a larger angle puts the
    // shoulders further to the image's right of the hips.
    if (std::fabs(e) > 0.2f) pose.push_back({{Part::Body, e > 0 ? Action::LeanRight : Action::LeanLeft, std::fabs(e)}, 5.0f * std::fabs(e)});
  }
  if (R.boneReliability[HeadTilt] > 0.3f && L.boneReliability[HeadTilt] > 0.3f) {
    const float e = boneDelta(HeadTilt);
    if (std::fabs(e) > 0.15f) pose.push_back({{Part::Head, e > 0 ? Action::TiltRight : Action::TiltLeft, std::fabs(e)}, 4.0f * std::fabs(e)});
  }
  if (R.headReliability > 0.3f && L.headReliability > 0.3f) {
    const float e = R.headYaw - L.headYaw;
    if (std::fabs(e) > 0.3f) pose.push_back({{Part::Head, e > 0 ? Action::LookRight : Action::LookLeft, std::fabs(e)}, 3.0f * std::fabs(e)});
  }
  if (R.boneReliability[ShoulderLine] > 0.3f && L.boneReliability[ShoulderLine] > 0.3f &&
      std::fabs(R.direction[ShoulderLine]) < 0.08f && std::fabs(boneDelta(ShoulderLine)) > 0.15f) {
    pose.push_back({{Part::Shoulders, Action::StraightenUp, std::fabs(boneDelta(ShoulderLine))}, 2.0f});
  }
  std::stable_sort(framing.begin(), framing.end(), [](const Candidate& a, const Candidate& b) { return a.priority > b.priority; });
  std::stable_sort(pose.begin(), pose.end(), [](const Candidate& a, const Candidate& b) { return a.priority > b.priority; });
  // Framing first (it moves everything), then the biggest pose changes.
  for (const Candidate& c : framing) {
    if (m.advice.size() < 2) m.advice.push_back(c.advice);
  }
  for (const Candidate& c : pose) {
    if (m.advice.size() < 4) m.advice.push_back(c.advice);
  }
  if (m.advice.empty() && m.score >= 80.0f) m.advice.push_back({Part::Body, Action::Perfect, 0});
  return m;
}

// ------------------------------------------------------------------ tracker

void Tracker::reset() {
  last_ = {};
  seenAt_.fill(-1);
  lastRel_.fill(0);
  lastTime_ = -1;
  personSeenAt_ = -1;
  stillness_ = 0;
  previous_ = Pose{};
}

Pose Tracker::update(const Pose& pose, int64_t timeMs) {
  if (lastTime_ < 0) {
    seenAt_.fill(-1);
    lastRel_.fill(0);
  }
  const int64_t dt = lastTime_ >= 0 ? std::max<int64_t>(1, timeMs - lastTime_) : 33;
  lastTime_ = timeMs;
  if (!pose.valid) {
    // Nobody this frame: the last pose, fading, for the hold time.
    if (previous_.valid && personSeenAt_ >= 0 && timeMs - personSeenAt_ < kHoldMs) {
      Pose out = previous_;
      const float fade = 1.0f - static_cast<float>(timeMs - personSeenAt_) / static_cast<float>(kHoldMs);
      for (auto& p : out.points) p.weight = std::min(p.weight, fade);
      stillness_ *= 0.8f;
      return out;
    }
    stillness_ = 0;
    previous_ = Pose{};
    return Pose{};
  }
  personSeenAt_ = timeMs;
  aspect_ = pose.aspect;
  // How the body moved since the last frame (reliable landmarks seen in
  // both): held landmarks travel with it.
  float mx = 0, my = 0, speed = 0;
  int moved = 0;
  for (int i = 0; i < kLandmarks; ++i) {
    const size_t s = static_cast<size_t>(i);
    const Point& p = pose.points[s];
    if (reliability(p) >= 0.5f && seenAt_[s] >= 0 && timeMs - seenAt_[s] <= dt + 1) {
      const float dx = p.x - last_[s].x, dy = p.y - last_[s].y;
      mx += dx, my += dy, speed += std::sqrt(dx * dx * pose.aspect * pose.aspect + dy * dy), ++moved;
    }
  }
  if (moved > 0) mx /= static_cast<float>(moved), my /= static_cast<float>(moved), speed /= static_cast<float>(moved);
  Pose out = pose;
  for (int i = 0; i < kLandmarks; ++i) {
    const size_t s = static_cast<size_t>(i);
    const Point& p = pose.points[s];
    const float r = reliability(p);
    if (r >= 0.5f) {
      last_[s] = p, seenAt_[s] = timeMs, lastRel_[s] = r;
      continue;
    }
    // Hidden or gone for a moment: where it was, moved with the body,
    // trusted less as time passes.
    if (seenAt_[s] >= 0 && timeMs - seenAt_[s] < kHoldMs && lastRel_[s] >= 0.5f) {
      last_[s].x += mx, last_[s].y += my;
      const float fade = 1.0f - static_cast<float>(timeMs - seenAt_[s]) / static_cast<float>(kHoldMs);
      Point held = last_[s];
      held.weight = fade;
      if (reliability(held) > r) out.points[s] = held;
    }
  }
  // Stillness from the body's speed, in shoulder widths per second.
  const Description d = describe(out);
  if (moved > 0 && d.valid && d.scale > 1e-4f) {
    const float perSecond = speed / d.scale * 1000.0f / static_cast<float>(dt);
    const float now = 1.0f - smoothstep(0.08f, 0.5f, perSecond);
    stillness_ += 0.35f * (now - stillness_);
  }
  previous_ = out;
  return out;
}

}  // namespace photoplus::pose
