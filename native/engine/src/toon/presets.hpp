#pragma once

// Every Toon style's look in one table: tune a style here and nowhere else.
// Widths are in thousandths of the picture's short side, so a style looks
// the same at preview size and in the full-size export.

#include <array>

#include "../art/core.hpp"
#include "photoplus/toon.hpp"

namespace photoplus::toon {

enum class Smoothing { Bilateral, Kuwahara, Guided };
enum class Ink { Black, Brown, Coloured };
enum class Tone { None, Screentone, BenDay };
enum class BackTone { Painted, Screentone };

struct StylePreset {
  // ---- colour
  int clusters;           // k-means colours on the person (Lab), at 3 shading steps
  int backClusters;       // ... on the background
  float lWeight;          // lightness against hue in the clustering (more: more tonal steps)
  float celSoft;          // width of a cel boundary (Lab units): crisp 1 .. soft 6
  float gradient;         // 0 flat cel colour .. 1 the smooth colour kept (webtoon's soft gradients)
  float saturation;       // colour gain
  float vibrance;         // extra gain on muted colours
  float contrast;         // lightness stretched about its middle
  float pastel;           // lighter and softer colour (webtoon's airy palette)
  bool mono;              // black, white and tone (manga)
  bool periodPalette;     // colours drawn from a 90s cel-paint palette (retro)
  float shadowCool;       // shadows towards blue-violet, lights warmer (comic)
  // ---- shapes
  Smoothing smoothing;
  float smooth;           // how strongly areas are flattened
  // ---- skin
  int skinTones;          // 2 (base, shadow) or 3 (and a highlight)
  float skinHighlight;    // how far a skin highlight may rise above the base (L units): no white shine
  bool skinWhite;         // manga: skin is the white of the paper, drawn with lines only
  float skinGlow;         // webtoon: a soft glow on the skin's light
  // ---- lines
  float lineWidth;        // thousandths of the short side
  float lineDark;         // ink opacity
  Ink ink;
  art::core::Rgb3 inkColour;
  float lineRound;        // cartoon: lines smoothed into round, even strokes
  float speck;            // lines shorter than this (thousandths of the short side) are dropped
  float faceLine;         // the features' line width (thousandths)
  float outline;          // the person's outline width (thousandths)
  float hairLines;        // strands drawn in the hair (0..1)
  // ---- background (kept)
  float backBlur;         // the scene softened (thousandths of the short side, sigma)
  float backDim;          // the scene quieter than the person: less contrast and colour
  float backInk;          // lines in the scene (0..1)
  BackTone backTone;
  // ---- finish
  Tone tone;              // dots in the shadows
  float toneStrength;
  float bloom;            // light bloom at full Glow
  float catchlight;       // eye sparkle at full Eyes
  float grain;
  float aberration;       // chromatic fringe (thousandths)
  float vhs;              // VHS warmth, lifted blacks, faint scanlines
};

// Anime, Manga, Comic, Cartoon, Webtoon, Retro 90s.
inline constexpr std::array<StylePreset, kStyleCount> kPresets = {{
    // Anime: soft 3-tone cel shading, clean thin lines, a touch more colour, bright eyes, a softened scene, slight bloom.
    {.clusters = 12, .backClusters = 8, .lWeight = 1.3f, .celSoft = 2.2f, .gradient = 0.12f, .saturation = 1.12f, .vibrance = 0.25f,
     .contrast = 0.04f, .pastel = 0, .mono = false, .periodPalette = false, .shadowCool = 0.15f, .smoothing = Smoothing::Bilateral,
     .smooth = 0.6f, .skinTones = 3, .skinHighlight = 7, .skinWhite = false, .skinGlow = 0, .lineWidth = 1.2f, .lineDark = 0.92f,
     .ink = Ink::Brown, .inkColour = {0.17f, 0.10f, 0.09f}, .lineRound = 0.2f, .speck = 14, .faceLine = 1.6f, .outline = 1.7f,
     .hairLines = 0.6f, .backBlur = 6.0f, .backDim = 0.25f, .backInk = 0.25f, .backTone = BackTone::Painted, .tone = Tone::None,
     .toneStrength = 0, .bloom = 0.45f, .catchlight = 1, .grain = 0, .aberration = 0, .vhs = 0},
    // Manga: grey-scale, strong ink, screentone only in the shadows, white skin with line-only features, toned background.
    {.clusters = 6, .backClusters = 4, .lWeight = 2.0f, .celSoft = 1.2f, .gradient = 0, .saturation = 0, .vibrance = 0,
     .contrast = 0.15f, .pastel = 0, .mono = true, .periodPalette = false, .shadowCool = 0, .smoothing = Smoothing::Bilateral,
     .smooth = 0.7f, .skinTones = 2, .skinHighlight = 0, .skinWhite = true, .skinGlow = 0, .lineWidth = 1.6f, .lineDark = 1.0f,
     .ink = Ink::Black, .inkColour = {0.04f, 0.04f, 0.05f}, .lineRound = 0.3f, .speck = 18, .faceLine = 1.8f, .outline = 2.4f,
     .hairLines = 0.9f, .backBlur = 4.5f, .backDim = 0.3f, .backInk = 0.45f, .backTone = BackTone::Screentone, .tone = Tone::Screentone,
     .toneStrength = 0.9f, .bloom = 0, .catchlight = 1, .grain = 0, .aberration = 0, .vhs = 0},
    // Comic (US): bold black outlines, 4-5 punchy colours, Ben-Day dots in the shadows, warm lights and cool shadows.
    {.clusters = 5, .backClusters = 4, .lWeight = 1.6f, .celSoft = 0.9f, .gradient = 0, .saturation = 1.35f, .vibrance = 0.3f,
     .contrast = 0.18f, .pastel = 0, .mono = false, .periodPalette = false, .shadowCool = 1.0f, .smoothing = Smoothing::Kuwahara,
     .smooth = 0.8f, .skinTones = 2, .skinHighlight = 4, .skinWhite = false, .skinGlow = 0, .lineWidth = 2.6f, .lineDark = 1.0f,
     .ink = Ink::Black, .inkColour = {0.03f, 0.03f, 0.05f}, .lineRound = 0.35f, .speck = 22, .faceLine = 2.0f, .outline = 3.6f,
     .hairLines = 0.7f, .backBlur = 6.0f, .backDim = 0.18f, .backInk = 0.5f, .backTone = BackTone::Painted, .tone = Tone::BenDay,
     .toneStrength = 0.55f, .bloom = 0, .catchlight = 0.6f, .grain = 0, .aberration = 0, .vhs = 0},
    // Cartoon: very flat bright colour (6-8), thick smooth outlines, a simplified scene.
    {.clusters = 6, .backClusters = 4, .lWeight = 1.0f, .celSoft = 0.8f, .gradient = 0, .saturation = 1.5f, .vibrance = 0.4f,
     .contrast = 0.1f, .pastel = 0.12f, .mono = false, .periodPalette = false, .shadowCool = 0, .smoothing = Smoothing::Kuwahara,
     .smooth = 1.0f, .skinTones = 2, .skinHighlight = 3, .skinWhite = false, .skinGlow = 0, .lineWidth = 3.0f, .lineDark = 1.0f,
     .ink = Ink::Black, .inkColour = {0.07f, 0.05f, 0.06f}, .lineRound = 1.0f, .speck = 40, .faceLine = 2.2f, .outline = 4.6f,
     .hairLines = 0.2f, .backBlur = 8.0f, .backDim = 0.12f, .backInk = 0.15f, .backTone = BackTone::Painted, .tone = Tone::None,
     .toneStrength = 0, .bloom = 0.15f, .catchlight = 0.9f, .grain = 0, .aberration = 0, .vhs = 0},
    // Webtoon: soft gradients (no hard steps), thin coloured lines, an airy pastel palette, a gentle glow on the skin.
    {.clusters = 14, .backClusters = 8, .lWeight = 1.2f, .celSoft = 6.0f, .gradient = 0.55f, .saturation = 0.95f, .vibrance = 0.1f,
     .contrast = -0.04f, .pastel = 0.45f, .mono = false, .periodPalette = false, .shadowCool = 0.25f, .smoothing = Smoothing::Guided,
     .smooth = 0.7f, .skinTones = 3, .skinHighlight = 8, .skinWhite = false, .skinGlow = 0.5f, .lineWidth = 0.9f, .lineDark = 0.8f,
     .ink = Ink::Coloured, .inkColour = {0, 0, 0}, .lineRound = 0.2f, .speck = 14, .faceLine = 1.3f, .outline = 1.3f, .hairLines = 0.5f,
     .backBlur = 6.0f, .backDim = 0.25f, .backInk = 0.1f, .backTone = BackTone::Painted, .tone = Tone::None, .toneStrength = 0,
     .bloom = 0.35f, .catchlight = 0.8f, .grain = 0, .aberration = 0, .vhs = 0},
    // Retro 90s: a period cel palette, hard cel shading, brown lines, film grain, a slight colour fringe, VHS warmth.
    {.clusters = 9, .backClusters = 6, .lWeight = 1.4f, .celSoft = 1.2f, .gradient = 0, .saturation = 1.0f, .vibrance = 0,
     .contrast = 0.08f, .pastel = 0, .mono = false, .periodPalette = true, .shadowCool = 0.35f, .smoothing = Smoothing::Bilateral,
     .smooth = 0.8f, .skinTones = 2, .skinHighlight = 4, .skinWhite = false, .skinGlow = 0, .lineWidth = 1.6f, .lineDark = 0.95f,
     .ink = Ink::Brown, .inkColour = {0.26f, 0.14f, 0.10f}, .lineRound = 0.4f, .speck = 20, .faceLine = 1.7f, .outline = 2.2f,
     .hairLines = 0.5f, .backBlur = 6.0f, .backDim = 0.25f, .backInk = 0.3f, .backTone = BackTone::Painted, .tone = Tone::None,
     .toneStrength = 0, .bloom = 0.1f, .catchlight = 0.7f, .grain = 0.06f, .aberration = 1.1f, .vhs = 1.0f},
}};

// The 90s cel-paint palette (Retro): the period's limited, slightly muted inks.
inline constexpr std::array<uint32_t, 24> kPeriodPalette = {
    0xF4E9D8, 0xE8C9A8, 0xC99A78, 0x8C5A44, 0x5A3A30, 0x2E2228,  // paper, skins, browns, deep brown
    0xF2D16B, 0xE0A04A, 0xC8563E, 0x9E2F3A, 0xE7849A, 0xB65E8C,  // yellow, orange, red, crimson, pink, magenta
    0x7A5BA6, 0x4B4A8C, 0x2F5F9E, 0x5E9BD0, 0xA6CBE6, 0x3E8C8A,  // violet, indigo, blue, sky, pale blue, teal
    0x6FA55A, 0x3F6E46, 0xB8C48A, 0x8E8A80, 0x5C5A60, 0xD8D4CC,  // green, deep green, sage, warm grey, slate, light grey
};

}  // namespace photoplus::toon
