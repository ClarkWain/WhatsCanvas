foreach(_required_variable IN ITEMS
        WHATSCANVAS_TEST_BUILD_DIR
        WHATSCANVAS_TEST_TARGET
        WHATSCANVAS_TEST_EXECUTABLE
        WHATSCANVAS_PYTHON_EXECUTABLE
        WHATSCANVAS_ORACLE_RUNNER
        WHATSCANVAS_ORACLE_FIXTURE
        WHATSCANVAS_ORACLE_GOLDEN)
    if (NOT DEFINED ${_required_variable} OR "${${_required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${_required_variable} is required.")
    endif()
endforeach()

set(_build_command
    "${CMAKE_COMMAND}" --build "${WHATSCANVAS_TEST_BUILD_DIR}"
    --target "${WHATSCANVAS_TEST_TARGET}"
)
if (DEFINED WHATSCANVAS_TEST_CONFIG AND NOT WHATSCANVAS_TEST_CONFIG STREQUAL "")
    list(APPEND _build_command --config "${WHATSCANVAS_TEST_CONFIG}")
endif()
execute_process(
    COMMAND ${_build_command}
    RESULT_VARIABLE _build_result
    OUTPUT_VARIABLE _build_output
    ERROR_VARIABLE _build_error
)
if (NOT _build_output STREQUAL "")
    message("${_build_output}")
endif()
if (NOT _build_error STREQUAL "")
    message("${_build_error}")
endif()
if (NOT _build_result EQUAL 0)
    message(FATAL_ERROR "Building ${WHATSCANVAS_TEST_TARGET} failed with exit code ${_build_result}.")
endif()

set(_oracle_command
    "${WHATSCANVAS_PYTHON_EXECUTABLE}" "${WHATSCANVAS_ORACLE_RUNNER}"
    --whatscanvas "${WHATSCANVAS_TEST_EXECUTABLE}"
    --golden "${WHATSCANVAS_ORACLE_GOLDEN}"
    --output-dir "${WHATSCANVAS_TEST_BUILD_DIR}/android-font-oracle"
)
if (DEFINED WHATSCANVAS_SKIA_ORACLE_EXECUTABLE
        AND NOT WHATSCANVAS_SKIA_ORACLE_EXECUTABLE STREQUAL "")
    list(APPEND _oracle_command
        --skia "${WHATSCANVAS_SKIA_ORACLE_EXECUTABLE}" --require-skia)
endif()
list(APPEND _oracle_command
    --
    --config "${WHATSCANVAS_ORACLE_FIXTURE}" "/oracle-fonts"
    --query "sans-serif|450|normal|ja|default"
    --query "sans-serif-medium|400|normal||default"
    --query "system-ui|600|italic|zh-TW|default"
    --query "sans-serif|400|normal||emoji"
)
execute_process(
    COMMAND ${_oracle_command}
    WORKING_DIRECTORY "${WHATSCANVAS_TEST_BUILD_DIR}"
    RESULT_VARIABLE _oracle_result
    OUTPUT_VARIABLE _oracle_output
    ERROR_VARIABLE _oracle_error
)
if (NOT _oracle_output STREQUAL "")
    message("${_oracle_output}")
endif()
if (NOT _oracle_error STREQUAL "")
    message("${_oracle_error}")
endif()
if (NOT _oracle_result EQUAL 0)
    message(FATAL_ERROR "Android font oracle verification failed with exit code ${_oracle_result}.")
endif()
