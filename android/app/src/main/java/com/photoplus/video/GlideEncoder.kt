package com.photoplus.video

import android.graphics.Bitmap
import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMuxer
import com.photoplus.engine.NativeEngine
import java.io.Closeable
import java.io.File
import java.nio.ByteBuffer

/**
 * H.264 encoder for processed Glide frames, with the original clip's sound
 * copied across unchanged. Frames are handed over as bitmaps with exact
 * presentation times (frame index / fps), so a slow frame never changes the
 * video's timing.
 */
class GlideEncoder(
  output: File,
  private val width: Int,
  private val height: Int,
  private val fps: Float,
  audioFrom: File?,
) : Closeable {
  private val codec: MediaCodec = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
  private val muxer = MediaMuxer(output.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
  private val info = MediaCodec.BufferInfo()
  private var videoTrack = -1
  private var started = false
  private var lastPtsUs = 0L
  private var audio: MediaExtractor? = null
  private var audioTrack = -1
  private var released = false

  init {
    val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
      setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatYUV420Flexible)
      setInteger(MediaFormat.KEY_BIT_RATE, GlideOptions.bitrate(width, height, fps))
      setFloat(MediaFormat.KEY_FRAME_RATE, fps)
      setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, 1)
    }
    try {
      codec.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
      codec.start()
      // The sound track is added before the muxer starts (it starts once the
      // video encoder reports its format).
      if (audioFrom != null) {
        val ex = MediaExtractor()
        ex.setDataSource(audioFrom.absolutePath)
        val index = (0 until ex.trackCount).firstOrNull {
          ex.getTrackFormat(it).getString(MediaFormat.KEY_MIME)?.startsWith("audio/") == true
        }
        if (index != null) {
          ex.selectTrack(index)
          audioTrack = muxer.addTrack(ex.getTrackFormat(index))
          audio = ex
        } else {
          ex.release()
        }
      }
    } catch (e: Exception) {
      close()
      throw e
    }
  }

  /** Queues one frame (ARGB_8888, [width] x [height]) shown at [ptsUs]. */
  fun encode(frame: Bitmap, ptsUs: Long) {
    require(frame.width == width && frame.height == height) { "frame size changed" }
    var index: Int
    while (true) {
      index = codec.dequeueInputBuffer(TIMEOUT_US)
      if (index >= 0) break
      drain(false)
    }
    val image = codec.getInputImage(index) ?: error("the encoder offered no input image")
    val planes = image.planes
    NativeEngine.nativeRgbaToYuv420(
      frame, planes[0].buffer, planes[1].buffer, planes[2].buffer,
      planes[0].rowStride, planes[1].rowStride, planes[1].pixelStride,
    )
    codec.queueInputBuffer(index, 0, width * height * 3 / 2, ptsUs, 0)
    lastPtsUs = ptsUs
    drain(false)
  }

  /** Ends the video, copies the sound (up to the video's length) and closes the file. */
  fun finish(durationUs: Long) {
    var index: Int
    while (true) {
      index = codec.dequeueInputBuffer(TIMEOUT_US)
      if (index >= 0) break
      drain(false)
    }
    codec.queueInputBuffer(index, 0, 0, lastPtsUs + 1, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
    drain(true)
    val ex = audio
    if (ex != null && started && audioTrack >= 0) {
      val buffer = ByteBuffer.allocate(256 * 1024)
      val ai = MediaCodec.BufferInfo()
      while (true) {
        val size = ex.readSampleData(buffer, 0)
        if (size < 0) break
        val t = ex.sampleTime
        if (t > durationUs) break
        ai.set(0, size, t, if ((ex.sampleFlags and MediaExtractor.SAMPLE_FLAG_SYNC) != 0) MediaCodec.BUFFER_FLAG_KEY_FRAME else 0)
        muxer.writeSampleData(audioTrack, buffer, ai)
        ex.advance()
      }
    }
    close()
  }

  private fun drain(endOfStream: Boolean) {
    while (true) {
      val index = codec.dequeueOutputBuffer(info, if (endOfStream) TIMEOUT_US else 0)
      when {
        index == MediaCodec.INFO_TRY_AGAIN_LATER -> if (!endOfStream) return
        index == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> {
          check(!started) { "encoder format changed twice" }
          videoTrack = muxer.addTrack(codec.outputFormat)
          muxer.start()
          started = true
        }
        index >= 0 -> {
          val buf = codec.getOutputBuffer(index)
          if (buf != null && info.size > 0 && (info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG) == 0 && started) {
            buf.position(info.offset)
            buf.limit(info.offset + info.size)
            muxer.writeSampleData(videoTrack, buf, info)
          }
          codec.releaseOutputBuffer(index, false)
          if ((info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM) != 0) return
        }
      }
    }
  }

  override fun close() {
    if (released) return
    released = true
    runCatching { codec.stop() }
    runCatching { codec.release() }
    runCatching { if (started) muxer.stop() }
    runCatching { muxer.release() }
    runCatching { audio?.release() }
  }

  private companion object {
    const val TIMEOUT_US = 10_000L
  }
}
