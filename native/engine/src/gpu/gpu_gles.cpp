// OpenGL ES 3.1 compute backend.
//
// A headless EGL context lives on one dedicated thread (GL contexts belong to
// a thread); callers post work to it and wait. A run of operations uploads
// the image once, runs one compute shader per operation on GPU buffers, and
// reads the result back once.
//
// Parity with the CPU is designed in, then verified: table-driven operations
// use the CPU's own tables, the blur uses the CPU's own kernel weights and
// the same accumulation order, and integer formulas are the same. At start-up
// a self-test renders every supported operation on both and compares; the GPU
// is enabled only if every pixel is within kMaxDiff levels.

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES3/gl31.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "photoplus/edit.hpp"
#include "photoplus/filters.hpp"
#include "photoplus/gpu.hpp"
#include "photoplus/operations.hpp"
#include "photoplus/profiler.hpp"

namespace photoplus::gpu {

namespace {

constexpr int kMaxDiff = 2;
// Previews and edits up to this size go to the GPU; larger renders stay on
// the CPU (its strips keep memory flat; GPU buffers are whole images).
constexpr int64_t kMaxPixels = 4'200'000;

// ---- shaders ------------------------------------------------------------------------

const char* kHeader = R"(#version 310 es
precision highp float;
precision highp int;
layout(local_size_x = 16, local_size_y = 16) in;
uniform ivec2 uSize;
uniform ivec3 uOrder; // byte index of R, G, B in a pixel
uint channel(uint v, int i) { return (v >> uint(8 * i)) & 255u; }
uint withChannel(uint v, int i, uint c) { uint s = uint(8 * i); return (v & ~(255u << s)) | (c << s); }
uint toByte(float v) { return uint(clamp(v, 0.0, 255.0) + 0.5); }
uint roundByte(float v) { return uint(floor(clamp(v, 0.0, 255.0) + 0.5)); }
)";

// Per-channel tables (tone, warmth): 3 x 256 entries.
const char* kLut = R"(
layout(std430, binding = 0) buffer Img { uint px[]; };
layout(std430, binding = 1) readonly buffer Lut { uint lut[]; };
void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= uSize.x || p.y >= uSize.y) return;
  int i = p.y * uSize.x + p.x;
  uint v = px[i];
  v = withChannel(v, uOrder.x, lut[channel(v, uOrder.x)]);
  v = withChannel(v, uOrder.y, lut[256u + channel(v, uOrder.y)]);
  v = withChannel(v, uOrder.z, lut[512u + channel(v, uOrder.z)]);
  px[i] = v;
}
)";

const char* kSaturation = R"(
layout(std430, binding = 0) buffer Img { uint px[]; };
uniform float uS;
void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= uSize.x || p.y >= uSize.y) return;
  int i = p.y * uSize.x + p.x;
  uint v = px[i];
  float r = float(channel(v, uOrder.x)), g = float(channel(v, uOrder.y)), b = float(channel(v, uOrder.z));
  float l = 0.2126 * r + 0.7152 * g + 0.0722 * b;
  v = withChannel(v, uOrder.x, roundByte(l + (r - l) * uS));
  v = withChannel(v, uOrder.y, roundByte(l + (g - l) * uS));
  v = withChannel(v, uOrder.z, roundByte(l + (b - l) * uS));
  px[i] = v;
}
)";

const char* kGray = R"(
layout(std430, binding = 0) buffer Img { uint px[]; };
void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= uSize.x || p.y >= uSize.y) return;
  int i = p.y * uSize.x + p.x;
  uint v = px[i];
  uint l = (channel(v, uOrder.x) * 54u + channel(v, uOrder.y) * 183u + channel(v, uOrder.z) * 19u) >> 8u;
  v = withChannel(withChannel(withChannel(v, uOrder.x, l), uOrder.y, l), uOrder.z, l);
  px[i] = v;
}
)";

// Separable Gaussian, premultiplied alpha, same taps and order as the CPU.
const char* kBlurH = R"(
layout(std430, binding = 0) readonly buffer Img { uint px[]; };
layout(std430, binding = 1) writeonly buffer Mid { vec4 mid[]; };
layout(std430, binding = 2) readonly buffer K { float w[]; };
uniform int uFirst;
uniform int uCount;
uniform int uAlpha; // byte index of alpha, or -1
vec4 load(uint v) {
  vec4 c = vec4(float(channel(v, 0)), float(channel(v, 1)), float(channel(v, 2)), float(channel(v, 3)));
  if (uAlpha >= 0) {
    float a = c[uAlpha] * (1.0 / 255.0);
    for (int k = 0; k < 4; ++k) if (k != uAlpha) c[k] = c[k] * a;
  }
  return c;
}
void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= uSize.x || p.y >= uSize.y) return;
  vec4 acc = vec4(0.0);
  for (int k = 0; k < uCount; ++k) {
    float wk = w[k];
    if (wk == 0.0) continue;
    int x = clamp(p.x + uFirst + k, 0, uSize.x - 1);
    acc += wk * load(px[p.y * uSize.x + x]);
  }
  mid[p.y * uSize.x + p.x] = acc;
}
)";

const char* kBlurV = R"(
layout(std430, binding = 0) writeonly buffer Img { uint px[]; };
layout(std430, binding = 1) readonly buffer Mid { vec4 mid[]; };
layout(std430, binding = 2) readonly buffer K { float w[]; };
uniform int uFirst;
uniform int uCount;
uniform int uAlpha;
void main() {
  ivec2 p = ivec2(gl_GlobalInvocationID.xy);
  if (p.x >= uSize.x || p.y >= uSize.y) return;
  vec4 acc = vec4(0.0);
  for (int k = 0; k < uCount; ++k) {
    float wk = w[k];
    if (wk == 0.0) continue;
    int y = clamp(p.y + uFirst + k, 0, uSize.y - 1);
    acc += wk * mid[y * uSize.x + p.x];
  }
  uint v = 0u;
  if (uAlpha >= 0) {
    float a = acc[uAlpha];
    float inv = a > 0.5 ? 255.0 / a : 0.0;
    for (int k = 0; k < 4; ++k) v |= (k == uAlpha ? toByte(a) : toByte(acc[k] * inv)) << uint(8 * k);
  } else {
    for (int k = 0; k < 4; ++k) v |= toByte(acc[k]) << uint(8 * k);
  }
  px[p.y * uSize.x + p.x] = v;
}
)";

enum Kernel { kLutK = 0, kSatK, kGrayK, kBlurHK, kBlurVK, kKernelCount };

// ---- the GPU thread -----------------------------------------------------------------

class Device {
 public:
  static Device& instance() {
    static Device* d = new Device();  // never destroyed: shutdown() frees resources
    return *d;
  }

  // Runs fn on the GPU thread and waits for it.
  template <typename T>
  T call(std::function<T()> fn) {
    std::packaged_task<T()> task(std::move(fn));
    auto result = task.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
      queue_.emplace_back([&task] { task(); });
    }
    wake_.notify_one();
    return result.get();
  }

  // GPU-thread state.
  EGLDisplay display = EGL_NO_DISPLAY;
  EGLContext context = EGL_NO_CONTEXT;
  EGLSurface surface = EGL_NO_SURFACE;
  std::array<GLuint, kKernelCount> programs{};
  Status state;
  bool initialised = false;

 private:
  void loop() {
    for (;;) {
      std::function<void()> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return !queue_.empty(); });
        job = std::move(queue_.front());
        queue_.pop_front();
      }
      job();
    }
  }

  std::mutex mutex_;
  std::condition_variable wake_;
  std::deque<std::function<void()>> queue_;
  std::thread thread_;
};

std::mutex g_statusMutex;
Status g_status;
std::atomic<bool> g_available{false};

void publish(const Status& s) {
  std::lock_guard<std::mutex> lock(g_statusMutex);
  g_status = s;
  g_available.store(s.available, std::memory_order_release);
}

GLuint compile(const char* body, std::string* log) {
  const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
  const char* sources[2] = {kHeader, body};
  glShaderSource(shader, 2, sources, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char buf[1024] = {0};
    glGetShaderInfoLog(shader, sizeof(buf) - 1, nullptr, buf);
    *log = buf;
    glDeleteShader(shader);
    return 0;
  }
  const GLuint program = glCreateProgram();
  glAttachShader(program, shader);
  glLinkProgram(program);
  glDeleteShader(shader);
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    char buf[1024] = {0};
    glGetProgramInfoLog(program, sizeof(buf) - 1, nullptr, buf);
    *log = buf;
    glDeleteProgram(program);
    return 0;
  }
  return program;
}

void destroyContext(Device& d) {
  for (GLuint& p : d.programs) {
    if (p != 0 && d.context != EGL_NO_CONTEXT) glDeleteProgram(p);
    p = 0;
  }
  if (d.display != EGL_NO_DISPLAY) {
    eglMakeCurrent(d.display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    if (d.surface != EGL_NO_SURFACE) eglDestroySurface(d.display, d.surface);
    if (d.context != EGL_NO_CONTEXT) eglDestroyContext(d.display, d.context);
    eglTerminate(d.display);
  }
  d.display = EGL_NO_DISPLAY;
  d.context = EGL_NO_CONTEXT;
  d.surface = EGL_NO_SURFACE;
  d.initialised = false;
}

// GPU thread: EGL display, ES 3.1 context (surfaceless when supported, else
// a 1x1 pbuffer), and the compute programs.
bool createContext(Device& d, std::string* reason) {
  EGLDisplay display = EGL_NO_DISPLAY;
#ifdef EGL_PLATFORM_SURFACELESS_MESA
  const char* clientExt = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (clientExt != nullptr && std::strstr(clientExt, "EGL_MESA_platform_surfaceless") != nullptr) {
    auto getPlatformDisplay = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(eglGetProcAddress("eglGetPlatformDisplayEXT"));
    if (getPlatformDisplay != nullptr) display = getPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
  }
#endif
  if (display == EGL_NO_DISPLAY) display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  EGLint major = 0, minor = 0;
  if (display == EGL_NO_DISPLAY || !eglInitialize(display, &major, &minor)) {
    *reason = "No EGL display";
    return false;
  }
  d.display = display;
  eglBindAPI(EGL_OPENGL_ES_API);
  const char* ext = eglQueryString(display, EGL_EXTENSIONS);
  const bool surfaceless = ext != nullptr && std::strstr(ext, "EGL_KHR_surfaceless_context") != nullptr;
  const EGLint configAttrs[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, surfaceless ? 0 : EGL_PBUFFER_BIT,
                                EGL_NONE};
  EGLConfig config = nullptr;
  EGLint count = 0;
  if (!eglChooseConfig(display, configAttrs, &config, 1, &count) || count == 0) {
    *reason = "No OpenGL ES 3 configuration";
    return false;
  }
  const EGLint contextAttrs[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 1, EGL_NONE};
  d.context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
  if (d.context == EGL_NO_CONTEXT) {
    *reason = "OpenGL ES 3.1 is not supported";
    return false;
  }
  if (!surfaceless) {
    const EGLint pbuffer[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
    d.surface = eglCreatePbufferSurface(display, config, pbuffer);
  }
  if (!eglMakeCurrent(display, d.surface, d.surface, d.context)) {
    *reason = "Unable to use the OpenGL ES context";
    return false;
  }
  const char* bodies[kKernelCount] = {kLut, kSaturation, kGray, kBlurH, kBlurV};
  for (int k = 0; k < kKernelCount; ++k) {
    std::string log;
    d.programs[k] = compile(bodies[k], &log);
    if (d.programs[k] == 0) {
      *reason = "Shader compile failed: " + log.substr(0, 120);
      return false;
    }
  }
  const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
  d.state.renderer = renderer != nullptr ? renderer : "";
  d.initialised = true;
  return true;
}

struct Order {
  int r, g, b, alpha;
};

Order orderOf(PixelFormat f) { return f == PixelFormat::BGRA8 ? Order{2, 1, 0, 3} : Order{0, 1, 2, 3}; }

GLuint makeBuffer(GLsizeiptr bytes, const void* data, GLenum usage) {
  GLuint b = 0;
  glGenBuffers(1, &b);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, b);
  glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, usage);
  return b;
}

void setCommon(GLuint program, int w, int h, const Order& o) {
  glUseProgram(program);
  glUniform2i(glGetUniformLocation(program, "uSize"), w, h);
  glUniform3i(glGetUniformLocation(program, "uOrder"), o.r, o.g, o.b);
}

void dispatch(int w, int h) {
  glDispatchCompute(static_cast<GLuint>((w + 15) / 16), static_cast<GLuint>((h + 15) / 16), 1);
  glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
}

// GPU thread: runs ops on a copy of `input`.
Result<ImageBuffer> execute(Device& d, const ImageBuffer& input, const pp_op* ops, std::size_t count,
                            const CancellationToken* token) {
  const int w = input.width(), h = input.height();
  const Order o = orderOf(input.format());
  const std::size_t pixels = static_cast<std::size_t>(w) * h;
  std::vector<uint32_t> packed(pixels);
  for (int y = 0; y < h; ++y) std::memcpy(packed.data() + static_cast<std::size_t>(y) * w, input.row(y), static_cast<std::size_t>(w) * 4);
  const GLuint img = makeBuffer(static_cast<GLsizeiptr>(pixels * 4), packed.data(), GL_DYNAMIC_COPY);
  GLuint mid = 0, aux = 0;
  auto cleanup = [&] {
    if (img) glDeleteBuffers(1, &img);
    if (mid) glDeleteBuffers(1, &mid);
    if (aux) glDeleteBuffers(1, &aux);
  };
  for (std::size_t i = 0; i < count; ++i) {
    if (isCancelled(token)) {
      cleanup();
      return failure(makeError(PP_ERROR_CANCELLED, "Processing was cancelled"));
    }
    const pp_op& op = ops[i];
    if (aux) glDeleteBuffers(1, &aux), aux = 0;
    switch (op.type) {
      case PP_OP_TONE:
      case PP_OP_WARMTH: {
        std::array<uint32_t, 768> table{};
        if (op.type == PP_OP_TONE) {
          const auto lut = ops::toneLut({op.p[0], op.p[1], op.p[2]});
          for (int c = 0; c < 3; ++c) {
            for (int v = 0; v < 256; ++v) table[static_cast<std::size_t>(c * 256 + v)] = lut[static_cast<std::size_t>(v)];
          }
        } else {
          const auto luts = edit::warmthLuts(op.p[0], op.p[1]);
          for (int c = 0; c < 3; ++c) {
            for (int v = 0; v < 256; ++v) table[static_cast<std::size_t>(c * 256 + v)] = luts[static_cast<std::size_t>(c)][static_cast<std::size_t>(v)];
          }
        }
        aux = makeBuffer(sizeof(table), table.data(), GL_STATIC_DRAW);
        setCommon(d.programs[kLutK], w, h, o);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, img);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, aux);
        dispatch(w, h);
        break;
      }
      case PP_OP_SATURATION:
        setCommon(d.programs[kSatK], w, h, o);
        glUniform1f(glGetUniformLocation(d.programs[kSatK], "uS"), 1.0f + std::clamp(op.p[0], -1.0f, 1.0f));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, img);
        dispatch(w, h);
        break;
      case PP_OP_GRAYSCALE:
        setCommon(d.programs[kGrayK], w, h, o);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, img);
        dispatch(w, h);
        break;
      case PP_OP_BLUR: {
        if (op.p[0] < 0.3f) break;  // the CPU copies the image unchanged too
        const ops::GaussianKernel k = ops::gaussianKernel(op.p[0]);
        aux = makeBuffer(static_cast<GLsizeiptr>(k.weights.size() * sizeof(float)), k.weights.data(), GL_STATIC_DRAW);
        if (!mid) mid = makeBuffer(static_cast<GLsizeiptr>(pixels * 16), nullptr, GL_DYNAMIC_COPY);
        for (int pass = 0; pass < 2; ++pass) {
          const GLuint prog = d.programs[pass == 0 ? kBlurHK : kBlurVK];
          setCommon(prog, w, h, o);
          glUniform1i(glGetUniformLocation(prog, "uFirst"), k.first);
          glUniform1i(glGetUniformLocation(prog, "uCount"), static_cast<GLint>(k.weights.size()));
          glUniform1i(glGetUniformLocation(prog, "uAlpha"), o.alpha);
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, img);
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, mid);
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, aux);
          dispatch(w, h);
        }
        break;
      }
      default:
        cleanup();
        return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Operation not supported on the GPU"));
    }
    if (token != nullptr) token->advance(1.0f / static_cast<float>(count));
  }
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, img);
  const void* mapped = glMapBufferRange(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(pixels * 4), GL_MAP_READ_BIT);
  if (mapped == nullptr || glGetError() != GL_NO_ERROR) {
    if (mapped) glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
    cleanup();
    return failure(makeError(PP_ERROR_INTERNAL, "Reading the GPU result failed"));
  }
  auto out = ImageBuffer::allocate(w, h, input.format());
  if (out) {
    for (int y = 0; y < h; ++y) {
      std::memcpy(out->row(y), static_cast<const uint8_t*>(mapped) + static_cast<std::size_t>(y) * w * 4, static_cast<std::size_t>(w) * 4);
    }
  }
  glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
  cleanup();
  if (!out) return failure(out.error());
  return std::move(out).value();
}

// A test photo: gradients, noise-like detail, translucency.
ImageBuffer testImage() {
  auto img = ImageBuffer::allocate(193, 131, PixelFormat::RGBA8);
  for (int y = 0; y < 131; ++y) {
    for (int x = 0; x < 193; ++x) {
      uint8_t* p = img->row(y) + x * 4;
      p[0] = static_cast<uint8_t>((x * 255) / 192);
      p[1] = static_cast<uint8_t>((y * 255) / 130);
      p[2] = static_cast<uint8_t>((x * 37 + y * 91) % 256);
      p[3] = static_cast<uint8_t>(x % 7 == 0 ? 120 : 255);
    }
  }
  return std::move(img).value();
}

int maxDifference(const ImageBuffer& a, const ImageBuffer& b) {
  int worst = 0;
  for (int y = 0; y < a.height(); ++y) {
    for (int x = 0; x < a.width() * 4; ++x) worst = std::max(worst, std::abs(a.row(y)[x] - b.row(y)[x]));
  }
  return worst;
}

pp_op makeOp(int type, float a = 0, float b = 0, float c = 0) {
  pp_op o{};
  o.type = type;
  o.p[0] = a, o.p[1] = b, o.p[2] = c;
  return o;
}

// GPU thread: renders each supported operation on both and compares.
int selfTest(Device& d, std::string* reason) {
  const ImageBuffer src = testImage();
  const pp_op cases[] = {makeOp(PP_OP_TONE, 0.7f, 0.1f, 0.25f), makeOp(PP_OP_TONE, -1.2f, -0.2f, -0.3f),
                         makeOp(PP_OP_WARMTH, 0.6f, -0.3f),        makeOp(PP_OP_SATURATION, 0.45f),
                         makeOp(PP_OP_SATURATION, -0.8f),          makeOp(PP_OP_GRAYSCALE),
                         makeOp(PP_OP_BLUR, 1.2f),                 makeOp(PP_OP_BLUR, 6.5f)};
  int worst = 0;
  for (const pp_op& op : cases) {
    auto cpu = cloneImage(src);
    Error e{PP_OK, {}};
    switch (op.type) {
      case PP_OP_TONE:
        e = ops::tone(cpu.value(), {op.p[0], op.p[1], op.p[2]}, nullptr);
        break;
      case PP_OP_WARMTH:
        e = edit::warmth(cpu.value(), op.p[0], op.p[1], nullptr);
        break;
      case PP_OP_SATURATION:
        e = ops::saturation(cpu.value(), op.p[0], nullptr);
        break;
      case PP_OP_GRAYSCALE:
        e = ops::grayscale(cpu.value(), nullptr);
        break;
      case PP_OP_BLUR: {
        auto b = ops::blur(src, op.p[0], nullptr);
        if (b) cpu = std::move(b);
        break;
      }
      default:
        break;
    }
    auto gpu = execute(d, src, &op, 1, nullptr);
    if (e.status != PP_OK || !cpu || !gpu) {
      *reason = "Self-test could not run";
      return 255;
    }
    worst = std::max(worst, maxDifference(cpu.value(), gpu.value()));
  }
  return worst;
}

}  // namespace

Status init() {
  Device& d = Device::instance();
  return d.call<Status>([&d]() -> Status {
    if (d.initialised || !d.state.reason.empty()) return d.state;
    Status s;
    s.compiled = true;
    std::string reason;
    if (!createContext(d, &reason)) {
      destroyContext(d);
      s.reason = reason;
      d.state = s;
      publish(s);
      return s;
    }
    s.renderer = d.state.renderer;
    s.maxDifference = selfTest(d, &reason);
    if (s.maxDifference > kMaxDiff) {
      s.reason = reason.empty() ? "GPU results differ from the CPU (by " + std::to_string(s.maxDifference) + " levels)" : reason;
      destroyContext(d);
    } else {
      s.available = true;
    }
    d.state = s;
    publish(s);
    return s;
  });
}

Status status() {
  std::lock_guard<std::mutex> lock(g_statusMutex);
  Status s = g_status;
  s.compiled = true;
  return s;
}

bool available() { return g_available.load(std::memory_order_acquire); }

void shutdown() {
  if (!available()) return;
  Device& d = Device::instance();
  d.call<int>([&d]() -> int {
    destroyContext(d);
    d.state = Status{};
    d.state.compiled = true;
    publish(d.state);
    return 0;
  });
}

bool supports(const pp_op& op) {
  switch (op.type) {
    case PP_OP_TONE:
    case PP_OP_WARMTH:
    case PP_OP_SATURATION:
    case PP_OP_GRAYSCALE:
      return true;
    case PP_OP_BLUR:
      return op.p[0] <= 8.0f;  // larger blurs use the CPU's reduced-copy path
    default:
      return false;
  }
}

Result<ImageBuffer> run(const ImageBuffer& input, const pp_op* ops, std::size_t count, const CancellationToken* token) {
  ScopedTimer timer("gpu.run");
  if (!available()) return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "No verified GPU"));
  if (input.format() != PixelFormat::RGBA8 && input.format() != PixelFormat::BGRA8) {
    return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "The GPU path takes RGBA images"));
  }
  if (static_cast<int64_t>(input.width()) * input.height() > kMaxPixels) {
    return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Large renders run on the CPU"));
  }
  for (std::size_t i = 0; i < count; ++i) {
    if (!supports(ops[i])) return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "Operation not supported on the GPU"));
  }
  Device& d = Device::instance();
  return d.call<Result<ImageBuffer>>([&]() -> Result<ImageBuffer> {
    if (!d.initialised) return failure(makeError(PP_ERROR_UNSUPPORTED_FORMAT, "No verified GPU"));
    return execute(d, input, ops, count, token);
  });
}

}  // namespace photoplus::gpu
