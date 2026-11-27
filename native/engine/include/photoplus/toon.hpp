#pragma once

// Toon: a photo turned into anime or a cartoon — not a filter over the
// photo, but the way those pictures are made: shapes simplified into flat
// colour areas, cel shading in a few bands (two on skin), clean ink line
// art with a bold outline around the person, bright eyes with catchlights,
// and a background in the same hand (an anime sky with cel-shaded clouds, a
// sunset, a starry night, a soft-focus or plain backdrop).
//
// Styles: anime (cel-shaded, bright, warm ink), manga (black ink and
// screentone on white), comic (bold ink, flat colour and halftone dots),
// cartoon (very flat, thick outlines, bold colour), webtoon (soft shading,
// coloured lines, pastel glow), retro (90s cel anime: muted colour, brown
// lines, film grain).
//
// Deterministic for a seed, parallel by rows, cancellable.

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
  uint32_t seed = 1;
  // Optional person mask (GRAY8, any size; 255 = the person): the outline, the backdrop.
  const ImageBuffer* subject = nullptr;
  // Faces (normalised), with the eye centres and mouth when known.
  std::vector<art::FaceBox> faces;
};

// Renders `image` (RGBA8 or BGRA8) in place. Alpha is kept.
Error render(ImageBuffer& image, const Options& options, const CancellationToken* token);

}  // namespace photoplus::toon
