cmake_minimum_required(VERSION 3.16)
project(WhatsCanvasAndroidConsumerSmoke LANGUAGES CXX)

find_package(whatscanvas REQUIRED CONFIG)

add_library(whatscanvas_consumer_smoke SHARED consumer_smoke.cpp)
target_compile_features(whatscanvas_consumer_smoke PRIVATE cxx_std_17)
target_link_libraries(whatscanvas_consumer_smoke PRIVATE
    whatscanvas::whatscanvas
    android
    EGL
    GLESv3
    log)
