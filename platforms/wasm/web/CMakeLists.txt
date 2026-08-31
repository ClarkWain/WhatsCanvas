cmake_minimum_required(VERSION 3.16)

if (NOT EMSCRIPTEN)
    message(FATAL_ERROR "The Web host requires the Emscripten toolchain.")
endif()
if (NOT TARGET WhatsCanvas::OpenGLES)
    message(FATAL_ERROR "The Web host requires WhatsCanvas::OpenGLES.")
endif()

set(_repo_root "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(_desktop_src "${_repo_root}/platforms/desktop/src")
set(_spider_src "${_repo_root}/examples/game/spider_solitaire")

add_executable(WhatsCanvasWeb
    "${CMAKE_CURRENT_LIST_DIR}/src/WebApp.cpp"
    "${_desktop_src}/scenes/FeatureShowcaseScene.cpp"
)

target_include_directories(WhatsCanvasWeb PRIVATE
    "${_desktop_src}"
)

target_link_libraries(WhatsCanvasWeb PRIVATE
    WhatsCanvas::OpenGLES
)

add_executable(SpiderSolitaireWeb
    "${_spider_src}/web/SpiderWebApp.cpp"
    "${_spider_src}/SpiderGame.cpp"
    "${_spider_src}/SpiderBackground.cpp"
    "${_spider_src}/SpiderGameRenderer.cpp"
    "${_spider_src}/SpiderCardAtlas.cpp"
)

target_include_directories(SpiderSolitaireWeb PRIVATE
    "${_spider_src}"
)

target_link_libraries(SpiderSolitaireWeb PRIVATE
    WhatsCanvas::OpenGLES
)

set(_latin_font
    "${_repo_root}/third_party/harfbuzz/test/subset/data/fonts/Roboto-Regular.ttf")
set(_cjk_font
    "${_repo_root}/third_party/harfbuzz/test/api/fonts/Mplus1p-Regular.ttf")
set(_emoji_font
    "${CMAKE_CURRENT_LIST_DIR}/assets/NotoColorEmoji.demo.subset.ttf")
foreach(_font IN ITEMS "${_latin_font}" "${_cjk_font}" "${_emoji_font}")
    if (NOT EXISTS "${_font}")
        message(FATAL_ERROR "Missing Web demo font asset: ${_font}")
    endif()
endforeach()

set(_web_link_options
    "SHELL:--preload-file ${_latin_font}@/fonts/Roboto-Regular.ttf"
    "SHELL:--preload-file ${_cjk_font}@/fonts/Mplus1p-Regular.ttf"
    "SHELL:--preload-file ${_emoji_font}@/fonts/NotoColorEmoji.demo.subset.ttf"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web"
    "-sFULL_ES3=1"
    "-sGL_ENABLE_GET_PROC_ADDRESS=1"
    "-sMAX_WEBGL_VERSION=2"
    "-sMIN_WEBGL_VERSION=2"
    "-sEXIT_RUNTIME=0"
    "$<$<CONFIG:Debug>:-sASSERTIONS=1>"
    "$<$<CONFIG:Debug>:-sGL_ASSERTIONS=1>"
)

target_link_options(WhatsCanvasWeb PRIVATE
    "--shell-file=${CMAKE_CURRENT_LIST_DIR}/shell.html"
    ${_web_link_options}
)

target_link_options(SpiderSolitaireWeb PRIVATE
    "--shell-file=${_spider_src}/web/shell.html"
    ${_web_link_options}
)

set_target_properties(WhatsCanvasWeb PROPERTIES
    OUTPUT_NAME "index"
    SUFFIX ".html"
)

set_target_properties(SpiderSolitaireWeb PROPERTIES
    OUTPUT_NAME "spider"
    SUFFIX ".html"
)
