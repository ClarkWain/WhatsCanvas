cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasTutorials LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

if (TARGET WhatsCanvasSoftware)
    set(TUTORIAL_SOURCES
        chapter02_cards.cpp
        chapter03_buttons.cpp
        chapter04_gauge.cpp
        chapter05_flower.cpp
        chapter06_gallery.cpp
        chapter07_chat.cpp
        chapter08_filters.cpp
    )

    foreach(src ${TUTORIAL_SOURCES})
        get_filename_component(name ${src} NAME_WE)
        add_executable(${name} ${src})
        target_link_libraries(${name} PRIVATE WhatsCanvasSoftware)
        foreach(config Debug Release RelWithDebInfo MinSizeRel)
            string(TOUPPER ${config} config_upper)
            set_target_properties(${name} PROPERTIES
                "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
            )
        endforeach()
    endforeach()
endif()
