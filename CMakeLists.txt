cmake_minimum_required(VERSION 3.16)

project(WhatsCanvas VERSION 0.1.20 LANGUAGES C CXX)

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
option(WHATSCANVAS_INSTALL_TEXT_DEPENDENCIES "Install bundled FreeType/HarfBuzz when an exported renderer links them." ON)
option(WHATSCANVAS_BUILD_OPENGL "Build the desktop OpenGL renderer library target." ON)
option(WHATSCANVAS_BUILD_OPENGLES "Build the OpenGL ES renderer library target." OFF)
option(WHATSCANVAS_BUILD_SOFTWARE "Build the dependency-free CPU (software) renderer library target." ON)
option(WHATSCANVAS_BUILD_BENCHMARKS "Build lightweight benchmark executables." ON)
option(WHATSCANVAS_BUILD_NANOVG_BENCHMARK_ADAPTER "Build the optional NanoVG cross-library benchmark adapter." OFF)
set(WHATSCANVAS_NANOVG_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.nanovg"
    CACHE PATH "Path to a NanoVG source checkout used only by its benchmark adapter.")
option(WHATSCANVAS_INSTALL_GLFW "Install GLFW when WhatsCanvas is embedded by a package that exports a GLFW-dependent target." OFF)
option(WHATSCANVAS_ENABLE_OPENTYPE_SHAPING "Enable HarfBuzz OpenType shaping when HarfBuzz is available." ON)
option(WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER "Enable FreeType glyph rasterization when FreeType is available." ON)
option(WHATSCANVAS_USE_SYSTEM_DEPENDENCIES "Use package-manager-provided glad, glm, stb, FreeType, and HarfBuzz instead of bundled sources." OFF)
option(WHATSCANVAS_ENABLE_VULKAN "Enable the optional Vulkan render backend when a Vulkan SDK is available." OFF)
# The Metal backend is Apple-only. Default ON on Darwin/iOS platforms so
# macOS/iOS builds get a first-class Metal path without extra configuration.
if (APPLE)
    option(WHATSCANVAS_ENABLE_METAL "Enable the Metal render backend on Apple platforms." ON)
else()
    option(WHATSCANVAS_ENABLE_METAL "Enable the Metal render backend on Apple platforms." OFF)
endif()
# The Metal backend requires Objective-C++. Enable OBJCXX as a project language
# up-front so CMake dispatches ".mm" translation units to the correct compiler
# driver on Apple platforms.
if (APPLE AND WHATSCANVAS_ENABLE_METAL)
    enable_language(OBJCXX)
endif()
set(WHATSCANVAS_X11 "AUTO" CACHE STRING "X11 presentation support on Linux: AUTO, ON, or OFF.")
set_property(CACHE WHATSCANVAS_X11 PROPERTY STRINGS AUTO ON OFF)
if (NOT WHATSCANVAS_X11 MATCHES "^(AUTO|ON|OFF)$")
    message(FATAL_ERROR "WHATSCANVAS_X11 must be AUTO, ON, or OFF (got '${WHATSCANVAS_X11}').")
endif()
set(WHATSCANVAS_DEFAULT_SMOKE_HASH "2808591959994190122" CACHE STRING "Default local smoke hash for regression ctest registration.")
set(WHATSCANVAS_CLIP_SMOKE_HASH "7173893163224947210" CACHE STRING "Default local clip-path smoke hash for regression ctest registration.")

if (MSVC)
    # All source files are UTF-8.  Pin the source/execution charset instead of
    # relying on the active Windows ANSI code page; otherwise UTF-8 comments
    # can corrupt preprocessing on non-English developer machines.
    add_compile_options(/utf-8 /wd4819)

    # Prebuilt-binary portability across MSVC toolset versions.
    #
    # Newer MSVC STL updates dispatch std::min_element / max_element / find (etc.)
    # on trivial types to out-of-line, ABI-versioned SIMD helpers such as
    # __std_min_element_f_. Object files compiled with a newer toolset reference
    # symbols that older STL runtimes/import libs do not provide, so a library
    # built on a newer CI toolchain fails to link on a consumer's older VS
    # (LNK2019 __std_*_element_*). Forcing the scalar path keeps our compiled
    # objects free of these version-specific symbols, so the released binaries
    # link against any VS 2022 STL. See doc/TROUBLESHOOTING.md.
    add_compile_definitions(_USE_STD_VECTOR_ALGORITHMS=0)
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
    add_custom_target(WhatsCanvasCheckPerformanceClaims
        COMMAND ${Python3_EXECUTABLE} scripts/performance_claims_check.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Checking public performance claims against benchmark evidence"
        VERBATIM
    )
    if (BUILD_TESTING)
        add_test(NAME WhatsCanvasPerformanceClaimsCheck
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/scripts/performance_claims_check.py
        )
        set_tests_properties(WhatsCanvasPerformanceClaimsCheck PROPERTIES
            LABELS "smoke;performance;docs"
        )
    endif()
else()
    message(STATUS "Python3 interpreter not found; API, version, and performance evidence targets are disabled.")
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

set(WHATSCANVAS_VULKAN_BACKEND_ENABLED OFF)
if (TARGET WhatsCanvasOpenGL)
    get_property(WHATSCANVAS_VULKAN_BACKEND_ENABLED
        GLOBAL PROPERTY WHATSCANVAS_VULKAN_BACKEND_ENABLED)
endif()

set(WHATSCANVAS_METAL_BACKEND_ENABLED OFF)
if (TARGET WhatsCanvasOpenGL)
    get_property(WHATSCANVAS_METAL_BACKEND_ENABLED
        GLOBAL PROPERTY WHATSCANVAS_METAL_BACKEND_ENABLED)
endif()

if (WHATSCANVAS_BUILD_DEMO AND TARGET WhatsCanvasOpenGL)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/showcase")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/aa_showcase")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/image_filter_showcase")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/gl_present")
    if (WIN32)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/software_present")
    endif()
    if (WHATSCANVAS_VULKAN_BACKEND_ENABLED)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/vulkan_present")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/vulkan_canvas_present")
    endif()
    if (WHATSCANVAS_METAL_BACKEND_ENABLED)
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/metal_present")
    endif()
endif()

if (WHATSCANVAS_BUILD_DEMO AND TARGET WhatsCanvasSoftware)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/examples/hello_world")
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

    if (WHATSCANVAS_VULKAN_BACKEND_ENABLED)
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

        add_executable(WhatsCanvasVulkanImageFilterTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanImageFilterTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanImageFilterTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanImageFilterTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanImageFilterTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanImageFilterTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanImageFilterTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanImageFilterTests PROPERTIES
            LABELS "vulkan;image-filter"
            TIMEOUT 60
        )

        add_executable(WhatsCanvasVulkanFilterPixelParityTests
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanFilterPixelParityTests.cpp"
        )
        target_include_directories(WhatsCanvasVulkanFilterPixelParityTests PRIVATE
            "${SRC_DIR}"
        )
        target_link_libraries(WhatsCanvasVulkanFilterPixelParityTests PRIVATE
            WhatsCanvasOpenGL
            WhatsCanvasGLM
        )
        add_test(NAME WhatsCanvasVulkanFilterPixelParityTests
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanFilterPixelParityTests
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanFilterPixelParityTests>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
        )
        set_tests_properties(WhatsCanvasVulkanFilterPixelParityTests PROPERTIES
            LABELS "vulkan;image-filter;pixel-parity"
            TIMEOUT 60
        )

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
            WhatsCanvasVulkanImageFilterTests
            WhatsCanvasVulkanFilterPixelParityTests
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

    add_executable(WhatsCanvasDirectWriteBackendTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/DirectWriteBackendTests.cpp"
    )
    target_include_directories(WhatsCanvasDirectWriteBackendTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(WhatsCanvasDirectWriteBackendTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasDirectWriteBackendTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasDirectWriteBackendTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasDirectWriteBackendTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasDirectWriteBackendTests PROPERTIES LABELS "unit;text")

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

    add_executable(WhatsCanvasOpenGLMultiTextureBatchTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/OpenGLMultiTextureBatchTests.cpp"
    )
    target_include_directories(WhatsCanvasOpenGLMultiTextureBatchTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_link_libraries(WhatsCanvasOpenGLMultiTextureBatchTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    whatscanvas_link_gl_app(
        WhatsCanvasOpenGLMultiTextureBatchTests
        ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME WhatsCanvasOpenGLMultiTextureBatchTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasOpenGLMultiTextureBatchTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasOpenGLMultiTextureBatchTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasOpenGLMultiTextureBatchTests PROPERTIES
        LABELS "unit;render;opengl")

    add_executable(WhatsCanvasCommandDrawListEncoderTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/CommandDrawListEncoderTests.cpp"
    )
    target_include_directories(WhatsCanvasCommandDrawListEncoderTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasCommandDrawListEncoderTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasCommandDrawListEncoderTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasCommandDrawListEncoderTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasCommandDrawListEncoderTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasCommandDrawListEncoderTests PROPERTIES LABELS "unit;render")

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

    add_executable(WhatsCanvasTextPixelAlignmentTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/TextPixelAlignmentTests.cpp"
    )
    target_include_directories(WhatsCanvasTextPixelAlignmentTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasTextPixelAlignmentTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasTextPixelAlignmentTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasTextPixelAlignmentTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasTextPixelAlignmentTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasTextPixelAlignmentTests PROPERTIES LABELS "unit;text")

    add_executable(WhatsCanvasClearTypeCompositingTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/ClearTypeCompositingTests.cpp"
    )
    target_include_directories(WhatsCanvasClearTypeCompositingTests PRIVATE
        "${SRC_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_link_libraries(WhatsCanvasClearTypeCompositingTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    whatscanvas_link_gl_app(WhatsCanvasClearTypeCompositingTests ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME WhatsCanvasClearTypeCompositingTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasClearTypeCompositingTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasClearTypeCompositingTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasClearTypeCompositingTests PROPERTIES LABELS "unit;text;opengl" TIMEOUT 30)

    add_executable(WhatsCanvasOpenGLAAGeometryTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/OpenGLAAGeometryTests.cpp"
    )
    target_include_directories(WhatsCanvasOpenGLAAGeometryTests PRIVATE
        "${SRC_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_link_libraries(WhatsCanvasOpenGLAAGeometryTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    whatscanvas_link_gl_app(WhatsCanvasOpenGLAAGeometryTests ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME WhatsCanvasOpenGLAAGeometryTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasOpenGLAAGeometryTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasOpenGLAAGeometryTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasOpenGLAAGeometryTests
        PROPERTIES LABELS "unit;geometry;opengl;antialias" TIMEOUT 30)

    add_executable(WhatsCanvasOpenGLBackdropFilterTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/OpenGLBackdropFilterTests.cpp"
    )
    target_include_directories(WhatsCanvasOpenGLBackdropFilterTests PRIVATE
        "${SRC_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_link_libraries(WhatsCanvasOpenGLBackdropFilterTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    whatscanvas_link_gl_app(WhatsCanvasOpenGLBackdropFilterTests ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME WhatsCanvasOpenGLBackdropFilterTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasOpenGLBackdropFilterTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasOpenGLBackdropFilterTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasOpenGLBackdropFilterTests
        PROPERTIES LABELS "unit;image-filter;opengl" TIMEOUT 30)

    add_executable(WhatsCanvasOpenGLFilterPixelParityTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GLFilterPixelParityTests.cpp"
    )
    target_include_directories(WhatsCanvasOpenGLFilterPixelParityTests PRIVATE
        "${SRC_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_link_libraries(WhatsCanvasOpenGLFilterPixelParityTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    whatscanvas_link_gl_app(
        WhatsCanvasOpenGLFilterPixelParityTests ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME WhatsCanvasOpenGLFilterPixelParityTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasOpenGLFilterPixelParityTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasOpenGLFilterPixelParityTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasOpenGLFilterPixelParityTests PROPERTIES
        LABELS "image-filter;opengl;pixel-parity"
        TIMEOUT 60
    )

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

    add_executable(WhatsCanvasSoftwarePresentTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/SoftwarePresentTests.cpp"
    )
    target_include_directories(WhatsCanvasSoftwarePresentTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasSoftwarePresentTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasSoftwarePresentTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasSoftwarePresentTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasSoftwarePresentTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasSoftwarePresentTests PROPERTIES LABELS "unit;diagnostics")

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

    add_executable(WhatsCanvasMetalBackendSelectionTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalBackendSelectionTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalBackendSelectionTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalBackendSelectionTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalBackendSelectionTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalBackendSelectionTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalBackendSelectionTests PROPERTIES LABELS "unit;metal")

    add_executable(WhatsCanvasMetalCommandTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalCommandTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalCommandTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalCommandTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalCommandTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalCommandTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalCommandTests PROPERTIES LABELS "unit;metal" TIMEOUT 60)

    add_executable(WhatsCanvasMetalTextTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalTextTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalTextTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalTextTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalTextTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalTextTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalTextTests PROPERTIES LABELS "unit;metal;text" TIMEOUT 60)

    add_executable(WhatsCanvasMetalClipTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalClipTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalClipTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalClipTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalClipTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalClipTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalClipTests PROPERTIES LABELS "unit;metal;clip" TIMEOUT 60)

    add_executable(WhatsCanvasMetalFilterTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalFilterTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalFilterTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalFilterTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalFilterTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalFilterTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalFilterTests PROPERTIES LABELS "unit;metal;image-filter" TIMEOUT 60)

    add_executable(WhatsCanvasMetalImageEffectsTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalImageEffectsTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalImageEffectsTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalImageEffectsTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalImageEffectsTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalImageEffectsTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalImageEffectsTests PROPERTIES LABELS "unit;metal;image" TIMEOUT 60)

    add_executable(WhatsCanvasMetalBlendModeTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalBlendModeTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalBlendModeTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalBlendModeTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalBlendModeTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalBlendModeTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalBlendModeTests PROPERTIES LABELS "unit;metal;blend" TIMEOUT 60)

    add_executable(WhatsCanvasMetalGeometryTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalGeometryTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalGeometryTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalGeometryTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalGeometryTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalGeometryTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalGeometryTests PROPERTIES LABELS "unit;metal;geometry" TIMEOUT 60)

    add_executable(WhatsCanvasMetalLayerTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalLayerTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalLayerTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalLayerTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalLayerTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalLayerTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalLayerTests PROPERTIES LABELS "unit;metal;layer" TIMEOUT 60)

    add_executable(WhatsCanvasMetalGradientTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalGradientTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalGradientTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalGradientTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalGradientTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalGradientTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalGradientTests PROPERTIES LABELS "unit;metal;gradient" TIMEOUT 60)

    add_executable(WhatsCanvasMetalRenderTargetTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalRenderTargetTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalRenderTargetTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalRenderTargetTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalRenderTargetTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalRenderTargetTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalRenderTargetTests PROPERTIES LABELS "unit;metal;render-target" TIMEOUT 60)

    add_executable(WhatsCanvasMetalMipmapTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalMipmapTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalMipmapTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalMipmapTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalMipmapTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalMipmapTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalMipmapTests PROPERTIES LABELS "unit;metal;mipmap" TIMEOUT 60)

    add_executable(WhatsCanvasMetalGpuTimingTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalGpuTimingTests.cpp"
    )
    target_include_directories(WhatsCanvasMetalGpuTimingTests PRIVATE
        "${SRC_DIR}"
    )
    target_link_libraries(WhatsCanvasMetalGpuTimingTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalGpuTimingTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalGpuTimingTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalGpuTimingTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalGpuTimingTests PROPERTIES LABELS "unit;metal;performance" TIMEOUT 60)

    add_executable(WhatsCanvasMetalInteractionTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalInteractionTests.cpp"
    )
    target_link_libraries(WhatsCanvasMetalInteractionTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasMetalInteractionTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasMetalInteractionTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasMetalInteractionTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasMetalInteractionTests PROPERTIES LABELS "unit;metal;interaction" TIMEOUT 60)

    # Ad-hoc Metal-vs-OpenGL throughput smoke. Built as an executable rather
    # than a CTest gate so per-run timing noise never breaks the CI signal.
    add_executable(WhatsCanvasMetalPerformanceSmoke
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/MetalPerformanceSmoke.cpp"
    )
    target_link_libraries(WhatsCanvasMetalPerformanceSmoke PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )

    if (WHATSCANVAS_VULKAN_BACKEND_ENABLED)
        find_package(Vulkan QUIET)
        if (Vulkan_FOUND)
            add_executable(WhatsCanvasVulkanWrapExternalTests
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/VulkanWrapExternalTests.cpp"
            )
            target_include_directories(WhatsCanvasVulkanWrapExternalTests PRIVATE
                "${SRC_DIR}"
            )
            target_link_libraries(WhatsCanvasVulkanWrapExternalTests PRIVATE
                WhatsCanvasOpenGL
                WhatsCanvasGLM
                Vulkan::Vulkan
            )
            add_test(NAME WhatsCanvasVulkanWrapExternalTests
                COMMAND ${CMAKE_COMMAND}
                    -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
                    -DWHATSCANVAS_TEST_TARGET=WhatsCanvasVulkanWrapExternalTests
                    -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasVulkanWrapExternalTests>
                    -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                    -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
            )
            set_tests_properties(WhatsCanvasVulkanWrapExternalTests PROPERTIES LABELS "unit;vulkan")
        endif()
    endif()

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

    add_executable(WhatsCanvasBackendCreateTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/BackendCreateTests.cpp"
    )
    target_link_libraries(WhatsCanvasBackendCreateTests PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasGLM
    )
    add_test(NAME WhatsCanvasBackendCreateTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasBackendCreateTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasBackendCreateTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasBackendCreateTests PROPERTIES LABELS "unit;backend")

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

if (BUILD_TESTING AND TARGET WhatsCanvasOpenGLES)
    whatscanvas_add_glfw_dependency("${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(WhatsCanvasOpenGLESMultiTextureBatchTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/OpenGLMultiTextureBatchTests.cpp"
    )
    target_include_directories(WhatsCanvasOpenGLESMultiTextureBatchTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_compile_definitions(
        WhatsCanvasOpenGLESMultiTextureBatchTests PRIVATE
        WHATSCANVAS_TEST_OPENGLES
    )
    target_link_libraries(WhatsCanvasOpenGLESMultiTextureBatchTests PRIVATE
        WhatsCanvasOpenGLES
        glfw
    )
    add_test(NAME WhatsCanvasOpenGLESMultiTextureBatchTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasOpenGLESMultiTextureBatchTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasOpenGLESMultiTextureBatchTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasOpenGLESMultiTextureBatchTests PROPERTIES
        LABELS "unit;render;opengles")

    add_executable(WhatsCanvasOpenGLESFilterPixelParityTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GLFilterPixelParityTests.cpp"
    )
    target_include_directories(WhatsCanvasOpenGLESFilterPixelParityTests PRIVATE
        "${SRC_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_compile_definitions(WhatsCanvasOpenGLESFilterPixelParityTests PRIVATE
        WHATSCANVAS_PARITY_OPENGLES
    )
    target_link_libraries(WhatsCanvasOpenGLESFilterPixelParityTests PRIVATE
        WhatsCanvasOpenGLES
        glfw
    )
    add_test(NAME WhatsCanvasOpenGLESFilterPixelParityTests
        COMMAND ${CMAKE_COMMAND}
            -DWHATSCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DWHATSCANVAS_TEST_TARGET=WhatsCanvasOpenGLESFilterPixelParityTests
            -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasOpenGLESFilterPixelParityTests>
            -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(WhatsCanvasOpenGLESFilterPixelParityTests PROPERTIES
        LABELS "image-filter;opengles;pixel-parity"
        TIMEOUT 60
    )
endif()

if (BUILD_TESTING)
    add_executable(WhatsCanvasStrokeTessellatorTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/StrokeTessellatorTests.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/src/canvas/StrokeTessellator.cpp"
    )
    target_include_directories(WhatsCanvasStrokeTessellatorTests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
    )
    add_test(NAME WhatsCanvasStrokeTessellatorTests
        COMMAND $<TARGET_FILE:WhatsCanvasStrokeTessellatorTests>
    )
    set_tests_properties(WhatsCanvasStrokeTessellatorTests PROPERTIES LABELS "unit;geometry;compatibility")
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

    add_executable(WhatsCanvasImageFilterBenchmarks
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/ImageFilterBenchmarks.cpp"
    )
    target_link_libraries(WhatsCanvasImageFilterBenchmarks PRIVATE
        WhatsCanvasOpenGL
    )
    target_compile_definitions(WhatsCanvasImageFilterBenchmarks PRIVATE
        WHATSCANVAS_FILTER_BENCHMARK_ENABLE_OPENGL
    )
    if (WHATSCANVAS_VULKAN_BACKEND_ENABLED)
        target_compile_definitions(WhatsCanvasImageFilterBenchmarks PRIVATE
            WHATSCANVAS_FILTER_BENCHMARK_ENABLE_VULKAN
        )
    else()
        target_compile_definitions(WhatsCanvasImageFilterBenchmarks PRIVATE
            WHATSCANVAS_FILTER_BENCHMARK_DISABLE_VULKAN
        )
    endif()
    whatscanvas_link_gl_app(
        WhatsCanvasImageFilterBenchmarks ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(WhatsCanvasPerformanceSuite
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/PerformanceSuite.cpp"
    )
    target_link_libraries(WhatsCanvasPerformanceSuite PRIVATE
        WhatsCanvasOpenGL
    )
    target_compile_definitions(WhatsCanvasPerformanceSuite PRIVATE
        WHATSCANVAS_PERF_BUILD_TYPE="$<CONFIG>"
        WHATSCANVAS_PERF_VERSION="${PROJECT_VERSION}"
        WHATSCANVAS_PERF_CONTRACT_FONT="${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/perf/fonts/Roboto-Regular.ttf"
    )
    if (WHATSCANVAS_VULKAN_BACKEND_ENABLED)
        target_compile_definitions(WhatsCanvasPerformanceSuite PRIVATE
            WHATSCANVAS_PERF_ENABLE_VULKAN
        )
        target_link_libraries(WhatsCanvasPerformanceSuite PRIVATE
            Vulkan::Vulkan
        )
    endif()
    if (WIN32)
        target_link_libraries(WhatsCanvasPerformanceSuite PRIVATE psapi)
    endif()
    whatscanvas_link_gl_app(
        WhatsCanvasPerformanceSuite ${CMAKE_CURRENT_SOURCE_DIR})
elseif (WHATSCANVAS_BUILD_BENCHMARKS AND TARGET WhatsCanvasSoftware)
    add_executable(WhatsCanvasImageFilterBenchmarks
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/ImageFilterBenchmarks.cpp"
    )
    target_link_libraries(WhatsCanvasImageFilterBenchmarks PRIVATE
        WhatsCanvasSoftware
    )
    target_compile_definitions(WhatsCanvasImageFilterBenchmarks PRIVATE
        WHATSCANVAS_FILTER_BENCHMARK_DISABLE_OPENGL
        WHATSCANVAS_FILTER_BENCHMARK_DISABLE_VULKAN
    )

    add_executable(WhatsCanvasPerformanceSuite
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/PerformanceSuite.cpp"
    )
    target_link_libraries(WhatsCanvasPerformanceSuite PRIVATE
        WhatsCanvasSoftware
    )
    target_compile_definitions(WhatsCanvasPerformanceSuite PRIVATE
        WHATSCANVAS_PERF_DISABLE_OPENGL
        WHATSCANVAS_PERF_BUILD_TYPE="$<CONFIG>"
        WHATSCANVAS_PERF_VERSION="${PROJECT_VERSION}"
        WHATSCANVAS_PERF_CONTRACT_FONT="${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/perf/fonts/Roboto-Regular.ttf"
    )
    if (WIN32)
        target_link_libraries(WhatsCanvasPerformanceSuite PRIVATE psapi)
    endif()
endif()

if (BUILD_TESTING AND TARGET WhatsCanvasPerformanceSuite)
    add_test(NAME WhatsCanvasPerformanceSuiteSmoke
        COMMAND $<TARGET_FILE:WhatsCanvasPerformanceSuite>
            --backend software
            --profile quick
            --scene solid_rects,path_cached,geometry_stress,image_grid,text_stress
            --width 320
            --height 180
            --output "${CMAKE_CURRENT_BINARY_DIR}/performance_suite_smoke.jsonl"
    )
    set_tests_properties(WhatsCanvasPerformanceSuiteSmoke PROPERTIES
        LABELS "smoke;benchmark;software"
        TIMEOUT 120
    )
    if (Python3_Interpreter_FOUND)
        add_test(NAME WhatsCanvasPerformanceMatrixSmoke
            COMMAND ${Python3_EXECUTABLE}
                "${CMAKE_CURRENT_SOURCE_DIR}/scripts/run_benchmark_matrix.py"
                --executable $<TARGET_FILE:WhatsCanvasPerformanceSuite>
                --backend software
                --preset smoke
                --profile quick
                --width 320
                --height 180
                --output-dir
                    "${CMAKE_CURRENT_BINARY_DIR}/performance_matrix_smoke"
        )
        set_tests_properties(WhatsCanvasPerformanceMatrixSmoke PROPERTIES
            LABELS "smoke;benchmark;performance;software"
            TIMEOUT 180
        )
    endif()
endif()

if (WHATSCANVAS_BUILD_BENCHMARKS
    AND WHATSCANVAS_BUILD_NANOVG_BENCHMARK_ADAPTER
    AND TARGET WhatsCanvasOpenGL)
    if (NOT EXISTS "${WHATSCANVAS_NANOVG_SOURCE_DIR}/src/nanovg.c"
        OR NOT EXISTS "${WHATSCANVAS_NANOVG_SOURCE_DIR}/src/nanovg_gl.h")
        message(FATAL_ERROR
            "NanoVG benchmark adapter requested, but WHATSCANVAS_NANOVG_SOURCE_DIR "
            "does not contain a NanoVG checkout.")
    endif()
    execute_process(
        COMMAND git -C "${WHATSCANVAS_NANOVG_SOURCE_DIR}" rev-parse HEAD
        OUTPUT_VARIABLE WHATSCANVAS_NANOVG_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    if (NOT WHATSCANVAS_NANOVG_REVISION)
        set(WHATSCANVAS_NANOVG_REVISION "unknown")
    endif()
    whatscanvas_add_glfw_dependency("${CMAKE_CURRENT_SOURCE_DIR}")
    add_executable(WhatsCanvasNanoVGBenchmarkAdapter
        "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/adapters/NanoVGBenchmarkAdapter.cpp"
        "${WHATSCANVAS_NANOVG_SOURCE_DIR}/src/nanovg.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/src/glad.c"
    )
    target_include_directories(WhatsCanvasNanoVGBenchmarkAdapter PRIVATE
        "${WHATSCANVAS_NANOVG_SOURCE_DIR}/src"
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party/glad/include"
    )
    target_compile_definitions(WhatsCanvasNanoVGBenchmarkAdapter PRIVATE
        _CRT_SECURE_NO_WARNINGS
        WHATSCANVAS_NANOVG_VERSION="${WHATSCANVAS_NANOVG_REVISION}"
        WHATSCANVAS_NANOVG_BUILD_TYPE="$<CONFIG>"
        WHATSCANVAS_NANOVG_CONTRACT_FONT="${CMAKE_CURRENT_SOURCE_DIR}/third_party/harfbuzz/perf/fonts/Roboto-Regular.ttf"
    )
    target_link_libraries(WhatsCanvasNanoVGBenchmarkAdapter PRIVATE
        glfw
        OpenGL::GL
    )
    if (BUILD_TESTING)
        add_test(NAME WhatsCanvasNanoVGBenchmarkAdapterSmoke
            COMMAND $<TARGET_FILE:WhatsCanvasNanoVGBenchmarkAdapter>
                --profile quick
                --scene geometry_stress
                --width 320
                --height 180
                --output "${CMAKE_CURRENT_BINARY_DIR}/nanovg_adapter_smoke.jsonl"
                --capture-dir "${CMAKE_CURRENT_BINARY_DIR}/nanovg_adapter_captures"
        )
        set_tests_properties(WhatsCanvasNanoVGBenchmarkAdapterSmoke PROPERTIES
            LABELS "smoke;benchmark;adapter;nanovg;opengl"
            TIMEOUT 120
        )
    endif()
endif()

if (BUILD_TESTING AND Python3_Interpreter_FOUND)
    add_test(NAME WhatsCanvasCrossLibraryBenchmarkTests
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/CrossLibraryBenchmarkTests.py"
    )
    set_tests_properties(WhatsCanvasCrossLibraryBenchmarkTests PROPERTIES
        LABELS "unit;benchmark;adapter"
        TIMEOUT 30
    )
    add_test(NAME WhatsCanvasPerformanceMatrixTests
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/PerformanceMatrixTests.py"
    )
    set_tests_properties(WhatsCanvasPerformanceMatrixTests PROPERTIES
        LABELS "unit;benchmark;performance"
        TIMEOUT 30
    )
    add_test(NAME WhatsCanvasCrossLibraryMatrixTests
        COMMAND ${Python3_EXECUTABLE}
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/CrossLibraryMatrixTests.py"
    )
    set_tests_properties(WhatsCanvasCrossLibraryMatrixTests PROPERTIES
        LABELS "unit;benchmark;adapter;performance"
        TIMEOUT 30
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
        add_test(NAME WhatsCanvasSoftwarePackageConsumerSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_PACKAGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_PACKAGE_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/software-package-consumer-smoke
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifySoftwarePackageConsumer.cmake
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
        add_test(NAME WhatsCanvasSoftwarePackageConsumerSmoke
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_PACKAGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DWHATSCANVAS_PACKAGE_WORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/software-package-consumer-smoke
                -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/VerifySoftwarePackageConsumer.cmake
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
        WhatsCanvasSoftwarePackageConsumerSmoke
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 1800
    )
    # These script gates configure/build from the same source checkout and
    # share its submodule metadata plus several build/package directories.
    # CTest may otherwise launch them concurrently, racing on Git's submodule
    # config lock or partially rewriting generated package files.
    set_tests_properties(
        WhatsCanvasSmoke
        WhatsCanvasClipPathSmoke
        WhatsCanvasValidationSceneSmoke
        WhatsCanvasPackageConsumerSmoke
        PROPERTIES
            RESOURCE_LOCK "whatscanvas_root_build"
    )
    if (WHATSCANVAS_ENABLE_LOCAL_REGRESSION_TEST)
        set_tests_properties(
            WhatsCanvasRegressionSmoke
            WhatsCanvasTextPixelRegression
            PROPERTIES
                RESOURCE_LOCK "whatscanvas_root_build"
        )
    endif()
    set_tests_properties(WhatsCanvasSmoke PROPERTIES LABELS "smoke")
    set_tests_properties(WhatsCanvasClipPathSmoke PROPERTIES LABELS "smoke;clip")
    set_tests_properties(WhatsCanvasExamplesSmoke PROPERTIES LABELS "smoke;examples")
    set_tests_properties(WhatsCanvasValidationSceneSmoke PROPERTIES LABELS "smoke;validation")
    set_tests_properties(WhatsCanvasOpenGLESSmoke PROPERTIES LABELS "smoke;opengles")
    set_tests_properties(WhatsCanvasApiReferenceCheck PROPERTIES LABELS "smoke;api;docs")
    set_tests_properties(WhatsCanvasVersionConsistencyCheck PROPERTIES LABELS "smoke;version;package")
    set_tests_properties(WhatsCanvasPackageConsumerSmoke PROPERTIES LABELS "smoke;package")
    set_tests_properties(WhatsCanvasSoftwarePackageConsumerSmoke PROPERTIES LABELS "smoke;package;software")
endif()

if (WHATSCANVAS_INSTALL)
    set(WHATSCANVAS_OPENGL_PACKAGE_IMPLIB "")
    set(WHATSCANVAS_PACKAGE_HAS_OPENGL OFF)
    set(WHATSCANVAS_PACKAGE_HAS_OPENGLES OFF)
    set(WHATSCANVAS_PACKAGE_HAS_SOFTWARE OFF)
    set(WHATSCANVAS_PACKAGE_REQUIRES_OPENGL OFF)
    set(WHATSCANVAS_PACKAGE_REQUIRES_GLAD OFF)
    set(WHATSCANVAS_PACKAGE_REQUIRES_X11 OFF)
    set(WHATSCANVAS_PACKAGE_REQUIRES_FREETYPE OFF)
    set(WHATSCANVAS_PACKAGE_REQUIRES_HARFBUZZ OFF)

    if (TARGET WhatsCanvasOpenGL)
        set(WHATSCANVAS_PACKAGE_HAS_OPENGL ON)
        get_target_property(WHATSCANVAS_OPENGL_OUTPUT_NAME WhatsCanvasOpenGL OUTPUT_NAME)
        if (NOT WHATSCANVAS_OPENGL_OUTPUT_NAME)
            set(WHATSCANVAS_OPENGL_OUTPUT_NAME "WhatsCanvasOpenGL")
        endif()

        set(WHATSCANVAS_OPENGL_IMPORTED_TYPE STATIC)
        set(WHATSCANVAS_OPENGL_PACKAGE_LOCATION "${CMAKE_INSTALL_LIBDIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${WHATSCANVAS_OPENGL_OUTPUT_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")
        set(WHATSCANVAS_PACKAGE_REQUIRES_OPENGL ON)
        if (WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
            set(WHATSCANVAS_PACKAGE_REQUIRES_GLAD ON)
        endif()

        if (WIN32 AND BUILD_SHARED_LIBS)
            set(WHATSCANVAS_OPENGL_IMPORTED_TYPE SHARED)
            set(WHATSCANVAS_OPENGL_PACKAGE_LOCATION "${CMAKE_INSTALL_BINDIR}/${WHATSCANVAS_OPENGL_OUTPUT_NAME}${CMAKE_SHARED_LIBRARY_SUFFIX}")
            set(WHATSCANVAS_OPENGL_PACKAGE_IMPLIB "${CMAKE_INSTALL_LIBDIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}${WHATSCANVAS_OPENGL_OUTPUT_NAME}${CMAKE_IMPORT_LIBRARY_SUFFIX}")
        endif()
    endif()

    if (TARGET WhatsCanvasOpenGLES)
        set(WHATSCANVAS_PACKAGE_HAS_OPENGLES ON)
        if (WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
            set(WHATSCANVAS_PACKAGE_REQUIRES_GLAD ON)
        endif()
    endif()
    if (TARGET WhatsCanvasSoftware)
        set(WHATSCANVAS_PACKAGE_HAS_SOFTWARE ON)
    endif()

    foreach(_whatscanvas_target IN ITEMS WhatsCanvasOpenGL WhatsCanvasOpenGLES WhatsCanvasSoftware)
        if (TARGET ${_whatscanvas_target})
            get_target_property(_whatscanvas_target_requires_x11 ${_whatscanvas_target} WHATSCANVAS_REQUIRES_X11)
            if (_whatscanvas_target_requires_x11)
                set(WHATSCANVAS_PACKAGE_REQUIRES_X11 ON)
            endif()
        endif()
    endforeach()

    # Advertise text dependencies when their targets remain in the installed
    # static library's link interface. Bundled builds install matching configs;
    # package-manager builds resolve the dependencies from the active registry.
    if ((TARGET WhatsCanvasOpenGL OR TARGET WhatsCanvasOpenGLES)
        AND (WHATSCANVAS_INSTALL_TEXT_DEPENDENCIES OR WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
        AND WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER
        AND TARGET Freetype::Freetype)
        set(WHATSCANVAS_PACKAGE_REQUIRES_FREETYPE ON)
    endif()
    if ((TARGET WhatsCanvasOpenGL OR TARGET WhatsCanvasOpenGLES)
        AND (WHATSCANVAS_INSTALL_TEXT_DEPENDENCIES OR WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
        AND WHATSCANVAS_ENABLE_OPENTYPE_SHAPING
        AND (TARGET harfbuzz::harfbuzz OR TARGET HarfBuzz::HarfBuzz))
        set(WHATSCANVAS_PACKAGE_REQUIRES_HARFBUZZ ON)
    endif()

    # Export each renderer independently so COMPONENTS Software can import the
    # CPU target without creating OpenGL or text-stack targets as side effects.
    foreach(_whatscanvas_renderer IN ITEMS OpenGL OpenGLES Software)
        if (TARGET WhatsCanvas${_whatscanvas_renderer})
            install(TARGETS WhatsCanvas${_whatscanvas_renderer}
                EXPORT WhatsCanvas${_whatscanvas_renderer}Targets
                RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            )
            install(EXPORT WhatsCanvas${_whatscanvas_renderer}Targets
                FILE WhatsCanvas${_whatscanvas_renderer}Targets.cmake
                NAMESPACE WhatsCanvas::
                DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/WhatsCanvas"
            )
        endif()
    endforeach()

    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/wsc"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/WhatsCanvasConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/WhatsCanvasConfig.cmake"
        INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/WhatsCanvas"
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
endif()
