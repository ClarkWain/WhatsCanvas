cmake_minimum_required(VERSION 3.16)

project(PrismCanvas LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    # 全局禁用警告 C4819
    add_compile_options(/wd4819)
endif()

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party)
set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

set(GLFW_PATH ${THIRD_PARTY_DIR}/glfw)
set(GLAD_PATH ${THIRD_PARTY_DIR}/glad)
set(STB_PATH ${THIRD_PARTY_DIR}/stb)
set(GLM_PATH ${THIRD_PARTY_DIR}/glm)
set(POLYLINE2D_PATH ${THIRD_PARTY_DIR}/polyline2d)

if (NOT EXISTS "${GLFW_PATH}/CMakeLists.txt" OR
    NOT EXISTS "${GLM_PATH}/glm/glm.hpp" OR
    NOT EXISTS "${STB_PATH}/stb_image.h" OR
    NOT EXISTS "${POLYLINE2D_PATH}/include/Polyline2D.h")
    message(FATAL_ERROR "Missing third-party dependencies. Run: git submodule update --init --recursive")
endif()

find_package(OpenGL REQUIRED)

set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(${GLFW_PATH} EXCLUDE_FROM_ALL)

add_library(GLAD STATIC "${GLAD_PATH}/src/glad.c")
target_include_directories(GLAD PUBLIC "${GLAD_PATH}/include")

add_library(PrismCanvasGLM INTERFACE)
target_include_directories(PrismCanvasGLM INTERFACE
    "${GLM_PATH}"
    "${GLM_PATH}/glm"
)

add_library(PrismCanvasSTB INTERFACE)
target_include_directories(PrismCanvasSTB INTERFACE "${STB_PATH}")

add_library(PrismCanvasPolyline2D INTERFACE)
target_include_directories(PrismCanvasPolyline2D SYSTEM INTERFACE "${POLYLINE2D_PATH}/include")

add_executable(PrismCanvasDemo
    "${SRC_DIR}/main.cpp"
    "${SRC_DIR}/canvas/Canvas.cpp"
    "${SRC_DIR}/canvas/Image.cpp"
    "${SRC_DIR}/canvas/Paint.cpp"
    "${SRC_DIR}/opengl/GLProgram.cpp"
    "${SRC_DIR}/opengl/GLVertexArray.cpp"
    "${SRC_DIR}/command/DrawPoints.cpp"
    "${SRC_DIR}/command/DrawLines.cpp"
    "${SRC_DIR}/command/DrawPath.cpp"
    "${SRC_DIR}/command/DrawImage.cpp"
    "${SRC_DIR}/command/DrawText.cpp"
    "${SRC_DIR}/render/Renderer.cpp"
)

target_include_directories(PrismCanvasDemo PRIVATE "${SRC_DIR}")

target_compile_definitions(PrismCanvasDemo PRIVATE GLEW_STATIC)

target_link_libraries(PrismCanvasDemo
    PRIVATE
        glfw
        GLAD
        OpenGL::GL
        PrismCanvasGLM
        PrismCanvasSTB
        PrismCanvasPolyline2D
)

if (WIN32)
    target_link_libraries(PrismCanvasDemo PRIVATE gdi32)
endif()

