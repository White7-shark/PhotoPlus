#pragma once

// Masters: a photo painted the way twenty great artists worked — not a
// filter over the photo, but a painting made from it: a toned ground, then
// brush strokes from broad to fine that follow the forms (each with bristle
// marks, paint that thins at its tail and piles up in impasto), the
// artist's palette and light, and the finish of the real thing (canvas
// weave, varnish, craquelure; chalk on laid paper; an engraver's lines; a
// woodblock's flat colours and key lines; gold leaf).
//
// Deterministic for a given seed, parallel by tiles, cancellable.

#include <array>
#include <cstdint>
#include <vector>

#include "photoplus/cancellation.hpp"
#include "photoplus/image.hpp"
#include "photoplus/status.hpp"

namespace photoplus::art {

enum class Artist : int32_t {
  Leonardo = 0,  // sfumato: smoke-soft modelling, umber and olive, varnish and fine cracks
  Michelangelo,  // red chalk study on cream laid paper, sculptural cross-contour hatching
  Raphael,       // serene, luminous harmony; clear ultramarine and vermilion; invisible brushwork
  Durer,         // copper engraving: swelling burin lines, cross-hatching, on white paper
  Titian,        // warm Venetian glazes: deep reds and golds, broad soft strokes
  Caravaggio,    // tenebrism: figure from a black ground in one hard raking light
  Rembrandt,     // chiaroscuro: golden light, thick impasto in the lights, glazed brown shadows
  Vermeer,       // window daylight, cool–warm, pearly pointillé highlights, lapis and lead-tin yellow
  Velazquez,     // loose silvery alla prima strokes that resolve at a distance
  Goya,          // dark, expressive, rough brushwork in blacks and earths
  Hokusai,       // ukiyo-e woodblock: flat colour, Prussian blue, black key lines, washi paper
  Turner,        // light dissolving form: luminous golden haze, sweeping washes
  Monet,         // impressionist broken colour: dabs of pure hue, violet shadows, high key
  Degas,         // pastel on toned paper: chalky hatching, paper showing through
  Renoir,        // soft, feathery, rosy shimmer
  Cezanne,       // constructive parallel brushstrokes building planes of colour
  Seurat,        // pointillism: dots of pure colour mixing in the eye
  VanGogh,       // swirling impasto dashes along the forms, vivid blues and yellows
  Gauguin,       // synthetism: flat saturated fields with bold dark contours
  Klimt,         // golden phase: a finely painted face amid gold-leaf ornament
};
constexpr int32_t kArtistCount = 20;

// How far the painting goes: a quick study, a finished painting, or a
// masterpiece (finest brushes, every layer, the full finish).
enum class Level : int32_t { Study = 0, Painting = 1, Masterpiece = 2 };

// A face (normalised 0..1 box): painted with the finest brushes and kept true.
struct FaceBox {
  float x = 0, y = 0, w = 0, h = 0;
};

// The person's own say over the painting. The defaults are the artist's own
// way; every control stays within what that artist would do.
struct Controls {
  float brush = 1.0f;       // 0.6 finer marks .. 1.6 bolder marks (strokes, hatching, dots, tesserae)
  float palette = 1.0f;     // 0 the photo's own colours .. 1 the artist's palette (light and marks stay the artist's)
  float vivid = 0.0f;       // -1 muted .. +1 vivid colour
  float likeness = 0.5f;    // 0 the artist's freedom .. 1 the face kept closest to the sitter
  float texture = 1.0f;     // 0 smooth .. 1.5 heavy canvas, paper grain and paint relief
  float age = 1.0f;         // 0 freshly painted .. 1.5 old varnish and craquelure
  float background = 1.0f;  // 0 the photo's own background, painted .. 1 the artist's (dark ground, haze, gold)
};

struct Options {
  Artist artist = Artist::Leonardo;
  Level level = Level::Painting;
  uint32_t seed = 1;
  // Optional person mask (GRAY8, any size; 255 = the person): lets the
  // background be treated as the artist would (a dark ground, a landscape
  // haze, gold leaf) while the person stays the subject.
  const ImageBuffer* subject = nullptr;
  std::vector<FaceBox> faces;
  Controls controls;
};

// Paints `image` (RGBA8 or BGRA8) in place. Alpha is kept.
Error paint(ImageBuffer& image, const Options& options, const CancellationToken* token);

}  // namespace photoplus::art
