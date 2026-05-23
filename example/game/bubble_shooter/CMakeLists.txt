cmake_minimum_required(VERSION 3.16)
project(BubbleShooter LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../../..)
set(THIRD_PARTY_DIR ${PROJECT_ROOT}/third_party)
set(SRC_DIR ${PROJECT_ROOT}/src)

set(GLFW_PATH ${THIRD_PARTY_DIR}/glfw)
set(GLAD_PATH ${THIRD_PARTY_DIR}/glad)
set(STB_PATH ${THIRD_PARTY_DIR}/stb)
set(GLM_PATH ${THIRD_PARTY_DIR}/glm)
set(POLYLINE2D_PATH ${THIRD_PARTY_DIR}/polyline2d)

find_package(OpenGL REQUIRED)

add_subdirectory(${GLFW_PATH} "third_party/glfw" EXCLUDE_FROM_ALL)

add_library(GLAD STATIC "${GLAD_PATH}/src/glad.c")
target_include_directories(GLAD PUBLIC "${GLAD_PATH}/include")

add_library(PrismCanvasGLM INTERFACE)
target_include_directories(PrismCanvasGLM INTERFACE "${GLM_PATH}" "${GLM_PATH}/glm")

add_library(PrismCanvasSTB INTERFACE)
target_include_directories(PrismCanvasSTB INTERFACE "${STB_PATH}")

add_library(PrismCanvasPolyline2D INTERFACE)
target_include_directories(PrismCanvasPolyline2D SYSTEM INTERFACE "${POLYLINE2D_PATH}/include")

add_executable(BubbleShooter
    "BubbleShooter.cpp"
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

target_include_directories(BubbleShooter PRIVATE "${SRC_DIR}")
target_compile_definitions(BubbleShooter PRIVATE GLEW_STATIC)

target_link_libraries(BubbleShooter
    PRIVATE
        glfw
        GLAD
        OpenGL::GL
        PrismCanvasGLM
        PrismCanvasSTB
        PrismCanvasPolyline2D
)

if (WIN32)
    target_link_libraries(BubbleShooter PRIVATE gdi32)
endif()