if (NOT DEFINED PRISMCANVAS_TEST_BUILD_DIR OR PRISMCANVAS_TEST_BUILD_DIR STREQUAL "")
    message(FATAL_ERROR "PRISMCANVAS_TEST_BUILD_DIR is required.")
endif()

if (NOT DEFINED PRISMCANVAS_TEST_TARGET OR PRISMCANVAS_TEST_TARGET STREQUAL "")
    message(FATAL_ERROR "PRISMCANVAS_TEST_TARGET is required.")
endif()

if (NOT DEFINED PRISMCANVAS_TEST_EXECUTABLE OR PRISMCANVAS_TEST_EXECUTABLE STREQUAL "")
    message(FATAL_ERROR "PRISMCANVAS_TEST_EXECUTABLE is required.")
endif()

set(_prismcanvas_test_build_command
    "${CMAKE_COMMAND}" --build "${PRISMCANVAS_TEST_BUILD_DIR}" --target "${PRISMCANVAS_TEST_TARGET}"
)
if (DEFINED PRISMCANVAS_TEST_CONFIG AND NOT PRISMCANVAS_TEST_CONFIG STREQUAL "")
    list(APPEND _prismcanvas_test_build_command --config "${PRISMCANVAS_TEST_CONFIG}")
endif()

execute_process(
    COMMAND ${_prismcanvas_test_build_command}
    RESULT_VARIABLE _prismcanvas_test_build_result
    OUTPUT_VARIABLE _prismcanvas_test_build_output
    ERROR_VARIABLE _prismcanvas_test_build_error
)

if (NOT _prismcanvas_test_build_output STREQUAL "")
    message("${_prismcanvas_test_build_output}")
endif()

if (NOT _prismcanvas_test_build_error STREQUAL "")
    message("${_prismcanvas_test_build_error}")
endif()

if (NOT _prismcanvas_test_build_result EQUAL 0)
    message(FATAL_ERROR "Building ${PRISMCANVAS_TEST_TARGET} failed with exit code ${_prismcanvas_test_build_result}.")
endif()

set(_prismcanvas_test_command "${PRISMCANVAS_TEST_EXECUTABLE}")
if (DEFINED PRISMCANVAS_TEST_ARGS AND NOT PRISMCANVAS_TEST_ARGS STREQUAL "")
    list(APPEND _prismcanvas_test_command ${PRISMCANVAS_TEST_ARGS})
endif()

execute_process(
    COMMAND ${_prismcanvas_test_command}
    WORKING_DIRECTORY "${PRISMCANVAS_TEST_BUILD_DIR}"
    RESULT_VARIABLE _prismcanvas_test_run_result
    OUTPUT_VARIABLE _prismcanvas_test_run_output
    ERROR_VARIABLE _prismcanvas_test_run_error
)

if (NOT _prismcanvas_test_run_output STREQUAL "")
    message("${_prismcanvas_test_run_output}")
endif()

if (NOT _prismcanvas_test_run_error STREQUAL "")
    message("${_prismcanvas_test_run_error}")
endif()

if (NOT _prismcanvas_test_run_result EQUAL 0)
    message(FATAL_ERROR "Running ${PRISMCANVAS_TEST_TARGET} failed with exit code ${_prismcanvas_test_run_result}.")
endif()