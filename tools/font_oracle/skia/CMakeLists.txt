cmake_minimum_required(VERSION 3.24)
project(WhatsCanvasSkiaAndroidFontOracle LANGUAGES CXX)

if (NOT WIN32 OR NOT MSVC)
    message(FATAL_ERROR "The checked-in adapter currently supports the pinned Windows/MSVC GN artifacts")
endif()

set(SKIA_SOURCE_DIR "" CACHE PATH "Path to the pinned Skia source checkout")
set(SKIA_BUILD_DIR "" CACHE PATH "Path to the compatible GN Skia build")

if (SKIA_SOURCE_DIR STREQUAL "" OR SKIA_BUILD_DIR STREQUAL "")
    message(FATAL_ERROR "SKIA_SOURCE_DIR and SKIA_BUILD_DIR are required")
endif()

find_package(Git REQUIRED)
set(EXPECTED_SKIA_REVISION "653397c6be15b87fe8f89a4492582fbb825f6da8")
set(EXPECTED_EXPAT_REVISION "6154446fccefbf3ca644894f598969113b0c7bcd")
set(EXPECTED_FREETYPE_REVISION "264b5fbf5b912b39f98d038bf75d39be0a73f21b")
set(EXPECTED_LIBPNG_REVISION "d5515b5b8be3901aac04e5bd8bd5c89f287bcd33")
set(EXPECTED_ZLIB_REVISION "646b7f569718921d7d4b5b8e22572ff6c76f2596")
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C "${SKIA_SOURCE_DIR}" rev-parse HEAD
    OUTPUT_VARIABLE ACTUAL_SKIA_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE SKIA_REVISION_RESULT)
execute_process(
    COMMAND "${GIT_EXECUTABLE}" -C
        "${SKIA_SOURCE_DIR}/third_party/externals/expat" rev-parse HEAD
    OUTPUT_VARIABLE ACTUAL_EXPAT_REVISION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE EXPAT_REVISION_RESULT)
if (NOT SKIA_REVISION_RESULT EQUAL 0
        OR NOT ACTUAL_SKIA_REVISION STREQUAL EXPECTED_SKIA_REVISION)
    message(FATAL_ERROR
        "Skia revision mismatch: expected ${EXPECTED_SKIA_REVISION}, got ${ACTUAL_SKIA_REVISION}")
endif()
if (NOT EXPAT_REVISION_RESULT EQUAL 0
        OR NOT ACTUAL_EXPAT_REVISION STREQUAL EXPECTED_EXPAT_REVISION)
    message(FATAL_ERROR
        "Expat revision mismatch: expected ${EXPECTED_EXPAT_REVISION}, got ${ACTUAL_EXPAT_REVISION}")
endif()
foreach(_dependency IN ITEMS FREETYPE LIBPNG ZLIB)
    string(TOLOWER "${_dependency}" _directory)
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C
            "${SKIA_SOURCE_DIR}/third_party/externals/${_directory}" rev-parse HEAD
        OUTPUT_VARIABLE ACTUAL_${_dependency}_REVISION
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE ${_dependency}_REVISION_RESULT)
    if (NOT ${_dependency}_REVISION_RESULT EQUAL 0
            OR NOT ACTUAL_${_dependency}_REVISION STREQUAL EXPECTED_${_dependency}_REVISION)
        message(FATAL_ERROR
            "${_dependency} revision mismatch: expected ${EXPECTED_${_dependency}_REVISION}, got ${ACTUAL_${_dependency}_REVISION}")
    endif()
endforeach()

set(SKIA_PARSER_OBJECT
    "${SKIA_BUILD_DIR}/obj/src/ports/fontmgr_android_parser.SkFontMgr_android_parser.obj")
foreach(required
        "${SKIA_SOURCE_DIR}/src/ports/SkFontMgr_android_parser.h"
        "${SKIA_SOURCE_DIR}/third_party/externals/expat/expat/lib/expat.h"
        "${SKIA_BUILD_DIR}/skia.lib"
        "${SKIA_BUILD_DIR}/expat.lib"
        "${SKIA_BUILD_DIR}/freetype2.lib"
        "${SKIA_BUILD_DIR}/libpng.lib"
        "${SKIA_BUILD_DIR}/zlib.lib"
        "${SKIA_PARSER_OBJECT}")
    if (NOT EXISTS "${required}")
        message(FATAL_ERROR "Required pinned-Skia artifact not found: ${required}")
    endif()
endforeach()

set_source_files_properties("${SKIA_PARSER_OBJECT}" PROPERTIES
    EXTERNAL_OBJECT TRUE
    GENERATED TRUE)

add_executable(SkiaAndroidFontOracleProbe
    "${CMAKE_CURRENT_LIST_DIR}/../SkiaAndroidFontOracleProbe.cpp"
    "${SKIA_PARSER_OBJECT}"
)
set_property(TARGET SkiaAndroidFontOracleProbe PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_compile_features(SkiaAndroidFontOracleProbe PRIVATE cxx_std_20)
target_compile_definitions(SkiaAndroidFontOracleProbe PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _HAS_EXCEPTIONS=0
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    NDEBUG
    XML_STATIC
)
target_include_directories(SkiaAndroidFontOracleProbe PRIVATE
    "${SKIA_SOURCE_DIR}"
    "${SKIA_SOURCE_DIR}/third_party/externals/expat/expat/lib"
    "${SKIA_SOURCE_DIR}/third_party/expat/include/expat_config"
)
if (MSVC)
    target_compile_options(SkiaAndroidFontOracleProbe PRIVATE /utf-8 /GR- /Zc:lambda)
endif()
target_link_libraries(SkiaAndroidFontOracleProbe PRIVATE
    "${SKIA_BUILD_DIR}/skia.lib"
    "${SKIA_BUILD_DIR}/expat.lib"
)

add_executable(SkiaFontScannerProbe
    "${CMAKE_CURRENT_LIST_DIR}/../SkiaFontScannerProbe.cpp")
target_compile_features(SkiaFontScannerProbe PRIVATE cxx_std_20)
set_property(TARGET SkiaFontScannerProbe PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_compile_definitions(SkiaFontScannerProbe PRIVATE
    _CRT_SECURE_NO_WARNINGS _HAS_EXCEPTIONS=0 WIN32_LEAN_AND_MEAN NOMINMAX NDEBUG)
target_include_directories(SkiaFontScannerProbe PRIVATE "${SKIA_SOURCE_DIR}")
if (MSVC)
    target_compile_options(SkiaFontScannerProbe PRIVATE /utf-8 /GR- /Zc:lambda)
endif()
target_link_libraries(SkiaFontScannerProbe PRIVATE
    "${SKIA_BUILD_DIR}/skia.lib"
    "${SKIA_BUILD_DIR}/freetype2.lib"
    "${SKIA_BUILD_DIR}/libpng.lib"
    "${SKIA_BUILD_DIR}/zlib.lib"
)

add_executable(SkiaAndroidFontManagerProbe
    "${CMAKE_CURRENT_LIST_DIR}/../SkiaAndroidFontManagerProbe.cpp")
target_compile_features(SkiaAndroidFontManagerProbe PRIVATE cxx_std_20)
set_property(TARGET SkiaAndroidFontManagerProbe PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_compile_definitions(SkiaAndroidFontManagerProbe PRIVATE
    _CRT_SECURE_NO_WARNINGS _HAS_EXCEPTIONS=0 WIN32_LEAN_AND_MEAN NOMINMAX NDEBUG)
target_include_directories(SkiaAndroidFontManagerProbe PRIVATE "${SKIA_SOURCE_DIR}")
if (MSVC)
    target_compile_options(SkiaAndroidFontManagerProbe PRIVATE /utf-8 /GR- /Zc:lambda)
endif()
target_link_libraries(SkiaAndroidFontManagerProbe PRIVATE
    "${SKIA_BUILD_DIR}/skia.lib"
    "${SKIA_BUILD_DIR}/expat.lib"
    "${SKIA_BUILD_DIR}/freetype2.lib"
    "${SKIA_BUILD_DIR}/libpng.lib"
    "${SKIA_BUILD_DIR}/zlib.lib"
)

add_executable(SkiaFontRasterProbe
    "${CMAKE_CURRENT_LIST_DIR}/../SkiaFontRasterProbe.cpp")
target_compile_features(SkiaFontRasterProbe PRIVATE cxx_std_20)
set_property(TARGET SkiaFontRasterProbe PROPERTY
    MSVC_RUNTIME_LIBRARY "MultiThreaded")
target_compile_definitions(SkiaFontRasterProbe PRIVATE
    _CRT_SECURE_NO_WARNINGS _HAS_EXCEPTIONS=0 WIN32_LEAN_AND_MEAN NOMINMAX NDEBUG)
target_include_directories(SkiaFontRasterProbe PRIVATE "${SKIA_SOURCE_DIR}")
if (MSVC)
    target_compile_options(SkiaFontRasterProbe PRIVATE /utf-8 /GR- /Zc:lambda)
endif()
target_link_libraries(SkiaFontRasterProbe PRIVATE
    "${SKIA_BUILD_DIR}/skia.lib"
    "${SKIA_BUILD_DIR}/expat.lib"
    "${SKIA_BUILD_DIR}/freetype2.lib"
    "${SKIA_BUILD_DIR}/libpng.lib"
    "${SKIA_BUILD_DIR}/zlib.lib"
)
