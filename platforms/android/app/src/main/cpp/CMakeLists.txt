cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasAndroid LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WHATSCANVAS_ANDROID_PROFILE
    "Build an installable, symbolized Android profiling variant" OFF)
if(WHATSCANVAS_ANDROID_PROFILE)
    # AGP selects CMake's Debug configuration whenever a build type is
    # debuggable, and the NDK's Debug flags end in -O0. Append the profiling
    # flags to the configuration variables themselves so -O2 is the final
    # optimization option while run-as/simpleperf and symbols remain usable.
    string(APPEND CMAKE_C_FLAGS_DEBUG " -O2 -g -DNDEBUG")
    string(APPEND CMAKE_CXX_FLAGS_DEBUG " -O2 -g -DNDEBUG")
endif()

get_filename_component(WHATSCANVAS_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../../"
    ABSOLUTE)

set(WHATSCANVAS_BUILD_OPENGL OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_OPENGLES ON CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_SOFTWARE OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_ENABLE_SCRIPT_TESTS OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_ENABLE_VULKAN OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_ENABLE_METAL OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER ON CACHE BOOL "" FORCE)
set(WHATSCANVAS_ENABLE_OPENTYPE_SHAPING ON CACHE BOOL "" FORCE)
set(WHATSCANVAS_INSTALL OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_X11 OFF CACHE STRING "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

find_library(ANDROID_LIBRARY android REQUIRED)
find_library(EGL_LIBRARY EGL REQUIRED)
find_library(GLES3_LIBRARY GLESv3 REQUIRED)
find_library(LOG_LIBRARY log REQUIRED)

set(WHATSCANVAS_OPENGLES_LIBRARIES
    "${EGL_LIBRARY};${GLES3_LIBRARY}"
    CACHE STRING "" FORCE)

add_subdirectory(
    "${WHATSCANVAS_ROOT}"
    "${CMAKE_CURRENT_BINARY_DIR}/whatscanvas")

add_library(whatscanvas_android SHARED
    whatscanvas_jni.cpp)

target_include_directories(whatscanvas_android PRIVATE
    "${WHATSCANVAS_ROOT}")

target_compile_options(whatscanvas_android PRIVATE
    -Wall
    -Wextra
    -Wpedantic)

target_link_libraries(whatscanvas_android PRIVATE
    WhatsCanvas::OpenGLES
    ${ANDROID_LIBRARY}
    ${EGL_LIBRARY}
    ${GLES3_LIBRARY}
    ${LOG_LIBRARY}
    ${CMAKE_DL_LIBS})
