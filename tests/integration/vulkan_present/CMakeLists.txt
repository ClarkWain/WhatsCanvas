cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasVulkanPresent LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

if (NOT COMMAND whatscanvas_add_glfw_dependency)
    set(PROJECT_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
    include("${PROJECT_ROOT}/cmake/WhatsCanvasOpenGL.cmake")
endif()

find_package(Vulkan QUIET)

if (Vulkan_FOUND)
    whatscanvas_add_glfw_dependency("${CMAKE_CURRENT_LIST_DIR}/../../..")

    add_executable(WhatsCanvasVulkanPresent
        "main.cpp"
    )

    foreach(config Debug Release RelWithDebInfo MinSizeRel)
        string(TOUPPER ${config} config_upper)
        set_target_properties(WhatsCanvasVulkanPresent PROPERTIES
            "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
        )
    endforeach()

    target_link_libraries(WhatsCanvasVulkanPresent
        PRIVATE
            glfw
            Vulkan::Vulkan
    )
else()
    message(STATUS "WhatsCanvasVulkanPresent skipped: Vulkan SDK not found.")
endif()
