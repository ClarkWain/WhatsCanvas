cmake_minimum_required(VERSION 3.16)

if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "platforms/wasm must be configured with Emscripten.")
endif()

add_subdirectory(web)
