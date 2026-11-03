cmake_minimum_required(VERSION 3.18)
project(photoplus_jni LANGUAGES CXX)

# Platform-agnostic engine lives outside the Android tree so it can be unit
# tested on the host (see native/engine).
set(PP_ENGINE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../native/engine)
add_subdirectory(${PP_ENGINE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/engine)

add_library(photoplus_jni SHARED photoplus_jni.cpp)
target_compile_features(photoplus_jni PRIVATE cxx_std_17)
target_compile_options(photoplus_jni PRIVATE -Wall -Wextra -fvisibility=hidden)
target_link_libraries(photoplus_jni PRIVATE photoplus_engine jnigraphics log android)
# 16 KB page-size compatibility required for newer Android devices.
target_link_options(photoplus_jni PRIVATE "-Wl,-z,max-page-size=16384")
