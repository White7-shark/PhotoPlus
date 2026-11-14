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
#define PP_ENGINE_VERSION_MINOR 3
#define PP_ENGINE_VERSION_PATCH 0

typedef enum pp_status {
  PP_OK = 0,
  PP_ERROR_INVALID_ARGUMENT = 1,
  PP_ERROR_OUT_OF_MEMORY = 2,
  PP_ERROR_BUDGET_EXCEEDED = 3,
  PP_ERROR_CANCELLED = 4,
  PP_ERROR_UNSUPPORTED_FORMAT = 5,
  PP_ERROR_INTERNAL = 6,
  PP_ERROR_IO = 7 /* a file could not be read or written */
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
  /* p[0] = target width, p[1] = target height, p[2] = filter (pp_resize_filter;
   * 0 = auto: area when shrinking, Lanczos-3 when enlarging) */
  PP_OP_RESIZE = 6,
  /* p[0] = amount (0..3), p[1] = radius in pixels (0 = 1 px), p[2] = threshold
   * (0..0.2). Unsharp mask on luminance (no colour fringes). */
  PP_OP_SHARPEN = 7,
  /* p[0] = saturation multiplier delta (-1..1) */
  PP_OP_SATURATION = 8,
  /* p[0] = straighten angle in degrees (-45..45, clockwise); crops to the
   * largest same-aspect rectangle holding only photo */
  PP_OP_STRAIGHTEN = 9,
  /* p[0] = temperature (-1 cool .. 1 warm), p[1] = tint (-1 green .. 1 magenta) */
  PP_OP_WARMTH = 10,
  /* p[0] = look id (pp_filter_id), p[1] = strength (0..1) */
  PP_OP_FILTER = 11,
  /* p[0] = kind (1 radial, 2 tilt), p[1..2] = centre x, y (0..1), p[3] = size
   * (fraction of the shorter side), p[4] = tilt angle (deg), p[5] = strength
   * (0..1), p[6] = feather (0..1) */
  PP_OP_FOCUS = 12,
  /* p[0] = Gaussian sigma in pixels (0..500) */
  PP_OP_BLUR = 13,
  /* p[0] = strength (0..1), scaled to the noise the photo has */
  PP_OP_DENOISE = 14,
  /* p[0] = vertical, p[1] = horizontal keystone (-1..1; > 0 widens the top /
   * the right side, straightening lines that converge there) */
  PP_OP_PERSPECTIVE = 15
} pp_op_type;

typedef enum pp_resize_filter {
  PP_RESIZE_AUTO = 0,
  PP_RESIZE_BILINEAR = 1,
  PP_RESIZE_AREA = 2,
  PP_RESIZE_LANCZOS = 3
} pp_resize_filter;

/* Where pp_process_ex may run (see pp_gpu_*). Final renders should use CPU
 * when results must be identical on every device. */
typedef enum pp_backend {
  PP_BACKEND_AUTO = 0, /* GPU when available and verified, else CPU */
  PP_BACKEND_CPU = 1,
  PP_BACKEND_GPU = 2 /* fails with PP_ERROR_UNSUPPORTED_FORMAT if no verified GPU */
} pp_backend;

typedef struct pp_render_options {
  /* > 0: a preview. The input is reduced so its longer edge fits, and settings
   * measured in pixels (crop, resize, blur and sharpen radii) are scaled too,
   * so the preview matches the full-resolution render. 0 = full resolution. */
  int32_t max_edge;
  int32_t backend; /* pp_backend */
  int32_t reserved[6];
} pp_render_options;

/* Looks for PP_OP_FILTER. */
typedef enum pp_filter_id {
  PP_FILTER_NATURAL = 1,
  PP_FILTER_VIVID = 2,
  PP_FILTER_WARM = 3,
  PP_FILTER_COOL = 4,
  PP_FILTER_MONO = 5,
  PP_FILTER_FADE = 6,
  PP_FILTER_SOFT = 7,
  PP_FILTER_NOIR = 8,
  PP_FILTER_CINEMA = 9,
  PP_FILTER_GOLDEN = 10,
  PP_FILTER_FILM = 11,
  PP_FILTER_LUSH = 12
} pp_filter_id;

#define PP_OP_PARAMS 8

typedef struct pp_op {
  int32_t type; /* pp_op_type */
  float p[PP_OP_PARAMS];
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
  uint64_t cached_bytes;       /* idle buffers kept for reuse (count against the budget) */
  uint64_t reused_allocations; /* allocations served from kept buffers */
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
/* How far the job using this token has got, 0..1 (never decreases; poll it
 * from any thread to show progress). A token is meant for one job. */
float pp_cancel_token_progress(const pp_cancel_token* token);

/* ---- Processing --------------------------------------------------------- */
/* Runs `ops` in order on `input` and returns a newly allocated image in *out.
 * `token` may be NULL. The input image is never modified. */
pp_status pp_process(const pp_image* input, const pp_op* ops, size_t op_count,
                     const pp_cancel_token* token, pp_image** out);

/* pp_process with options (preview size, backend). `options` may be NULL.
 * Progress is reported through `token`. */
pp_status pp_process_ex(const pp_image* input, const pp_op* ops, size_t op_count, const pp_render_options* options,
                        const pp_cancel_token* token, pp_image** out);
/* Upper bound of the engine memory pp_process_ex will hold at its peak for an
 * input of this size (the input included), without running it. Also checks
 * the operations, so invalid lists fail here first. */
pp_status pp_process_estimate(int32_t width, int32_t height, int32_t format, const pp_op* ops, size_t op_count,
                              const pp_render_options* options, uint64_t* peak_bytes);

/* ---- Memory ------------------------------------------------------------- */
void pp_memory_get_stats(pp_memory_stats* out);
/* 0 disables the budget. Allocations that would exceed it fail with
 * PP_ERROR_BUDGET_EXCEEDED rather than risking a process kill. */
void pp_memory_set_budget(uint64_t bytes);
void pp_memory_reset_peak(void);
/* Frees the buffers kept for reuse (call on memory pressure). */
void pp_memory_trim(void);
/* Most memory kept for reuse between operations (default 96 MB, and never
 * more than a quarter of the budget); 0 disables reuse. */
void pp_memory_set_reuse_limit(uint64_t bytes);

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
  /* Video (zero = automatic, as for photos): */
  int32_t fixed_reference; /* 1: frame 0 is always the reference (centre of a video window) */
  float measured_blur;     /* > 0: blur (px) measured once for the clip, instead of per frame */
  float noise_sigma;       /* > 0: noise level measured once for the clip */
  float reserved[4];
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

/* ---- Editor: props, frames, watermark --------------------------------------- */

/* A prop (hat, glasses, accessory; straight-alpha RGBA) placed on the photo
 * so it looks part of the scene: lit and tinted like the scene, with a soft
 * contact shadow, and as sharp and grainy as the photo. */
typedef struct pp_prop_params {
  float cx, cy;        /* centre, image pixels */
  float width;         /* width, image pixels */
  float rotation_deg;  /* clockwise */
  int32_t flip;        /* mirror horizontally */
  float opacity;       /* 0..1 */
  float blend;         /* 0..1: takes on the photo's light, colour, sharpness, grain */
  float shadow;        /* 0..1: contact shadow */
} pp_prop_params;

pp_status pp_place_prop(pp_image* image, const pp_image* prop, const pp_prop_params* params,
                        const pp_cancel_token* token, int64_t* covered_pixels);

/* Frames: the photo inside a shape on a designed card. Colours are 0xAARRGGBB. */
typedef struct pp_frame_params {
  int32_t shape;        /* 0 rect, 1 ellipse, 2 arch, 3 star, 4 heart, 5 hexagon, 6 diamond, 7 octagon,
                           8 scallop (wavy circle), 9 sparkle (four-pointed star) */
  float shape_param;    /* rect corner radius (fraction) / star inner ratio / sparkle curvature / scallop wave depth */
  float rotation_deg;
  float window[4];      /* x, y, w, h: where the photo sits, canvas fractions */
  int32_t backdrop;     /* 0 solid, 1 gradient, 2 blurred photo, 3 paper */
  uint32_t color_a, color_b;
  float border;         /* fraction of the window's shorter side */
  uint32_t border_color;
  int32_t metallic_border;
  float outline_gap, outline_width;
  uint32_t outline_color;
  float shadow, shadow_offset, shadow_blur;
  float glow;
  uint32_t glow_color;
  float zoom, offset_x, offset_y;
  int32_t decoration;   /* 1 = film sprocket holes */
} pp_frame_params;

/* `canvas` (4-channel, the final size) is fully overwritten. */
pp_status pp_frame(const pp_image* photo, pp_image* canvas, const pp_frame_params* params,
                   const pp_cancel_token* token);

/* Composites a straight-alpha stamp (e.g. the watermark) at (x, y). Reports
 * how many stamp pixels landed in the image with real visibility (effective
 * alpha >= 0.25), so exports can verify a watermark was rendered. */
pp_status pp_overlay(pp_image* image, const pp_image* stamp, int32_t x, int32_t y, float opacity,
                     int64_t* visible_pixels);

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
  float retouch;  /* skin smoothing and spot removal 0..1 (the Glow presets) */
  float brighten; /* gentle lift of the subject's light and skin 0..1 */
  float pop;      /* subject clarity and separation 0..1 */
  /* Advanced skin (Phase 5), 0..1; negative = follow `retouch`. */
  float smooth;  /* texture softening (a share of real texture always stays) */
  float blemish; /* spot healing */
  float even;    /* blotch evening toward the person's own tone */
  /* Selective background, -1..1 (0 = unchanged). */
  float bg_light; /* in stops; highlights roll off */
  float bg_color; /* -1 = black and white ... +1 = richer */
  /* Edge refinement 0..1: colour matte (hair against similar-brightness
   * backgrounds) and colour decontamination of edge pixels. */
  float edge;
  float reserved[5];
} pp_portrait_params;

typedef struct pp_portrait_report {
  float subject_coverage; /* fraction of the frame that is subject */
  float skin_coverage;    /* fraction of the frame retouched as skin */
  int32_t spots_removed;
  float blur_radius; /* largest blur radius, pixels */
  float focus_depth; /* the subject's depth, 0 (far) .. 1 (near) */
  int32_t depth_estimated; /* 1 when no depth map was given */
  float edge_cleaned;      /* fraction of the frame whose edge colour was decontaminated */
  float reserved[3];
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

/* ---- Faces (Phase 5) -----------------------------------------------------
 * Landmarks of one face, normalised 0..1 in the image (x, y pairs), in the
 * layout of on-device face contour detection. Index ranges: */
#define PP_FACE_OVAL 0                 /* 36 points around the face */
#define PP_FACE_LEFT_BROW_TOP 36       /* 5 */
#define PP_FACE_LEFT_BROW_BOTTOM 41    /* 5 */
#define PP_FACE_RIGHT_BROW_TOP 46      /* 5 */
#define PP_FACE_RIGHT_BROW_BOTTOM 51   /* 5 */
#define PP_FACE_LEFT_EYE 56            /* 16 around the eye opening */
#define PP_FACE_RIGHT_EYE 72           /* 16 */
#define PP_FACE_UPPER_LIP_TOP 88       /* 11, corner to corner */
#define PP_FACE_UPPER_LIP_BOTTOM 99    /* 9 */
#define PP_FACE_LOWER_LIP_TOP 108      /* 9 */
#define PP_FACE_LOWER_LIP_BOTTOM 117   /* 9 */
#define PP_FACE_NOSE_BRIDGE 126        /* 2 */
#define PP_FACE_NOSE_BOTTOM 128        /* 3 */
#define PP_FACE_POINTS 131

typedef struct pp_face {
  float points[PP_FACE_POINTS * 2];
  int32_t detailed; /* 1: every contour measured; 0: approximated from a few landmarks */
  float roll;       /* head tilt in degrees (informational) */
  float yaw;        /* head turn in degrees (informational) */
  float reserved[5];
} pp_face;

/* Face-precise adjustments, all 0..1 (0 = off) unless noted. */
typedef struct pp_face_params {
  float eyes;         /* brighter whites, clearer irises */
  float teeth;        /* whiter teeth (inside the open mouth only) */
  float lips;         /* richer lip colour and definition */
  float under_eye;    /* softens dark circles toward the cheek's own tone */
  float shine;        /* tames shiny highlights on the skin */
  float slim;         /* gently narrows the jaw and cheeks (a few % at most) */
  float face_light;   /* opens shadows on the face; lifts a backlit face */
  float studio_light; /* soft directional key light shaped to the face */
  float light_angle;  /* studio light from -1 (picture left) .. 1 (right) */
  /* Beauty make-up (low-frequency light and colour), 0..1. */
  float blush;          /* colour on the cheek apples */
  float blush_color[3]; /* gamma sRGB 0..1 */
  float contour;        /* shading under the cheekbones, temples, jaw */
  float highlight;      /* light on nose bridge, forehead, cheekbones, chin */
  float reserved[1];
} pp_face_params;

typedef struct pp_face_report {
  int32_t faces;     /* faces given */
  int32_t adjusted;  /* faces large enough to adjust */
  float light_gain;  /* face exposure gain applied (1 = none), largest face */
  float reserved[5];
} pp_face_report;

void pp_face_default_params(pp_face_params* out); /* all off */
/* Adjusts each face in `image` (RGBA8/BGRA8) in place. Faces smaller than
 * ~24 px are left alone. Landmarks outside the image are allowed. */
pp_status pp_face_adjust(pp_image* image, const pp_face* faces, size_t count, const pp_face_params* params,
                         const pp_cancel_token* token, pp_face_report* report);

/* ---- Beauty (layers C, D, E and the compositor) ---------------------------
 * Every field is a float, in this order (the bridge copies arrays). Ranges:
 * signed fields -1..1, amounts 0..1, colours gamma sRGB 0..1. */
typedef struct pp_beauty_grade {
  /* E: colour */
  float temperature, tint, contrast, blacks, highlights, saturation, vibrance;
  float shadow_color[3], shadow_amount, highlight_color[3], highlight_amount; /* split toning */
  float band_color[3], band_width, band_hue_shift, band_saturation; /* one selective hue band (degrees) */
  float skin_protect, skin_warmth, skin_rosy, skin_luminance;
  /* D: lighting */
  float subject_light, light_warmth, shadow_lift, rim, rim_color[3], bloom, soft_focus, skin_sheen;
  /* C: background */
  float bg_exposure, bg_saturation, bg_tint[3], bg_tint_amount;
  float bg_glow, bg_glow_x, bg_glow_y, bg_glow_size, bg_glow_color[3], bg_vignette, bg_haze;
  float bg_warmth; /* -1 cool .. 1 warm, the background only */
  float reserved[7];
} pp_beauty_grade;

#define PP_BEAUTY_GRADE_FLOATS 58 /* sizeof(pp_beauty_grade) / sizeof(float) */

typedef struct pp_beauty_params {
  pp_portrait_params portrait; /* A: skin (+ Portrait mode's blur, background, edge) */
  pp_face_params face;         /* B: face and make-up */
  pp_beauty_grade grade;       /* C, D, E */
  int32_t auto_light;          /* 1: key light side judged on the face's shading */
  float reserved[7];
} pp_beauty_params;

typedef struct pp_beauty_report {
  float light_angle;      /* key light side used, -1 .. 1 */
  float subject_coverage; /* fraction of the frame that is subject */
  float skin_coverage;    /* fraction graded as skin */
  float clipped_before;   /* fraction of pixels clipped in any channel */
  float clipped_after;
  int32_t spots_removed;
  int32_t faces_adjusted;
  int32_t layers; /* bit 0 skin/portrait, 1 face, 2 grade */
  float reserved[4];
} pp_beauty_report;

/* What the grade measures about a photo (also fed to the live camera
 * shader): the person's skin (median luma, chroma, spread), the subject's
 * centre and width (0..1 of the image width), the key light's side and the
 * skin's luma spread. */
typedef struct pp_beauty_scene {
  int32_t has_skin;
  float skin_y, skin_cb, skin_cr, skin_spread;
  float subject_x, subject_w;
  float light_angle;
  float skin_y_spread; /* spread of the skin's own luma (shine is judged against it) */
  float reserved[3];
} pp_beauty_scene;

pp_status pp_beauty_measure(const pp_image* image, const pp_image* subject, const pp_image* face_mask,
                            const pp_image* protect, const pp_face* faces, size_t count, pp_beauty_scene* out);

/* Neutral: changes nothing (skin protect 0.75, auto light on). */
void pp_beauty_default_params(pp_beauty_params* out);

/* The beauty compositor: skin → face → lighting/background/colour, in
 * place on `image` (RGBA8/BGRA8). Masks are GRAY8 of any size and may be
 * NULL (`subject` NULL: no background separation). `faces` (landmarks) may
 * be NULL when count is 0. `report` may be NULL. */
pp_status pp_beauty(pp_image* image, const pp_image* subject, const pp_image* face_mask, const pp_image* protect,
                    const pp_image* depth, const pp_face* faces, size_t count, const pp_beauty_params* params,
                    const pp_cancel_token* token, pp_beauty_report* report);

/* ---- Backdrops: background designs behind the person --------------------- */
typedef enum pp_backdrop_design {
  PP_BACKDROP_SIGNATURE = 0, /* depth-aware lens blur with gentle fall-off (the camera's default) */
  PP_BACKDROP_SOFT_STUDIO,
  PP_BACKDROP_STAGE,
  PP_BACKDROP_NOIR,
  PP_BACKDROP_PORCELAIN,
  PP_BACKDROP_CANVAS,
  PP_BACKDROP_GOLDEN_HOUR,
  PP_BACKDROP_CITY_LIGHTS,
  PP_BACKDROP_NEON,
  PP_BACKDROP_COLOR_POP,
  PP_BACKDROP_VELOCITY,
  PP_BACKDROP_SWIRL,
  PP_BACKDROP_AURORA,
  PP_BACKDROP_DREAM,
  PP_BACKDROP_COUNT
} pp_backdrop_design;

typedef struct pp_backdrop_params {
  int32_t design;  /* pp_backdrop_design */
  float strength;  /* 0..1 */
  uint32_t seed;   /* varies procedural elements (lights, canvas) */
  float reserved[5];
} pp_backdrop_params;

/* Replaces or restyles the background of `image` (RGBA8/BGRA8) in place.
 * `subject` is a GRAY8 mask of any size (required); `depth` (optional,
 * 0 far .. 255 near) refines Signature's blur. `coverage_out` (optional, 2
 * floats) receives the subject and edge fractions of the frame. */
pp_status pp_backdrop(pp_image* image, const pp_image* subject, const pp_image* depth, const pp_backdrop_params* params,
                      const pp_cancel_token* token, float* coverage_out);

/* ---- Skin Tone: the person's skin moved toward a chosen shade ------------- */
typedef struct pp_skin_tone_params {
  float gain_r, gain_g, gain_b; /* the shade: gains on the skin's own colour, linear light (1 = unchanged) */
  float glow;                   /* 0..1 soft sheen on the skin's highlights */
  float strength;               /* 0..1 */
  float evenness;               /* 0..1 how much blotchy colour is evened */
  float reserved[4];
} pp_skin_tone_params;

/* Recolours the skin of the face and the visible body in place, as a change
 * of the skin's own colour under the photo's light (hair, eyes, brows, lips,
 * clothes and background keep their colours). `face_mask`
 * (GRAY8, face area from landmarks) is needed to measure the skin; without
 * a usable face nothing changes and *has_skin_out is 0. `subject` (GRAY8,
 * optional) extends the search to the body; `protect` (GRAY8, optional)
 * keeps eyes, brows and lips out. `has_skin_out` and `coverage_out` (share
 * of the frame recoloured) may be NULL. */
pp_status pp_skin_tone(pp_image* image, const pp_image* subject, const pp_image* face_mask, const pp_image* protect,
                       const pp_skin_tone_params* params, const pp_cancel_token* token, int32_t* has_skin_out,
                       float* coverage_out);

/* ---- Smart lighting: dark rooms, night, flash and harsh light ------------ */
typedef enum pp_light_plan {
  PP_LIGHT_NORMAL = 0,
  PP_LIGHT_MOONLIGHT = 1,      /* dim: merged burst, night rendering */
  PP_LIGHT_FLASH = 2,          /* very dark: flash (rear: Natural Flash; front: screen flash) */
  PP_LIGHT_BRIGHT_BALANCE = 3  /* harsh or backlit: highlight-safe burst, local tone */
} pp_light_plan;

typedef struct pp_light_meter_input {
  float iso;             /* the camera's current exposure; 0 = unknown */
  float exposure_s;
  float f_number;        /* 0 = unknown */
  int32_t front;         /* 1: front camera */
  int32_t has_flash;     /* 1: a flash unit */
  float face_luma;       /* main face's mean luma 0..1, -1 = no face */
  float focus_diopters;  /* the lens's focus (1/metres), -1 = unknown */
  float reserved[3];
} pp_light_meter_input;

typedef struct pp_light_meter_result {
  float ev100, lux;
  int32_t exposure_known;
  float key, highlights, shadows, range_stops;
  int32_t backlit;
  int32_t plan;           /* pp_light_plan */
  int32_t frames;         /* burst length for the plan */
  float exposure_bias;    /* EV for the burst */
  float night;            /* 0..1 night rendering for pp_light_tone */
  uint8_t screen_flash[4];/* RGB overlay for a front "flash" (4th unused) */
  float reserved[4];
} pp_light_meter_result;

/* Reads the scene from a small preview frame and the camera's exposure. */
pp_status pp_light_meter(const pp_image* preview, const pp_light_meter_input* input, pp_light_meter_result* out);

typedef struct pp_light_tone_params {
  int32_t auto_exposure; /* 1: bring the scene's key to a pleasing level */
  float exposure;        /* stops, added */
  float shadows;         /* 0..1 lift; -1 = automatic */
  float highlights;      /* 0..1 compression; -1 = automatic */
  float night;           /* 0..1 night rendering */
  float detail;          /* 0..1 local contrast */
  float strength;        /* 0..1 */
  float white_balance;   /* 0..1 low-light colour correction (kept a touch warm) */
  float capture_bias;    /* EV the frames were deliberately underexposed by (<= 0, e.g. the
                            meter's exposure_bias); restored in full, while the scene's own
                            darkness is lifted only part way */
  float reserved[2];
} pp_light_tone_params;

void pp_light_tone_default_params(pp_light_tone_params* out);

/* Local tone mapping in place (after a burst merge): highlights compressed,
 * shadows lifted where they hold detail, night kept looking like night.
 * `report_out` (optional, 6 floats): key before, key after, range (stops),
 * shadows, highlights, share clipped. */
pp_status pp_light_tone(pp_image* image, const pp_light_tone_params* params, const pp_cancel_token* token, float* report_out);

typedef struct pp_flash_fuse_params {
  float fill;      /* 0..1 how much of the flash's own light is kept (0: the ambient look only) */
  float strength;  /* 0..1 */
  float reserved[4];
} pp_flash_fuse_params;

/* Natural Flash: `ambient` (no flash, ideally a merged burst) and `flash`
 * (same size) fused into `out` (same size): the room's light and colour,
 * the flash frame's clean detail; flash shadows and hot spots from the
 * ambient. `report_out` (optional, 4 floats): aligned (0/1), flash gain,
 * shadow share, hot-spot share. */
pp_status pp_flash_fuse(const pp_image* ambient, const pp_image* flash, pp_image* out, const pp_flash_fuse_params* params,
                        const pp_cancel_token* token, float* report_out);

/* ---- Pose assistant: body landmarks, pose matching and guidance ---------
 * Landmarks are MediaPipe's 33 (BlazePose topology), normalised image
 * coordinates of an upright image, the person's own left/right. */
#define PP_POSE_LANDMARKS 33
#define PP_POSE_BONES 12

typedef struct pp_pose_point {
  float x, y, z;       /* normalised; z relative depth (x's scale, smaller = nearer) */
  float visibility;    /* 0..1 not hidden */
  float presence;      /* 0..1 in the picture */
} pp_pose_point;

typedef struct pp_pose {
  pp_pose_point points[PP_POSE_LANDMARKS];
  float aspect;        /* image width / height */
  int32_t valid;       /* 0: nobody */
  float reserved[2];
} pp_pose;

/* The pose as seen in a mirror: x flipped, left and right swapped. */
pp_status pp_pose_mirror(const pp_pose* in, pp_pose* out);

typedef enum pp_pose_bone {
  PP_POSE_LEFT_UPPER_ARM = 0, PP_POSE_RIGHT_UPPER_ARM, PP_POSE_LEFT_FOREARM, PP_POSE_RIGHT_FOREARM,
  PP_POSE_LEFT_THIGH, PP_POSE_RIGHT_THIGH, PP_POSE_LEFT_SHIN, PP_POSE_RIGHT_SHIN,
  PP_POSE_TORSO, PP_POSE_SHOULDER_LINE, PP_POSE_HIP_LINE, PP_POSE_HEAD_TILT
} pp_pose_bone;

typedef enum pp_pose_shot {
  PP_POSE_SHOT_FACE = 0, PP_POSE_SHOT_HEAD_AND_SHOULDERS, PP_POSE_SHOT_HALF_BODY, PP_POSE_SHOT_THREE_QUARTER,
  PP_POSE_SHOT_FULL_BODY
} pp_pose_shot;

/* The pose's representation: independent of where the person stands and
 * how big they are (body units = a face-on shoulder width). */
typedef struct pp_pose_description {
  int32_t valid;
  float anchor_x, anchor_y;     /* shoulder centre (or the face's), normalised */
  int32_t anchor_from_shoulders;
  float scale;                  /* shoulder width / image height */
  float nx[PP_POSE_LANDMARKS], ny[PP_POSE_LANDMARKS];  /* landmarks in body units from the anchor */
  float reliability[PP_POSE_LANDMARKS];               /* 0..1 */
  float direction[PP_POSE_BONES];        /* radians in the image plane (0 = right, +pi/2 = down) */
  float bone_reliability[PP_POSE_BONES];
  float left_elbow_bend, right_elbow_bend, left_knee_bend, right_knee_bend;  /* radians, 0 = straight */
  float body_yaw, head_yaw, head_roll;   /* turn (radians), head turn (-1..1), head tilt (radians) */
  float body_yaw_reliability, head_reliability;
  int32_t shot;                          /* pp_pose_shot */
  float top, bottom, left, right;        /* extent of the reliable landmarks */
  float reserved[4];
} pp_pose_description;

pp_status pp_pose_describe(const pp_pose* pose, pp_pose_description* out);

typedef enum pp_pose_part {
  PP_POSE_PART_BODY = 0, PP_POSE_PART_HEAD, PP_POSE_PART_LEFT_HAND, PP_POSE_PART_RIGHT_HAND, PP_POSE_PART_LEFT_ELBOW,
  PP_POSE_PART_RIGHT_ELBOW, PP_POSE_PART_LEFT_FOOT, PP_POSE_PART_RIGHT_FOOT, PP_POSE_PART_LEFT_KNEE,
  PP_POSE_PART_RIGHT_KNEE, PP_POSE_PART_SHOULDERS, PP_POSE_PART_CAMERA
} pp_pose_part;

/* Directions are the image's (left = toward the image's left edge); "in"
 * and "out" are toward and away from the body's centre line. */
typedef enum pp_pose_action {
  PP_POSE_NO_PERSON = 0, PP_POSE_SHOW, PP_POSE_CLOSER, PP_POSE_BACK, PP_POSE_MOVE_LEFT, PP_POSE_MOVE_RIGHT,
  PP_POSE_CAMERA_UP, PP_POSE_CAMERA_DOWN, PP_POSE_TURN_LEFT, PP_POSE_TURN_RIGHT, PP_POSE_UP, PP_POSE_DOWN,
  PP_POSE_IN, PP_POSE_OUT, PP_POSE_TILT_LEFT, PP_POSE_TILT_RIGHT, PP_POSE_LOOK_LEFT, PP_POSE_LOOK_RIGHT,
  PP_POSE_LEAN_LEFT, PP_POSE_LEAN_RIGHT, PP_POSE_STRAIGHTEN_UP, PP_POSE_HOLD_STILL, PP_POSE_PERFECT
} pp_pose_action;

typedef struct pp_pose_advice {
  int32_t part;    /* pp_pose_part */
  int32_t action;  /* pp_pose_action */
  float amount;
} pp_pose_advice;

typedef struct pp_pose_match_params {
  int32_t framing;   /* 1: compare position and size in the frame too */
  float strictness;  /* 0..1 */
  float reserved[2];
} pp_pose_match_params;

typedef struct pp_pose_match_result {
  float score;          /* 0..100 */
  float coverage;       /* 0..1 share of the reference compared */
  float pose_score, framing_score;  /* 0..100 */
  float bone_score[PP_POSE_BONES];  /* 0..1, -1 = not compared */
  float left_hand_score, right_hand_score;  /* 0..1, -1 = not compared */
  float offset_x, offset_y;  /* person minus reference position (share of the frame) */
  float size_ratio;          /* person size / reference size */
  int32_t advice_count;
  pp_pose_advice advice[4];  /* most important first */
  float reserved[4];
} pp_pose_match_result;

pp_status pp_pose_match(const pp_pose* reference, const pp_pose* live, const pp_pose_match_params* params,
                        pp_pose_match_result* out);

/* Steadies live poses through brief occlusions (a hidden landmark is held,
 * trusted less, for up to 0.8 s) and measures stillness. `in` may be NULL
 * or invalid (nobody in this frame). `stillness_out` (optional): 0..1. */
typedef struct pp_pose_tracker pp_pose_tracker;
pp_status pp_pose_tracker_create(pp_pose_tracker** out);
void pp_pose_tracker_destroy(pp_pose_tracker* tracker);
pp_status pp_pose_tracker_update(pp_pose_tracker* tracker, const pp_pose* in, int64_t time_ms, pp_pose* out,
                                 float* stillness_out);

/* ------------------------------------------------------------------ studio
 * The photoshoot studio: a person cut out of their photo (transparent
 * background) and placed into another scene. */

typedef enum pp_studio_brush {
  PP_STUDIO_KEEP = 0,   /* this is the person */
  PP_STUDIO_REMOVE = 1, /* this is not */
  PP_STUDIO_HAIR = 2    /* fine detail here: let the colours decide */
} pp_studio_brush;

typedef struct pp_studio_stroke {
  int32_t brush;        /* pp_studio_brush */
  float radius;         /* share of the image's short side */
  int32_t count;        /* points */
  const float* points;  /* count × (x, y), normalised 0..1 */
} pp_studio_stroke;

typedef struct pp_studio_cutout_params {
  float softness;        /* 0..1 */
  float shift;           /* -1 grow .. +1 shrink the edge */
  float hair_detail;     /* 0..1 */
  int32_t clean_colours; /* free edge colours of the old background */
} pp_studio_cutout_params;

typedef struct pp_studio_cutout_report {
  float coverage, edge, hair_edge;
  float left, top, right, bottom; /* the person's box, normalised */
  int32_t cut_left, cut_top, cut_right, cut_bottom;
} pp_studio_cutout_report;

/* `subject`: GRAY8 person mask, any size. `zones`: optional GRAY8 person-parts
 * map (0 background, 1 hair, 2 body skin, 3 face skin, 4 clothes, 5 accessory).
 * `out`: same size and format as `image`; written with straight alpha. */
pp_status pp_studio_cutout(const pp_image* image, const pp_image* subject, const pp_image* zones,
                           const pp_studio_stroke* strokes, int32_t stroke_count,
                           const pp_studio_cutout_params* params, pp_image* out, const pp_cancel_token* token,
                           pp_studio_cutout_report* report);

typedef struct pp_studio_placement {
  float x, y;     /* bottom centre of the person's box in the frame, 0..1 */
  float height;   /* box height as a share of the frame height */
  float tilt;     /* -1..1 vertical perspective */
  float angle;    /* degrees */
  int32_t flip;
} pp_studio_placement;

typedef struct pp_studio_look {
  float color_match, light_wrap, shadow, cast_shadow; /* 0..1 */
  float light_x;  /* -1 light from the left .. 1 from the right */
  float horizon;  /* 0..1 from the top */
  float bg_blur, grain; /* 0..1 */
  uint32_t seed;
  float rim;      /* 0..1 rim light on the side facing the scene's light */
} pp_studio_look;

typedef struct pp_studio_compose_report {
  float left, top, right, bottom;
  float exposure;
  float balance[3];
  int32_t grounded;
} pp_studio_compose_report;

/* `background` is the output frame, edited in place; `cutout` is a
 * pp_studio_cutout result with the same channel order. */
pp_status pp_studio_compose(pp_image* background, const pp_image* cutout, const pp_studio_placement* placement,
                            const pp_studio_look* look, const pp_cancel_token* token, pp_studio_compose_report* report);

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

/* ---- Video: "Glide" (Phase 6) ------------------------------------------------
 * A short handheld clip made steady and clean, one frame at a time:
 *   pass 1: pp_video_estimate_motion(previous, current) for every frame;
 *   plan:   pp_video_stabilize(all motions) -> one warp per frame + crop zoom;
 *   once:   noise and blur measured on a middle frame (pp_analyze_quality),
 *           tone curves fitted from it (pp_video_fit_curves);
 *   pass 2: per frame, pp_video_enhance_frame (temporal denoise with the
 *           neighbours, lens deblur, the clip's tone), portrait tools, then
 *           pp_video_warp. */
typedef struct pp_video_motion {
  /* x' = a x - b y + tx, y' = b x + a y + ty (pixels, origin at the image
   * centre): where content of the previous frame appears in this one. */
  float a, b, tx, ty;
  float confidence; /* share of image blocks that agree, 0..1 */
  float reserved[3];
} pp_video_motion;

pp_status pp_video_estimate_motion(const pp_image* previous, const pp_image* current, pp_video_motion* out);
/* motions[k] relates frame k-1 to k (motions[0] ignored). strength 0..1,
 * max_zoom 1..1.5. `warps` receives count * 6 floats: output pixel -> source
 * pixel affine (sx = w0 x + w1 y + w2, sy = w3 x + w4 y + w5). `report` (3,
 * may be NULL) = zoom, shake before, shake after (px per frame). */
pp_status pp_video_stabilize(const pp_video_motion* motions, size_t count, int32_t width, int32_t height, float strength,
                             float max_zoom, float* warps, float* report);
/* dst: same format as src (any size), filled through the warp. */
pp_status pp_video_warp(const pp_image* src, const float warp[6], pp_image* dst, const pp_cancel_token* token);
/* Per-channel tone curves (768 bytes: R, G, B) turning `before` into `after`. */
pp_status pp_video_fit_curves(const pp_image* before, const pp_image* after, uint8_t curves[768]);
pp_status pp_video_apply_curves(pp_image* image, const uint8_t curves[768], const pp_cancel_token* token);
typedef struct pp_video_frame_params {
  float noise_sigma; /* the clip's noise (pp_quality_report.noise_sigma) */
  float blur_sigma;  /* the clip's blur (pp_quality_report.blur_sigma) */
  float denoise;     /* 0..1 */
  float sharpen;     /* 0..1 */
  float reserved[4];
} pp_video_frame_params;
/* One frame of the clip, cleaned into `dst` (same size and format).
 * `previous`/`next` (may be NULL at the ends) are the neighbouring frames;
 * `from_previous` is this frame's motion, `to_next` the next frame's.
 * `curves` (may be NULL) is the clip's tone. */
pp_status pp_video_enhance_frame(const pp_image* frame, const pp_image* previous, const pp_image* next,
                                 const pp_video_motion* from_previous, const pp_video_motion* to_next,
                                 const pp_video_frame_params* params, const uint8_t curves[768], pp_image* dst,
                                 const pp_cancel_token* token);
/* Temporal smoothing of a subject mask (GRAY8): `previous` becomes the
 * steadied mask for `current`. smoothing 0..0.9. */
pp_status pp_video_steady_mask(pp_image* previous, const pp_image* current, float smoothing);
/* Consensus of three neighbouring frames' subject masks (GRAY8, same size):
 * the per-pixel median, written to `out` (same size). A mistake in a single
 * frame is outvoted; real movement passes through. `previous` / `next` may
 * be NULL at the ends of the clip. */
pp_status pp_video_consensus_mask(const pp_image* previous, const pp_image* current, const pp_image* next, pp_image* out);

/* The room remembered behind the person, to correct a clip's subject masks:
 * a piece of the room the person model swallows for many frames (a beam
 * above the head, a shelf beside it), which would otherwise grow from the
 * person and shrink back like a shadow, is given back to the room because
 * the room was in plain view there a moment before. Hair and anything that
 * looks like the person nearby is kept. One per clip; call
 * pp_video_room_check once per frame, in order, before steadying the mask. */
typedef struct pp_video_room pp_video_room;
pp_status pp_video_room_create(pp_video_room** out);
void pp_video_room_destroy(pp_video_room* room);
/* `frame` RGBA/BGRA, `mask` GRAY8 with the frame's aspect (any size),
 * corrected in place. `from_previous` (optional): the frame's motion from the
 * previous one (as from pp_video_estimate_motion, in this frame's pixels), so
 * the memory follows the camera. `reclaimed_out` (optional): share of the
 * mask's person area given back to the room. */
pp_status pp_video_room_check(pp_video_room* room, const pp_image* frame, pp_image* mask, const pp_video_motion* from_previous,
                              const pp_cancel_token* token, float* reclaimed_out);

/* ---- Codec (Phase 4) ---------------------------------------------------- */
/* JPEG and PNG (read and write), BMP and GIF (read). Decoded images are RGBA8
 * with straight alpha, upright (JPEG EXIF orientation applied). Dimensions are
 * checked before decoding, and decoding counts toward the memory budget. */
typedef enum pp_codec {
  PP_CODEC_UNKNOWN = 0,
  PP_CODEC_JPEG = 1,
  PP_CODEC_PNG = 2,
  PP_CODEC_BMP = 3,
  PP_CODEC_GIF = 4
} pp_codec;

typedef struct pp_decode_options {
  int32_t max_edge;          /* > 0: shrink so the longer edge fits (area filter) */
  int32_t apply_orientation; /* 1 (default when options is NULL): upright pixels */
  int32_t reserved[6];
} pp_decode_options;

typedef struct pp_decode_info {
  int32_t width, height;               /* returned image */
  int32_t source_width, source_height; /* as stored */
  int32_t orientation;                 /* EXIF 1..8 */
  int32_t codec;                       /* pp_codec */
  int32_t has_alpha;
  int32_t reserved;
} pp_decode_info;

pp_status pp_decode_memory(const void* data, size_t size, const pp_decode_options* options,
                           const pp_cancel_token* token, pp_image** out, pp_decode_info* info);
pp_status pp_decode_file(const char* path, const pp_decode_options* options, const pp_cancel_token* token,
                         pp_image** out, pp_decode_info* info);
/* *data is allocated by the engine; release it with pp_free(). quality 1..100 (JPEG). */
pp_status pp_encode_memory(const pp_image* image, int32_t codec, int32_t quality, void** data, size_t* size);
/* Written atomically (temporary file, then rename). */
pp_status pp_encode_file(const pp_image* image, const char* path, int32_t codec, int32_t quality);
void pp_free(void* data);

/* ---- Geometry, colour, compositing, masks (Phase 4) ----------------------- */
/* Straightens the quadrilateral `quad` (x,y of top-left, top-right,
 * bottom-right, bottom-left in source pixels; convex) into width x height. */
pp_status pp_warp_perspective(const pp_image* image, const float quad[8], int32_t width, int32_t height,
                              const pp_cancel_token* token, pp_image** out);
/* Suggested PP_OP_PERSPECTIVE vertical amount (-1..1) that makes the photo's
 * near-vertical lines parallel; 0 when there are too few lines to judge. */
pp_status pp_detect_keystone(const pp_image* image, const pp_cancel_token* token, float* vertical);
/* 3D LUT: size^3 RGB float triples (red fastest, as in .cube files), values
 * 0..1; size 2..65; tetrahedral interpolation; strength 0..1. In place. */
pp_status pp_apply_lut3d(pp_image* image, const float* table, int32_t size, float strength,
                         const pp_cancel_token* token);
/* out = M (r,g,b,1), M row-major 3x4; linear_light != 0 applies it in linear light. In place. */
pp_status pp_color_matrix(pp_image* image, const float matrix[12], int32_t linear_light, const pp_cancel_token* token);

typedef enum pp_blend_mode {
  PP_BLEND_NORMAL = 0,
  PP_BLEND_MULTIPLY = 1,
  PP_BLEND_SCREEN = 2,
  PP_BLEND_OVERLAY = 3,
  PP_BLEND_SOFT_LIGHT = 4,
  PP_BLEND_HARD_LIGHT = 5,
  PP_BLEND_DARKEN = 6,
  PP_BLEND_LIGHTEN = 7,
  PP_BLEND_DIFFERENCE = 8,
  PP_BLEND_ADD = 9,
  PP_BLEND_COLOR = 10,
  PP_BLEND_LUMINOSITY = 11
} pp_blend_mode;

/* Draws `layer` onto `image` at (x, y), clipped (W3C compositing, straight
 * alpha). `mask` (GRAY8, layer-sized, may be NULL) scales the layer's alpha:
 * with a subject matte this is background replacement. */
pp_status pp_blend(pp_image* image, const pp_image* layer, const pp_image* mask, int32_t x, int32_t y,
                   int32_t mode, float opacity, const pp_cancel_token* token);

typedef enum pp_mask_op {
  PP_MASK_INVERT = 1,
  PP_MASK_FEATHER = 2,  /* amount = sigma in pixels */
  PP_MASK_GROW = 3,     /* amount = pixels; negative shrinks */
  PP_MASK_THRESHOLD = 4 /* amount = level 0..1 */
} pp_mask_op;

/* In place on a GRAY8 mask. */
pp_status pp_mask_adjust(pp_image* mask, int32_t op, float amount, const pp_cancel_token* token);
/* Snaps a coarse mask (any size, e.g. a model's output) to the edges of
 * `guide` and returns it at the guide's size (fast guided filter). */
pp_status pp_mask_refine(const pp_image* mask, const pp_image* guide, int32_t radius, float eps,
                         const pp_cancel_token* token, pp_image** out);

/* ---- Machine-learning helpers (Phase 4) ------------------------------------ */
/* Every on-device model shares these: preparing its input and turning its
 * output into a mask are native, parallel and identical everywhere. */
typedef enum pp_tensor_layout { PP_TENSOR_NHWC = 0, PP_TENSOR_NCHW = 1 } pp_tensor_layout;
typedef enum pp_tensor_fit {
  PP_FIT_STRETCH = 0,
  PP_FIT_LETTERBOX = 1, /* whole photo, padded with the mean colour (0 after normalisation) */
  PP_FIT_CROP_CENTER = 2
} pp_tensor_fit;

typedef struct pp_tensor_spec {
  int32_t width, height; /* model input size */
  int32_t layout;        /* pp_tensor_layout */
  int32_t fit;           /* pp_tensor_fit */
  float mean[3];         /* value = (pixel/255 - mean) / std, RGB order */
  float std[3];
  int32_t reserved[4];
} pp_tensor_spec;

/* Where the photo landed in the tensor: tensor_xy = photo_xy * scale + offset. */
typedef struct pp_tensor_placement {
  float scale_x, scale_y, offset_x, offset_y;
} pp_tensor_placement;

/* Fills `out` (width*height*3 floats) from the image. `placement` may be NULL. */
pp_status pp_image_to_tensor(const pp_image* image, const pp_tensor_spec* spec, float* out, size_t out_count,
                             const pp_cancel_token* token, pp_tensor_placement* placement);

typedef enum pp_activation { PP_ACTIVATION_NONE = 0, PP_ACTIVATION_SIGMOID = 1, PP_ACTIVATION_SOFTMAX = 2 } pp_activation;

/* A model's score map (width x height x channels, NHWC or NCHW) -> GRAY8 mask
 * of `channel` at out_width x out_height. `placement` (from
 * pp_image_to_tensor, may be NULL) crops out padding; photo_width/height are
 * the original photo's. Fails on NaN/Inf output (a broken model run). */
pp_status pp_tensor_to_mask(const float* data, int32_t width, int32_t height, int32_t channels, int32_t layout,
                            int32_t channel, int32_t activation, int32_t out_width, int32_t out_height,
                            const pp_tensor_placement* placement, int32_t photo_width, int32_t photo_height,
                            const pp_cancel_token* token, pp_image** out);

/* ---- GPU (Phase 4) -------------------------------------------------------- */
typedef struct pp_gpu_status {
  int32_t compiled;       /* built with GPU support */
  int32_t available;      /* initialised and verified against the CPU */
  int32_t max_difference; /* largest CPU/GPU difference in the self-test (0..255), -1 if not run */
  int32_t reserved;
  char renderer[96];
  char reason[160];
} pp_gpu_status;

/* Initialises and verifies the GPU (once; later calls report the result).
 * Safe to call from any thread; the GPU work runs on its own thread. */
pp_status pp_gpu_init(pp_gpu_status* out);
void pp_gpu_get_status(pp_gpu_status* out);
/* Frees the GPU context (memory pressure). pp_gpu_init can start it again. */
void pp_gpu_shutdown(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PHOTOPLUS_PP_ENGINE_H */
