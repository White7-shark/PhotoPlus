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
  src/portrait/matte.cpp
  src/portrait/portrait.cpp
  src/portrait/style.cpp
)
target_include_directories(photoplus_engine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(photoplus_engine PRIVATE
  -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion -Werror=return-type)

if(PP_ENGINE_SANITIZE)
  target_compile_options(photoplus_engine PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(photoplus_engine PUBLIC -fsanitize=address,undefined)
endif()

if(PP_ENGINE_BUILD_TESTS)
  enable_testing()
  find_package(Threads REQUIRED)
  add_executable(photoplus_engine_tests tests/test_main.cpp tests/engine_tests.cpp tests/enhance_tests.cpp tests/portrait_tests.cpp)
  target_link_libraries(photoplus_engine_tests PRIVATE photoplus_engine Threads::Threads)
  add_test(NAME photoplus_engine_tests COMMAND photoplus_engine_tests)
endif()

if(PP_ENGINE_BUILD_TOOLS)
  find_package(Threads REQUIRED)
  add_executable(enhance_bench tools/enhance_bench.cpp)
  add_executable(portrait_demo tools/portrait_demo.cpp)
  target_link_libraries(portrait_demo PRIVATE photoplus_engine Threads::Threads)
  target_link_libraries(enhance_bench PRIVATE photoplus_engine Threads::Threads)
endif()
