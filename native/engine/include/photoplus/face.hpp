#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

// Face-precise portrait adjustments driven by facial landmarks (Phase 5).
// The landmark layout matches on-device face contour detection; see
// PP_FACE_* in pp_engine.h for the index ranges.
namespace photoplus::portrait {

constexpr int kFacePoints = 131;
constexpr int kOval = 0, kLeftBrowTop = 36, kLeftBrowBottom = 41, kRightBrowTop = 46, kRightBrowBottom = 51;
constexpr int kLeftEye = 56, kRightEye = 72, kUpperLipTop = 88, kUpperLipBottom = 99, kLowerLipTop = 108;
constexpr int kLowerLipBottom = 117, kNoseBridge = 126, kNoseBottom = 128;

struct FaceLandmarks {
  std::array<float, kFacePoints * 2> points{};  // x, y normalised to the image
  bool detailed = true;                         // false: approximated outlines (gentler feature edits)
};

struct FaceParams {
  float eyes = 0, teeth = 0, lips = 0, underEye = 0, shine = 0, slim = 0, faceLight = 0, studio = 0;
  float lightAngle = 0;  // -1 picture left .. 1 right
  bool any() const;
};

struct FaceReport {
  int32_t faces = 0;
  int32_t adjusted = 0;
  float lightGain = 1.0f;
};

Error adjustFaces(ImageBuffer& image, const std::vector<FaceLandmarks>& faces, const FaceParams& params,
                  const CancellationToken* token, FaceReport* report);

}  // namespace photoplus::portrait
