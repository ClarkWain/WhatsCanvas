cmake_minimum_required (VERSION 3.8)

project (CppDemo)

set(CMAKE_CXX_STANDARD 17)

if (MSVC)
    # 全局禁用警告 C4819
    add_compile_options(/wd4819)
endif()

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party)
set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

set(GLFW_PATH ${THIRD_PARTY_DIR}/glfw_3.3_x64)
set(GLAD_PATH ${THIRD_PARTY_DIR}/glad)
set(STB_PATH ${THIRD_PARTY_DIR}/stb)
set(GLM_PATH ${THIRD_PARTY_DIR}/glm)

include_directories(${SRC_DIR})
include_directories(${GLFW_PATH}/include)
include_directories(${GLAD_PATH}/include) 
include_directories(${GLM_PATH})
include_directories(${STB_PATH})

add_subdirectory(${THIRD_PARTY_DIR}/glm)

add_library(GLFW STATIC IMPORTED )
set_target_properties( GLFW PROPERTIES IMPORTED_LOCATION ${GLFW_PATH}/lib-vc2022/glfw3.lib )

add_library(GLAD STATIC "${GLAD_PATH}/src/glad.c")

add_executable(CppDemo 
   "main.cpp"
   "${SRC_DIR}/canvas/Canvas.cpp"
   "${SRC_DIR}/canvas/Image.cpp"
   "${SRC_DIR}/canvas/Paint.cpp"
   "${SRC_DIR}/opengl/GLProgram.cpp"
   "${SRC_DIR}/opengl/GLVertexArray.cpp"
    "${SRC_DIR}/command/DrawPoints.cpp"
    "${SRC_DIR}/command/DrawLines.cpp"
   "${SRC_DIR}/render/Renderer.cpp"
)

target_compile_definitions(CppDemo PRIVATE GLEW_STATIC)

target_link_options(CppDemo PRIVATE /NODEFAULTLIB:MSVCRT /NODEFAULTLIB:LIBCMT)
 
target_link_libraries(CppDemo 
    GLFW 
    GLAD 
    opengl32
    glm
)

