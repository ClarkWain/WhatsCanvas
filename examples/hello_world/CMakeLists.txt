cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasHelloWorld LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

if (TARGET WhatsCanvasSoftware)
    add_executable(WhatsCanvasHelloWorld
        "main.cpp"
    )

    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${config} config_upper)
        set_target_properties(WhatsCanvasHelloWorld PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
        )
    endforeach()

    target_link_libraries(WhatsCanvasHelloWorld
        PRIVATE
            WhatsCanvasSoftware
    )
endif()
