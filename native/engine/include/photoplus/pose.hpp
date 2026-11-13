#pragma once

#include <array>
#include <cstdint>
#include <vector>

// Pose assistant: the person's pose as body landmarks, a representation of
// it that does not depend on where they stand or how big they are in the
// frame, a comparison with a reference pose (a score, which parts differ,
// and what to do about it), and a tracker that keeps guidance steady when
// parts of the body are briefly hidden.
//
// Landmarks are the 33 of MediaPipe's pose landmarker (BlazePose topology),
// in normalised image coordinates (x across, y down, 0..1, upright image),
// each with:
//   - visibility: the likelihood it is not hidden;
//   - presence: the likelihood it is in the picture at all.
// The model also guesses positions for parts outside the picture; presence
// (and the frame edge) tells them apart.
//
// "Left" and "right" are the person's own, as the model labels them in the
// image the pose comes from (facing the camera, their left is on the
// image's right). Comparisons happen in display space: a mirrored selfie
// preview is given as mirrored landmarks (mirror(): flipped, and left and
// right swapped), so the reference and the person line up as the user sees
// them; the app names sides accordingly.
namespace photoplus::pose {

constexpr int kLandmarks = 33;

enum Landmark : int32_t {
  Nose = 0,
  LeftEyeInner, LeftEye, LeftEyeOuter, RightEyeInner, RightEye, RightEyeOuter,
  LeftEar, RightEar, MouthLeft, MouthRight,
  LeftShoulder, RightShoulder, LeftElbow, RightElbow, LeftWrist, RightWrist,
  LeftPinky, RightPinky, LeftIndex, RightIndex, LeftThumb, RightThumb,
  LeftHip, RightHip, LeftKnee, RightKnee, LeftAnkle, RightAnkle,
  LeftHeel, RightHeel, LeftFootIndex, RightFootIndex,
};

struct Point {
  float x = 0, y = 0, z = 0;  // normalised image coordinates; z: relative depth (x's scale, smaller = nearer)
  float visibility = 0;       // 0..1
  float presence = 0;         // 0..1
  float weight = 1;           // extra trust factor (the tracker lowers it for landmarks it is holding)
};

struct Pose {
  std::array<Point, kLandmarks> points{};
  float aspect = 1;  // image width / height
  bool valid = false;
};

// A mirrored pose: x flipped and left/right swapped (the same pose as seen
// in a mirror, or done with the other hand).
Pose mirror(const Pose& pose);

// How much each landmark can be trusted: presence × visibility, and inside
// the picture.
float reliability(const Point& p);

// Bones and features compared between poses.
enum Bone : int32_t {
  LeftUpperArm = 0, RightUpperArm, LeftForearm, RightForearm,
  LeftThigh, RightThigh, LeftShin, RightShin,
  Torso, ShoulderLine, HipLine, HeadTilt,
  kBones,
};

enum Shot : int32_t { Face = 0, HeadAndShoulders, HalfBody, ThreeQuarter, FullBody };

struct Description {
  bool valid = false;
  // The body's own frame: centred on the shoulders (always seen in
  // portraits), in units of the body's scale (a face-on shoulder width).
  float anchorX = 0, anchorY = 0;  // shoulder centre (or the face's), normalised image coordinates
  bool anchorFromShoulders = false;
  float scale = 0;                 // shoulder width, as a share of the image height
  // Each landmark relative to the anchor, in body units (x across, y down,
  // the image's aspect taken into account), with its reliability.
  std::array<float, kLandmarks> nx{}, ny{}, reliability{};
  // Bone directions in the image plane (radians, 0 = pointing right,
  // +π/2 = pointing down), their reliability, and the elbow/knee bends.
  std::array<float, kBones> direction{}, boneReliability{};
  float leftElbowBend = 0, rightElbowBend = 0, leftKneeBend = 0, rightKneeBend = 0;  // radians, 0 = straight
  // Orientation: body turn (radians, 0 = facing the camera, + = turned
  // toward the image's left), head turn (−1..1) and tilt (radians).
  float bodyYaw = 0, headYaw = 0, headRoll = 0;
  float bodyYawReliability = 0, headReliability = 0;
  // Framing.
  Shot shot = Face;
  float top = 0, bottom = 0, left = 0, right = 0;  // reliable landmarks' extent (normalised)
};

Description describe(const Pose& pose);

// What to do, as codes the app words for the user.
enum class Part : int32_t {
  Body = 0, Head, LeftHand, RightHand, LeftElbow, RightElbow, LeftFoot, RightFoot, LeftKnee, RightKnee, Shoulders, Camera,
};
enum class Action : int32_t {
  NoPerson = 0,   // nobody in view
  Show,           // bring the part into view
  Closer,         // come closer (or zoom in)
  Back,           // step back
  MoveLeft,       // move toward the image's left
  MoveRight,
  CameraUp,       // aim the camera higher (the person sits too high in the frame... is too low)
  CameraDown,
  TurnLeft,       // turn the body toward the image's left
  TurnRight,
  Up,             // move the part up
  Down,
  In,             // toward the body's centre line
  Out,            // away from it
  TiltLeft,       // tilt the head toward the image's left
  TiltRight,
  LookLeft,       // turn the head toward the image's left
  LookRight,
  LeanLeft,       // lean the upper body toward the image's left
  LeanRight,
  StraightenUp,   // level the shoulders
  HoldStill,
  Perfect,        // nothing to change
};

struct Advice {
  Part part = Part::Body;
  Action action = Action::NoPerson;
  float amount = 0;  // how far (body units, radians or a share of the frame, by action)
};

struct MatchParams {
  bool framing = true;  // compare where the person is in the frame and how big
  float strictness = 0.5f;  // 0..1: tolerances shrink as it grows
};

struct Match {
  float score = 0;      // 0..100: how well the pose matches (over what can be seen)
  float coverage = 0;   // 0..1: share of the reference that could be compared
  float poseScore = 0;  // 0..100 body pose only
  float framingScore = 0;  // 0..100 position and size in the frame
  std::array<float, kBones> boneScore{};  // 0..1 per bone (−1: not compared)
  float leftHandScore = -1, rightHandScore = -1;
  float offsetX = 0, offsetY = 0;  // where the person is minus where the reference is (share of the frame)
  float sizeRatio = 1;             // the person's size / the reference's
  std::vector<Advice> advice;      // most important first (at most 4)
};

Match match(const Pose& reference, const Pose& live, const MatchParams& params);

// Keeps guidance steady through brief occlusions: a landmark that drops out
// (a hand passing behind the head, a knee out of the frame for a moment) is
// held where it was last seen for a short time, trusted less and less; and
// reports how still the person is (for "hold still" and automatic capture).
class Tracker {
 public:
  // `pose` may be invalid (nobody detected). Returns the steadied pose.
  Pose update(const Pose& pose, int64_t timeMs);
  float stillness() const { return stillness_; }  // 0..1 (1 = still)
  void reset();
  static constexpr int64_t kHoldMs = 800;

 private:
  std::array<Point, kLandmarks> last_{};
  std::array<int64_t, kLandmarks> seenAt_{};
  std::array<float, kLandmarks> lastRel_{};
  int64_t lastTime_ = -1;
  int64_t personSeenAt_ = -1;
  float aspect_ = 1;
  float stillness_ = 0;
  Pose previous_{};
};

}  // namespace photoplus::pose
