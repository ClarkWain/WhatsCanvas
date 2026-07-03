cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasAAShowcase LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

if (TARGET WhatsCanvas::OpenGL)
    add_executable(WhatsCanvasAAShowcase
        "main.cpp"
    )

    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${config} config_upper)
        set_target_properties(WhatsCanvasAAShowcase PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
        )
    endforeach()

    target_include_directories(WhatsCanvasAAShowcase
        PRIVATE
            "${CMAKE_CURRENT_LIST_DIR}/../../third_party/stb"
    )

    target_link_libraries(WhatsCanvasAAShowcase
        PRIVATE
            WhatsCanvas::OpenGL
    )

    whatscanvas_link_gl_app(WhatsCanvasAAShowcase "${CMAKE_CURRENT_LIST_DIR}/../..")
endif()
