cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasImageFilterShowcase LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

if (TARGET WhatsCanvas::OpenGL)
    add_executable(WhatsCanvasImageFilterShowcase
        "main.cpp"
    )

    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${config} config_upper)
        set_target_properties(WhatsCanvasImageFilterShowcase PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
        )
    endforeach()

    target_include_directories(WhatsCanvasImageFilterShowcase
        PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../../third_party/glad/include"
            "${CMAKE_CURRENT_LIST_DIR}/../../third_party/stb"
    )

    target_link_libraries(WhatsCanvasImageFilterShowcase
        PRIVATE
            WhatsCanvas::OpenGL
    )

    whatscanvas_link_gl_app(WhatsCanvasImageFilterShowcase "${CMAKE_CURRENT_LIST_DIR}/../..")
endif()
