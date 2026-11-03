package com.photoplus.util

import com.facebook.react.bridge.Arguments
import com.facebook.react.bridge.ReactContext
import com.facebook.react.bridge.WritableArray
import com.facebook.react.bridge.WritableMap

/** Converts plain Kotlin collections into bridge-safe values. */
fun Map<String, Any?>.toWritableMap(): WritableMap {
  val out = Arguments.createMap()
  for ((key, value) in this) {
    when (value) {
      null -> out.putNull(key)
      is Boolean -> out.putBoolean(key, value)
      is Int -> out.putInt(key, value)
      is Number -> out.putDouble(key, value.toDouble())
      is String -> out.putString(key, value)
      is Map<*, *> -> @Suppress("UNCHECKED_CAST") out.putMap(key, (value as Map<String, Any?>).toWritableMap())
      is Iterable<*> -> out.putArray(key, value.toWritableArray())
      is Array<*> -> out.putArray(key, value.asIterable().toWritableArray())
      else -> out.putString(key, value.toString())
    }
  }
  return out
}

fun Iterable<*>.toWritableArray(): WritableArray {
  val out = Arguments.createArray()
  for (value in this) {
    when (value) {
      null -> out.pushNull()
      is Boolean -> out.pushBoolean(value)
      is Int -> out.pushInt(value)
      is Number -> out.pushDouble(value.toDouble())
      is String -> out.pushString(value)
      is Map<*, *> -> @Suppress("UNCHECKED_CAST") out.pushMap((value as Map<String, Any?>).toWritableMap())
      is Iterable<*> -> out.pushArray(value.toWritableArray())
      else -> out.pushString(value.toString())
    }
  }
  return out
}

/** Emits a DeviceEventEmitter event if the JS runtime is alive. */
fun ReactContext.emitSafely(event: String, payload: Map<String, Any?>) {
  if (!hasActiveReactInstance()) return
  try {
    emitDeviceEvent(event, payload.toWritableMap())
  } catch (_: RuntimeException) {
    // The instance can be torn down between the check and the emit.
  }
}
