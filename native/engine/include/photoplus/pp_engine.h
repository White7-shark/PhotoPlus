/*
 * Photo Plus processing engine — stable C ABI.
 *
 * This header is the only contract between the engine and its hosts
 * (Android JNI bridge today; other hosts or tools later). It is plain C so it
 * can be consumed from any language and stays ABI-stable across C++ refactors.
 *
 * Ownership rules:
 *   - Every pp_image returned through an out-parameter is owned by the caller
 *     and must be released with pp_image_destroy().
 *   - Wrapped images (pp_image_wrap) never own their pixels; the caller must
 *     keep the pixel memory alive until the wrapper is destroyed.
 *   - Cancel tokens are created/destroyed by the caller and may be signalled
 *     from any thread.
 *
 * Error reporting:
 *   - Functions return pp_status. On failure, pp_last_error_message() returns
 *     a human readable message for the *calling thread*.
 */
#ifndef PHOTOPLUS_PP_ENGINE_H
#define PHOTOPLUS_PP_ENGINE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PP_ENGINE_VERSION_MAJOR 0
#define PP_ENGINE_VERSION_MINOR 1
#define PP_ENGINE_VERSION_PATCH 0

typedef enum pp_status {
  PP_OK = 0,
  PP_ERROR_INVALID_ARGUMENT = 1,
  PP_ERROR_OUT_OF_MEMORY = 2,
  PP_ERROR_BUDGET_EXCEEDED = 3,
  PP_ERROR_CANCELLED = 4,
  PP_ERROR_UNSUPPORTED_FORMAT = 5,
  PP_ERROR_INTERNAL = 6
} pp_status;

/* All formats with alpha use *straight* (non-premultiplied) alpha. Hosts must
 * un-premultiply before handing pixels to the engine (Android: decode with
 * ImageDecoder.setUnpremultipliedRequired and keep Bitmap.isPremultiplied
 * false), otherwise tonal operations would corrupt translucent edges. */
typedef enum pp_pixel_format {
  PP_FORMAT_RGBA8 = 0, /* Android Bitmap.Config.ARGB_8888 byte order */
  PP_FORMAT_BGRA8 = 1,
  PP_FORMAT_RGB8 = 2,
  PP_FORMAT_GRAY8 = 3
} pp_pixel_format;

typedef enum pp_op_type {
  /* p[0] = exposure EV (-5..5), p[1] = brightness (-1..1), p[2] = contrast (-1..1) */
  PP_OP_TONE = 1,
  /* no params */
  PP_OP_GRAYSCALE = 2,
  /* p[0] = quarter turns clockwise (any integer, normalised mod 4) */
  PP_OP_ROTATE90 = 3,
  /* p[0] = horizontal (0/1), p[1] = vertical (0/1) */
  PP_OP_FLIP = 4,
  /* p[0..3] = x, y, width, height in pixels of the current image */
  PP_OP_CROP = 5,
  /* p[0] = target width, p[1] = target height (bilinear) */
  PP_OP_RESIZE = 6,
  /* p[0] = amount (0..2), unsharp mask with 3x3 box blur */
  PP_OP_SHARPEN = 7,
  /* p[0] = saturation multiplier delta (-1..1) */
  PP_OP_SATURATION = 8
} pp_op_type;

typedef struct pp_op {
  int32_t type; /* pp_op_type */
  float p[4];
} pp_op;

typedef struct pp_image pp_image;
typedef struct pp_cancel_token pp_cancel_token;

typedef struct pp_image_info {
  int32_t width;
  int32_t height;
  int32_t stride; /* bytes per row */
  int32_t format; /* pp_pixel_format */
  int32_t owns_pixels;
} pp_image_info;

typedef struct pp_memory_stats {
  uint64_t current_bytes;
  uint64_t peak_bytes;
  uint64_t budget_bytes; /* 0 = unlimited */
  uint64_t live_allocations;
  uint64_t total_allocations;
  uint64_t failed_allocations;
} pp_memory_stats;

/* ---- Library info ------------------------------------------------------- */
const char* pp_version(void);

/* ---- Errors ------------------------------------------------------------- */
const char* pp_status_name(pp_status status);
const char* pp_last_error_message(void);

/* ---- Images ------------------------------------------------------------- */
/* `format` is a pp_pixel_format value. It is typed int32_t so that values
 * from foreign callers can be validated without undefined behaviour. */
pp_status pp_image_create(int32_t width, int32_t height, int32_t format, pp_image** out);
pp_status pp_image_wrap(void* pixels, int32_t width, int32_t height, int32_t stride,
                        int32_t format, pp_image** out);
void pp_image_destroy(pp_image* image);
pp_status pp_image_get_info(const pp_image* image, pp_image_info* out);
void* pp_image_pixels(pp_image* image);
/* Copies src into dst. Both must have equal dimensions and format; strides may differ. */
pp_status pp_image_copy_pixels(const pp_image* src, pp_image* dst);

/* ---- Cancellation ------------------------------------------------------- */
pp_cancel_token* pp_cancel_token_create(void);
void pp_cancel_token_cancel(pp_cancel_token* token);
int32_t pp_cancel_token_is_cancelled(const pp_cancel_token* token);
void pp_cancel_token_destroy(pp_cancel_token* token);

/* ---- Processing --------------------------------------------------------- */
/* Runs `ops` in order on `input` and returns a newly allocated image in *out.
 * `token` may be NULL. The input image is never modified. */
pp_status pp_process(const pp_image* input, const pp_op* ops, size_t op_count,
                     const pp_cancel_token* token, pp_image** out);

/* ---- Memory ------------------------------------------------------------- */
void pp_memory_get_stats(pp_memory_stats* out);
/* 0 disables the budget. Allocations that would exceed it fail with
 * PP_ERROR_BUDGET_EXCEEDED rather than risking a process kill. */
void pp_memory_set_budget(uint64_t bytes);
void pp_memory_reset_peak(void);

/* ---- Profiling ---------------------------------------------------------- */
/* Writes a JSON report into buffer (always NUL terminated when capacity > 0).
 * Returns the number of bytes required excluding the terminator. */
size_t pp_profiler_report_json(char* buffer, size_t capacity);
void pp_profiler_reset(void);

/* ---- Computational photography ------------------------------------------ */

typedef struct pp_enhance_params {
  int32_t upscale;   /* 1, or 2 for super-resolution (multi-frame when count > 1) */
  float denoise;     /* 0..1 multiplier on the automatic noise reduction (0 = off) */
  float sharpen;     /* 0..1 multiplier on the automatic deblur (0 = off) */
  float detail;      /* 0..1 local contrast ("clarity") */
  int32_t auto_tone; /* 1 = automatic white balance and levels */
  float vibrance;    /* 0..1 */
} pp_enhance_params;

typedef struct pp_quality_report {
  float noise_sigma; /* estimated noise std-dev (luma, 0..1 scale) */
  float blur_sigma;  /* estimated Gaussian blur in pixels (0 = undetermined) */
  float sharpness;   /* normalised variance of the Laplacian */
  float mean_luma;
  float highlights_clipped; /* fraction of pixels */
  float shadows_clipped;
} pp_quality_report;

typedef struct pp_enhance_report {
  int32_t frames_input;
  int32_t frames_used;     /* frames that aligned well enough to merge */
  int32_t reference_index; /* sharpest frame, used as the alignment anchor */
  float noise_before;
  float noise_after;
  float blur_before;
  float deblur_sigma;
} pp_enhance_report;

void pp_enhance_default_params(pp_enhance_params* out);

/* Enhances one frame, or fuses a burst of `count` same-sized frames (handheld
 * shots of the same scene) into a single cleaner, sharper image. `report` may
 * be NULL. */
pp_status pp_enhance(const pp_image* const* frames, size_t count, const pp_enhance_params* params,
                     const pp_cancel_token* token, pp_image** out, pp_enhance_report* report);

/* As pp_enhance, but writes into `destination` (a 4-channel image of exactly
 * frames' size x upscale, typically a wrapped Android Bitmap) instead of
 * allocating, which saves a full-image copy. */
pp_status pp_enhance_into(const pp_image* const* frames, size_t count, const pp_enhance_params* params,
                          const pp_cancel_token* token, pp_image* destination, pp_enhance_report* report);

pp_status pp_analyze_quality(const pp_image* image, pp_quality_report* out);

/* ---- Lens quality ("Transform") ------------------------------------------
 * Measures what a basic phone lens leaves in a photo, on the full-resolution
 * image, and removes it so the photo looks as if a high-end lens took it. */

typedef struct pp_lens_report {
  float detail_blur;     /* lens blur (px) where the photo is sharpest */
  float corner_blur;     /* lens blur (px) at the corners */
  float fringe_px;       /* colour fringe width at the corners (px) */
  float noise;           /* noise std-dev, luma 0..1 */
  float grade;           /* 0 basic ... 1 flagship-like */
  int32_t measured_tiles;/* regions with enough edges to judge sharpness */
  int32_t reliable;      /* 1 when the photo had enough detail to judge */
} pp_lens_report;

pp_status pp_lens_analyze(const pp_image* image, const pp_cancel_token* token, pp_lens_report* out);

typedef struct pp_transform_params {
  int32_t upscale;         /* 1, or 2 for twice the detail */
  int32_t enrich;          /* a touch of vibrance (0 keeps a styled photo's colours exactly) */
  float strength;          /* 0..1 */
  float detail;            /* finishing local contrast 0..1 (default 0.4) */
} pp_transform_params;

void pp_transform_default_params(pp_transform_params* out);

typedef struct pp_transform_report {
  pp_lens_report before;
  float fringe_fixed_px;
  float deblur_centre;   /* PSF sigma removed (px, output scale) */
  float deblur_corner;   /* extra PSF sigma removed at the corners */
  float noise_before;
  float noise_after;
} pp_transform_report;

/* `output` (4-channel, input size x upscale) may be `input` when upscale = 1. */
pp_status pp_transform(const pp_image* input, pp_image* output, const pp_transform_params* params,
                       const pp_cancel_token* token, pp_transform_report* report);

/* Focus peaking on a camera luma (Y) plane, read in place (e.g. a direct
 * ByteBuffer from CameraX), so no frame copy is made. Writes `argb` into
 * pixels of `out` (out_width x out_height, a downscaled overlay) whose edge
 * strength exceeds `threshold` (0..1), transparent elsewhere. `sharpness`
 * (optional) receives a 0..1 focus score for the frame centre. */
pp_status pp_focus_peaking(const uint8_t* luma, int32_t width, int32_t height, int32_t row_stride,
                           int32_t pixel_stride, float threshold, uint32_t argb, uint32_t* out, int32_t out_width,
                           int32_t out_height, float* sharpness);

/* ---- Portrait ("Spotlight") and look transfer ("Vibe Match") ----------- */

typedef struct pp_portrait_params {
  float blur;     /* background blur 0..1 (0 = none) */
  float retouch;  /* skin smoothing and spot removal 0..1 */
  float brighten; /* gentle lift of the subject's light and skin 0..1 */
  float pop;      /* subject clarity and separation 0..1 */
} pp_portrait_params;

typedef struct pp_portrait_report {
  float subject_coverage; /* fraction of the frame that is subject */
  float skin_coverage;    /* fraction of the frame retouched as skin */
  int32_t spots_removed;
  float blur_radius; /* largest blur radius, pixels */
  float focus_depth; /* the subject's depth, 0 (far) .. 1 (near) */
  int32_t depth_estimated; /* 1 when no depth map was given */
} pp_portrait_report;

void pp_portrait_default_params(pp_portrait_params* out);

/* Processes `image` (RGBA8/BGRA8) in place. Masks are GRAY8 of any size:
 * `subject` (required), `faces` (optional: enables skin retouching calibrated
 * to those faces), `protect` (optional: eyes, brows, lips, never smoothed),
 * `depth` (optional: relative depth, 0 far .. 255 near; blur then follows
 * each area's distance from the subject's focus plane, as with a real lens;
 * without it depth is estimated from the subject's position).
 * `report` may be NULL. */
pp_status pp_portrait(pp_image* image, const pp_image* subject, const pp_image* faces, const pp_image* protect,
                      const pp_image* depth, const pp_portrait_params* params, const pp_cancel_token* token,
                      pp_portrait_report* report);

/* Subject cut-out grown from `count` tap points (x0,y0,x1,y1,... normalised
 * 0..1; count 0 = frame centre). Returns a new GRAY8 mask (~320 px). */
pp_status pp_subject_mask(const pp_image* image, const float* points, size_t count, const pp_cancel_token* token,
                          pp_image** out);

#define PP_STYLE_QUANTILES 9
typedef struct pp_style {
  float luma[PP_STYLE_QUANTILES]; /* at 1, 5, 15, 30, 50, 70, 85, 95, 99 % */
  float zone_cb[3];               /* shadows, midtones, highlights */
  float zone_cr[3];
  float chroma_std[2];
  float vignette; /* corner / centre light */
  float grain;
  float mean_luma;
} pp_style;

pp_status pp_style_analyze(const pp_image* image, pp_style* out);
pp_status pp_style_apply(pp_image* image, const pp_style* target, float strength, const pp_cancel_token* token);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PHOTOPLUS_PP_ENGINE_H */
