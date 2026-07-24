cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasPackageConsumerExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(WHATSCANVAS_PACKAGE_TARGET "Software" CACHE STRING
    "Packaged WhatsCanvas target to link (OpenGL, OpenGLES, Software)")
set_property(CACHE WHATSCANVAS_PACKAGE_TARGET PROPERTY STRINGS OpenGL OpenGLES Software)

set(WHATSCANVAS_RUNTIME_BACKEND "Software" CACHE STRING
    "Runtime Canvas backend to request (OpenGL, OpenGLES, Vulkan, Software)")
set_property(CACHE WHATSCANVAS_RUNTIME_BACKEND PROPERTY STRINGS OpenGL OpenGLES Vulkan Software)

if (WHATSCANVAS_PACKAGE_TARGET STREQUAL "Software")
    if (NOT WHATSCANVAS_RUNTIME_BACKEND STREQUAL "Software")
        message(FATAL_ERROR "Software package target only supports WHATSCANVAS_RUNTIME_BACKEND=Software")
    endif()
    find_package(WhatsCanvas CONFIG REQUIRED COMPONENTS Software)
    set(WHATSCANVAS_LINK_TARGET WhatsCanvas::Software)
elseif (WHATSCANVAS_PACKAGE_TARGET STREQUAL "OpenGLES")
    if (NOT WHATSCANVAS_RUNTIME_BACKEND STREQUAL "OpenGLES")
        message(FATAL_ERROR "OpenGLES package target only supports WHATSCANVAS_RUNTIME_BACKEND=OpenGLES")
    endif()
    find_package(WhatsCanvas CONFIG REQUIRED COMPONENTS OpenGLES)
    set(WHATSCANVAS_LINK_TARGET WhatsCanvas::OpenGLES)
elseif (WHATSCANVAS_PACKAGE_TARGET STREQUAL "OpenGL")
    if (NOT WHATSCANVAS_RUNTIME_BACKEND STREQUAL "OpenGL"
        AND NOT WHATSCANVAS_RUNTIME_BACKEND STREQUAL "Vulkan")
        message(FATAL_ERROR "OpenGL package target supports WHATSCANVAS_RUNTIME_BACKEND=OpenGL or Vulkan")
    endif()
    find_package(WhatsCanvas CONFIG REQUIRED)
    set(WHATSCANVAS_LINK_TARGET WhatsCanvas::OpenGL)
else()
    message(FATAL_ERROR "WHATSCANVAS_PACKAGE_TARGET must be OpenGL, OpenGLES or Software")
endif()

add_executable(WhatsCanvasPackageConsumerExample main.cpp)
target_link_libraries(WhatsCanvasPackageConsumerExample PRIVATE ${WHATSCANVAS_LINK_TARGET})

get_filename_component(WHATSCANVAS_PACKAGE_PREFIX "${WhatsCanvas_DIR}/../../.." ABSOLUTE)
if (WIN32 AND EXISTS "${WHATSCANVAS_PACKAGE_PREFIX}/bin")
    add_custom_command(TARGET WhatsCanvasPackageConsumerExample POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${WHATSCANVAS_PACKAGE_PREFIX}/bin"
            "$<TARGET_FILE_DIR:WhatsCanvasPackageConsumerExample>"
        COMMENT "Copying WhatsCanvas runtime DLLs next to the example executable"
    )
endif()

if (WHATSCANVAS_PACKAGE_TARGET STREQUAL "OpenGL")
    target_compile_definitions(WhatsCanvasPackageConsumerExample PRIVATE WHATSCANVAS_EXAMPLE_PACKAGE_OPENGL=1)
elseif (WHATSCANVAS_PACKAGE_TARGET STREQUAL "OpenGLES")
    target_compile_definitions(WhatsCanvasPackageConsumerExample PRIVATE WHATSCANVAS_EXAMPLE_PACKAGE_OPENGLES=1)
else()
    target_compile_definitions(WhatsCanvasPackageConsumerExample PRIVATE WHATSCANVAS_EXAMPLE_PACKAGE_SOFTWARE=1)
endif()

if (WHATSCANVAS_RUNTIME_BACKEND STREQUAL "OpenGL")
    target_compile_definitions(WhatsCanvasPackageConsumerExample PRIVATE WHATSCANVAS_EXAMPLE_RUNTIME_OPENGL=1)
elseif (WHATSCANVAS_RUNTIME_BACKEND STREQUAL "OpenGLES")
    target_compile_definitions(WhatsCanvasPackageConsumerExample PRIVATE WHATSCANVAS_EXAMPLE_RUNTIME_OPENGLES=1)
elseif (WHATSCANVAS_RUNTIME_BACKEND STREQUAL "Vulkan")
    target_compile_definitions(WhatsCanvasPackageConsumerExample PRIVATE WHATSCANVAS_EXAMPLE_RUNTIME_VULKAN=1)
else()
    target_compile_definitions(WhatsCanvasPackageConsumerExample PRIVATE WHATSCANVAS_EXAMPLE_RUNTIME_SOFTWARE=1)
endif()