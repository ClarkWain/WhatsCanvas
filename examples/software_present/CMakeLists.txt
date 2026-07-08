cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasSoftwarePresent LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

whatscanvas_add_glfw_dependency("${CMAKE_CURRENT_LIST_DIR}/../..")

add_executable(WhatsCanvasSoftwarePresent
    "main.cpp"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(WhatsCanvasSoftwarePresent PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
    )
endforeach()

target_link_libraries(WhatsCanvasSoftwarePresent
    PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
        glfw
)
