cmake_minimum_required(VERSION 3.25...4.1)
project(photoplus_engine LANGUAGES CXX)

# The engine is platform-agnostic C++23. It is consumed by the Android JNI
# bridge (android/app/src/main/cpp) and built standalone for host tests.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(PP_ENGINE_BUILD_TESTS "Build host unit tests" OFF)
option(PP_ENGINE_SANITIZE "Build with AddressSanitizer + UBSan" OFF)
option(PP_ENGINE_BUILD_TOOLS "Build developer tools (enhance_bench)" OFF)

add_library(photoplus_engine STATIC
  src/memory.cpp
  src/thread_pool.cpp
  src/video/video.cpp
  src/image.cpp
  src/operations.cpp
  src/pipeline.cpp
  src/profiler.cpp
  src/pp_api.cpp
  src/plane.cpp
  src/enhance/quality.cpp
  src/enhance/align.cpp
  src/enhance/fusion.cpp
  src/enhance/superres.cpp
  src/enhance/restore.cpp
  src/enhance/tone.cpp
  src/enhance/enhance.cpp
  src/portrait/face.cpp
  src/beauty/beauty.cpp
  src/backdrop/backdrop.cpp
  src/matte/tools.cpp
  src/studio/cutout.cpp
  src/studio/compose.cpp
  src/skintone/skintone.cpp
  src/light/light.cpp
  src/pose/pose.cpp
  src/portrait/matte.cpp
  src/portrait/portrait.cpp
  src/portrait/style.cpp
  src/lens/lens.cpp
  src/edit/adjust.cpp
  src/edit/composite.cpp
  src/edit/frame.cpp
  src/ops/resample.cpp
  src/ops/filters.cpp
  src/ops/blend.cpp
  src/codec/codec.cpp
  src/codec/stb_impl.cpp
  src/ml/tensor.cpp
)
target_include_directories(photoplus_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(photoplus_engine PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/stb)
# Third-party code: compiled without the engine's warning set.
set_source_files_properties(src/codec/stb_impl.cpp PROPERTIES COMPILE_OPTIONS "-w")

# GPU acceleration (OpenGL ES 3.1 compute). Android always has EGL/GLES; on
# the host it is used when EGL and GLES headers are installed (Mesa runs the
# same shaders in software, so GPU results are tested on every build).
option(PP_ENGINE_GPU "Build the OpenGL ES 3.1 compute backend" ON)
set(PP_GPU_FOUND OFF)
if(PP_ENGINE_GPU)
  if(ANDROID)
    set(PP_GPU_FOUND ON)
    set(PP_GPU_LIBS EGL GLESv3)
  else()
    find_path(PP_EGL_INCLUDE EGL/egl.h)
    find_path(PP_GLES3_INCLUDE GLES3/gl31.h)
    find_library(PP_EGL_LIB EGL)
    find_library(PP_GLES_LIB GLESv2)
    if(PP_EGL_INCLUDE AND PP_GLES3_INCLUDE AND PP_EGL_LIB AND PP_GLES_LIB)
      set(PP_GPU_FOUND ON)
      set(PP_GPU_LIBS ${PP_EGL_LIB} ${PP_GLES_LIB})
    endif()
  endif()
endif()
if(PP_GPU_FOUND)
  target_sources(photoplus_engine PRIVATE src/gpu/gpu_gles.cpp)
  target_link_libraries(photoplus_engine PUBLIC ${PP_GPU_LIBS})
  target_compile_definitions(photoplus_engine PUBLIC PP_ENGINE_HAS_GPU=1)
  message(STATUS "Photo Plus engine: GPU backend ON")
else()
  target_sources(photoplus_engine PRIVATE src/gpu/gpu_none.cpp)
  message(STATUS "Photo Plus engine: GPU backend OFF")
endif()
target_compile_options(photoplus_engine PRIVATE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror=return-type)

if(PP_ENGINE_SANITIZE)
  target_compile_options(photoplus_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(photoplus_engine PUBLIC -fsanitize=address,undefined)
endif()

if(PP_ENGINE_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)
  add_executable(photoplus_engine_tests tests/test_main.cpp tests/engine_tests.cpp tests/enhance_tests.cpp tests/portrait_tests.cpp tests/lens_tests.cpp tests/edit_tests.cpp tests/core_tests.cpp tests/gpu_tests.cpp tests/face_tests.cpp tests/video_tests.cpp tests/beauty_tests.cpp tests/backdrop_tests.cpp tests/skintone_tests.cpp tests/light_tests.cpp tests/pose_tests.cpp tests/studio_tests.cpp)
  target_link_libraries(photoplus_engine_tests PRIVATE photoplus_engine Threads::Threads)
  add_test(NAME photoplus_engine_tests COMMAND photoplus_engine_tests)
endif()

if(PP_ENGINE_BUILD_TOOLS)
  find_package(Threads REQUIRED)
  add_executable(enhance_bench tools/enhance_bench.cpp)
  add_executable(portrait_demo tools/portrait_demo.cpp)
  add_executable(transform_demo tools/transform_demo.cpp)
  add_executable(edit_demo tools/edit_demo.cpp)
  add_executable(ops_probe tools/ops_probe.cpp)
  add_executable(video_demo tools/video_demo.cpp)
  add_executable(beauty_demo tools/beauty_demo.cpp)
  add_executable(backdrop_demo tools/backdrop_demo.cpp)
  target_link_libraries(backdrop_demo PRIVATE photoplus_engine Threads::Threads)
  add_executable(skintone_demo tools/skintone_demo.cpp)
  target_link_libraries(skintone_demo PRIVATE photoplus_engine Threads::Threads)
  add_executable(light_demo tools/light_demo.cpp)
  target_link_libraries(light_demo PRIVATE photoplus_engine Threads::Threads)
  add_executable(pose_demo tools/pose_demo.cpp)
  add_executable(studio_demo tools/studio_demo.cpp)
  target_link_libraries(studio_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(pose_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(beauty_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(video_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(ops_probe PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(edit_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(transform_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(portrait_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(enhance_bench PRIVATE photoplus_engine Threads::Threads)
endif()
