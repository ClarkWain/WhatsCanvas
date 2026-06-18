if (NOT DEFINED WHATSCANVAS_GATE_SCRIPT OR WHATSCANVAS_GATE_SCRIPT STREQUAL "")
    message(FATAL_ERROR "WHATSCANVAS_GATE_SCRIPT is required.")
endif()

if (NOT DEFINED WHATSCANVAS_GATE_WORKDIR OR WHATSCANVAS_GATE_WORKDIR STREQUAL "")
    get_filename_component(WHATSCANVAS_GATE_WORKDIR "${WHATSCANVAS_GATE_SCRIPT}" DIRECTORY)
endif()

set(_whatscanvas_gate_command)
if (DEFINED WHATSCANVAS_GATE_LAUNCHER AND NOT WHATSCANVAS_GATE_LAUNCHER STREQUAL "")
    set(_whatscanvas_gate_command ${WHATSCANVAS_GATE_LAUNCHER})
endif()

list(APPEND _whatscanvas_gate_command "${WHATSCANVAS_GATE_SCRIPT}")
if (DEFINED WHATSCANVAS_GATE_ARGS AND NOT WHATSCANVAS_GATE_ARGS STREQUAL "")
    list(APPEND _whatscanvas_gate_command ${WHATSCANVAS_GATE_ARGS})
endif()

execute_process(
    COMMAND ${_whatscanvas_gate_command}
    WORKING_DIRECTORY "${WHATSCANVAS_GATE_WORKDIR}"
    RESULT_VARIABLE _whatscanvas_gate_result
    OUTPUT_VARIABLE _whatscanvas_gate_output
    ERROR_VARIABLE _whatscanvas_gate_error
)

if (NOT _whatscanvas_gate_output STREQUAL "")
    message("${_whatscanvas_gate_output}")
endif()

if (NOT _whatscanvas_gate_error STREQUAL "")
    message("${_whatscanvas_gate_error}")
endif()

if (NOT _whatscanvas_gate_result EQUAL 0)
    message(FATAL_ERROR "Script gate failed with exit code ${_whatscanvas_gate_result}.")
endif()