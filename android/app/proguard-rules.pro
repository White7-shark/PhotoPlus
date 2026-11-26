# Photo Plus: R8 rules for release builds (code shrinking, optimisation and
# obfuscation; resource shrinking). Libraries bring their own rules (React
# Native, AndroidX, WorkManager, ML Kit, Play services, Ads); these cover
# what reaches our code by name rather than by a normal call.

# Native code (C++ engine, JNI) creates this exception by class name and
# constructor signature (photoplus_jni.cpp: FindClass / GetMethodID).
-keep class com.photoplus.engine.EngineException { <init>(java.lang.String, java.lang.String); }
# Methods implemented in C++ keep their names (and their classes) so JNI finds them.
-keepclasseswithmembernames,includedescriptorclasses class com.photoplus.** { native <methods>; }

# TurboModules and native components: React Native's C++ layer calls their
# methods by name (codegen specs in com.photoplus.specs), and view managers'
# props are set by name.
-keep class com.photoplus.specs.** { *; }
-keep class com.photoplus.** extends com.facebook.react.bridge.BaseJavaModule { *; }
-keep class com.photoplus.** extends com.facebook.react.uimanager.ViewManager { *; }
-keep class com.photoplus.PhotoPlusPackage { *; }

# Background work is created by class name by WorkManager.
-keep class com.photoplus.** extends androidx.work.ListenableWorker { <init>(...); }

# MediaPipe (pose) and LiteRT (depth, matting): their native code calls back
# into Java classes and protobuf messages by name.
-keep class com.google.mediapipe.** { *; }
-keep class com.google.protobuf.** { *; }
-keep class org.tensorflow.lite.** { *; }
-keep class com.google.ai.edge.litert.** { *; }
-dontwarn com.google.mediapipe.**
-dontwarn com.google.protobuf.**
-dontwarn org.tensorflow.lite.**
-dontwarn com.google.ai.edge.litert.**
-dontwarn javax.annotation.**
-dontwarn com.google.auto.value.**

# Crash reports stay readable with the mapping file (uploaded with the app bundle).
-keepattributes SourceFile,LineNumberTable,*Annotation*,Signature,InnerClasses,EnclosingMethod
-renamesourcefileattribute SourceFile
