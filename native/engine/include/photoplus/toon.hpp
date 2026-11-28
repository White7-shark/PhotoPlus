#pragma once

// Toon: a photo turned into anime or a cartoon — not a filter over the
// photo, but the way those pictures are made:
//
//  - the photo cleaned first (noise, low light, flat contrast), so sensor
//    grain never becomes blotches of colour;
//  - the person and the scene drawn apart (the person mask, refined to the
//    photo's own edges), the scene simplified so it never competes;
//  - flat colour areas from a palette found in Lab (k-means), skin in its
//    own hue-true tones (shadow, base, highlight) with no white shine;
//  - clean line art (extended difference of Gaussians, gated to real edges,
//    specks removed) at the same size and place as the colour;
//  - the face drawn, not traced: from its contour points the eyes (white,
//    iris, lash line, catchlight), brows, nostrils and lips, so no feature
//    is ever filled with black; without contours, a light line pass instead;
//  - hair given back its shape (local contrast) and strands;
//  - each style's own finish (presets.hpp): screentone and dots only in
//    shadows and the scene, bloom, period palette, grain.
//
// Styles: anime, manga, comic, cartoon, webtoon, retro (90s).
// Deterministic for a seed, parallel by rows, cancellable.

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/art.hpp"
#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

namespace photoplus::toon {

enum class Style : int32_t { Anime = 0, Manga, Comic, Cartoon, Webtoon, Retro };
constexpr int32_t kStyleCount = 6;

enum class Background : int32_t { Keep = 0, Soft, Sky, Sunset, Night, Plain };
constexpr int32_t kBackgroundCount = 6;

// A face's contour points (normalised x, y; negative where unknown), in ML
// Kit's contour order: the face oval (36), left brow top and bottom (5 + 5),
// right brow top and bottom (5 + 5), left eye (16), right eye (16), upper lip
// top (11) and bottom (9), lower lip top (9) and bottom (9), nose bridge (2)
// and nose bottom (3). "Left" and "right" may be either side: only the shapes matter.
namespace contour {
constexpr int kOval = 0, kLeftBrowTop = 36, kLeftBrowBottom = 41, kRightBrowTop = 46, kRightBrowBottom = 51, kLeftEye = 56,
              kRightEye = 72, kUpperLipTop = 88, kUpperLipBottom = 99, kLowerLipTop = 108, kLowerLipBottom = 117, kNoseBridge = 126,
              kNoseBottom = 128, kPoints = 131;
}  // namespace contour
struct FaceContour {
  std::array<float, contour::kPoints * 2> p{};
};

struct Options {
  Style style = Style::Anime;
  Background background = Background::Keep;
  float lines = 0.5f;   // 0 fine .. 1 bold ink lines
  int32_t shades = 3;   // cel-shading bands, 2..6
  float vivid = 0.3f;   // -1 muted .. 1 vivid colour
  float detail = 0.5f;  // 0 simplest shapes .. 1 most detail kept
  float eyes = 0.4f;    // 0 the person's own eyes .. 1 big anime eyes (with catchlights)
  float skin = 0.6f;    // 0 the skin's own texture .. 1 smooth, two-tone anime skin
  float glow = 0.3f;    // 0 .. 1 a soft glow around the lights
  float strength = 1;   // 0 the photo .. 1 fully drawn (blended)
  // Finer controls (0.5 / 0: the style's own look).
  float outline = 0.5f;     // 0 no outline around the person .. 1 twice as bold
  float backdrop = 0.5f;    // 0 the kept scene crisp .. 1 very soft
  float brightness = 0;     // -1 darker .. 1 brighter
  float warmth = 0;         // -1 cooler .. 1 warmer
  float texture = 0.5f;     // 0 no dots, screentone or grain .. 1 twice as much
  float faceDetail = 0.5f;  // 0 the lightest face lines .. 1 the boldest
  float hairDetail = 0.5f;  // 0 smooth hair .. 1 every strand
  int32_t ink = 0;          // 0 the style's own ink, 1 black, 2 brown, 3 a darker shade of the colour
  uint32_t seed = 1;
  // Optional person mask (GRAY8, any size; 255 = the person): the outline, the backdrop.
  const ImageBuffer* subject = nullptr;
  // Faces (normalised), with the eye centres and mouth when known.
  std::vector<art::FaceBox> faces;
  // Their contour points, when known (any order; matched to a face by position).
  std::vector<FaceContour> contours;
};

// Renders `image` (RGBA8 or BGRA8) in place. Alpha is kept.
Error render(ImageBuffer& image, const Options& options, const CancellationToken* token);

}  // namespace photoplus::toon
