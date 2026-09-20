cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasParityProbe LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
    add_compile_options(/wd4819)
endif()

# Requires both backends and a GLFW dependency (already used by the tree).
if (NOT TARGET WhatsCanvasOpenGL OR NOT TARGET WhatsCanvasSoftware)
    return()
endif()

add_executable(WhatsCanvasParityProbe "main.cpp")

foreach(config Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${config} config_upper)
    set_target_properties(WhatsCanvasParityProbe PROPERTIES
        "RUNTIME_OUTPUT_DIRECTORY_${config_upper}" "${CMAKE_BINARY_DIR}/${config}"
    )
endforeach()

target_link_libraries(WhatsCanvasParityProbe
    PRIVATE
        WhatsCanvasOpenGL
        WhatsCanvasSoftware
)

# Reuse the same helper the other GL-linked demos use to bring in GLFW etc.
whatscanvas_link_gl_app(WhatsCanvasParityProbe "${CMAKE_CURRENT_LIST_DIR}/../..")

# Register as a diagnostic-only CTest. It is NOT run by the default
# `ctest` invocation — pass `-L parity-probe` (or the broader `diagnostic`
# label) to opt in. The probe surfaces cross-backend drift for developer
# review and never gates CI; a failure only means the probe binary could
# not run against a real GL context.
if (BUILD_TESTING)
    add_test(NAME WhatsCanvasParityProbe
        COMMAND $<TARGET_FILE:WhatsCanvasParityProbe>
            --out ${CMAKE_BINARY_DIR}/parity_probe_out
            --size 256
    )
    set_tests_properties(WhatsCanvasParityProbe PROPERTIES
        LABELS "diagnostic;parity-probe;opengl"
        TIMEOUT 120
    )
endif()
