cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasPackageConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WHATSCANVAS_PACKAGE_TARGET "OpenGL" CACHE STRING "Packaged renderer target to consume (OpenGL or Software)")
set_property(CACHE WHATSCANVAS_PACKAGE_TARGET PROPERTY STRINGS OpenGL Software)

if (WHATSCANVAS_PACKAGE_TARGET STREQUAL "Software")
    # Request only the Software component so that consuming a package that also
    # ships OpenGL does not force OpenGL discovery on a headless consumer.
    find_package(WhatsCanvas CONFIG REQUIRED COMPONENTS Software)
elseif (WHATSCANVAS_PACKAGE_TARGET STREQUAL "OpenGL")
    find_package(WhatsCanvas CONFIG REQUIRED)
else()
    message(FATAL_ERROR "WHATSCANVAS_PACKAGE_TARGET must be OpenGL or Software")
endif()

if (WHATSCANVAS_PACKAGE_TARGET STREQUAL "Software")
    # Prove that a Software-only package does not leak GPU renderer targets into
    # the consumer's find_package(WhatsCanvas) result.
    if (TARGET WhatsCanvas::OpenGL OR TARGET WhatsCanvas::WhatsCanvasOpenGL)
        message(FATAL_ERROR "find_package(WhatsCanvas) leaked an OpenGL target into a Software-only consumer")
    endif()
    if (TARGET WhatsCanvas::OpenGLES OR TARGET WhatsCanvas::WhatsCanvasOpenGLES)
        message(FATAL_ERROR "find_package(WhatsCanvas) leaked an OpenGLES target into a Software-only consumer")
    endif()
endif()

add_executable(WhatsCanvasPackageConsumer main.cpp)
if (WHATSCANVAS_PACKAGE_TARGET STREQUAL "Software")
    target_link_libraries(WhatsCanvasPackageConsumer PRIVATE WhatsCanvas::Software)
    target_compile_definitions(WhatsCanvasPackageConsumer PRIVATE WHATSCANVAS_PACKAGE_USE_SOFTWARE=1)
else()
    target_link_libraries(WhatsCanvasPackageConsumer PRIVATE WhatsCanvas::OpenGL)
endif()
