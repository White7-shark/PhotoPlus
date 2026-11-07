package com.photoplus.beauty

import android.app.ActivityManager
import android.content.Context
import android.content.res.AssetManager
import android.graphics.SurfaceTexture
import android.opengl.EGL14
import android.opengl.EGLConfig
import android.opengl.EGLContext
import android.opengl.EGLDisplay
import android.opengl.EGLExt
import android.opengl.EGLSurface
import android.opengl.GLES11Ext
import android.opengl.GLES30
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import android.view.Surface
import androidx.camera.core.CameraEffect
import androidx.camera.core.SurfaceOutput
import androidx.camera.core.SurfaceProcessor
import androidx.camera.core.SurfaceRequest
import androidx.core.util.Consumer
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.util.concurrent.Executor

private const val TAG = "PhotoPlusBeautyGl"

/** EGL display, a GLES 3 context and its surfaces (one GL thread). */
internal class EglCore {
  val display: EGLDisplay = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY)
  private val config: EGLConfig
  val context: EGLContext
  private val pbuffer: EGLSurface

  init {
    check(display != EGL14.EGL_NO_DISPLAY) { "no EGL display" }
    val version = IntArray(2)
    check(EGL14.eglInitialize(display, version, 0, version, 1)) { "eglInitialize failed" }
    val attribs = intArrayOf(
      EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8, EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8,
      EGL14.EGL_RENDERABLE_TYPE, EGLExt.EGL_OPENGL_ES3_BIT_KHR,
      EGL14.EGL_SURFACE_TYPE, EGL14.EGL_WINDOW_BIT or EGL14.EGL_PBUFFER_BIT,
      EGL14.EGL_NONE,
    )
    val configs = arrayOfNulls<EGLConfig>(1)
    val count = IntArray(1)
    check(EGL14.eglChooseConfig(display, attribs, 0, configs, 0, 1, count, 0) && count[0] > 0) { "no GLES 3 config" }
    config = configs[0]!!
    context = EGL14.eglCreateContext(display, config, EGL14.EGL_NO_CONTEXT, intArrayOf(EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE), 0)
    check(context != EGL14.EGL_NO_CONTEXT) { "no GLES 3 context" }
    pbuffer = EGL14.eglCreatePbufferSurface(display, config, intArrayOf(EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE), 0)
    makeCurrent(pbuffer)
  }

  fun createWindowSurface(surface: Surface): EGLSurface {
    val s = EGL14.eglCreateWindowSurface(display, config, surface, intArrayOf(EGL14.EGL_NONE), 0)
    check(s != EGL14.EGL_NO_SURFACE) { "eglCreateWindowSurface failed" }
    return s
  }

  fun makeCurrent(surface: EGLSurface) {
    check(EGL14.eglMakeCurrent(display, surface, surface, context)) { "eglMakeCurrent failed" }
  }

  fun makeCurrentOffscreen() = makeCurrent(pbuffer)

  fun swap(surface: EGLSurface, timestampNs: Long) {
    EGLExt.eglPresentationTimeANDROID(display, surface, timestampNs)
    EGL14.eglSwapBuffers(display, surface)
  }

  fun releaseSurface(surface: EGLSurface) {
    EGL14.eglDestroySurface(display, surface)
  }

  fun release() {
    EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_CONTEXT)
    EGL14.eglDestroySurface(display, pbuffer)
    EGL14.eglDestroyContext(display, context)
    EGL14.eglReleaseThread()
    EGL14.eglTerminate(display)
  }
}

/**
 * The live beauty passes (assets beauty/): a quarter-resolution low-pass
 * (subject mask in alpha), a half-resolution skin base, then the full
 * composite into each output. The same shader files are checked against the
 * CPU engine on real photos (scripts/beauty-gpu-parity.mjs).
 */
internal class BeautyRenderer(private val assets: AssetManager) {
  private class Program(val id: Int) {
    private val cache = HashMap<String, Int>()
    fun loc(name: String) = cache.getOrPut(name) { GLES30.glGetUniformLocation(id, name) }
  }

  private class Target(var tex: Int = 0, var fbo: Int = 0, var w: Int = 0, var h: Int = 0)

  private lateinit var copy: Program
  private lateinit var down: Program
  private lateinit var blur: Program
  private lateinit var composite: Program
  private var vbo = 0
  private val lowA = Target()
  private val lowB = Target()
  private val smallA = Target()
  private val smallB = Target()
  private var maskTex = 0
  private var maskSource: ByteArray? = null
  private var studioAngle = Float.NaN
  private var studioMean = 0f

  fun init() {
    val prefix = "#version 300 es\n#extension GL_OES_EGL_image_external_essl3 : require\n#define CAMERA_SAMPLER samplerExternalOES\n"
    val common = asset("common.glsl")
    val vert = compile(GLES30.GL_VERTEX_SHADER, prefix + common + asset("fullscreen.vert"))
    fun program(frag: String): Program {
      val fs = compile(GLES30.GL_FRAGMENT_SHADER, prefix + common + asset(frag))
      val id = GLES30.glCreateProgram()
      GLES30.glAttachShader(id, vert)
      GLES30.glAttachShader(id, fs)
      GLES30.glBindAttribLocation(id, 0, "aPos")
      GLES30.glLinkProgram(id)
      val ok = IntArray(1)
      GLES30.glGetProgramiv(id, GLES30.GL_LINK_STATUS, ok, 0)
      check(ok[0] != 0) { "link $frag: ${GLES30.glGetProgramInfoLog(id)}" }
      GLES30.glDeleteShader(fs)
      return Program(id)
    }
    copy = program("copy.frag")
    down = program("down.frag")
    blur = program("blur.frag")
    composite = program("composite.frag")
    GLES30.glDeleteShader(vert)
    val ids = IntArray(1)
    GLES30.glGenBuffers(1, ids, 0)
    vbo = ids[0]
    val tri = ByteBuffer.allocateDirect(6 * 4).order(ByteOrder.nativeOrder()).asFloatBuffer().put(floatArrayOf(-1f, -1f, 3f, -1f, -1f, 3f))
    tri.position(0)
    GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo)
    GLES30.glBufferData(GLES30.GL_ARRAY_BUFFER, 6 * 4, tri, GLES30.GL_STATIC_DRAW)
  }

  private fun asset(name: String) = assets.open("beauty/$name").bufferedReader().use { it.readText() }

  private fun compile(type: Int, src: String): Int {
    val id = GLES30.glCreateShader(type)
    GLES30.glShaderSource(id, src)
    GLES30.glCompileShader(id)
    val ok = IntArray(1)
    GLES30.glGetShaderiv(id, GLES30.GL_COMPILE_STATUS, ok, 0)
    check(ok[0] != 0) { "compile: ${GLES30.glGetShaderInfoLog(id)}" }
    return id
  }

  private fun texture(): Int {
    val ids = IntArray(1)
    GLES30.glGenTextures(1, ids, 0)
    GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, ids[0])
    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_S, GLES30.GL_CLAMP_TO_EDGE)
    GLES30.glTexParameteri(GLES30.GL_TEXTURE_2D, GLES30.GL_TEXTURE_WRAP_T, GLES30.GL_CLAMP_TO_EDGE)
    return ids[0]
  }

  private fun ensure(t: Target, w: Int, h: Int) {
    if (t.tex != 0 && t.w == w && t.h == h) return
    if (t.tex == 0) {
      t.tex = texture()
      val ids = IntArray(1)
      GLES30.glGenFramebuffers(1, ids, 0)
      t.fbo = ids[0]
    }
    GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, t.tex)
    GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_RGBA8, w, h, 0, GLES30.GL_RGBA, GLES30.GL_UNSIGNED_BYTE, null)
    GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, t.fbo)
    GLES30.glFramebufferTexture2D(GLES30.GL_FRAMEBUFFER, GLES30.GL_COLOR_ATTACHMENT0, GLES30.GL_TEXTURE_2D, t.tex, 0)
    check(GLES30.glCheckFramebufferStatus(GLES30.GL_FRAMEBUFFER) == GLES30.GL_FRAMEBUFFER_COMPLETE) { "incomplete framebuffer" }
    t.w = w
    t.h = h
  }

  private fun uploadMask(a: LiveBeautyState.Analysis) {
    val bytes = a.mask ?: return
    if (bytes === maskSource) return
    if (maskTex == 0) maskTex = texture()
    GLES30.glBindTexture(GLES30.GL_TEXTURE_2D, maskTex)
    GLES30.glPixelStorei(GLES30.GL_UNPACK_ALIGNMENT, 1)
    GLES30.glTexImage2D(GLES30.GL_TEXTURE_2D, 0, GLES30.GL_R8, a.maskW, a.maskH, 0, GLES30.GL_RED, GLES30.GL_UNSIGNED_BYTE, ByteBuffer.wrap(bytes))
    maskSource = bytes
  }

  private fun use(p: Program, fbo: Int, w: Int, h: Int, camMatrix: FloatArray) {
    GLES30.glUseProgram(p.id)
    GLES30.glBindFramebuffer(GLES30.GL_FRAMEBUFFER, fbo)
    GLES30.glViewport(0, 0, w, h)
    GLES30.glUniformMatrix4fv(p.loc("uCamMatrix"), 1, false, camMatrix, 0)
    GLES30.glBindBuffer(GLES30.GL_ARRAY_BUFFER, vbo)
    GLES30.glEnableVertexAttribArray(0)
    GLES30.glVertexAttribPointer(0, 2, GLES30.GL_FLOAT, false, 0, 0)
  }

  private fun bind(p: Program, name: String, unit: Int, target: Int, tex: Int) {
    GLES30.glActiveTexture(GLES30.GL_TEXTURE0 + unit)
    GLES30.glBindTexture(target, tex)
    GLES30.glUniform1i(p.loc(name), unit)
  }

  private fun draw() = GLES30.glDrawArrays(GLES30.GL_TRIANGLES, 0, 3)

  private fun blurPass(a: Target, b: Target, sigma: Float, camMatrix: FloatArray) {
    val w = LiveBeautyUniforms.blurWeights(sigma)
    for ((src, dst, dir) in listOf(Triple(a, b, floatArrayOf(1f / a.w, 0f)), Triple(b, a, floatArrayOf(0f, 1f / a.h)))) {
      use(blur, dst.fbo, dst.w, dst.h, camMatrix)
      bind(blur, "uSource", 0, GLES30.GL_TEXTURE_2D, src.tex)
      GLES30.glUniform2f(blur.loc("uDir"), dir[0], dir[1])
      GLES30.glUniform1fv(blur.loc("uWeights"), 13, w.weights, 0)
      GLES30.glUniform1i(blur.loc("uTaps"), w.taps)
      draw()
    }
  }

  /** Renders one camera frame ([cameraTex], external OES) into the current surface ([outW] x [outH]). */
  fun render(cameraTex: Int, camMatrix: FloatArray, outW: Int, outH: Int, orientOrNull: Affine?) {
    val recipe = LiveBeautyState.recipe
    val analysis = LiveBeautyState.analysis
    if (recipe == null || !recipe.active || LiveBeautyState.showOriginal) {
      use(copy, 0, outW, outH, camMatrix)
      bind(copy, "uCamera", 0, GLES11Ext.GL_TEXTURE_EXTERNAL_OES, cameraTex)
      draw()
      return
    }
    val orient = orientOrNull ?: PreviewSpace.FLIP_Y
    val orientJ = (orient.inverse() ?: PreviewSpace.FLIP_Y).linearMat2()
    val low = LiveBeautyUniforms.lowSize(outW, outH)
    ensure(lowA, low.width, low.height)
    ensure(lowB, low.width, low.height)
    ensure(smallA, (outW + 1) / 2, (outH + 1) / 2)
    ensure(smallB, (outW + 1) / 2, (outH + 1) / 2)
    val hasMask = analysis?.mask != null
    if (analysis != null && hasMask) uploadMask(analysis)
    val shortSide = minOf(outW, outH).toFloat()
    // Low-pass and skin base.
    for ((t, f) in listOf(lowA to low.factor, smallA to 2)) {
      use(down, t.fbo, t.w, t.h, camMatrix)
      bind(down, "uCamera", 0, GLES11Ext.GL_TEXTURE_EXTERNAL_OES, cameraTex)
      if (hasMask) bind(down, "uMask", 1, GLES30.GL_TEXTURE_2D, maskTex)
      GLES30.glUniformMatrix3fv(down.loc("uOrient"), 1, false, orient.toMat3(), 0)
      GLES30.glUniform2f(down.loc("uStep"), f.toFloat() / outW, f.toFloat() / outH)
      GLES30.glUniform1f(down.loc("uHasMask"), if (hasMask) 1f else 0f)
      draw()
    }
    blurPass(lowA, lowB, LiveBeautyUniforms.LOW_SIGMA * shortSide / low.factor, camMatrix)
    blurPass(smallA, smallB, LiveBeautyUniforms.SMALL_SIGMA * shortSide / 2f, camMatrix)
    // Composite.
    val p = composite
    use(p, 0, outW, outH, camMatrix)
    bind(p, "uCamera", 0, GLES11Ext.GL_TEXTURE_EXTERNAL_OES, cameraTex)
    bind(p, "uLow", 1, GLES30.GL_TEXTURE_2D, lowA.tex)
    bind(p, "uSmall", 2, GLES30.GL_TEXTURE_2D, smallA.tex)
    if (hasMask) bind(p, "uMask", 3, GLES30.GL_TEXTURE_2D, maskTex)
    GLES30.glUniformMatrix3fv(p.loc("uOrient"), 1, false, orient.toMat3(), 0)
    GLES30.glUniformMatrix2fv(p.loc("uOrientJ"), 1, false, orientJ, 0)
    GLES30.glUniform1f(p.loc("uHasMask"), if (hasMask) 1f else 0f)
    GLES30.glUniform1f(p.loc("uMaskSharpen"), 1f)
    GLES30.glUniform2f(p.loc("uLowSize"), low.width.toFloat(), low.height.toFloat())
    val pw = (analysis?.previewW ?: outW).toFloat()
    val ph = (analysis?.previewH ?: outH).toFloat()
    GLES30.glUniform2f(p.loc("uSize"), pw, ph)
    GLES30.glUniform1fv(p.loc("uG"), recipe.grade.size, recipe.grade, 0)
    val scene = analysis?.scene ?: LiveBeautyState.NO_SCENE
    GLES30.glUniform1fv(p.loc("uScene"), 9, scene, 0)
    GLES30.glUniform1fv(p.loc("uSkin"), 4, recipe.skin, 0)
    GLES30.glUniform1f(p.loc("uBlur"), recipe.blur)
    GLES30.glUniform1fv(p.loc("uFaceP"), 13, recipe.face, 0)
    val face = analysis?.face
    GLES30.glUniform1f(p.loc("uFaceOn"), if (face != null) 1f else 0f)
    if (face != null) {
      GLES30.glUniform4fv(p.loc("uFaceFrame"), 1, face.frame, 0)
      GLES30.glUniform2fv(p.loc("uFaceHalf"), 1, face.half, 0)
      GLES30.glUniform3fv(p.loc("uFaceMarks"), 1, face.marks, 0)
      GLES30.glUniform4fv(p.loc("uEyes"), 1, face.eyes, 0)
      GLES30.glUniform1f(p.loc("uEyeW"), face.eyeW)
      GLES30.glUniform3fv(p.loc("uMouth"), 1, face.mouth, 0)
    }
    if (scene[7] != studioAngle) {
      studioAngle = scene[7]
      studioMean = LiveBeautyUniforms.studioMean(studioAngle)
    }
    GLES30.glUniform1f(p.loc("uStudioMean"), studioMean)
    draw()
  }

  fun release() {
    for (prog in listOf(copy, down, blur, composite)) GLES30.glDeleteProgram(prog.id)
    for (t in listOf(lowA, lowB, smallA, smallB)) {
      if (t.tex != 0) GLES30.glDeleteTextures(1, intArrayOf(t.tex), 0)
      if (t.fbo != 0) GLES30.glDeleteFramebuffers(1, intArrayOf(t.fbo), 0)
    }
    if (maskTex != 0) GLES30.glDeleteTextures(1, intArrayOf(maskTex), 0)
    if (vbo != 0) GLES30.glDeleteBuffers(1, intArrayOf(vbo), 0)
  }
}

/**
 * CameraX surface processor for the live beauty preview: camera frames
 * arrive on a SurfaceTexture, the beauty passes render into each output
 * (the viewfinder). Runs on its own GL thread. Any GL failure is reported
 * through [onFailure]; the camera then rebinds without the effect.
 */
class BeautyProcessor(context: Context, private val onFailure: (Throwable) -> Unit) : SurfaceProcessor {
  private val assets = context.applicationContext.assets
  private val thread = HandlerThread("PhotoPlusBeauty").apply { start() }
  private val handler = Handler(thread.looper)
  val executor: Executor = Executor { handler.post(it) }
  private var egl: EglCore? = null
  private var renderer: BeautyRenderer? = null
  private var cameraTex = 0
  private var surfaceTexture: SurfaceTexture? = null
  private val outputs = LinkedHashMap<SurfaceOutput, EGLSurface>()
  private val stMatrix = FloatArray(16)
  private val camMatrix = FloatArray(16)
  private val sensorValues = FloatArray(9)
  @Volatile private var released = false
  private var failed = false

  private fun ensureGl(): Boolean {
    if (egl != null) return true
    if (failed) return false
    return try {
      val core = EglCore()
      val r = BeautyRenderer(assets).also { it.init() }
      egl = core
      renderer = r
      true
    } catch (t: Throwable) {
      fail(t)
      false
    }
  }

  private fun fail(t: Throwable) {
    if (failed) return
    failed = true
    Log.w(TAG, "live beauty unavailable", t)
    LiveBeautyState.unavailableReason = t.message ?: "GPU unavailable"
    onFailure(t)
  }

  override fun onInputSurface(request: SurfaceRequest) {
    if (released || !ensureGl()) {
      request.willNotProvideSurface()
      return
    }
    val ids = IntArray(1)
    GLES30.glGenTextures(1, ids, 0)
    cameraTex = ids[0]
    GLES30.glBindTexture(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, cameraTex)
    GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES30.GL_TEXTURE_MIN_FILTER, GLES30.GL_LINEAR)
    GLES30.glTexParameteri(GLES11Ext.GL_TEXTURE_EXTERNAL_OES, GLES30.GL_TEXTURE_MAG_FILTER, GLES30.GL_LINEAR)
    val st = SurfaceTexture(cameraTex)
    st.setDefaultBufferSize(request.resolution.width, request.resolution.height)
    val surface = Surface(st)
    surfaceTexture = st
    st.setOnFrameAvailableListener({ drawFrame(it) }, handler)
    request.provideSurface(surface, executor) {
      st.setOnFrameAvailableListener(null)
      if (surfaceTexture === st) surfaceTexture = null
      st.release()
      surface.release()
    }
  }

  override fun onOutputSurface(output: SurfaceOutput) {
    val core = egl
    if (released || core == null) {
      output.close()
      return
    }
    val surface = output.getSurface(executor) {
      outputs.remove(output)?.let { s -> egl?.releaseSurface(s) }
      output.close()
    }
    try {
      outputs[output] = core.createWindowSurface(surface)
    } catch (t: Throwable) {
      output.close()
      fail(t)
    }
  }

  private fun drawFrame(st: SurfaceTexture) {
    val core = egl ?: return
    val r = renderer ?: return
    if (released || failed) return
    try {
      core.makeCurrentOffscreen()
      st.updateTexImage()
      st.getTransformMatrix(stMatrix)
      for ((output, surface) in outputs) {
        core.makeCurrent(surface)
        output.updateTransformMatrix(camMatrix, stMatrix)
        output.sensorToBufferTransform.getValues(sensorValues)
        val a = LiveBeautyState.analysis
        val orient = a?.let {
          PreviewSpace.orient(output.size.width, output.size.height, Affine.fromValues(sensorValues), it.analysisSensorToBuffer, it.previewFromAnalysis)
        }
        r.render(cameraTex, camMatrix, output.size.width, output.size.height, orient)
        core.swap(surface, st.timestamp)
      }
    } catch (t: Throwable) {
      fail(t)
    }
  }

  /** Releases the GL thread and everything on it (after the camera unbinds the effect). */
  fun release() {
    released = true
    handler.post {
      runCatching {
        egl?.makeCurrentOffscreen()
        renderer?.release()
        if (cameraTex != 0) GLES30.glDeleteTextures(1, intArrayOf(cameraTex), 0)
        for ((output, surface) in outputs) {
          egl?.releaseSurface(surface)
          output.close()
        }
        outputs.clear()
        egl?.release()
      }
      egl = null
      renderer = null
      thread.quitSafely()
    }
  }

  companion object {
    @Volatile private var probed: Boolean? = null

    /**
     * Whether this phone can run the live preview: GLES 3 with external
     * camera textures in GLSL 3, and not a low-memory device. Photos are
     * finished with the recipe on every phone either way.
     */
    fun supported(context: Context): Boolean {
      probed?.let { return it }
      val am = context.getSystemService(Context.ACTIVITY_SERVICE) as? ActivityManager
      val ok = am?.isLowRamDevice != true && runCatching {
        val core = EglCore()
        try {
          val ext = GLES30.glGetString(GLES30.GL_EXTENSIONS).orEmpty()
          ext.contains("GL_OES_EGL_image_external_essl3")
        } finally {
          core.release()
        }
      }.getOrDefault(false)
      probed = ok
      return ok
    }
  }
}

/** The live beauty effect on the camera preview (photos and video are not affected: they are finished natively). */
class BeautyEffect(processor: BeautyProcessor, onError: Consumer<Throwable>) :
  CameraEffect(PREVIEW, processor.executor, processor, onError)
