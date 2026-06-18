function(whatscanvas_add_common_dependencies project_root)
    set(third_party_dir "${project_root}/third_party")
    set(glfw_path "${third_party_dir}/glfw")
    set(glad_path "${third_party_dir}/glad")
    set(stb_path "${third_party_dir}/stb")
    set(glm_path "${third_party_dir}/glm")
    set(polyline2d_path "${third_party_dir}/polyline2d")

    if (NOT EXISTS "${glfw_path}/CMakeLists.txt" OR
        NOT EXISTS "${glm_path}/glm/glm.hpp" OR
        NOT EXISTS "${stb_path}/stb_image.h" OR
        NOT EXISTS "${polyline2d_path}/include/Polyline2D.h")
        message(FATAL_ERROR "Missing third-party dependencies. Run: git submodule update --init --recursive")
    endif()

    find_package(OpenGL REQUIRED)

    if (NOT TARGET glfw)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory("${glfw_path}" "${CMAKE_CURRENT_BINARY_DIR}/third_party/glfw" EXCLUDE_FROM_ALL)
    endif()

    if (NOT TARGET GLAD)
        add_library(GLAD STATIC "${glad_path}/src/glad.c")
        target_include_directories(GLAD PUBLIC "${glad_path}/include")
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
endfunction()

function(whatscanvas_add_opengl_library target_name project_root)
    set(src_dir "${project_root}/src")

    add_library(${target_name}
        "${src_dir}/canvas/Canvas.cpp"
        "${src_dir}/canvas/Image.cpp"
        "${src_dir}/canvas/Paint.cpp"
        "${src_dir}/text/BasicTextBackend.cpp"
        "${src_dir}/text/NativeText.cpp"
        "${src_dir}/text/TextUtils.cpp"
        "${src_dir}/opengl/GLTextureUtils.cpp"
        "${src_dir}/opengl/GLProgram.cpp"
        "${src_dir}/opengl/GLVertexArray.cpp"
        "${src_dir}/command/DrawPoints.cpp"
        "${src_dir}/command/DrawLines.cpp"
        "${src_dir}/command/DrawPath.cpp"
        "${src_dir}/command/DrawImage.cpp"
        "${src_dir}/command/DrawText.cpp"
        "${src_dir}/render/RenderContext.cpp"
        "${src_dir}/render/OpenGLRenderDevice.cpp"
        "${src_dir}/render/Renderer.cpp"
    )

    target_include_directories(${target_name}
        PRIVATE
            "${src_dir}"
        INTERFACE
            "$<BUILD_INTERFACE:${project_root}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    )
    target_compile_definitions(${target_name} PUBLIC GLEW_STATIC)
    target_link_libraries(${target_name}
        PUBLIC
            WhatsCanvasGLM
            WhatsCanvasSTB
            WhatsCanvasPolyline2D
        PRIVATE
            glfw
            GLAD
            OpenGL::GL
    )

    if (WIN32 AND BUILD_SHARED_LIBS)
        set_target_properties(${target_name} PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
    endif()

    if (WIN32)
        target_link_libraries(${target_name} PRIVATE gdi32)
    endif()
endfunction()

function(whatscanvas_link_gl_app target_name)
    target_link_libraries(${target_name}
        PRIVATE
            glfw
            GLAD
            OpenGL::GL
    )

    target_compile_definitions(${target_name} PRIVATE GLEW_STATIC)
endfunction()