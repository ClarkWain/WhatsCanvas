foreach(_required_variable IN ITEMS
        WHATSCANVAS_TEST_BUILD_DIR
        WHATSCANVAS_TEST_TARGET
        WHATSCANVAS_TEST_EXECUTABLE
        WHATSCANVAS_PYTHON_EXECUTABLE
        WHATSCANVAS_ORACLE_CORPUS_RUNNER
        WHATSCANVAS_ORACLE_CORPUS_MANIFEST
        WHATSCANVAS_SKIA_ORACLE_EXECUTABLE)
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
    COMMAND "${WHATSCANVAS_PYTHON_EXECUTABLE}"
        "${WHATSCANVAS_ORACLE_CORPUS_RUNNER}"
        --whatscanvas "${WHATSCANVAS_TEST_EXECUTABLE}"
        --skia "${WHATSCANVAS_SKIA_ORACLE_EXECUTABLE}"
        --manifest "${WHATSCANVAS_ORACLE_CORPUS_MANIFEST}"
        --output-dir "${WHATSCANVAS_TEST_BUILD_DIR}/android-font-oracle/corpus"
    RESULT_VARIABLE _oracle_result
    OUTPUT_VARIABLE _oracle_output
    ERROR_VARIABLE _oracle_error)
if (NOT _oracle_output STREQUAL "")
    message("${_oracle_output}")
endif()
if (NOT _oracle_error STREQUAL "")
    message("${_oracle_error}")
endif()
if (NOT _oracle_result EQUAL 0)
    message(FATAL_ERROR "Android font oracle corpus verification failed with exit code ${_oracle_result}.")
endif()
