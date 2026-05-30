cmake_minimum_required(VERSION 3.16)

project(PrismCanvas LANGUAGES C CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(PRISMCANVAS_ENABLE_SCRIPT_TESTS "Register existing smoke/example script gates with CTest." ON)
option(PRISMCANVAS_ENABLE_LOCAL_REGRESSION_TEST "Register the strict local pixel-hash regression gate with CTest." OFF)
set(PRISMCANVAS_DEFAULT_SMOKE_HASH "2458027664413625913" CACHE STRING "Default local smoke hash for regression ctest registration.")
set(PRISMCANVAS_CLIP_SMOKE_HASH "12248791335057056593" CACHE STRING "Default local clip-path smoke hash for regression ctest registration.")

if (MSVC)
    # 全局禁用警告 C4819
    add_compile_options(/wd4819)
endif()

set(THIRD_PARTY_DIR ${CMAKE_CURRENT_LIST_DIR}/third_party)
set(SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)

set(GLFW_PATH ${THIRD_PARTY_DIR}/glfw)
set(GLAD_PATH ${THIRD_PARTY_DIR}/glad)
set(STB_PATH ${THIRD_PARTY_DIR}/stb)
set(GLM_PATH ${THIRD_PARTY_DIR}/glm)
set(POLYLINE2D_PATH ${THIRD_PARTY_DIR}/polyline2d)
set(PRISMCANVAS_CMAKE_DIR ${CMAKE_CURRENT_LIST_DIR}/cmake)

include(${PRISMCANVAS_CMAKE_DIR}/PrismCanvasOpenGL.cmake)

prismcanvas_add_common_dependencies(${CMAKE_CURRENT_LIST_DIR})
prismcanvas_add_opengl_library(PrismCanvasOpenGL ${CMAKE_CURRENT_LIST_DIR})

add_library(PrismCanvas::OpenGL ALIAS PrismCanvasOpenGL)

add_executable(PrismCanvasDemo
    "${SRC_DIR}/main.cpp"
    src/render/OpenGLRenderDevice.h
)

target_link_libraries(PrismCanvasDemo
    PRIVATE
        PrismCanvas::OpenGL
)

prismcanvas_link_gl_app(PrismCanvasDemo)

if (BUILD_TESTING)
    add_executable(PrismCanvasGraphicsStateStackTests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/GraphicsStateStackTests.cpp"
    )
    target_include_directories(PrismCanvasGraphicsStateStackTests PRIVATE
        "${SRC_DIR}"
        "${SRC_DIR}/canvas"
    )
    target_link_libraries(PrismCanvasGraphicsStateStackTests PRIVATE PrismCanvasGLM)
    add_test(NAME PrismCanvasGraphicsStateStackTests
        COMMAND ${CMAKE_COMMAND}
            -DPRISMCANVAS_TEST_BUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
            -DPRISMCANVAS_TEST_TARGET=PrismCanvasGraphicsStateStackTests
            -DPRISMCANVAS_TEST_EXECUTABLE=${CMAKE_CURRENT_BINARY_DIR}/Debug/PrismCanvasGraphicsStateStackTests.exe
            -DPRISMCANVAS_TEST_CONFIG=$<CONFIG>
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunBuiltExecutableTest.cmake
    )
    set_tests_properties(PrismCanvasGraphicsStateStackTests PROPERTIES LABELS "unit")
endif()

if (BUILD_TESTING AND PRISMCANVAS_ENABLE_SCRIPT_TESTS)
    set(PRISMCANVAS_TEST_WRAPPER "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RunScriptGate.cmake")
    if (WIN32)
        add_test(NAME PrismCanvasSmoke
            COMMAND ${CMAKE_COMMAND}
                -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.bat
                -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${PRISMCANVAS_TEST_WRAPPER}
        )
        add_test(NAME PrismCanvasClipPathSmoke
            COMMAND ${CMAKE_COMMAND}
                -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/clip_path_smoke.bat
                -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${PRISMCANVAS_TEST_WRAPPER}
        )
        add_test(NAME PrismCanvasExamplesSmoke
            COMMAND ${CMAKE_COMMAND}
                -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/examples_smoke.bat
                -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -P ${PRISMCANVAS_TEST_WRAPPER}
        )

        if (PRISMCANVAS_ENABLE_LOCAL_REGRESSION_TEST)
            add_test(NAME PrismCanvasRegressionSmoke
                COMMAND ${CMAKE_COMMAND}
                    -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/regression_smoke.bat
                    -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -DPRISMCANVAS_GATE_ARGS=${PRISMCANVAS_DEFAULT_SMOKE_HASH};${PRISMCANVAS_CLIP_SMOKE_HASH}
                    -P ${PRISMCANVAS_TEST_WRAPPER}
            )
            set_tests_properties(PrismCanvasRegressionSmoke PROPERTIES LABELS "smoke;regression;local")
            set_tests_properties(PrismCanvasRegressionSmoke PROPERTIES TIMEOUT 1800)
        endif()
    else()
        add_test(NAME PrismCanvasSmoke
            COMMAND ${CMAKE_COMMAND}
                -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/smoke_test.sh
                -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DPRISMCANVAS_GATE_LAUNCHER=sh
                -P ${PRISMCANVAS_TEST_WRAPPER}
        )
        add_test(NAME PrismCanvasClipPathSmoke
            COMMAND ${CMAKE_COMMAND}
                -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/clip_path_smoke.sh
                -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DPRISMCANVAS_GATE_LAUNCHER=sh
                -P ${PRISMCANVAS_TEST_WRAPPER}
        )
        add_test(NAME PrismCanvasExamplesSmoke
            COMMAND ${CMAKE_COMMAND}
                -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/examples_smoke.sh
                -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                -DPRISMCANVAS_GATE_LAUNCHER=sh
                -P ${PRISMCANVAS_TEST_WRAPPER}
        )

        if (PRISMCANVAS_ENABLE_LOCAL_REGRESSION_TEST)
            add_test(NAME PrismCanvasRegressionSmoke
                COMMAND ${CMAKE_COMMAND}
                    -DPRISMCANVAS_GATE_SCRIPT=${CMAKE_CURRENT_SOURCE_DIR}/regression_smoke.sh
                    -DPRISMCANVAS_GATE_WORKDIR=${CMAKE_CURRENT_SOURCE_DIR}
                    -DPRISMCANVAS_GATE_LAUNCHER=sh
                    -DPRISMCANVAS_GATE_ARGS=${PRISMCANVAS_DEFAULT_SMOKE_HASH};${PRISMCANVAS_CLIP_SMOKE_HASH}
                    -P ${PRISMCANVAS_TEST_WRAPPER}
            )
            set_tests_properties(PrismCanvasRegressionSmoke PROPERTIES LABELS "smoke;regression;local")
            set_tests_properties(PrismCanvasRegressionSmoke PROPERTIES TIMEOUT 1800)
        endif()
    endif()

    set_tests_properties(
        PrismCanvasSmoke
        PrismCanvasClipPathSmoke
        PrismCanvasExamplesSmoke
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            TIMEOUT 1800
    )
    set_tests_properties(PrismCanvasSmoke PROPERTIES LABELS "smoke")
    set_tests_properties(PrismCanvasClipPathSmoke PROPERTIES LABELS "smoke;clip")
    set_tests_properties(PrismCanvasExamplesSmoke PROPERTIES LABELS "smoke;examples")
endif()

