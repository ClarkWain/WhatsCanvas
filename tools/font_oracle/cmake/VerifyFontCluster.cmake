foreach(_required IN ITEMS
        WHATSCANVAS_TEST_BUILD_DIR
        WHATSCANVAS_TEST_TARGET
        WHATSCANVAS_TEST_EXECUTABLE
        WHATSCANVAS_TEST_FONT
        WHATSCANVAS_TEST_CODEPOINTS)
    if (NOT DEFINED ${_required} OR "${${_required}}" STREQUAL "")
        message(FATAL_ERROR "${_required} is required")
    endif()
endforeach()

set(_build_command
    "${CMAKE_COMMAND}" --build "${WHATSCANVAS_TEST_BUILD_DIR}"
    --target "${WHATSCANVAS_TEST_TARGET}")
if (DEFINED WHATSCANVAS_TEST_CONFIG AND NOT WHATSCANVAS_TEST_CONFIG STREQUAL "")
    list(APPEND _build_command --config "${WHATSCANVAS_TEST_CONFIG}")
endif()
execute_process(
    COMMAND ${_build_command}
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE _build_error)
if (NOT _build_result EQUAL 0)
    message(FATAL_ERROR
        "Building cluster probe failed (${_build_result})\n${_build_output}\n${_build_error}")
endif()

execute_process(
    COMMAND "${WHATSCANVAS_TEST_EXECUTABLE}"
        "${WHATSCANVAS_TEST_FONT}" "${WHATSCANVAS_TEST_CODEPOINTS}"
    RESULT_VARIABLE _probe_result
    OUTPUT_VARIABLE _probe_output
    ERROR_VARIABLE _probe_error)
if (NOT _probe_result EQUAL 0)
    message(FATAL_ERROR
        "Cluster probe failed (${_probe_result})\n${_probe_output}\n${_probe_error}")
endif()
if (NOT _probe_output MATCHES "shaped=1 glyphs=1")
    message(FATAL_ERROR "Cluster did not shape to one glyph\n${_probe_output}")
endif()
if (NOT _probe_output MATCHES "shapedGlyph=[0-9]+ source=U\\+1F1E8 raster=1")
    message(FATAL_ERROR "Shaped flag glyph did not rasterize\n${_probe_output}")
endif()
message("${_probe_output}")
