cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasDesktopPlatform LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

if (NOT TARGET WhatsCanvas::OpenGL)
    message(WARNING "platforms/desktop requires WhatsCanvas::OpenGL. Skipping.")
    return()
endif()

set(_desktop_src_dir "${CMAKE_CURRENT_LIST_DIR}/src")

add_executable(WhatsCanvasDesktopHost
    "${_desktop_src_dir}/main.cpp"
    "${_desktop_src_dir}/SceneCatalog.cpp"
    "${_desktop_src_dir}/scenes/FeatureShowcaseScene.cpp"
    "${_desktop_src_dir}/hosts/glfw/GlfwHost.cpp"
)

target_include_directories(WhatsCanvasDesktopHost
    PRIVATE
        "${_desktop_src_dir}"
)

target_link_libraries(WhatsCanvasDesktopHost
    PRIVATE
        WhatsCanvas::OpenGL
)

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(WhatsCanvasDesktopHost PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
    )
endforeach()

# Pull in GLFW + system OpenGL loader flags, mirroring what examples use.
whatscanvas_link_gl_app(WhatsCanvasDesktopHost "${CMAKE_CURRENT_LIST_DIR}/../..")
