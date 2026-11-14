package com.photoplus.studio

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import com.photoplus.engine.EngineException
import com.photoplus.engine.ImageCodec
import java.io.File
import kotlin.math.max
import kotlin.math.min
import kotlin.math.roundToInt

/** Decodes the studio's backgrounds (built-in plates, people's own photos, plain colours) for photos and videos. */
object SceneBackgrounds {
  /** The background decoded so its crop is at least `edge` on the long side (plates and photos only). */
  fun decode(context: Context, resolve: (String) -> File, bg: SceneBackground, edge: Int, crop: SceneCrop, maxEdge: Int): Bitmap {
    val need = (edge / max(crop.w, crop.h)).roundToInt()
    return when (bg) {
      is SceneBackground.Plate -> {
        val asset = StudioModule.plateAsset(bg.assetId)
        val bounds = context.assets.open(asset).use { input ->
          BitmapFactory.Options().apply { inJustDecodeBounds = true }.also { BitmapFactory.decodeStream(input, null, it) }
        }
        if (bounds.outWidth <= 0) throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown background")
        context.assets.open(asset).use { input ->
          BitmapFactory.decodeStream(input, null, SceneStudio.decodeOptions(bounds.outWidth, bounds.outHeight, need))
            ?: throw EngineException(EngineException.IO_ERROR, "The background could not be read")
        }
      }
      is SceneBackground.Photo -> ImageCodec.decode(resolve(bg.path), min(need, maxEdge)).bitmap
      else -> throw EngineException(EngineException.INVALID_ARGUMENT, "Unknown background")
    }
  }

  /** The background as an opaque frame of width × height (a video's frame). */
  fun frame(context: Context, resolve: (String) -> File, bg: SceneBackground, crop: SceneCrop, width: Int, height: Int): Bitmap = when (bg) {
    is SceneBackground.Plain -> SceneStudio.plain(bg.color, bg.vignette, width, height)
    is SceneBackground.Transparent -> throw EngineException(EngineException.INVALID_ARGUMENT, "A video needs a background")
    else -> {
      val src = decode(context, resolve, bg, max(width, height), crop, 4096)
      try {
        SceneStudio.frame(src, crop, width, height)
      } finally {
        src.recycle()
      }
    }
  }
}
