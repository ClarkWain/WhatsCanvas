cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasMetalPresent LANGUAGES C CXX OBJCXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

whatscanvas_add_glfw_dependency("${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(WhatsCanvasMetalPresent
    "main.mm"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(WhatsCanvasMetalPresent PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
    )
endforeach()

# Enable ARC for the Objective-C++ source. Also opt in to modules so the
# Metal / QuartzCore / AppKit imports pick up the correct framework map.
set_source_files_properties("main.mm" PROPERTIES
    COMPILE_FLAGS "-fobjc-arc -fmodules")

target_link_libraries(WhatsCanvasMetalPresent
    PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
        glfw
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
        "-framework AppKit"
        "-framework CoreGraphics"
)
