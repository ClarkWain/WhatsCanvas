cmake_minimum_required(VERSION 3.16)
project(SpiderSolitaire LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
include(${PROJECT_ROOT}/cmake/WhatsCanvasOpenGL.cmake)

whatscanvas_add_common_dependencies(${PROJECT_ROOT})
whatscanvas_add_opengl_library(WhatsCanvasExampleOpenGL ${PROJECT_ROOT})

add_executable(SpiderSolitaire "SpiderSolitaire.cpp")
target_link_libraries(SpiderSolitaire PRIVATE WhatsCanvasExampleOpenGL)
whatscanvas_link_gl_app(SpiderSolitaire "${PROJECT_ROOT}")
