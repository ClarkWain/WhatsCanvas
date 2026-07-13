# Builds, installs, discovers, links, and runs a Software-only WhatsCanvas
# package. It intentionally uses a fresh nested work directory so it proves
# that no build-tree aliases or OpenGL targets leak into find_package().

cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED WHATSCANVAS_PACKAGE_SOURCE_DIR OR WHATSCANVAS_PACKAGE_SOURCE_DIR STREQUAL "")
    message(FATAL_ERROR "WHATSCANVAS_PACKAGE_SOURCE_DIR is required")
endif()
if(NOT DEFINED WHATSCANVAS_PACKAGE_WORK_DIR OR WHATSCANVAS_PACKAGE_WORK_DIR STREQUAL "")
    message(FATAL_ERROR "WHATSCANVAS_PACKAGE_WORK_DIR is required")
endif()

get_filename_component(_source "${WHATSCANVAS_PACKAGE_SOURCE_DIR}" ABSOLUTE)
get_filename_component(_work "${WHATSCANVAS_PACKAGE_WORK_DIR}" ABSOLUTE)
if(NOT EXISTS "${_source}/CMakeLists.txt" OR NOT EXISTS "${_source}/tests/package_consumer/CMakeLists.txt")
    message(FATAL_ERROR "Source directory does not look like WhatsCanvas")
endif()
if(_source STREQUAL _work)
    message(FATAL_ERROR "Work directory must not be the source directory")
endif()

set(_package_build "${_work}/package-build")
set(_prefix "${_work}/prefix")
set(_consumer_build "${_work}/consumer-build")
file(REMOVE_RECURSE "${_work}")

execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}" -B "${_package_build}"
                        -DWHATSCANVAS_BUILD_OPENGL=OFF
                        -DWHATSCANVAS_BUILD_OPENGLES=OFF
                        -DWHATSCANVAS_BUILD_SOFTWARE=ON
                        -DWHATSCANVAS_BUILD_DEMO=OFF
                        -DWHATSCANVAS_BUILD_BENCHMARKS=OFF
                        -DBUILD_TESTING=OFF
                        -DWHATSCANVAS_INSTALL=ON
                        "-DCMAKE_INSTALL_PREFIX=${_prefix}"
                RESULT_VARIABLE _configure_result)
if(NOT _configure_result EQUAL 0)
    message(FATAL_ERROR "Software package configure failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_package_build}" --config Release
                RESULT_VARIABLE _build_result)
if(NOT _build_result EQUAL 0)
    message(FATAL_ERROR "Software package build failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${_package_build}" --config Release
                RESULT_VARIABLE _install_result)
if(NOT _install_result EQUAL 0)
    message(FATAL_ERROR "Software package install failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${_source}/tests/package_consumer" -B "${_consumer_build}"
                        "-DCMAKE_PREFIX_PATH=${_prefix}"
                        -DWHATSCANVAS_PACKAGE_TARGET=Software
                RESULT_VARIABLE _consumer_configure_result)
if(NOT _consumer_configure_result EQUAL 0)
    message(FATAL_ERROR "Software package consumer configure failed")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${_consumer_build}" --config Release
                RESULT_VARIABLE _consumer_build_result)
if(NOT _consumer_build_result EQUAL 0)
    message(FATAL_ERROR "Software package consumer build failed")
endif()
file(GLOB_RECURSE _consumer_candidates "${_consumer_build}/WhatsCanvasPackageConsumer*" )
list(FILTER _consumer_candidates EXCLUDE REGEX "\\.(cmake|vcxproj|filters|obj|pdb|ilk)$")
list(LENGTH _consumer_candidates _consumer_count)
if(_consumer_count EQUAL 0)
    message(FATAL_ERROR "Software package consumer executable was not found")
endif()
list(GET _consumer_candidates 0 _consumer)
execute_process(COMMAND "${_consumer}" RESULT_VARIABLE _consumer_result)
if(NOT _consumer_result EQUAL 0)
    message(FATAL_ERROR "Software package consumer failed: ${_consumer_result}")
endif()

message(STATUS "WhatsCanvas Software package consumer smoke passed")
