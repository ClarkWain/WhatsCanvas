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
    "${_desktop_src_dir}/hosts/software/SoftwareRuntime.cpp"
)

target_include_directories(WhatsCanvasDesktopHost
    PRIVATE
        "${_desktop_src_dir}"
)

target_link_libraries(WhatsCanvasDesktopHost
    PRIVATE
        WhatsCanvas::OpenGL
)
if (TARGET WhatsCanvas::Software)
    target_link_libraries(WhatsCanvasDesktopHost PRIVATE WhatsCanvas::Software)
endif()

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(WhatsCanvasDesktopHost PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
    )
endforeach()

# Pull in GLFW + system OpenGL loader flags, mirroring what examples use.
whatscanvas_link_gl_app(WhatsCanvasDesktopHost "${CMAKE_CURRENT_LIST_DIR}/../..")

if (BUILD_TESTING)
    add_executable(WhatsCanvasDesktopSceneViewportTests
        "${CMAKE_CURRENT_LIST_DIR}/../../tests/DesktopSceneViewportTests.cpp"
    )
    target_include_directories(WhatsCanvasDesktopSceneViewportTests PRIVATE
        "${CMAKE_CURRENT_LIST_DIR}/../.."
    )
    add_test(NAME WhatsCanvasDesktopSceneViewportTests
        COMMAND WhatsCanvasDesktopSceneViewportTests
    )
    set_tests_properties(WhatsCanvasDesktopSceneViewportTests PROPERTIES
        LABELS "unit;desktop;platforms"
        TIMEOUT 30
    )
endif()
