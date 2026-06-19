cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasShowcase LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

if (TARGET WhatsCanvas::OpenGL)
    add_executable(WhatsCanvasDemo
        "main.cpp"
    )

    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${config} config_upper)
        set_target_properties(WhatsCanvasDemo PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
        )
    endforeach()

    target_link_libraries(WhatsCanvasDemo
        PRIVATE
            WhatsCanvas::OpenGL
    )

    whatscanvas_link_gl_app(WhatsCanvasDemo)
    return()
endif()

set(PROJECT_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
include(${PROJECT_ROOT}/cmake/WhatsCanvasOpenGL.cmake)

whatscanvas_add_common_dependencies(${PROJECT_ROOT})
whatscanvas_add_opengl_library(WhatsCanvasShowcaseOpenGL ${PROJECT_ROOT})

add_executable(WhatsCanvasDemo
    "main.cpp"
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(WhatsCanvasDemo PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
    )
endforeach()

target_link_libraries(WhatsCanvasDemo
    PRIVATE
        WhatsCanvasShowcaseOpenGL
)

whatscanvas_link_gl_app(WhatsCanvasDemo)
