cmake_minimum_required(VERSION 3.16)

project(WhatsCanvas VERSION 0.1.11 LANGUAGES C CXX)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Python3 COMPONENTS Interpreter QUIET)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(WHATSCANVAS_ENABLE_SCRIPT_TESTS "Register existing smoke/example script gates with CTest." ON)
option(WHATSCANVAS_ENABLE_LOCAL_REGRESSION_TEST "Register the strict local pixel-hash regression gate with CTest." OFF)
option(WHATSCANVAS_BUILD_DEMO "Build the root WhatsCanvas demo executable." ON)
option(WHATSCANVAS_INSTALL "Enable install and package export rules." ON)
option(WHATSCANVAS_BUILD_OPENGL "Build the desktop OpenGL renderer library target." ON)
option(WHATSCANVAS_BUILD_OPENGLES "Build the OpenGL ES renderer library target." OFF)
option(WHATSCANVAS_BUILD_SOFTWARE "Build the dependency-free CPU (software) renderer library target." ON)
option(WHATSCANVAS_BUILD_BENCHMARKS "Build lightweight benchmark executables." ON)
option(WHATSCANVAS_ENABLE_OPENTYPE_SHAPING "Enable optional OpenType text shaping adapter hooks when a supported shaper is available." OFF)
option(WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER "Enable optional FreeType glyph rasterizer when FreeType is available." ON)
option(WHATSCANVAS_ENABLE_VULKAN "Enable the experimental Vulkan render backend when a Vulkan SDK is available." OFF)
set(WHATSCANVAS_DEFAULT_SMOKE_HASH "2808591959994190122" CACHE STRING "Default local smoke hash for regression ctest registration.")
set(WHATSCANVAS_CLIP_SMOKE_HASH "7173893163224947210" CACHE STRING "Default local clip-path smoke hash for regression ctest registration.")

if (MSVC)
    # Globally disable warning C4819
    add_compile_options(/wd4819)
endif()

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party)
set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

set(WHATSCANVAS_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(${WHATSCANVAS_CMAKE_DIR}/WhatsCanvasOpenGL.cmake)

whatscanvas_add_common_dependencies(${CMAKE_CURRENT_LIST_DIR})

if (Python3_Interpreter_FOUND)
    add_custom_target(WhatsCanvasGenerateApiReference
        COMMAND ${Python3_EXECUTABLE} scripts/generate_api_reference.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Generating doc/API_REFERENCE.md from include/wsc"
        VERBATIM
    )
    add_custom_target(WhatsCanvasCheckApiReference
        COMMAND ${Python3_EXECUTABLE} scripts/generate_api_reference.py --check
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking generated public API reference freshness"
        VERBATIM
    )
    add_custom_target(WhatsCanvasCheckVersionConsistency
        COMMAND ${Python3_EXECUTABLE} scripts/version_consistency_check.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking release version metadata consistency"
        VERBATIM
    )
else()
    message(STATUS "Python3 interpreter not found; API reference and version consistency targets are disabled.")
endif()

if (WIN32)
    add_custom_target(WhatsCanvasCheckPackageConsumer
        COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_consumer_smoke.bat
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking external CMake package consumer"
        VERBATIM
    )
else()
    add_custom_target(WhatsCanvasCheckPackageConsumer
        COMMAND sh ${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_consumer_smoke.sh
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking external CMake package consumer"
        VERBATIM
    )
endif()

if (WHATSCANVAS_BUILD_OPENGL)
    whatscanvas_add_opengl_library(WhatsCanvasOpenGL ${CMAKE_CURRENT_LIST_DIR})
    add_library(WhatsCanvas::OpenGL ALIAS WhatsCanvasOpenGL)
endif()

if (WHATSCANVAS_BUILD_OPENGLES)
    whatscanvas_add_opengles_library(WhatsCanvasOpenGLES ${CMAKE_CURRENT_LIST_DIR})
    add_library(WhatsCanvas::OpenGLES ALIAS WhatsCanvasOpenGLES)
endif()

if (WHATSCANVAS_BUILD_SOFTWARE)
    whatscanvas_add_software_library(WhatsCanvasSoftware ${CMAKE_CURRENT_LIST_DIR})
    add_library(WhatsCanvas::Software ALIAS WhatsCanvasSoftware)
endif()

if (NOT TARGET WhatsCanvasOpenGL AND NOT TARGET WhatsCanvasOpenGLES AND NOT TARGET WhatsCanvasSoftware)
    message(FATAL_ERROR "At least one renderer target must be enabled: WHATSCANVAS_BUILD_OPENGL, WHATSCANVAS_BUILD_OPENGLES or WHATSCANVAS_BUILD_SOFTWARE.")
endif()

if (WHATSCANVAS_BUILD_DEMO AND TARGET WhatsCanvasOpenGL)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/showcase")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/aa_showcase")
    if (WHATSCANVAS_ENABLE_VULKAN)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/vulkan_present")
    endif()
endif()

if (TARGET WhatsCanvasOpenGL)
    add_executable(WhatsCanvasFeatureSnippets
        "${CMAKE_CURRENT_SOURCE_DIR}/examples/snippets/FeatureSnippets.cpp"
    )
    target_link_libraries(WhatsCanvasFeatureSnippets PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
endif()

if (BUILD_TESTING AND TARGET WhatsCanvasOpenGL)
    add_executable(WhatsCanvasGraphicsStateStackTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GraphicsStateStackTests.cpp"
    )
    target_include_directories(WhatsCanvasGraphicsStateStackTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasGraphicsStateStackTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasGraphicsStateStackTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasGraphicsStateStackTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasGraphicsStateStackTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasGraphicsStateStackTests PROPERTIES LABELS "unit")

    if (WHATSCANVAS_ENABLE_VULKAN)
        add_executable(WhatsCanvasVulkanDeviceTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanDeviceTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanDeviceTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanDeviceTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanDeviceTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanDeviceTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanDeviceTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanDeviceTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanRenderTargetTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanRenderTargetTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanRenderTargetTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanRenderTargetTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanRenderTargetTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanRenderTargetTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanRenderTargetTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanRenderTargetTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanSolidGeometryTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanSolidGeometryTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanSolidGeometryTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanSolidGeometryTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanSolidGeometryTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanSolidGeometryTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanSolidGeometryTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanSolidGeometryTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanPaintTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanPaintTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanPaintTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanPaintTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanPaintTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanPaintTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanPaintTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanPaintTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanTextureTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanTextureTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanTextureTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanTextureTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanTextureTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanTextureTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanTextureTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanTextureTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanLayerTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanLayerTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanLayerTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanLayerTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanLayerTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanLayerTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanLayerTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanLayerTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanClipTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanClipTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanClipTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanClipTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanClipTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanClipTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanClipTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanClipTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanDrawListTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanDrawListTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanDrawListTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanDrawListTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanDrawListTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanDrawListTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanDrawListTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanDrawListTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanCommandTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanCommandTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanCommandTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanCommandTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanCommandTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanCommandTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanCommandTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanCommandTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanImageColorTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanImageColorTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanImageColorTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanImageColorTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanImageColorTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanImageColorTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanImageColorTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanImageColorTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanGradientTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanGradientTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanGradientTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanGradientTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanGradientTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanGradientTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanGradientTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanGradientTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanAATests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanAATests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanAATests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanAATests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanAATests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanAATests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanAATests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanAATests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanTextTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanTextTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanTextTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanTextTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanTextTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanTextTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanTextTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanTextTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanBlendModeTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanBlendModeTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanBlendModeTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanBlendModeTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanBlendModeTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanBlendModeTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanBlendModeTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanBlendModeTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanShadowTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanShadowTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanShadowTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanShadowTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanShadowTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanShadowTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanShadowTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanShadowTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanClipCommandTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanClipCommandTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanClipCommandTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanClipCommandTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanClipCommandTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanClipCommandTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanClipCommandTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanClipCommandTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanMipmapTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanMipmapTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanMipmapTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanMipmapTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanMipmapTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanMipmapTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanMipmapTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanMipmapTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        add_executable(WhatsCanvasVulkanExternalImageTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanExternalImageTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanExternalImageTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanExternalImageTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanExternalImageTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanExternalImageTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanExternalImageTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanExternalImageTests PROPERTIES LABELS "vulkan" TIMEOUT 60)

        # Serialize all Vulkan GPU tests via a shared resource lock. Each test
        # creates its own VkInstance/VkDevice; running them concurrently can race
        # on driver/device creation on some drivers and cause spurious failures.
        set_tests_properties(
            WhatsCanvasVulkanDeviceTests
            WhatsCanvasVulkanRenderTargetTests
            WhatsCanvasVulkanSolidGeometryTests
            WhatsCanvasVulkanPaintTests
            WhatsCanvasVulkanTextureTests
            WhatsCanvasVulkanLayerTests
            WhatsCanvasVulkanClipTests
            WhatsCanvasVulkanDrawListTests
            WhatsCanvasVulkanCommandTests
            WhatsCanvasVulkanImageColorTests
            WhatsCanvasVulkanGradientTests
            WhatsCanvasVulkanAATests
            WhatsCanvasVulkanTextTests
            WhatsCanvasVulkanBlendModeTests
            WhatsCanvasVulkanShadowTests
            WhatsCanvasVulkanClipCommandTests
            WhatsCanvasVulkanMipmapTests
            WhatsCanvasVulkanExternalImageTests
            PROPERTIES RESOURCE_LOCK "vulkan_gpu"
        )
    endif()

    add_executable(WhatsCanvasTextLayoutTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/TextLayoutTests.cpp"
    )
    target_link_libraries(WhatsCanvasTextLayoutTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasTextLayoutTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasTextLayoutTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasTextLayoutTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasTextLayoutTests PROPERTIES LABELS "unit;text")

    add_executable(WhatsCanvasTextUtilsTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/TextUtilsTests.cpp"
    )
    target_include_directories(WhatsCanvasTextUtilsTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasTextUtilsTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasTextUtilsTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasTextUtilsTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasTextUtilsTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasTextUtilsTests PROPERTIES LABELS "unit;text")

    add_executable(WhatsCanvasUnicodeBidiConformanceTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/UnicodeBidiConformanceTests.cpp"
    )
    target_include_directories(WhatsCanvasUnicodeBidiConformanceTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasUnicodeBidiConformanceTests PRIVATE
        WhatsCanvasOpenGL
    )
    add_test(NAME WhatsCanvasUnicodeBidiConformanceTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasUnicodeBidiConformanceTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasUnicodeBidiConformanceTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -DWHATSCANVAS_TEST_ARGS=${CMAKE_CURRENT_SOURCE_DIR}/tests/unicode
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasUnicodeBidiConformanceTests PROPERTIES
        LABELS "unit;text;unicode"
        TIMEOUT 120
    )

    add_executable(WhatsCanvasFontManagerTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/FontManagerTests.cpp"
    )
    target_link_libraries(WhatsCanvasFontManagerTests PRIVATE
        WhatsCanvasOpenGL
    )
    add_test(NAME WhatsCanvasFontManagerTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasFontManagerTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasFontManagerTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasFontManagerTests PROPERTIES LABELS "unit;text;font")

    add_executable(WhatsCanvasTextBackendContractTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/TextBackendContractTests.cpp"
    )
    target_include_directories(WhatsCanvasTextBackendContractTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasTextBackendContractTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasTextBackendContractTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasTextBackendContractTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasTextBackendContractTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasTextBackendContractTests PROPERTIES LABELS "unit;text")

    add_executable(WhatsCanvasTextRegressionTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/TextRegressionTests.cpp"
    )
    target_include_directories(WhatsCanvasTextRegressionTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasTextRegressionTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasTextRegressionTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasTextRegressionTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasTextRegressionTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasTextRegressionTests PROPERTIES LABELS "unit;text")

    add_executable(WhatsCanvasRenderStatsTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/RenderStatsTests.cpp"
    )
    target_include_directories(WhatsCanvasRenderStatsTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasRenderStatsTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasRenderStatsTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasRenderStatsTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasRenderStatsTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasRenderStatsTests PROPERTIES LABELS "unit;diagnostics")

    add_executable(WhatsCanvasBatchMergeTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/BatchMergeTests.cpp"
    )
    target_include_directories(WhatsCanvasBatchMergeTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasBatchMergeTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasBatchMergeTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasBatchMergeTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasBatchMergeTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasBatchMergeTests PROPERTIES LABELS "unit;diagnostics")

    add_executable(WhatsCanvasLruCacheTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/LruCacheTests.cpp"
    )
    target_include_directories(WhatsCanvasLruCacheTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasLruCacheTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasLruCacheTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasLruCacheTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasLruCacheTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasLruCacheTests PROPERTIES LABELS "unit;diagnostics")

    add_executable(WhatsCanvasPathTessellationCacheTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/PathTessellationCacheTests.cpp"
    )
    target_include_directories(WhatsCanvasPathTessellationCacheTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasPathTessellationCacheTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasPathTessellationCacheTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasPathTessellationCacheTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasPathTessellationCacheTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasPathTessellationCacheTests PROPERTIES LABELS "unit;diagnostics")

    add_executable(WhatsCanvasGaussianKernelTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GaussianKernelTests.cpp"
    )
    target_include_directories(WhatsCanvasGaussianKernelTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasGaussianKernelTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasGaussianKernelTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasGaussianKernelTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasGaussianKernelTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasGaussianKernelTests PROPERTIES LABELS "unit;diagnostics")

    add_executable(WhatsCanvasSoftwareRendererTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/SoftwareRendererTests.cpp"
    )
    target_include_directories(WhatsCanvasSoftwareRendererTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasSoftwareRendererTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasSoftwareRendererTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasSoftwareRendererTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasSoftwareRendererTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasSoftwareRendererTests PROPERTIES LABELS "unit;diagnostics")

    add_executable(WhatsCanvasSoftwareGoldenTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/SoftwareGoldenTests.cpp"
    )
    target_include_directories(WhatsCanvasSoftwareGoldenTests PRIVATE
        "${SRC_DIR}"
    )
    target_compile_definitions(WhatsCanvasSoftwareGoldenTests PRIVATE
        "WHATSCANVAS_SOFTWARE_BASELINE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/tests/baselines/software\""
    )
    target_link_libraries(WhatsCanvasSoftwareGoldenTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasSoftwareGoldenTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasSoftwareGoldenTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasSoftwareGoldenTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasSoftwareGoldenTests PROPERTIES LABELS "unit;diagnostics")

    add_executable(WhatsCanvasVulkanBackendSelectionTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanBackendSelectionTests.cpp"
    )
    target_link_libraries(WhatsCanvasVulkanBackendSelectionTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasVulkanBackendSelectionTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanBackendSelectionTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanBackendSelectionTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasVulkanBackendSelectionTests PROPERTIES LABELS "unit;vulkan")

    add_executable(WhatsCanvasRenderTargetPoolTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/RenderTargetPoolTests.cpp"
    )
    target_include_directories(WhatsCanvasRenderTargetPoolTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasRenderTargetPoolTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasRenderTargetPoolTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasRenderTargetPoolTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasRenderTargetPoolTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasRenderTargetPoolTests PROPERTIES LABELS "unit;render-target")

    add_executable(WhatsCanvasCanvasAdapterTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/CanvasAdapterTests.cpp"
    )
    target_link_libraries(WhatsCanvasCanvasAdapterTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasCanvasAdapterTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasCanvasAdapterTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasCanvasAdapterTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasCanvasAdapterTests PROPERTIES LABELS "unit;adapter")

    add_executable(WhatsCanvasMatrixClipTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MatrixClipTests.cpp"
    )
    target_link_libraries(WhatsCanvasMatrixClipTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMatrixClipTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMatrixClipTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMatrixClipTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMatrixClipTests PROPERTIES LABELS "unit;matrix;clip")

    add_executable(WhatsCanvasPaintStateTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/PaintStateTests.cpp"
    )
    target_link_libraries(WhatsCanvasPaintStateTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasPaintStateTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasPaintStateTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasPaintStateTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasPaintStateTests PROPERTIES LABELS "unit;paint")

    add_executable(WhatsCanvasImageResourceLifecycleTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ImageResourceLifecycleTests.cpp"
    )
    target_include_directories(WhatsCanvasImageResourceLifecycleTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasImageResourceLifecycleTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasImageResourceLifecycleTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasImageResourceLifecycleTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasImageResourceLifecycleTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasImageResourceLifecycleTests PROPERTIES LABELS "unit;image")

    add_executable(WhatsCanvasContextLifecycleTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ContextLifecycleTests.cpp"
    )
    target_include_directories(WhatsCanvasContextLifecycleTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasContextLifecycleTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasContextLifecycleTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasContextLifecycleTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasContextLifecycleTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasContextLifecycleTests PROPERTIES LABELS "unit;context")

    add_executable(WhatsCanvasGlyphAtlasTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GlyphAtlasTests.cpp"
    )
    target_include_directories(WhatsCanvasGlyphAtlasTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasGlyphAtlasTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasGlyphAtlasTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasGlyphAtlasTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasGlyphAtlasTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasGlyphAtlasTests PROPERTIES LABELS "unit;text;glyph-atlas")

    add_executable(WhatsCanvasDeprecationTrackerTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/DeprecationTrackerTests.cpp"
    )
    target_include_directories(WhatsCanvasDeprecationTrackerTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasDeprecationTrackerTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasDeprecationTrackerTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasDeprecationTrackerTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasDeprecationTrackerTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasDeprecationTrackerTests PROPERTIES LABELS "unit;api")
endif()

# Standalone proof for the GPU-free path: the same head-less software tests
# linked against ONLY the dependency-free WhatsCanvasSoftware library (no
# OpenGL / glad / Vulkan). Guarded solely on WhatsCanvasSoftware so it is
# available in a truly GPU-free configuration (WHATSCANVAS_BUILD_OPENGL=OFF,
# WHATSCANVAS_BUILD_SOFTWARE=ON) as well as the default build.
if (BUILD_TESTING AND TARGET WhatsCanvasSoftware)
    add_executable(WhatsCanvasSoftwareLibTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/SoftwareRendererTests.cpp"
    )
    target_link_libraries(WhatsCanvasSoftwareLibTests PRIVATE
        WhatsCanvasSoftware
    )
    add_test(NAME WhatsCanvasSoftwareLibTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasSoftwareLibTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasSoftwareLibTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasSoftwareLibTests PROPERTIES LABELS "unit;diagnostics")
endif()

if (WHATSCANVAS_BUILD_BENCHMARKS AND TARGET WhatsCanvasOpenGL)
    add_executable(WhatsCanvasCoreBenchmarks
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/CoreBenchmarks.cpp"
    )
    target_include_directories(WhatsCanvasCoreBenchmarks PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasCoreBenchmarks PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
endif()

if (BUILD_TESTING AND TARGET WhatsCanvasAAShowcase)
    add_test(NAME WhatsCanvasAAShowcaseSmoke
        COMMAND WhatsCanvasAAShowcase "${CMAKE_CURRENT_BINARY_DIR}/aa_showcase_smoke.png"
    )
    set_tests_properties(WhatsCanvasAAShowcaseSmoke PROPERTIES LABELS "smoke;aa;examples" TIMEOUT 120)
endif()

if (BUILD_TESTING AND WHATSCANVAS_ENABLE_SCRIPT_TESTS)
    set(WHATSCANVAS_TEST_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunScriptGate.cmake")
    if (WIN32)
        add_test(NAME WhatsCanvasSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke_test.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasClipPathSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/clip_path_smoke.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasExamplesSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/examples_smoke.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasValidationSceneSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/validation_scene_smoke.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasOpenGLESSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/opengles_build_smoke.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasApiReferenceCheck
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/api_reference_check.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasVersionConsistencyCheck
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/version_consistency_check.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasPackageConsumerSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_consumer_smoke.bat
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )

        if (WHATSCANVAS_ENABLE_LOCAL_REGRESSION_TEST)
            add_test(NAME WhatsCanvasRegressionSmoke
                COMMAND ${CMAKE_COMMAND}
                    -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_smoke.bat
                    -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -DWHATSCANVAS_GATE_ARGS=${WHATSCANVAS_DEFAULT_SMOKE_HASH};${WHATSCANVAS_CLIP_SMOKE_HASH}
                    -P ${WHATSCANVAS_TEST_WRAPPER}
            )
            set_tests_properties(WhatsCanvasRegressionSmoke PROPERTIES LABELS "smoke;regression;local")
            set_tests_properties(WhatsCanvasRegressionSmoke PROPERTIES TIMEOUT 1800)
            add_test(NAME WhatsCanvasTextPixelRegression
                COMMAND ${CMAKE_COMMAND}
                    -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/text_pixel_regression.bat
                    -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -P ${WHATSCANVAS_TEST_WRAPPER}
            )
            set_tests_properties(WhatsCanvasTextPixelRegression PROPERTIES LABELS "smoke;regression;local;text;font")
            set_tests_properties(WhatsCanvasTextPixelRegression PROPERTIES TIMEOUT 1800)
        endif()
    else()
        add_test(NAME WhatsCanvasSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/smoke_test.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasClipPathSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/clip_path_smoke.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasExamplesSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/examples_smoke.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasValidationSceneSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/validation_scene_smoke.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasOpenGLESSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/opengles_build_smoke.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasApiReferenceCheck
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/api_reference_check.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasVersionConsistencyCheck
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/version_consistency_check.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )
        add_test(NAME WhatsCanvasPackageConsumerSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/package_consumer_smoke.sh
                -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_GATE_LAUNCHER=sh
                -P ${WHATSCANVAS_TEST_WRAPPER}
        )

        if (WHATSCANVAS_ENABLE_LOCAL_REGRESSION_TEST)
            add_test(NAME WhatsCanvasRegressionSmoke
                COMMAND ${CMAKE_COMMAND}
                    -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/regression_smoke.sh
                    -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -DWHATSCANVAS_GATE_LAUNCHER=sh
                    -DWHATSCANVAS_GATE_ARGS=${WHATSCANVAS_DEFAULT_SMOKE_HASH};${WHATSCANVAS_CLIP_SMOKE_HASH}
                    -P ${WHATSCANVAS_TEST_WRAPPER}
            )
            set_tests_properties(WhatsCanvasRegressionSmoke PROPERTIES LABELS "smoke;regression;local")
            set_tests_properties(WhatsCanvasRegressionSmoke PROPERTIES TIMEOUT 1800)
            add_test(NAME WhatsCanvasTextPixelRegression
                COMMAND ${CMAKE_COMMAND}
                    -DWHATSCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/scripts/text_pixel_regression.sh
                    -DWHATSCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -DWHATSCANVAS_GATE_LAUNCHER=sh
                    -P ${WHATSCANVAS_TEST_WRAPPER}
            )
            set_tests_properties(WhatsCanvasTextPixelRegression PROPERTIES LABELS "smoke;regression;local;text;font")
            set_tests_properties(WhatsCanvasTextPixelRegression PROPERTIES TIMEOUT 1800)
        endif()
    endif()

    set_tests_properties(
        WhatsCanvasSmoke
        WhatsCanvasClipPathSmoke
        WhatsCanvasExamplesSmoke
        WhatsCanvasValidationSceneSmoke
        WhatsCanvasOpenGLESSmoke
        WhatsCanvasApiReferenceCheck
        WhatsCanvasVersionConsistencyCheck
        WhatsCanvasPackageConsumerSmoke
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 1800
    )
    set_tests_properties(WhatsCanvasSmoke PROPERTIES LABELS "smoke")
    set_tests_properties(WhatsCanvasClipPathSmoke PROPERTIES LABELS "smoke;clip")
    set_tests_properties(WhatsCanvasExamplesSmoke PROPERTIES LABELS "smoke;examples")
    set_tests_properties(WhatsCanvasValidationSceneSmoke PROPERTIES LABELS "smoke;validation")
    set_tests_properties(WhatsCanvasOpenGLESSmoke PROPERTIES LABELS "smoke;opengles")
    set_tests_properties(WhatsCanvasApiReferenceCheck PROPERTIES LABELS "smoke;api;docs")
    set_tests_properties(WhatsCanvasVersionConsistencyCheck PROPERTIES LABELS "smoke;version;package")
    set_tests_properties(WhatsCanvasPackageConsumerSmoke PROPERTIES LABELS "smoke;package")
endif()

if (WHATSCANVAS_INSTALL)
    set(WHATSCANVAS_OPENGL_PACKAGE_IMPLIB "")
    set(WHATSCANVAS_PACKAGE_REQUIRES_OPENGL OFF)

    if (TARGET WhatsCanvasOpenGL)
        get_target_property(WHATSCANVAS_OPENGL_OUTPUT_NAME WhatsCanvasOpenGL OUTPUT_NAME)
        if (NOT WHATSCANVAS_OPENGL_OUTPUT_NAME)
            set(WHATSCANVAS_OPENGL_OUTPUT_NAME "WhatsCanvasOpenGL")
        endif()

        set(WHATSCANVAS_OPENGL_IMPORTED_TYPE STATIC)
        set(WHATSCANVAS_OPENGL_PACKAGE_LOCATION "${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${WHATSCANVAS_OPENGL_OUTPUT_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set(WHATSCANVAS_PACKAGE_REQUIRES_OPENGL ON)

        if (WIN32 AND BUILD_SHARED_LIBS)
            set(WHATSCANVAS_OPENGL_IMPORTED_TYPE SHARED)
            set(WHATSCANVAS_OPENGL_PACKAGE_LOCATION "${CMAKE_INSTALL_BINDIR}/${WHATSCANVAS_OPENGL_OUTPUT_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
            set(WHATSCANVAS_OPENGL_PACKAGE_IMPLIB "${CMAKE_INSTALL_LIBDIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}${WHATSCANVAS_OPENGL_OUTPUT_NAME}${CMAKE_IMPORT_LIBRARY_SUFFIX}")
        endif()
    endif()

    set(WHATSCANVAS_INSTALL_TARGETS)
    if (TARGET WhatsCanvasOpenGL)
        list(APPEND WHATSCANVAS_INSTALL_TARGETS WhatsCanvasOpenGL)
    endif()
    if (TARGET WhatsCanvasOpenGLES)
        list(APPEND WHATSCANVAS_INSTALL_TARGETS WhatsCanvasOpenGLES)
    endif()

    if (WHATSCANVAS_INSTALL_TARGETS)
        install(TARGETS ${WHATSCANVAS_INSTALL_TARGETS}
            EXPORT WhatsCanvasTargets
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        )
    endif()

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/wsc"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WhatsCanvasConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/WhatsCanvasConfig.cmake"
        @ONLY
    )

    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/WhatsCanvasConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/WhatsCanvasConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/WhatsCanvasConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/WhatsCanvas"
    )

    if (WHATSCANVAS_INSTALL_TARGETS)
        install(EXPORT WhatsCanvasTargets
            FILE WhatsCanvasTargets.cmake
            NAMESPACE WhatsCanvas::
            DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/WhatsCanvas"
        )
    endif()
endif()
