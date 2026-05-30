cmake_minimum_required(VERSION 3.16)
project(Racer LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
include(${PROJECT_ROOT}/cmake/PrismCanvasOpenGL.cmake)

prismcanvas_add_common_dependencies(${PROJECT_ROOT})
prismcanvas_add_opengl_library(PrismCanvasExampleOpenGL ${PROJECT_ROOT})

add_executable(Racer
    "Racer.cpp"
)

target_link_libraries(Racer
    PRIVATE
        PrismCanvasExampleOpenGL
)

prismcanvas_link_gl_app(Racer)