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
// The mouth (x0, y0, x1, y1) and the eye centres (left x, y, right x, y),
// normalised, when known (all zero otherwise): where teeth and the whites of
// the eyes can be, so they keep their light (a face's shine elsewhere doesn't).
struct FaceBox {
  float x = 0, y = 0, w = 0, h = 0;
  std::array<float, 4> mouth{};
  std::array<float, 4> eyes{};
};

// What it is painted on: the artist's own (canvas, panel, paper), or a
// surface the person chooses (its texture and tone over the painting).
enum class Surface : int32_t { Artist = 0, Linen, FineArtPaper, WatercolourPaper, RoughCanvas, Parchment, SmoothBoard };
constexpr int32_t kSurfaceCount = 7;

// Behind the person: the artist's own treatment, the photo's own scene, or a
// backdrop painted in the artist's hand (the person stays as they are).
enum class Backdrop : int32_t { Artist = 0, Scene, DarkStudio, WarmUmber, Sky, Landscape, Gold };
constexpr int32_t kBackdropCount = 7;

// A museum frame around a finished painting.
enum class Frame : int32_t { None = 0, Gilded, Baroque, BlackGallery, Walnut, WhiteMat };
constexpr int32_t kFrameCount = 6;

// The person's own say over the painting. The defaults are the artist's own
// way (with a smooth texture); every control stays within what that artist would do.
struct Controls {
  float brush = 1.0f;       // 0.6 finer marks .. 1.6 bolder marks (strokes, hatching, dots, tesserae)
  float palette = 1.0f;     // 0 the photo's own colours .. 1 the artist's palette (light and marks stay the artist's)
  float vivid = 0.0f;       // -1 muted .. +1 vivid colour
  float likeness = 0.5f;    // 0 the artist's freedom .. 1 the face kept closest to the sitter
  float texture = 0.4f;     // 0 smooth .. 1 the artist's full texture .. 1.5 heavy (canvas, paper grain, paint relief,
                            // bristle marks); smooth by default
  float age = 1.0f;         // 0 freshly painted .. 1.5 old varnish and craquelure
  float background = 1.0f;  // 0 the photo's own background, painted .. 1 the artist's (dark ground, haze, gold)
  // How far the photograph is transformed: 0 a natural painting (close to the
  // photo's colours, finest brushes) .. 1 the full artistic transformation.
  float intensity = 0.6f;
  Surface surface = Surface::Artist;
  Backdrop backdrop = Backdrop::Artist;
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

// Frames a finished painting: `out` is the painting inside the frame (larger
// than the painting by the frame's width on every side), same format.
Error frame(const ImageBuffer& painting, Frame style, ImageBuffer& out, const CancellationToken* token);

// ---- Pencil art -------------------------------------------------------------
// A pencil drawing of the photo — no paint, only the pencil on paper — in the
// way each master drew: their line, their hatching (Leonardo's left-handed
// parallel strokes, Michelangelo's cross-contour hatching, Dürer's
// engraver's cross-hatching, Rembrandt's loose sketching, Seurat's velvety
// conté tone…), their blending and how much of the sheet they left bare.
// Lines from the photo's edges as directional pencil strokes, tone matched
// to a real drawing's tones and built from hatching layers that darken by
// adding strokes, graphite caught on the paper's tooth.
enum class PencilMedium : int32_t { Graphite = 0, Charcoal, RedChalk };
constexpr int32_t kPencilMediumCount = 3;
enum class PencilPaper : int32_t { White = 0, Cream, Grey, Tan };  // grey and tan: toned paper with white chalk lights
constexpr int32_t kPencilPaperCount = 4;

struct PencilOptions {
  Artist artist = Artist::Leonardo;
  Level level = Level::Painting;
  uint32_t seed = 1;
  const ImageBuffer* subject = nullptr;  // optional GRAY8 person mask, any size
  std::vector<FaceBox> faces;
  PencilMedium medium = PencilMedium::Graphite;
  PencilPaper paper = PencilPaper::White;
  float intensity = 0.6f;  // 0 close to the photo .. 1 the master's full manner
  float lines = 1.0f;      // 0.5 lighter .. 1.5 bolder contour lines
  float shading = 1.0f;    // 0.5 lighter .. 1.5 deeper shading
};

// Draws `image` (RGBA8 or BGRA8) in place. Alpha is kept. Deterministic for a seed.
Error pencil(ImageBuffer& image, const PencilOptions& options, const CancellationToken* token);
// The blank sheet the drawing is made on (the same paper, pixel for pixel, as
// pencil() leaves where nothing is drawn), into `image` (its size and format).
Error pencilSheet(ImageBuffer& image, const PencilOptions& options);
// The primed ground a painting starts from (the artist's priming: a warm
// white, a brown imprimatura, plaster, paper), into `image`.
Error paintGround(ImageBuffer& image, const Options& options);

// ---- The live studio --------------------------------------------------------
// The work being made, stroke by stroke: the blank sheet, and a pencil (or a
// brush) moving over it, each movement revealing the finished work beneath —
// contour lines then hatching for a drawing; a block-in with broad strokes,
// then smaller strokes, then the finest details for a painting — until the
// work is complete. frame(t) for t in [0, 1]; t = 1 is the finished work.
enum class RevealTool : int32_t { Pencil = 0, Brush = 1 };
struct RevealOptions {
  RevealTool tool = RevealTool::Pencil;
  Artist artist = Artist::Leonardo;  // the hatching direction
  // The picture inside a frame (normalised; strokes stay inside, the frame is there from the start).
  float x0 = 0, y0 = 0, x1 = 1, y1 = 1;
  uint32_t seed = 1;
};
class Reveal;
// `result` and `blank`: RGBA8 or BGRA8, the same size and format.
Error revealCreate(const ImageBuffer& result, const ImageBuffer& blank, const RevealOptions& options, Reveal*& out,
                   const CancellationToken* token);
// The frame at t (0..1) into `out` (allocated to the result's size and format if it isn't).
Error revealFrame(Reveal& reveal, float t, ImageBuffer& out);
int32_t revealStrokeCount(const Reveal& reveal);
void revealDestroy(Reveal* reveal);

}  // namespace photoplus::art
