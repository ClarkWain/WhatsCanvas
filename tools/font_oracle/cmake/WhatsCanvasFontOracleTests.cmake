include_guard(GLOBAL)

set(WHATSCANVAS_SKIA_FONT_ORACLE_EXECUTABLE "" CACHE FILEPATH
    "Skia Android font oracle probe built at Flutter's pinned revision.")
set(WHATSCANVAS_SKIA_FONT_SCANNER_EXECUTABLE "" CACHE FILEPATH
    "Skia/FreeType real-font scanner probe built at Flutter's pinned revision.")
set(WHATSCANVAS_SKIA_ANDROID_FONT_MANAGER_EXECUTABLE "" CACHE FILEPATH
    "Real-file Skia Android font-manager probe built at Flutter's pinned revision.")
set(WHATSCANVAS_SKIA_FONT_RASTER_EXECUTABLE "" CACHE FILEPATH
    "Real-file Skia font-raster probe built at Flutter's pinned revision.")

function(whatscanvas_register_external_font_oracle_tests
         source_dir binary_dir python_executable probe_target)
    if (WHATSCANVAS_SKIA_FONT_ORACLE_EXECUTABLE STREQUAL ""
            AND WHATSCANVAS_SKIA_FONT_SCANNER_EXECUTABLE STREQUAL ""
            AND WHATSCANVAS_SKIA_ANDROID_FONT_MANAGER_EXECUTABLE STREQUAL ""
            AND WHATSCANVAS_SKIA_FONT_RASTER_EXECUTABLE STREQUAL "")
        message(FATAL_ERROR
            "WHATSCANVAS_ENABLE_EXTERNAL_FONT_ORACLES requires at least one probe executable.")
    endif()

    if (NOT WHATSCANVAS_SKIA_FONT_ORACLE_EXECUTABLE STREQUAL "")
        add_test(NAME WhatsCanvasAndroidFontOracleSkia
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${binary_dir}
                -DWHATSCANVAS_TEST_TARGET=${probe_target}
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:${probe_target}>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -DWHATSCANVAS_PYTHON_EXECUTABLE=${python_executable}
                -DWHATSCANVAS_ORACLE_RUNNER=${source_dir}/tools/font_oracle/run_android_font_oracle.py
                -DWHATSCANVAS_ORACLE_FIXTURE=${source_dir}/tests/fixtures/android_font_oracle/semantic_v1.xml
                -DWHATSCANVAS_ORACLE_GOLDEN=${source_dir}/tests/fixtures/android_font_oracle/semantic_v1.expected.json
                -DWHATSCANVAS_SKIA_ORACLE_EXECUTABLE=${WHATSCANVAS_SKIA_FONT_ORACLE_EXECUTABLE}
                -P ${source_dir}/tools/font_oracle/cmake/VerifyAndroidFontOracle.cmake)
        set_tests_properties(WhatsCanvasAndroidFontOracleSkia PROPERTIES
            LABELS "unit;text;font;android;oracle;skia")

        add_test(NAME WhatsCanvasAndroidFontOracleCorpusSkia
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${binary_dir}
                -DWHATSCANVAS_TEST_TARGET=${probe_target}
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:${probe_target}>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -DWHATSCANVAS_PYTHON_EXECUTABLE=${python_executable}
                -DWHATSCANVAS_ORACLE_CORPUS_RUNNER=${source_dir}/tools/font_oracle/run_android_font_oracle_corpus.py
                -DWHATSCANVAS_ORACLE_CORPUS_MANIFEST=${source_dir}/tests/fixtures/android_font_oracle/corpus_manifest.json
                -DWHATSCANVAS_SKIA_ORACLE_EXECUTABLE=${WHATSCANVAS_SKIA_FONT_ORACLE_EXECUTABLE}
                -P ${source_dir}/tools/font_oracle/cmake/VerifyAndroidFontOracleCorpus.cmake)
        set_tests_properties(WhatsCanvasAndroidFontOracleCorpusSkia PROPERTIES
            LABELS "unit;text;font;android;oracle;skia;corpus")
    endif()

    if (NOT WHATSCANVAS_SKIA_FONT_SCANNER_EXECUTABLE STREQUAL "")
        add_test(NAME WhatsCanvasSkiaFontScannerGolden
            COMMAND ${python_executable}
                ${source_dir}/tools/font_oracle/run_skia_font_scanner_golden.py
                --probe ${WHATSCANVAS_SKIA_FONT_SCANNER_EXECUTABLE}
                --font ${source_dir}/third_party/harfbuzz/test/subset/data/fonts/RobotoFlex-Variable.ttf
                --golden ${source_dir}/tests/fixtures/android_font_oracle/roboto_flex.skia-scanner.expected.json
                --output ${binary_dir}/android-font-oracle/roboto_flex.skia-scanner.json)
        set_tests_properties(WhatsCanvasSkiaFontScannerGolden PROPERTIES
            LABELS "unit;text;font;oracle;skia;freetype;variable-font")
    endif()

    if (NOT WHATSCANVAS_SKIA_ANDROID_FONT_MANAGER_EXECUTABLE STREQUAL "")
        add_test(NAME WhatsCanvasSkiaAndroidFontManagerGolden
            COMMAND ${python_executable}
                ${source_dir}/tools/font_oracle/run_skia_android_font_manager_golden.py
                --probe ${WHATSCANVAS_SKIA_ANDROID_FONT_MANAGER_EXECUTABLE}
                --config ${source_dir}/tests/fixtures/android_font_oracle/real_fonts.xml
                --font-dir ${source_dir}/third_party/harfbuzz/test
                --golden ${source_dir}/tests/fixtures/android_font_oracle/real_fonts.skia-manager.expected.json
                --output ${binary_dir}/android-font-oracle/real_fonts.skia-manager.json)
        set_tests_properties(WhatsCanvasSkiaAndroidFontManagerGolden PROPERTIES
            LABELS "unit;text;font;android;oracle;skia;freetype;variable-font")
    endif()

    if (NOT WHATSCANVAS_SKIA_FONT_RASTER_EXECUTABLE STREQUAL "")
        add_test(NAME WhatsCanvasSkiaFontRasterGolden
            COMMAND ${python_executable}
                ${source_dir}/tools/font_oracle/run_skia_font_raster_golden.py
                --probe ${WHATSCANVAS_SKIA_FONT_RASTER_EXECUTABLE}
                --config ${source_dir}/tests/fixtures/android_font_oracle/real_fonts.xml
                --font-dir ${source_dir}/third_party/harfbuzz/test
                --golden ${source_dir}/tests/fixtures/android_font_oracle/real_fonts.skia-raster.expected.json
                --output ${binary_dir}/android-font-oracle/real_fonts.skia-raster.json)
        set_tests_properties(WhatsCanvasSkiaFontRasterGolden PROPERTIES
            LABELS "unit;text;font;android;oracle;skia;freetype;raster;color-font")

        add_test(NAME WhatsCanvasFontRasterDifferential
            COMMAND ${CMAKE_COMMAND}
                -DWHATSCANVAS_TEST_BUILD_DIR=${binary_dir}
                -DWHATSCANVAS_TEST_TARGET=WhatsCanvasFontRasterProbe
                -DWHATSCANVAS_TEST_EXECUTABLE=$<TARGET_FILE:WhatsCanvasFontRasterProbe>
                -DWHATSCANVAS_TEST_CONFIG=$<CONFIG>
                -DWHATSCANVAS_PYTHON_EXECUTABLE=${python_executable}
                -DWHATSCANVAS_RASTER_DIFFERENTIAL_RUNNER=${source_dir}/tools/font_oracle/run_font_raster_differential.py
                -DWHATSCANVAS_REFERENCE_RASTER_EXECUTABLE=${WHATSCANVAS_SKIA_FONT_RASTER_EXECUTABLE}
                -DWHATSCANVAS_REFERENCE_CONFIG=${source_dir}/tests/fixtures/android_font_oracle/real_fonts.xml
                -DWHATSCANVAS_REFERENCE_FONT_DIR=${source_dir}/third_party/harfbuzz/test
                -DWHATSCANVAS_LATIN_FONT=${source_dir}/third_party/harfbuzz/test/subset/data/fonts/RobotoFlex-Variable.ttf
                -DWHATSCANVAS_CJK_FONT=${source_dir}/third_party/harfbuzz/test/api/fonts/SourceHanSans-Regular.41,4C2E.otf
                -DWHATSCANVAS_COLR_EMOJI_FONT=${source_dir}/third_party/harfbuzz/test/api/fonts/TwemojiMozilla.subset.ttf
                -DWHATSCANVAS_BITMAP_EMOJI_FONT=${source_dir}/third_party/harfbuzz/test/api/fonts/NotoColorEmoji.subset.ttf
                -DWHATSCANVAS_RASTER_DIFFERENTIAL_OUTPUT=${binary_dir}/android-font-oracle/real_fonts.raster-differential.json
                -P ${source_dir}/tools/font_oracle/cmake/VerifyFontRasterDifferential.cmake)
        set_tests_properties(WhatsCanvasFontRasterDifferential PROPERTIES
            LABELS "unit;text;font;android;oracle;skia;freetype;raster;color-font;differential")
    endif()
endfunction()
