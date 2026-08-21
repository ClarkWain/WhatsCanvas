function(whatscanvas_add_common_dependencies project_root)
    find_package(Threads REQUIRED)

    set(third_party_dir "${project_root}/third_party")
    set(glad_path "${third_party_dir}/glad")
    set(stb_path "${third_party_dir}/stb")
    set(glm_path "${third_party_dir}/glm")
    set(freetype_path "${third_party_dir}/freetype")
    set(harfbuzz_path "${third_party_dir}/harfbuzz")

    if (WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
        find_package(glm CONFIG REQUIRED)
        find_package(Stb REQUIRED)
        if ((WHATSCANVAS_BUILD_OPENGL OR WHATSCANVAS_BUILD_OPENGLES))
            find_package(glad CONFIG REQUIRED)
        endif()
    elseif (NOT EXISTS "${glm_path}/glm/glm.hpp" OR
            NOT EXISTS "${stb_path}/stb_image.h" OR
            ((WHATSCANVAS_BUILD_OPENGL OR WHATSCANVAS_BUILD_OPENGLES)
                AND (NOT EXISTS "${glad_path}/src/glad.c" OR
                     NOT EXISTS "${glad_path}/include/glad/glad.h")))
        message(FATAL_ERROR "Missing third-party dependencies. Run: git submodule update --init --recursive")
    endif()

    if (NOT WHATSCANVAS_USE_SYSTEM_DEPENDENCIES
        AND (WHATSCANVAS_BUILD_OPENGL OR WHATSCANVAS_BUILD_OPENGLES)
        AND NOT TARGET WhatsCanvasGLAD)
        add_library(WhatsCanvasGLAD INTERFACE)
        target_include_directories(WhatsCanvasGLAD INTERFACE "${glad_path}/include")
    endif()

    if (NOT TARGET WhatsCanvasGLM)
        add_library(WhatsCanvasGLM INTERFACE)
        if (WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
            target_link_libraries(WhatsCanvasGLM INTERFACE glm::glm)
        else()
            target_include_directories(WhatsCanvasGLM INTERFACE
                "${glm_path}"
                "${glm_path}/glm"
            )
        endif()
    endif()

    if (NOT TARGET WhatsCanvasSTB)
        add_library(WhatsCanvasSTB INTERFACE)
        if (WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
            target_include_directories(WhatsCanvasSTB INTERFACE "${Stb_INCLUDE_DIR}")
        else()
            target_include_directories(WhatsCanvasSTB INTERFACE "${stb_path}")
        endif()
    endif()

    # Do not create the bundled FreeType target when its rasterizer is
    # disabled. HarfBuzz's community CMake build automatically enables its
    # FreeType interop whenever a target named `freetype` already exists,
    # regardless of HB_HAVE_FREETYPE=OFF. Creating that otherwise-unused
    # target therefore leaked `freetype` into harfbuzz's installed link
    # interface while the matching FreeType package was intentionally not
    # installed, leaving exported consumers with a missing target.
    if (NOT WHATSCANVAS_USE_SYSTEM_DEPENDENCIES
        AND WHATSCANVAS_ENABLE_FREETYPE_RASTERIZER
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

    if (NOT WHATSCANVAS_USE_SYSTEM_DEPENDENCIES
        AND WHATSCANVAS_ENABLE_OPENTYPE_SHAPING
        AND EXISTS "${harfbuzz_path}/CMakeLists.txt"
        AND NOT TARGET harfbuzz)
        set(HB_HAVE_CAIRO OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_FREETYPE OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_GRAPHITE2 OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_GLIB OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_ICU OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_GOBJECT OFF CACHE BOOL "" FORCE)
        set(HB_HAVE_INTROSPECTION OFF CACHE BOOL "" FORCE)
        # HarfBuzz's bundled hb-coretext shaper includes ApplicationServices,
        # which trips HarfBuzz's own -Werror=cast-align pragma on recent
        # Command Line Tools SDK headers. We do not use HarfBuzz's CoreText
        # shaper (SystemFontEnumerator queries CoreText directly), so keep
        # the default backend and disable this optional path.
        set(HB_HAVE_CORETEXT OFF CACHE BOOL "" FORCE)
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

function(whatscanvas_link_x11_if_enabled target_name)
    if (NOT UNIX OR APPLE OR WHATSCANVAS_X11 STREQUAL "OFF")
        return()
    endif()

    if (WHATSCANVAS_X11 STREQUAL "ON")
        find_package(X11 REQUIRED)
    else()
        find_package(X11 QUIET)
    endif()

    if (X11_FOUND)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_HAS_X11)
        set_property(TARGET ${target_name} PROPERTY WHATSCANVAS_REQUIRES_X11 ON)
        if (TARGET X11::X11)
            target_link_libraries(${target_name} PRIVATE X11::X11)
        else()
            target_include_directories(${target_name} PRIVATE ${X11_INCLUDE_DIR})
            target_link_libraries(${target_name} PRIVATE ${X11_LIBRARIES})
        endif()
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
    set(options OPENGLES METAL_ONLY)
    cmake_parse_arguments(WSC_GL "${options}" "" "" ${ARGN})

    set(glad_sources)
    set(glad_include_directories)
    if (WSC_GL_METAL_ONLY)
        set(glad_library)
    elseif (WHATSCANVAS_USE_SYSTEM_DEPENDENCIES)
        set(glad_library glad::glad)
    else()
        list(APPEND glad_sources "${glad_path}/src/glad.c")
        list(APPEND glad_include_directories "${glad_path}/include")
        if (NOT TARGET WhatsCanvasGLAD)
            add_library(WhatsCanvasGLAD INTERFACE)
            target_include_directories(WhatsCanvasGLAD INTERFACE "${glad_path}/include")
        endif()
        set(glad_library "$<BUILD_INTERFACE:WhatsCanvasGLAD>")
    endif()

    set(text_shaping_sources)
    set(text_shaping_libraries)
    set(text_rasterizer_libraries)
    if (WHATSCANVAS_ENABLE_OPENTYPE_SHAPING)
        if (NOT TARGET harfbuzz::harfbuzz AND NOT TARGET HarfBuzz::HarfBuzz)
            find_package(harfbuzz CONFIG QUIET)
        endif()
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

    # Optional Metal backend. Like Vulkan, the Metal source (an Objective-C++
    # translation unit on Apple platforms, a small C++ stub elsewhere) is
    # always fed to the compiler so the render device factory can reference
    # MetalRenderDevice unconditionally. The real Metal API path only lights
    # up when the option is ON and we are configuring for an Apple platform.
    set(metal_backend_libraries)
    set(metal_backend_enabled OFF)
    set(metal_backend_source_ext "cpp")
    if (APPLE AND WHATSCANVAS_ENABLE_METAL)
        find_library(WHATSCANVAS_METAL_FRAMEWORK Metal)
        find_library(WHATSCANVAS_FOUNDATION_FRAMEWORK Foundation)
        find_library(WHATSCANVAS_QUARTZ_CORE_FRAMEWORK QuartzCore)
        find_library(WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK CoreGraphics)
        find_library(WHATSCANVAS_APPKIT_FRAMEWORK AppKit)
        find_library(WHATSCANVAS_UIKIT_FRAMEWORK UIKit)
        if (WHATSCANVAS_METAL_FRAMEWORK AND WHATSCANVAS_FOUNDATION_FRAMEWORK
                AND WHATSCANVAS_QUARTZ_CORE_FRAMEWORK)
            set(metal_backend_enabled ON)
            set(metal_backend_source_ext "mm")
            list(APPEND metal_backend_libraries
                "${WHATSCANVAS_METAL_FRAMEWORK}"
                "${WHATSCANVAS_FOUNDATION_FRAMEWORK}"
                "${WHATSCANVAS_QUARTZ_CORE_FRAMEWORK}")
            if (WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK)
                list(APPEND metal_backend_libraries
                    "${WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK}")
            endif()
            if (WHATSCANVAS_APPKIT_FRAMEWORK)
                list(APPEND metal_backend_libraries
                    "${WHATSCANVAS_APPKIT_FRAMEWORK}")
            endif()
            if (WHATSCANVAS_UIKIT_FRAMEWORK)
                list(APPEND metal_backend_libraries
                    "${WHATSCANVAS_UIKIT_FRAMEWORK}")
            endif()
            message(STATUS "WhatsCanvas Metal backend enabled (Metal + Foundation + QuartzCore).")
        else()
            message(STATUS "WHATSCANVAS_ENABLE_METAL is ON, but Metal/Foundation/QuartzCore frameworks were not found. Building the Metal backend as an inert stub.")
        endif()
    endif()

    set(gl_backend_sources)
    if (NOT WSC_GL_METAL_ONLY)
        list(APPEND gl_backend_sources
            ${glad_sources}
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
            "${src_dir}/render/OpenGLRenderDevice.cpp"
            "${src_dir}/render/GLPresent.cpp")
    endif()

    if (WSC_GL_METAL_ONLY)
        set(command_sources "${src_dir}/command/SoftwareCommandStubs.cpp")
        set(gl_renderer_support_sources)
    else()
        set(command_sources
            "${src_dir}/command/DrawCommand.cpp"
            "${src_dir}/command/DrawPoints.cpp"
            "${src_dir}/command/DrawLines.cpp"
            "${src_dir}/command/DrawPath.cpp"
            "${src_dir}/command/DrawImage.cpp"
            "${src_dir}/command/DrawText.cpp")
        set(gl_renderer_support_sources
            "${src_dir}/render/RenderContext.cpp"
            "${src_dir}/render/SpriteBatch.cpp")
    endif()

    if (APPLE)
        set(coretext_backend_source "${src_dir}/text/CoreTextTextBackend.mm")
    else()
        set(coretext_backend_source "${src_dir}/text/CoreTextTextBackend.cpp")
    endif()

    add_library(${target_name}
        ${gl_backend_sources}
        "${src_dir}/core/Log.cpp"
        "${src_dir}/canvas/base.cpp"
        "${src_dir}/canvas/Canvas.cpp"
        "${src_dir}/canvas/Image.cpp"
        "${src_dir}/canvas/Matrix.cpp"
        "${src_dir}/canvas/Paint.cpp"
        "${src_dir}/canvas/Path.cpp"
        "${src_dir}/canvas/StrokeTessellator.cpp"
        "${src_dir}/text/BasicTextBackend.cpp"
        "${src_dir}/text/FontRasterizer.cpp"
        "${src_dir}/text/FontResolver.cpp"
        "${src_dir}/text/GlyphAtlas.cpp"
        "${src_dir}/text/NativeText.cpp"
        "${src_dir}/text/DirectWriteTextBackend.cpp"
        "${coretext_backend_source}"
        "${src_dir}/text/SystemFontEnumerator.cpp"
        "${src_dir}/text/platform/AndroidFontConfig.cpp"
        "${src_dir}/text/platform/AndroidFontProvider.cpp"
        "${src_dir}/text/TextShaper.cpp"
        "${src_dir}/text/TextUtils.cpp"
        "${src_dir}/text/UnicodeBidi.cpp"
        ${text_shaping_sources}
        ${command_sources}
        "${src_dir}/render/CommandDrawListEncoder.cpp"
        "${src_dir}/render/FrameCompiler.cpp"
        "${src_dir}/render/FilterChain.cpp"
        ${gl_renderer_support_sources}
        "${src_dir}/render/RenderDeviceFactory.cpp"
        "${src_dir}/render/RenderTargetPool.cpp"
        "${src_dir}/render/Renderer.cpp"
        "${src_dir}/render/software/SoftwareRenderer.cpp"
        "${src_dir}/render/software/SoftwarePresent.cpp"
        "${src_dir}/render/vulkan/VulkanRenderDevice.cpp"
        "${src_dir}/render/metal/MetalRenderDevice.${metal_backend_source_ext}"
    )

    if (WSC_GL_OPENGLES)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_OPENGL_ES)
    endif()
    if (WSC_GL_METAL_ONLY)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_METAL_ONLY)
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
    set_property(TARGET ${target_name} PROPERTY
        WHATSCANVAS_VULKAN_BACKEND_ENABLED "${vulkan_backend_enabled}")
    set_property(GLOBAL PROPERTY
        WHATSCANVAS_VULKAN_BACKEND_ENABLED "${vulkan_backend_enabled}")

    if (metal_backend_enabled)
        target_compile_definitions(${target_name} PRIVATE WHATSCANVAS_ENABLE_METAL)
        target_link_libraries(${target_name} PRIVATE ${metal_backend_libraries})
        # The Metal backend source is Objective-C++ on Apple platforms. Enable
        # ARC and modules only for that one translation unit so the rest of the
        # library (plain C++) is untouched.
        set_source_files_properties(
            "${src_dir}/render/metal/MetalRenderDevice.mm"
            "${src_dir}/text/CoreTextTextBackend.mm"
            PROPERTIES
                COMPILE_FLAGS "-fobjc-arc -fmodules")
    endif()
    set_property(TARGET ${target_name} PROPERTY
        WHATSCANVAS_METAL_BACKEND_ENABLED "${metal_backend_enabled}")
    set_property(GLOBAL PROPERTY
        WHATSCANVAS_METAL_BACKEND_ENABLED "${metal_backend_enabled}")

    target_include_directories(${target_name}
        PRIVATE
            "${src_dir}"
            "${project_root}/include"
            ${glad_include_directories}
        INTERFACE
            "$<BUILD_INTERFACE:${project_root}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    if (NOT WSC_GL_METAL_ONLY)
        target_compile_definitions(${target_name} PRIVATE GLEW_STATIC)
    endif()
    target_link_libraries(${target_name}
        PRIVATE
            "$<BUILD_INTERFACE:WhatsCanvasGLM>"
            ${glad_library}
            "$<BUILD_INTERFACE:WhatsCanvasSTB>"
            Threads::Threads
            ${text_shaping_libraries}
            ${text_rasterizer_libraries}
    )

    if (WSC_GL_METAL_ONLY)
        # Standalone Metal builds intentionally have no OpenGL/OpenGLES link.
    elseif (NOT WSC_GL_OPENGLES)
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

    if (APPLE)
        find_library(WHATSCANVAS_CORETEXT_FRAMEWORK CoreText)
        if (WHATSCANVAS_CORETEXT_FRAMEWORK)
            target_link_libraries(${target_name} PRIVATE "${WHATSCANVAS_CORETEXT_FRAMEWORK}")
        endif()
        if (NOT DEFINED WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK
                OR NOT WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK)
            find_library(WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK CoreGraphics)
        endif()
        if (WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK)
            target_link_libraries(${target_name} PRIVATE
                "${WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK}")
        endif()
        if (NOT DEFINED WHATSCANVAS_FOUNDATION_FRAMEWORK OR NOT WHATSCANVAS_FOUNDATION_FRAMEWORK)
            find_library(WHATSCANVAS_FOUNDATION_FRAMEWORK Foundation)
        endif()
        if (WHATSCANVAS_FOUNDATION_FRAMEWORK)
            target_link_libraries(${target_name} PRIVATE "${WHATSCANVAS_FOUNDATION_FRAMEWORK}")
        endif()
    endif()

    if (UNIX AND NOT APPLE AND NOT EMSCRIPTEN)
        find_package(PkgConfig QUIET)
        if (PkgConfig_FOUND)
            pkg_check_modules(WHATSCANVAS_FONTCONFIG QUIET fontconfig)
            if (WHATSCANVAS_FONTCONFIG_FOUND)
                target_link_libraries(${target_name} PRIVATE ${WHATSCANVAS_FONTCONFIG_LIBRARIES})
                target_include_directories(${target_name} PRIVATE ${WHATSCANVAS_FONTCONFIG_INCLUDE_DIRS})
            endif()
        endif()
    endif()

    whatscanvas_link_x11_if_enabled(${target_name})
endfunction()

function(whatscanvas_add_opengl_library target_name project_root)
    whatscanvas_add_gl_family_library(${target_name} "${project_root}")
endfunction()

function(whatscanvas_add_opengles_library target_name project_root)
    whatscanvas_add_gl_family_library(${target_name} "${project_root}" OPENGLES)
endfunction()

function(whatscanvas_add_metal_library target_name project_root)
    whatscanvas_add_gl_family_library(${target_name} "${project_root}" METAL_ONLY)
endfunction()

# A dependency-free, CPU-only rendering library. It contains the same public
# Canvas API but is built with WHATSCANVAS_SOFTWARE_ONLY so every OpenGL /
# Vulkan / glad code path is compiled out. The draw commands are reduced to
# pure data carriers (SoftwareCommandStubs.cpp) consumed by the CPU
# SoftwareRenderer, so the resulting binary links no GPU libraries and runs in
# fully head-less environments. Text uses the built-in stb_truetype rasterizer.
function(whatscanvas_add_software_library target_name project_root)
    set(src_dir "${project_root}/src")

    if (APPLE)
        set(coretext_backend_source "${src_dir}/text/CoreTextTextBackend.mm")
    else()
        set(coretext_backend_source "${src_dir}/text/CoreTextTextBackend.cpp")
    endif()

    add_library(${target_name}
        "${src_dir}/core/Log.cpp"
        "${src_dir}/canvas/base.cpp"
        "${src_dir}/canvas/Canvas.cpp"
        "${src_dir}/canvas/Image.cpp"
        "${src_dir}/canvas/Matrix.cpp"
        "${src_dir}/canvas/Paint.cpp"
        "${src_dir}/canvas/Path.cpp"
        "${src_dir}/canvas/StrokeTessellator.cpp"
        "${src_dir}/text/BasicTextBackend.cpp"
        "${src_dir}/text/FontRasterizer.cpp"
        "${src_dir}/text/FontResolver.cpp"
        "${src_dir}/text/GlyphAtlas.cpp"
        "${src_dir}/text/NativeText.cpp"
        "${src_dir}/text/DirectWriteTextBackend.cpp"
        "${coretext_backend_source}"
        "${src_dir}/text/SystemFontEnumerator.cpp"
        "${src_dir}/text/platform/AndroidFontConfig.cpp"
        "${src_dir}/text/platform/AndroidFontProvider.cpp"
        "${src_dir}/text/TextShaper.cpp"
        "${src_dir}/text/TextUtils.cpp"
        "${src_dir}/text/UnicodeBidi.cpp"
        "${src_dir}/command/SoftwareCommandStubs.cpp"
        "${src_dir}/render/FilterChain.cpp"
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
            Threads::Threads
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

    if (APPLE)
        set_source_files_properties(
            "${src_dir}/text/CoreTextTextBackend.mm"
            PROPERTIES COMPILE_FLAGS "-fobjc-arc -fmodules")
        find_library(WHATSCANVAS_CORETEXT_FRAMEWORK CoreText)
        if (WHATSCANVAS_CORETEXT_FRAMEWORK)
            target_link_libraries(${target_name} PRIVATE "${WHATSCANVAS_CORETEXT_FRAMEWORK}")
        endif()
        if (NOT DEFINED WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK
                OR NOT WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK)
            find_library(WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK CoreGraphics)
        endif()
        if (WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK)
            target_link_libraries(${target_name} PRIVATE
                "${WHATSCANVAS_CORE_GRAPHICS_FRAMEWORK}")
        endif()
        if (NOT DEFINED WHATSCANVAS_FOUNDATION_FRAMEWORK OR NOT WHATSCANVAS_FOUNDATION_FRAMEWORK)
            find_library(WHATSCANVAS_FOUNDATION_FRAMEWORK Foundation)
        endif()
        if (WHATSCANVAS_FOUNDATION_FRAMEWORK)
            target_link_libraries(${target_name} PRIVATE "${WHATSCANVAS_FOUNDATION_FRAMEWORK}")
        endif()
    endif()

    if (UNIX AND NOT APPLE)
        find_package(PkgConfig QUIET)
        if (PkgConfig_FOUND)
            pkg_check_modules(WHATSCANVAS_FONTCONFIG QUIET fontconfig)
            if (WHATSCANVAS_FONTCONFIG_FOUND)
                target_link_libraries(${target_name} PRIVATE ${WHATSCANVAS_FONTCONFIG_LIBRARIES})
                target_include_directories(${target_name} PRIVATE ${WHATSCANVAS_FONTCONFIG_INCLUDE_DIRS})
            endif()
        endif()
    endif()

    whatscanvas_link_x11_if_enabled(${target_name})
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
