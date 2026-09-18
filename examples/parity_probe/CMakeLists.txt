cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasParityProbe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

# Requires both backends and a GLFW dependency (already used by the tree).
if (NOT TARGET WhatsCanvasOpenGL OR NOT TARGET WhatsCanvasSoftware)
    return()
endif()

add_executable(WhatsCanvasParityProbe "main.cpp")

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(WhatsCanvasParityProbe PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
    )
endforeach()

target_link_libraries(WhatsCanvasParityProbe
    PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasSoftware
)

# Reuse the same helper the other GL-linked demos use to bring in GLFW etc.
whatscanvas_link_gl_app(WhatsCanvasParityProbe "${CMAKE_CURRENT_LIST_DIR}/../..")
