package com.photoplus.engine

/**
 * Raised by the JNI bridge (see cpp/photoplus_jni.cpp) when the C engine
 * returns a non-OK status. [code] matches pp_status_name() and is forwarded
 * to JavaScript as the promise rejection code.
 */
class EngineException(val code: String, message: String) : RuntimeException(message) {
  companion object {
    const val CANCELLED = "CANCELLED"
    const val INVALID_ARGUMENT = "INVALID_ARGUMENT"
    const val OUT_OF_MEMORY = "OUT_OF_MEMORY"
    const val BUDGET_EXCEEDED = "BUDGET_EXCEEDED"
    const val UNSUPPORTED_FORMAT = "UNSUPPORTED_FORMAT"
    const val IO_ERROR = "IO_ERROR"
    const val INTERNAL = "INTERNAL"
  }
}
