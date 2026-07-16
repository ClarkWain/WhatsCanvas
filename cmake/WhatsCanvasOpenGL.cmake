function(whatscanvas_add_common_dependencies project_root)
    set(third_party_dir "${project_root}/third_party")
    set(glad_path "${third_party_dir}/glad")
    set(stb_path "${third_party_dir}/stb")
    set(glm_path "${third_party_dir}/glm")
    set(polyline2d_path "${third_party_dir}/polyline2d")
    set(freetype_path "${third_party_dir}/freetype")
    set(harfbuzz_path "${third_party_dir}/harfbuzz")

    if (NOT EXISTS "${glad_path}/src/glad.c" OR
        NOT EXISTS "${glad_path}/include/glad/glad.h" OR
        NOT EXISTS "${glm_path}/glm/glm.hpp" OR
        NOT EXISTS "${stb_path}/stb_image.h")
        message(FATAL_ERROR "Missing third-party dependencies. Run: git submodule update --init --recursive")
    endif()

    if (NOT TARGET WhatsCanvasGLAD)
        add_library(WhatsCanvasGLAD INTERFACE)
        target_include_directories(WhatsCanvasGLAD INTERFACE "${glad_path}/include")
    endif()

    if (NOT TARGET WhatsCanvasGLM)
        add_library(WhatsCanvasGLM INTERFACE)
        target_include_directories(WhatsCanvasGLM INTERFACE
            "${glm_path}"
            "${glm_path}/glm"
        )
    endif()

    if (NOT TARGET WhatsCanvasSTB)
        add_library(WhatsCanvasSTB INTERFACE)
        target_include_directories(WhatsCanvasSTB INTERFACE "${stb_path}")
    endif()

    if (NOT TARGET WhatsCanvasPolyline2D)
        add_library(WhatsCanvasPolyline2D INTERFACE)
        target_include_directories(WhatsCanvasPolyline2D SYSTEM INTERFACE "${polyline2d_path}/include")
    endif()

    # Do not create the bundled FreeType target when its rasterizer is
    # disabled. HarfBuzz's community CMake build automatically enables its
    # FreeType interop whenever a target named `freetype` already exists,
    # regardless of HB_HAVE_FREETYPE=OFF. Creating that otherwise-unused
    # target therefore leaked `freetype` into harfbuzz's installed link
    # interface while the matching FreeType package was intentionally not
    # installed, leaving exported consumers with a missing target.
    if (WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER
        AND EXISTS "${freetype_path}/CMakeLists.txt"
        AND NOT TARGET freetype)
        set(FT_DISABLE_ZLIB ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BZIP2 ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_PNG ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "" FORCE)
        set(FT_DISABLE_BROTLI ON CACHE BOOL "" FORCE)
        set(FT_ENABLE_ERROR_STRINGS OFF CACHE BOOL "" FORCE)
        if (WHATSCANVAS_INSTALL AND WHATSCANVAS_INSTALL_TEXT_DEPENDENCIES AND WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER)
            add_subdirectory("${freetype_path}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/freetype")
        else()
            add_subdirectory("${freetype_path}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/freetype" EXCLUDE_FROM_ALL)
        endif()
    endif()
    if (TARGET freetype-interface AND NOT TARGET Freetype::Freetype)
        add_library(Freetype::Freetype ALIAS freetype-interface)
    endif()

    if (WHATSCANVAS_ENABLE_OPENTYPE_SHAPING AND EXISTS "${harfbuzz_path}/CMakeLists.txt" AND NOT TARGET harfbuzz)
        set(HB_HAVE_CAIRO OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_GRAPHITE2 OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_INTROSPECTION OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_UTILS OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_SUBSET OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_RASTER OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_VECTOR OFF CACHE BOOL "" FORCE)
        set(HB_BUILD_GPU OFF CACHE BOOL "" FORCE)
        if (WHATSCANVAS_INSTALL AND WHATSCANVAS_INSTALL_TEXT_DEPENDENCIES)
            add_subdirectory("${harfbuzz_path}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/harfbuzz")
        else()
            add_subdirectory("${harfbuzz_path}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/harfbuzz" EXCLUDE_FROM_ALL)
        endif()
    endif()
    if (TARGET harfbuzz AND NOT TARGET harfbuzz::harfbuzz)
        add_library(harfbuzz::harfbuzz ALIAS harfbuzz)
    endif()
    if (TARGET harfbuzz AND NOT TARGET HarfBuzz::HarfBuzz)
        add_library(HarfBuzz::HarfBuzz ALIAS harfbuzz)
    endif()
endfunction()

function(whatscanvas_add_glfw_dependency project_root)
    set(glfw_path "${project_root}/third_party/glfw")

    if (NOT EXISTS "${glfw_path}/CMakeLists.txt")
        message(FATAL_ERROR "Missing GLFW dependency. Run: git submodule update --init --recursive")
    endif()

    if (NOT TARGET glfw)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        # WhatsCanvas itself does not require GLFW to consume its renderer
        # package. An embedding package (WhatsUI::Glfw) can opt in so the
        # matching glfw3Config/glfw target is installed in the same prefix.
        set(GLFW_INSTALL ${WHATSCANVAS_INSTALL_GLFW} CACHE BOOL "" FORCE)
        # Build only the X11 backend on Linux so the configure step does not
        # require the Wayland toolchain (wayland-scanner) on CI runners.
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
        if (WHATSCANVAS_INSTALL_GLFW)
            # EXCLUDE_FROM_ALL also keeps a nested project's install script out
            # of the parent install traversal. A package exporting
            # WhatsUI::Glfw must carry glfw3Config/glfwTargets in its prefix.
            add_subdirectory("${glfw_path}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/glfw")
        else()
            add_subdirectory("${glfw_path}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/glfw" EXCLUDE_FROM_ALL)
        endif()
    endif()
endfunction()

function(whatscanvas_add_gl_family_library target_name project_root)
    set(src_dir "${project_root}/src")
    set(glad_path "${project_root}/third_party/glad")
    set(options OPENGLES)
    cmake_parse_arguments(WSC_GL "${options}" "" "" ${ARGN})

    set(text_shaping_sources)
    set(text_shaping_libraries)
    set(text_rasterizer_libraries)
    if (WHATSCANVAS_ENABLE_OPENTYPE_SHAPING)
        if (NOT TARGET harfbuzz::harfbuzz AND NOT TARGET HarfBuzz::HarfBuzz)
            find_package(HarfBuzz CONFIG QUIET)
            if (NOT TARGET harfbuzz::harfbuzz AND NOT TARGET HarfBuzz::HarfBuzz)
                find_package(HarfBuzz QUIET)
            endif()
        endif()

        if (TARGET harfbuzz::harfbuzz)
            set(WHATSCANVAS_HARFBUZZ_TARGET harfbuzz::harfbuzz)
        elseif (TARGET HarfBuzz::HarfBuzz)
            set(WHATSCANVAS_HARFBUZZ_TARGET HarfBuzz::HarfBuzz)
        else()
            find_path(WHATSCANVAS_HARFBUZZ_INCLUDE_DIR
                NAMES hb.h
                PATH_SUFFIXES harfbuzz
            )
            find_library(WHATSCANVAS_HARFBUZZ_LIBRARY
                NAMES harfbuzz
            )
            if (WHATSCANVAS_HARFBUZZ_INCLUDE_DIR AND WHATSCANVAS_HARFBUZZ_LIBRARY)
                if (NOT TARGET WhatsCanvasHarfBuzz)
                    add_library(WhatsCanvasHarfBuzz UNKNOWN IMPORTED)
                    set_target_properties(WhatsCanvasHarfBuzz PROPERTIES
                        IMPORTED_LOCATION "${WHATSCANVAS_HARFBUZZ_LIBRARY}"
                        INTERFACE_INCLUDE_DIRECTORIES "${WHATSCANVAS_HARFBUZZ_INCLUDE_DIR}"
                    )
                endif()
                set(WHATSCANVAS_HARFBUZZ_TARGET WhatsCanvasHarfBuzz)
            endif()
        endif()

        if (WHATSCANVAS_HARFBUZZ_TARGET)
            list(APPEND text_shaping_sources "${src_dir}/text/HarfBuzzTextShaper.cpp")
            list(APPEND text_shaping_libraries "${WHATSCANVAS_HARFBUZZ_TARGET}")
        else()
            message(STATUS "WHATSCANVAS_ENABLE_OPENTYPE_SHAPING is ON, but HarfBuzz was not found. Falling back to simple text shaping.")
        endif()
    endif()

    if (WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER)
        find_package(Freetype QUIET)
        if (TARGET Freetype::Freetype)
            set(WHATSCANVAS_FREETYPE_TARGET Freetype::Freetype)
        else()
            find_path(WHATSCANVAS_FREETYPE_INCLUDE_DIR
                NAMES ft2build.h
                PATH_SUFFIXES freetype2
            )
            find_library(WHATSCANVAS_FREETYPE_LIBRARY
                NAMES freetype freetype6
            )
            if (WHATSCANVAS_FREETYPE_INCLUDE_DIR AND WHATSCANVAS_FREETYPE_LIBRARY)
                if (NOT TARGET WhatsCanvasFreeType)
                    add_library(WhatsCanvasFreeType UNKNOWN IMPORTED)
                    set_target_properties(WhatsCanvasFreeType PROPERTIES
                        IMPORTED_LOCATION "${WHATSCANVAS_FREETYPE_LIBRARY}"
                        INTERFACE_INCLUDE_DIRECTORIES "${WHATSCANVAS_FREETYPE_INCLUDE_DIR}"
                    )
                endif()
                set(WHATSCANVAS_FREETYPE_TARGET WhatsCanvasFreeType)
            endif()
        endif()

        if (WHATSCANVAS_FREETYPE_TARGET)
            list(APPEND text_rasterizer_libraries "${WHATSCANVAS_FREETYPE_TARGET}")
        else()
            message(STATUS "WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER is ON, but FreeType was not found. Falling back to stb_truetype rasterization.")
        endif()
    endif()

    # Optional Vulkan backend. The Vulkan source is always compiled so the render
    # device factory can reference it, but real Vulkan API usage is only enabled
    # when the option is set and a Vulkan SDK is discovered at configure time.
    set(vulkan_backend_libraries)
    set(vulkan_backend_enabled OFF)
    if (WHATSCANVAS_ENABLE_VULKAN)
        find_package(Vulkan QUIET)
        if (Vulkan_FOUND)
            set(vulkan_backend_enabled ON)
            list(APPEND vulkan_backend_libraries Vulkan::Vulkan)
            message(STATUS "WhatsCanvas Vulkan backend enabled (${Vulkan_LIBRARIES}).")
        else()
            message(STATUS "WHATSCANVAS_ENABLE_VULKAN is ON, but a Vulkan SDK was not found. Building the Vulkan backend as an inert stub.")
        endif()
    endif()

    add_library(${target_name}
        "${glad_path}/src/glad.c"
        "${src_dir}/core/Log.cpp"
        "${src_dir}/canvas/base.cpp"
        "${src_dir}/canvas/Canvas.cpp"
        "${src_dir}/canvas/Image.cpp"
        "${src_dir}/canvas/Matrix.cpp"
        "${src_dir}/canvas/Paint.cpp"
        "${src_dir}/canvas/Path.cpp"
        "${src_dir}/text/BasicTextBackend.cpp"
        "${src_dir}/text/FontRasterizer.cpp"
        "${src_dir}/text/GlyphAtlas.cpp"
        "${src_dir}/text/NativeText.cpp"
        "${src_dir}/text/DirectWriteTextBackend.cpp"
        "${src_dir}/text/TextShaper.cpp"
        "${src_dir}/text/TextUtils.cpp"
        "${src_dir}/text/UnicodeBidi.cpp"
        ${text_shaping_sources}
        "${src_dir}/opengl/GLTextureUtils.cpp"
        "${src_dir}/opengl/GLProgram.cpp"
        "${src_dir}/opengl/GLVertexArray.cpp"
        "${src_dir}/opengl/StreamBuffer.cpp"
        "${src_dir}/opengl/AsyncReadback.cpp"
        "${src_dir}/opengl/GlobalIndexBuffers.cpp"
        "${src_dir}/opengl/TexelBuffer.cpp"
        "${src_dir}/opengl/PixelFormatCaps.cpp"
        "${src_dir}/opengl/GaussianBlurProgram.cpp"
        "${src_dir}/opengl/ClipCoverageProgram.cpp"
        "${src_dir}/opengl/ClipMaskUniforms.cpp"
        "${src_dir}/opengl/DrawClipFillProgram.cpp"
        "${src_dir}/command/DrawCommand.cpp"
        "${src_dir}/command/DrawPoints.cpp"
        "${src_dir}/command/DrawLines.cpp"
        "${src_dir}/command/DrawPath.cpp"
        "${src_dir}/command/DrawImage.cpp"
        "${src_dir}/command/DrawText.cpp"
        "${src_dir}/render/CommandDrawListEncoder.cpp"
        "${src_dir}/render/RenderContext.cpp"
        "${src_dir}/render/OpenGLRenderDevice.cpp"
        "${src_dir}/render/GLPresent.cpp"
        "${src_dir}/render/RenderDeviceFactory.cpp"
        "${src_dir}/render/RenderTargetPool.cpp"
        "${src_dir}/render/SpriteBatch.cpp"
        "${src_dir}/render/Renderer.cpp"
        "${src_dir}/render/software/SoftwareRenderer.cpp"
        "${src_dir}/render/software/SoftwarePresent.cpp"
        "${src_dir}/render/vulkan/VulkanRenderDevice.cpp"
    )

    if (WSC_GL_OPENGLES)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_OPENGL_ES)
    endif()

    if (WHATSCANVAS_ENABLE_OPENTYPE_SHAPING)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_ENABLE_OPENTYPE_SHAPING)
    endif()

    if (WHATSCANVAS_HARFBUZZ_TARGET)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_HAS_HARFBUZZ)
    endif()

    if (WHATSCANVAS_FREETYPE_TARGET)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_HAS_FREETYPE)
    endif()

    if (vulkan_backend_enabled)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_ENABLE_VULKAN)
        target_link_libraries(${target_name} PRIVATE ${vulkan_backend_libraries})
    endif()

    target_include_directories(${target_name}
        PRIVATE
            "${src_dir}"
            "${project_root}/include"
            "${glad_path}/include"
        INTERFACE
            "$<BUILD_INTERFACE:${project_root}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    target_compile_definitions(${target_name} PRIVATE GLEW_STATIC)
    target_link_libraries(${target_name}
        PRIVATE
            "$<BUILD_INTERFACE:WhatsCanvasGLM>"
            "$<BUILD_INTERFACE:WhatsCanvasGLAD>"
            "$<BUILD_INTERFACE:WhatsCanvasSTB>"
            "$<BUILD_INTERFACE:WhatsCanvasPolyline2D>"
            ${text_shaping_libraries}
            ${text_rasterizer_libraries}
    )

    if (NOT WSC_GL_OPENGLES)
        find_package(OpenGL REQUIRED)
        target_link_libraries(${target_name}
            PRIVATE
                "$<BUILD_INTERFACE:OpenGL::GL>"
            INTERFACE
                "$<INSTALL_INTERFACE:OpenGL::GL>"
        )
    elseif (WHATSCANVAS_OPENGLES_LIBRARIES)
        target_link_libraries(${target_name} PRIVATE ${WHATSCANVAS_OPENGLES_LIBRARIES})
    endif()

    if (BUILD_SHARED_LIBS)
        target_compile_definitions(${target_name} PRIVATE WSC_EXPORTS PUBLIC WSC_SHARED)
        set_target_properties(${target_name} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
        )
    endif()

    if (WIN32)
        target_link_libraries(${target_name} PRIVATE gdi32 user32 dwrite d2d1 windowscodecs ole32)
    endif()

    if (UNIX AND NOT APPLE)
        find_package(X11)
        if (X11_FOUND)
            target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_HAS_X11)
            target_include_directories(${target_name} PRIVATE ${X11_INCLUDE_DIR})
            target_link_libraries(${target_name} PRIVATE ${X11_LIBRARIES})
        endif()
    endif()
endfunction()

function(whatscanvas_add_opengl_library target_name project_root)
    whatscanvas_add_gl_family_library(${target_name} "${project_root}")
endfunction()

function(whatscanvas_add_opengles_library target_name project_root)
    whatscanvas_add_gl_family_library(${target_name} "${project_root}" OPENGLES)
endfunction()

# A dependency-free, CPU-only rendering library. It contains the same public
# Canvas API but is built with WHATSCANVAS_SOFTWARE_ONLY so every OpenGL /
# Vulkan / glad code path is compiled out. The draw commands are reduced to
# pure data carriers (SoftwareCommandStubs.cpp) consumed by the CPU
# SoftwareRenderer, so the resulting binary links no GPU libraries and runs in
# fully head-less environments. Text uses the built-in stb_truetype rasterizer.
function(whatscanvas_add_software_library target_name project_root)
    set(src_dir "${project_root}/src")

    add_library(${target_name}
        "${src_dir}/core/Log.cpp"
        "${src_dir}/canvas/base.cpp"
        "${src_dir}/canvas/Canvas.cpp"
        "${src_dir}/canvas/Image.cpp"
        "${src_dir}/canvas/Matrix.cpp"
        "${src_dir}/canvas/Paint.cpp"
        "${src_dir}/canvas/Path.cpp"
        "${src_dir}/text/BasicTextBackend.cpp"
        "${src_dir}/text/FontRasterizer.cpp"
        "${src_dir}/text/GlyphAtlas.cpp"
        "${src_dir}/text/NativeText.cpp"
        "${src_dir}/text/DirectWriteTextBackend.cpp"
        "${src_dir}/text/TextShaper.cpp"
        "${src_dir}/text/TextUtils.cpp"
        "${src_dir}/text/UnicodeBidi.cpp"
        "${src_dir}/command/SoftwareCommandStubs.cpp"
        "${src_dir}/render/software/SoftwareRenderer.cpp"
        "${src_dir}/render/software/SoftwarePresent.cpp"
    )

    target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_SOFTWARE_ONLY)

    target_include_directories(${target_name}
        PRIVATE
            "${src_dir}"
            "${project_root}/include"
        INTERFACE
            "$<BUILD_INTERFACE:${project_root}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )

    target_link_libraries(${target_name}
        PRIVATE
            "$<BUILD_INTERFACE:WhatsCanvasGLM>"
            "$<BUILD_INTERFACE:WhatsCanvasSTB>"
            "$<BUILD_INTERFACE:WhatsCanvasPolyline2D>"
    )

    if (BUILD_SHARED_LIBS)
        target_compile_definitions(${target_name} PRIVATE WSC_EXPORTS PUBLIC WSC_SHARED)
        set_target_properties(${target_name} PROPERTIES
            CXX_VISIBILITY_PRESET hidden
            VISIBILITY_INLINES_HIDDEN YES
        )
    endif()

    if (WIN32)
        target_link_libraries(${target_name} PRIVATE gdi32 user32 dwrite d2d1 windowscodecs ole32)
    endif()

    if (UNIX AND NOT APPLE)
        find_package(X11)
        if (X11_FOUND)
            target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_HAS_X11)
            target_include_directories(${target_name} PRIVATE ${X11_INCLUDE_DIR})
            target_link_libraries(${target_name} PRIVATE ${X11_LIBRARIES})
        endif()
    endif()
endfunction()

function(whatscanvas_link_gl_app target_name project_root)
    whatscanvas_add_glfw_dependency("${project_root}")

    target_link_libraries(${target_name}
        PRIVATE
            glfw
            OpenGL::GL
    )

    target_compile_definitions(${target_name} PRIVATE GLEW_STATIC)
endfunction()
