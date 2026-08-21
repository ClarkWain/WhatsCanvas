cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasPresentExamples LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

whatscanvas_add_glfw_dependency("${CMAKE_CURRENT_LIST_DIR}/../..")

function(whatscanvas_configure_present_target target source)
    add_executable(${target} "${source}")
    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${config} config_upper)
        set_target_properties(${target} PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
        )
    endforeach()
    target_link_libraries(${target}
        PRIVATE WhatsCanvasOpenGL WhatsCanvasGLM glfw)
endfunction()

whatscanvas_configure_present_target(WhatsCanvasGLPresent gl_main.cpp)

if (WIN32)
    whatscanvas_configure_present_target(
        WhatsCanvasSoftwarePresent software_main.cpp)
endif()

if (WHATSCANVAS_VULKAN_BACKEND_ENABLED)
    whatscanvas_configure_present_target(
        WhatsCanvasVulkanCanvasPresent vulkan_main.cpp)
endif()

if (APPLE AND WHATSCANVAS_METAL_BACKEND_ENABLED)
    enable_language(OBJCXX)
    whatscanvas_configure_present_target(
        WhatsCanvasMetalPresent metal_main.mm)
    set_source_files_properties("metal_main.mm" PROPERTIES
        COMPILE_FLAGS "-fobjc-arc -fmodules")
    target_link_libraries(WhatsCanvasMetalPresent PRIVATE
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
        "-framework AppKit"
        "-framework CoreGraphics")
endif()
