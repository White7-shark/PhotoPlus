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
  float reserved[7];
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
