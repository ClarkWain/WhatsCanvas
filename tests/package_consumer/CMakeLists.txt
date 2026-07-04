cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasPackageConsumer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(WhatsCanvas CONFIG REQUIRED)

add_executable(WhatsCanvasPackageConsumer main.cpp)
target_link_libraries(WhatsCanvasPackageConsumer PRIVATE WhatsCanvas::OpenGL)
