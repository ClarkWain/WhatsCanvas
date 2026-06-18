if (NOT DEFINED WHATSCANVAS_TEST_BUILD_DIR OR WHATSCANVAS_TEST_BUILD_DIR STREQUAL "")
    message(FATAL_ERROR "WHATSCANVAS_TEST_BUILD_DIR is required.")
endif()

if (NOT DEFINED WHATSCANVAS_TEST_TARGET OR WHATSCANVAS_TEST_TARGET STREQUAL "")
    message(FATAL_ERROR "WHATSCANVAS_TEST_TARGET is required.")
endif()

if (NOT DEFINED WHATSCANVAS_TEST_EXECUTABLE OR WHATSCANVAS_TEST_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "WHATSCANVAS_TEST_EXECUTABLE is required.")
endif()

set(_whatscanvas_test_build_command
    "${CMAKE_COMMAND}" --build "${WHATSCANVAS_TEST_BUILD_DIR}" --target "${WHATSCANVAS_TEST_TARGET}"
)
if (DEFINED WHATSCANVAS_TEST_CONFIG AND NOT WHATSCANVAS_TEST_CONFIG STREQUAL "")
    list(APPEND _whatscanvas_test_build_command --config "${WHATSCANVAS_TEST_CONFIG}")
endif()

execute_process(
    COMMAND ${_whatscanvas_test_build_command}
    RESULT_VARIABLE _whatscanvas_test_build_result
    OUTPUT_VARIABLE _whatscanvas_test_build_output
    ERROR_VARIABLE _whatscanvas_test_build_error
)

if (NOT _whatscanvas_test_build_output STREQUAL "")
    message("${_whatscanvas_test_build_output}")
endif()

if (NOT _whatscanvas_test_build_error STREQUAL "")
    message("${_whatscanvas_test_build_error}")
endif()

if (NOT _whatscanvas_test_build_result EQUAL 0)
    message(FATAL_ERROR "Building ${WHATSCANVAS_TEST_TARGET} failed with exit code ${_whatscanvas_test_build_result}.")
endif()

set(_whatscanvas_test_command "${WHATSCANVAS_TEST_EXECUTABLE}")
if (DEFINED WHATSCANVAS_TEST_ARGS AND NOT WHATSCANVAS_TEST_ARGS STREQUAL "")
    list(APPEND _whatscanvas_test_command ${WHATSCANVAS_TEST_ARGS})
endif()

execute_process(
    COMMAND ${_whatscanvas_test_command}
    WORKING_DIRECTORY "${WHATSCANVAS_TEST_BUILD_DIR}"
    RESULT_VARIABLE _whatscanvas_test_run_result
    OUTPUT_VARIABLE _whatscanvas_test_run_output
    ERROR_VARIABLE _whatscanvas_test_run_error
)

if (NOT _whatscanvas_test_run_output STREQUAL "")
    message("${_whatscanvas_test_run_output}")
endif()

if (NOT _whatscanvas_test_run_error STREQUAL "")
    message("${_whatscanvas_test_run_error}")
endif()

if (NOT _whatscanvas_test_run_result EQUAL 0)
    message(FATAL_ERROR "Running ${WHATSCANVAS_TEST_TARGET} failed with exit code ${_whatscanvas_test_run_result}.")
endif()