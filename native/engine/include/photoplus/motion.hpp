#pragma once

// Photo Motion: a still photo brought to life as a seamless video loop.
//
//  - Camera: a 3D move through the photo (push in, pull out, orbit, pan,
//    dolly zoom, float, rise). The photo's depth makes near things move more
//    than far ones (parallax); the person is a separate layer, and the
//    background behind them is filled in, so moving reveals no holes.
//  - Living motion: the person's motion arrows (water, hair, smoke, a
//    dress) and anchors (what must stay still), and a sky that drifts by
//    itself — an Eulerian flow field looped seamlessly by two cross-faded
//    phases.
//  - Effects: snow, rain, bokeh lights, sparkles, petals, fireflies, a light
//    leak, film, sun rays — loops too, some falling behind the person.
//
// The same parameters render the same frame (deterministic for a seed);
// render(t) for t in [0, 1) is one loop.

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

namespace photoplus::motion {

enum class Camera : int32_t { Still = 0, PushIn, PullOut, Orbit, Pan, DollyZoom, Float, Rise };
constexpr int32_t kCameraCount = 8;

enum class Effect : int32_t { None = 0, Snow, Rain, Bokeh, Sparkles, Petals, Fireflies, LightLeak, Film, SunRays };
constexpr int32_t kEffectCount = 10;

struct Params {
  Camera camera = Camera::Float;
  float cameraAmount = 0.5f;  // 0..1
  float flowSpeed = 1.0f;     // 0..2: living motion's speed
  bool sky = false;           // the sky drifts (found automatically)
  float skySpeed = 0.5f;      // -1..1: direction (left / right) and speed
  Effect effect = Effect::None;
  float effectAmount = 0.6f;  // 0..1
  bool loop = true;           // a seamless loop (the camera returns); else one move
  uint32_t seed = 1;
};

// A motion arrow (normalised 0..1): what is at (x0, y0) moves towards (x1, y1) over one loop.
struct Arrow {
  float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};
// An anchor (normalised): nothing within radius r (share of the short side) moves.
struct Anchor {
  float x = 0, y = 0, r = 0.06f;
};

struct Input {
  const ImageBuffer* photo = nullptr;    // RGBA8 or BGRA8
  const ImageBuffer* depth = nullptr;    // optional GRAY8, 255 near .. 0 far, any size
  const ImageBuffer* subject = nullptr;  // optional GRAY8 person mask, any size
  std::vector<Arrow> arrows;
  std::vector<Anchor> anchors;
  Params params;
};

class Scene;

// Prepares a photo for animating (layers, filled background, flow field).
// On success `out` owns a new scene (free it with destroy); null on error.
Error create(const Input& input, Scene*& out, const CancellationToken* token);
// One frame at t in [0, 1) into `out` (same size and format as the photo).
Error render(const Scene& scene, float t, ImageBuffer& out, const CancellationToken* token);
// The sky found (0..1 share of the picture), for the app to offer sky motion.
float skyShare(const Scene& scene);
void destroy(Scene* scene);

}  // namespace photoplus::motion
