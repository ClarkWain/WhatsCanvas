foreach(_required_variable IN ITEMS
        WHATSCANVAS_TEST_BUILD_DIR
        WHATSCANVAS_TEST_TARGET
        WHATSCANVAS_TEST_EXECUTABLE
        WHATSCANVAS_PYTHON_EXECUTABLE
        WHATSCANVAS_RASTER_RUNNER
        WHATSCANVAS_LATIN_FONT
        WHATSCANVAS_CJK_FONT
        WHATSCANVAS_COLR_EMOJI_FONT
        WHATSCANVAS_BITMAP_EMOJI_FONT
        WHATSCANVAS_RASTER_GOLDEN
        WHATSCANVAS_RASTER_OUTPUT)
    if (NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${_required_variable} is required.")
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
if (NOT _build_output STREQUAL "")
    message("${_build_output}")
endif()
if (NOT _build_error STREQUAL "")
    message("${_build_error}")
endif()
if (NOT _build_result EQUAL 0)
    message(FATAL_ERROR "Building ${WHATSCANVAS_TEST_TARGET} failed with exit code ${_build_result}.")
endif()

execute_process(
    COMMAND "${WHATSCANVAS_PYTHON_EXECUTABLE}" "${WHATSCANVAS_RASTER_RUNNER}"
        --probe "${WHATSCANVAS_TEST_EXECUTABLE}"
        --latin-font "${WHATSCANVAS_LATIN_FONT}"
        --cjk-font "${WHATSCANVAS_CJK_FONT}"
        --colr-emoji-font "${WHATSCANVAS_COLR_EMOJI_FONT}"
        --bitmap-emoji-font "${WHATSCANVAS_BITMAP_EMOJI_FONT}"
        --golden "${WHATSCANVAS_RASTER_GOLDEN}"
        --output "${WHATSCANVAS_RASTER_OUTPUT}"
    RESULT_VARIABLE _raster_result
    OUTPUT_VARIABLE _raster_output
    ERROR_VARIABLE _raster_error)
if (NOT _raster_output STREQUAL "")
    message("${_raster_output}")
endif()
if (NOT _raster_error STREQUAL "")
    message("${_raster_error}")
endif()
if (NOT _raster_result EQUAL 0)
    message(FATAL_ERROR "WhatsCanvas font raster verification failed with exit code ${_raster_result}.")
endif()
