// pose_demo — runs the pose assistant on landmark files (developer tool).
//
//   pose_demo REFERENCE.txt LIVE.txt [--mirror-reference] [--no-framing] [--strictness v] [--fps n]
//
// Each line of a landmark file is one frame: the image aspect (width /
// height) then 33 × (x y z visibility presence), or the word "none" when
// nobody was found. The reference file's first line is the reference pose.
// For each live frame (tracked as a clip at --fps, default 30) prints one
// JSON line: the match, the advice and the steadied landmarks.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "photoplus/pp_engine.h"

namespace {

bool readPose(const std::string& line, pp_pose& out) {
  out = pp_pose{};
  std::istringstream in(line);
  std::string first;
  if (!(in >> first) || first == "none") return true;
  out.aspect = std::strtof(first.c_str(), nullptr);
  for (auto& p : out.points) {
    if (!(in >> p.x >> p.y >> p.z >> p.visibility >> p.presence)) return false;
  }
  out.valid = 1;
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::fprintf(stderr, "usage: pose_demo REFERENCE.txt LIVE.txt [--mirror-reference] [--no-framing] [--strictness v] [--fps n]\n");
    return 2;
  }
  bool mirrorRef = false;
  pp_pose_match_params params{};
  params.framing = 1;
  params.strictness = 0.5f;
  float fps = 30;
  for (int i = 3; i < argc; ++i) {
    if (!std::strcmp(argv[i], "--mirror-reference")) mirrorRef = true;
    else if (!std::strcmp(argv[i], "--no-framing")) params.framing = 0;
    else if (!std::strcmp(argv[i], "--strictness") && i + 1 < argc) params.strictness = std::strtof(argv[++i], nullptr);
    else if (!std::strcmp(argv[i], "--fps") && i + 1 < argc) fps = std::strtof(argv[++i], nullptr);
  }
  std::ifstream refFile(argv[1]), liveFile(argv[2]);
  std::string line;
  pp_pose reference{};
  if (!std::getline(refFile, line) || !readPose(line, reference) || !reference.valid) {
    std::fprintf(stderr, "no reference pose\n");
    return 1;
  }
  if (mirrorRef) {
    pp_pose m{};
    pp_pose_mirror(&reference, &m);
    reference = m;
  }
  pp_pose_description rd{};
  pp_pose_describe(&reference, &rd);
  std::fprintf(stderr, "reference: shot %d, scale %.3f, anchor (%.3f, %.3f)\n", rd.shot, static_cast<double>(rd.scale),
               static_cast<double>(rd.anchor_x), static_cast<double>(rd.anchor_y));
  pp_pose_tracker* tracker = nullptr;
  pp_pose_tracker_create(&tracker);
  int64_t frame = 0;
  while (std::getline(liveFile, line)) {
    pp_pose live{};
    if (!readPose(line, live)) {
      std::fprintf(stderr, "bad line %lld\n", static_cast<long long>(frame));
      return 1;
    }
    const auto ms = static_cast<int64_t>(static_cast<double>(frame) * 1000.0 / static_cast<double>(fps));
    pp_pose steady{};
    float still = 0;
    pp_pose_tracker_update(tracker, &live, ms, &steady, &still);
    pp_pose_match_result r{};
    pp_pose_match(&reference, &steady, &params, &r);
    pp_pose_description d{};
    pp_pose_describe(&steady, &d);
    std::printf("{\"frame\":%lld,\"score\":%.1f,\"pose\":%.1f,\"framing\":%.1f,\"coverage\":%.3f,\"stillness\":%.2f,"
                "\"offset\":[%.3f,%.3f],\"size\":%.3f,\"shot\":%d,\"advice\":[",
                static_cast<long long>(frame), static_cast<double>(r.score), static_cast<double>(r.pose_score),
                static_cast<double>(r.framing_score), static_cast<double>(r.coverage), static_cast<double>(still),
                static_cast<double>(r.offset_x), static_cast<double>(r.offset_y), static_cast<double>(r.size_ratio), d.shot);
    for (int i = 0; i < r.advice_count; ++i) {
      std::printf("%s[%d,%d,%.3f]", i ? "," : "", r.advice[i].part, r.advice[i].action, static_cast<double>(r.advice[i].amount));
    }
    std::printf("],\"bones\":[");
    for (int b = 0; b < PP_POSE_BONES; ++b) std::printf("%s%.2f", b ? "," : "", static_cast<double>(r.bone_score[b]));
    std::printf("],\"hands\":[%.2f,%.2f],\"points\":[", static_cast<double>(r.left_hand_score), static_cast<double>(r.right_hand_score));
    for (int i = 0; i < PP_POSE_LANDMARKS; ++i) {
      std::printf("%s[%.4f,%.4f,%.3f]", i ? "," : "", static_cast<double>(steady.points[i].x),
                  static_cast<double>(steady.points[i].y), static_cast<double>(steady.valid ? d.reliability[i] : 0.0f));
    }
    std::printf("]}\n");
    ++frame;
  }
  pp_pose_tracker_destroy(tracker);
  return 0;
}
