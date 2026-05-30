if (NOT DEFINED PRISMCANVAS_GATE_SCRIPT OR PRISMCANVAS_GATE_SCRIPT STREQUAL "")
    message(FATAL_ERROR "PRISMCANVAS_GATE_SCRIPT is required.")
endif()

if (NOT DEFINED PRISMCANVAS_GATE_WORKDIR OR PRISMCANVAS_GATE_WORKDIR STREQUAL "")
    get_filename_component(PRISMCANVAS_GATE_WORKDIR "${PRISMCANVAS_GATE_SCRIPT}" DIRECTORY)
endif()

set(_prismcanvas_gate_command)
if (DEFINED PRISMCANVAS_GATE_LAUNCHER AND NOT PRISMCANVAS_GATE_LAUNCHER STREQUAL "")
    set(_prismcanvas_gate_command ${PRISMCANVAS_GATE_LAUNCHER})
endif()

list(APPEND _prismcanvas_gate_command "${PRISMCANVAS_GATE_SCRIPT}")
if (DEFINED PRISMCANVAS_GATE_ARGS AND NOT PRISMCANVAS_GATE_ARGS STREQUAL "")
    list(APPEND _prismcanvas_gate_command ${PRISMCANVAS_GATE_ARGS})
endif()

execute_process(
    COMMAND ${_prismcanvas_gate_command}
    WORKING_DIRECTORY "${PRISMCANVAS_GATE_WORKDIR}"
    RESULT_VARIABLE _prismcanvas_gate_result
    OUTPUT_VARIABLE _prismcanvas_gate_output
    ERROR_VARIABLE _prismcanvas_gate_error
)

if (NOT _prismcanvas_gate_output STREQUAL "")
    message("${_prismcanvas_gate_output}")
endif()

if (NOT _prismcanvas_gate_error STREQUAL "")
    message("${_prismcanvas_gate_error}")
endif()

if (NOT _prismcanvas_gate_result EQUAL 0)
    message(FATAL_ERROR "Script gate failed with exit code ${_prismcanvas_gate_result}.")
endif()