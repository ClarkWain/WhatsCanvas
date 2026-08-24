cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasAndroidSdk LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

get_filename_component(WHATSCANVAS_ROOT
    "${CMAKE_CURRENT_LIST_DIR}/../../../../../../"
    ABSOLUTE)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_OPENGL OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_OPENGLES ON CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_METAL OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_SOFTWARE OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(WHATSCANVAS_BUILD_DESKTOP_PLATFORM OFF CACHE BOOL "" FORCE)
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

add_library(whatscanvas SHARED sdk_anchor.cpp)

# Pull every public WhatsCanvas translation unit into the SDK DSO. FreeType and
# HarfBuzz remain ordinary static dependencies and are pulled as their symbols
# are referenced, leaving consumers with one native library plus c++_shared.
target_link_libraries(whatscanvas PRIVATE
    "-Wl,--whole-archive"
    WhatsCanvas::OpenGLES
    "-Wl,--no-whole-archive"
    ${ANDROID_LIBRARY}
    ${EGL_LIBRARY}
    ${GLES3_LIBRARY}
    ${LOG_LIBRARY}
    ${CMAKE_DL_LIBS})

target_compile_options(whatscanvas PRIVATE
    -Wall
    -Wextra
    -Wpedantic)

set_target_properties(whatscanvas PROPERTIES
    OUTPUT_NAME whatscanvas)
