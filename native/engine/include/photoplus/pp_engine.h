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
pp_status pp_image_create(int32_t width, int32_t height, pp_pixel_format format, pp_image** out);
pp_status pp_image_wrap(void* pixels, int32_t width, int32_t height, int32_t stride,
                        pp_pixel_format format, pp_image** out);
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* PHOTOPLUS_PP_ENGINE_H */
