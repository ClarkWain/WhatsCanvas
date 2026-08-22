#include "DrawImage.h"

#include <iostream>
#include <string>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "DrawValidation.h"
#include "opengl/GlobalIndexBuffers.h"
#include "opengl/GLShaderSource.h"
#include "opengl/ClipMaskUniforms.h"
#include "render/GammaCorrect.h"

DrawImageProgram *DrawImageProgram::instance_ = nullptr;

DrawImageProgram::DrawImageProgram()
{
}

DrawImageProgram::~DrawImageProgram()
{
    release();
}

void DrawImageProgram::initialize(bool commonProgram)
{
    GLProgram *&requestedProgram = commonProgram
        ? commonProgram_ : program_;
    if (requestedProgram != nullptr) {
        return;
    }

    const std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        #ifdef WHATSCANVAS_OPENGL_ES
        precision highp float;
        precision highp int;
        #endif
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;

        uniform mat4 uProjection;
        uniform mat4 uTransform;

        out vec2 vUv;
        out vec2 vLocalPos;

        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            vUv = aUv;
            vLocalPos = aPos;
        }
    )";

    const std::string clearTypeOutputs =
#if defined(WHATSCANVAS_OPENGL_ES)
        "out vec4 FragColor;\n";
#else
        // GL 3.3 core dual-source output.  The secondary output is only read
        // while RenderContext has selected the ClearType blend function.
        "layout(location = 0, index = 0) out vec4 FragColor;\n"
        "layout(location = 0, index = 1) out vec4 FragBlend;\n";
#endif

    const std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective())
        + (commonProgram ? "#define WHATSCANVAS_COMMON_IMAGE 1\n" : "")
        + wsc::opengl::clipMaskFragmentUniforms() + R"(
        #ifdef WHATSCANVAS_OPENGL_ES
        precision highp float;
        precision highp int;
        #endif
        in vec2 vUv;
        in vec2 vLocalPos;

        uniform sampler2D uTexture;
        uniform vec4 uTintColor;
        uniform float uAlpha;
        #if !defined(WHATSCANVAS_OPENGL_ES)
        uniform bool uClearTypeMask;
        uniform bool uRgbCoverageFallback;
        #endif
        uniform bool uSourcePremultiplied;
        #if !defined(WHATSCANVAS_COMMON_IMAGE)
        uniform bool uUseColorMatrix;
        uniform mat4 uColorMatrix;
        uniform vec4 uColorMatrixOffset;
        uniform int uTileMode;
        #endif
        uniform int uGradientType;
        uniform int uGradientTileMode;
        uniform vec2 uLinearStart;
        uniform vec2 uLinearEnd;
        uniform vec2 uRadialCenter;
        uniform float uRadialRadius;
        uniform int uGradientStopCount;
        uniform float uGradientStopPositions[8];
        uniform vec4 uGradientStopColors[8];
        uniform bool uRoundedClip;
        uniform vec2 uRoundedSize;
        uniform float uRoundedRadius;

        float applyGradientTile(float t, out float visibility)
        {
            visibility = 1.0;
            if (uGradientTileMode == 1) {
                return fract(t);
            }
            if (uGradientTileMode == 2) {
                float period = floor(t);
                float localT = t - period;
                if (mod(abs(period), 2.0) > 0.5) {
                    localT = 1.0 - localT;
                }
                return localT;
            }
            if (uGradientTileMode == 3) {
                visibility = (t >= 0.0 && t <= 1.0) ? 1.0 : 0.0;
                return clamp(t, 0.0, 1.0);
            }
            return clamp(t, 0.0, 1.0);
        }

        vec4 sampleGradient(float t)
        {
            float visibility = 1.0;
            t = applyGradientTile(t, visibility);
            if (visibility <= 0.0 || uGradientStopCount <= 0) {
                return vec4(0.0);
            }
            if (uGradientStopCount == 1 || t <= uGradientStopPositions[0]) {
                return uGradientStopColors[0] * visibility;
            }
            for (int i = 1; i < 8; ++i) {
                if (i >= uGradientStopCount) {
                    break;
                }
                if (t <= uGradientStopPositions[i]) {
                    float startPos = uGradientStopPositions[i - 1];
                    float endPos = uGradientStopPositions[i];
                    float span = max(endPos - startPos, 0.0001);
                    float localT = clamp((t - startPos) / span, 0.0, 1.0);
                    return mix(uGradientStopColors[i - 1], uGradientStopColors[i], localT) * visibility;
                }
            }
            return uGradientStopColors[uGradientStopCount - 1] * visibility;
        }

        float roundedRectCoverage()
        {
            if (!uRoundedClip) {
                return 1.0;
            }
            vec2 halfSize = uRoundedSize * 0.5;
            float radius = min(uRoundedRadius, min(halfSize.x, halfSize.y));
            vec2 point = vUv * uRoundedSize;
            vec2 q = abs(point - halfSize) - (halfSize - vec2(radius));
            float distanceToEdge =
                length(max(q, vec2(0.0))) + min(max(q.x, q.y), 0.0) - radius;
            float aa = max(fwidth(distanceToEdge), 0.0001);
            return smoothstep(aa * 0.5, -aa * 0.5, distanceToEdge);
        }

        void main()
        {
            #if !defined(WHATSCANVAS_COMMON_IMAGE)
            if (uTileMode == 3 && (vUv.x < 0.0 || vUv.x > 1.0 || vUv.y < 0.0 || vUv.y > 1.0)) {
                FragColor = vec4(0.0);
#if !defined(WHATSCANVAS_OPENGL_ES)
                FragBlend = vec4(0.0);
#endif
                return;
            }
            #endif

            vec4 texColor = texture(uTexture, vUv);
            float roundedCoverage = roundedRectCoverage();
            if (uSourcePremultiplied && texColor.a > 0.000001) {
                texColor.rgb /= texColor.a;
            }
            vec4 paintColor = uTintColor;
            if (uGradientType == 1) {
                vec2 direction = uLinearEnd - uLinearStart;
                float lengthSq = max(dot(direction, direction), 0.0001);
                float t = dot(vLocalPos - uLinearStart, direction) / lengthSq;
                paintColor = sampleGradient(t);
            } else if (uGradientType == 2) {
                float t = length(vLocalPos - uRadialCenter) / max(uRadialRadius, 0.0001);
                paintColor = sampleGradient(t);
            }
            #if !defined(WHATSCANVAS_OPENGL_ES)
            if (uClearTypeMask) {
                // `texColor.rgb` is DirectWrite's independent R/G/B LCD
                // coverage. Keep it separate from the ordinary alpha path;
                // the fixed-function dual-source blend combines it with the
                // opaque destination per channel.
                vec3 coverage =
                    texColor.rgb * paintColor.a * uAlpha * roundedCoverage;
                if (uClipEnabled != 0) {
                    coverage *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
                }
                FragColor = vec4(paintColor.rgb * coverage, 0.0);
#if !defined(WHATSCANVAS_OPENGL_ES)
                FragBlend = vec4(coverage, 1.0);
#endif
                return;
            }
            if (uRgbCoverageFallback) {
                // LCD masks are not ordinary colored images. When the target
                // is not known opaque or dual-source blending is unavailable,
                // collapse the RGB coverage to the backend-provided alpha and
                // render a conventional grayscale mask. This keeps colored
                // text correct instead of multiplying coverage twice.
                vec4 color = vec4(
                    paintColor.rgb,
                    texColor.a * paintColor.a * uAlpha * roundedCoverage);
                #if !defined(WHATSCANVAS_COMMON_IMAGE)
                if (uUseColorMatrix) {
                    color = clamp(uColorMatrix * color + uColorMatrixOffset, 0.0, 1.0);
                }
                #endif
                if (uClipEnabled != 0) {
                    color.a *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
                }
                FragColor = color;
#if !defined(WHATSCANVAS_OPENGL_ES)
                FragBlend = vec4(0.0);
#endif
                return;
            }
            #endif
            vec4 color = vec4(
                texColor.rgb * paintColor.rgb,
                texColor.a * paintColor.a * uAlpha * roundedCoverage);
            #if !defined(WHATSCANVAS_COMMON_IMAGE)
            if (uUseColorMatrix) {
                color = clamp(uColorMatrix * color + uColorMatrixOffset, 0.0, 1.0);
            }
            #endif
            if (uClipEnabled != 0) {
                color.a *= texture(uClipMask, gl_FragCoord.xy / uClipViewport).r;
            }
            FragColor = color;
        }
    )";

    // Insert the declaration after the user uniforms, before helper functions.
    std::string resolvedFragmentSrc = fragmentSrc;
    resolvedFragmentSrc.insert(resolvedFragmentSrc.find("        float applyGradientTile"), clearTypeOutputs);

    requestedProgram = new GLProgram(
        commonProgram ? "draw_image_common" : "draw_image",
        vertexSrc, resolvedFragmentSrc);

    if (!initialized_) {
        glGenVertexArrays(1, &VAO_);
        vertexBuffer_.initialize(16);

        glBindVertexArray(VAO_);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_.handle());

        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindVertexArray(0);

        initialized_ = true;
    }
}

void DrawImageProgram::initializeFast(bool premultiplied)
{
    GLProgram *&requestedProgram = premultiplied
        ? fastPremultipliedProgram_ : fastStraightProgram_;
    if (requestedProgram != nullptr) {
        return;
    }

    const std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        #ifdef WHATSCANVAS_OPENGL_ES
        precision highp float;
        #endif
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        uniform mat4 uProjection;
        uniform mat4 uTransform;
        out vec2 vUv;
        void main()
        {
            gl_Position = uProjection * uTransform * vec4(aPos, 0.0, 1.0);
            vUv = aUv;
        }
    )";

    const std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective())
        + (premultiplied ? "#define WHATSCANVAS_FAST_PREMULTIPLIED 1\n" : "")
        + R"(
        #ifdef WHATSCANVAS_OPENGL_ES
        precision mediump float;
        #endif
        in vec2 vUv;
        uniform sampler2D uTexture;
        uniform vec4 uTintColor;
        uniform float uAlpha;
        out vec4 FragColor;
        void main()
        {
            vec4 texColor = texture(uTexture, vUv);
            #ifdef WHATSCANVAS_FAST_PREMULTIPLIED
            if (texColor.a > 0.000001) {
                texColor.rgb /= texColor.a;
            }
            #endif
            FragColor = vec4(
                texColor.rgb * uTintColor.rgb,
                texColor.a * uTintColor.a * uAlpha);
        }
    )";

    requestedProgram = new GLProgram(
        premultiplied ? "draw_image_fast_premul" : "draw_image_fast",
        vertexSrc, fragmentSrc);

    if (!initialized_) {
        glGenVertexArrays(1, &VAO_);
        vertexBuffer_.initialize(16);
        glBindVertexArray(VAO_);
        glBindBuffer(GL_ARRAY_BUFFER, vertexBuffer_.handle());
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                              4 * sizeof(float), (void *)0);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                              4 * sizeof(float), (void *)(2 * sizeof(float)));
        glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        initialized_ = true;
    }
}

void DrawImageProgram::release(bool abandon)
{
    if (!initialized_) {
        return;
    }

    if (program_ != nullptr) {
        if (abandon) program_->abandonVolatile();
        delete program_;
        program_ = nullptr;
    }
    if (commonProgram_ != nullptr) {
        if (abandon) commonProgram_->abandonVolatile();
        delete commonProgram_;
        commonProgram_ = nullptr;
    }
    if (fastStraightProgram_ != nullptr) {
        if (abandon) fastStraightProgram_->abandonVolatile();
        delete fastStraightProgram_;
        fastStraightProgram_ = nullptr;
    }
    if (fastPremultipliedProgram_ != nullptr) {
        if (abandon) fastPremultipliedProgram_->abandonVolatile();
        delete fastPremultipliedProgram_;
        fastPremultipliedProgram_ = nullptr;
    }

    if (VAO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteVertexArrays(1, &VAO_);
        VAO_ = static_cast<unsigned int>(-1);
    }

    if (abandon) vertexBuffer_.abandon(); else vertexBuffer_.release();

    initialized_ = false;
}

void DrawImageProgram::draw(const RenderContext &context, const DrawImageData &data)
{
    const bool useFastProgram =
        !data.hasColorMatrix
        && !data.clearTypeMask
        && !data.rgbCoverageMask
        && data.tileMode == DrawImageTileMode::Clamp
        && data.gradientType == DrawGradientType::None
        && !data.hasRoundedCorners()
        && !context.isClipMaskActive();
    const bool sourcePremultiplied =
        data.imageResource
        && data.imageResource->alphaType() == ImageAlphaType::Premultiplied;
    const bool useCommonProgram =
#if defined(WHATSCANVAS_OPENGL_ES)
        !useFastProgram
        && !data.hasColorMatrix && data.tileMode != DrawImageTileMode::Decal;
#else
        false;
#endif
    if (useFastProgram) {
        initializeFast(sourcePremultiplied);
    } else {
        initialize(useCommonProgram);
    }
    GLProgram *activeProgram = useFastProgram
        ? (sourcePremultiplied
            ? fastPremultipliedProgram_ : fastStraightProgram_)
        : (useCommonProgram ? commonProgram_ : program_);
    if (!DrawValidation::validateProgram(
            initialized_ && activeProgram != nullptr,
            "DrawImageProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateResource(data.imageResource, "imageResource", "DrawImageProgram::draw")) {
        return;
    }

    if (!DrawValidation::validateImageDimensions(data.width, data.height, "DrawImageProgram::draw")) {
        return;
    }

    const float left = data.x;
    const float top = data.y;
    const float right = data.x + data.width;
    const float bottom = data.y + data.height;

    const float vertices[] = {
        left,  top,    data.u0, data.v0,
        right, top,    data.u1, data.v0,
        right, bottom, data.u1, data.v1,
        left,  bottom, data.u0, data.v1
    };

    activeProgram->use();
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(context.getWidth()),
                                            static_cast<float>(context.getHeight()), 0.0f);
    activeProgram->setMat4("uProjection", projection);
    activeProgram->setMat4("uTransform", data.transform);
    float tintColor[4] = {data.tintColor[0], data.tintColor[1], data.tintColor[2], data.tintColor[3]};
    GammaCorrect::srgbToLinear4(tintColor);
    activeProgram->setVec4("uTintColor", glm::vec4(tintColor[0], tintColor[1], tintColor[2], tintColor[3]));
    activeProgram->setFloat("uAlpha", data.alpha);
    const bool roundedClip = data.hasRoundedCorners();
    if (!useFastProgram) {
        activeProgram->setInt("uRoundedClip", roundedClip ? 1 : 0);
    }
    if (!useFastProgram && roundedClip) {
        activeProgram->setVec2(
            "uRoundedSize", glm::vec2(data.width, data.height));
        activeProgram->setFloat("uRoundedRadius", data.roundedRadius);
    }
#if !defined(WHATSCANVAS_OPENGL_ES)
    activeProgram->setInt("uClearTypeMask",
                     data.clearTypeMask && context.isClearTypeBlendModeActive() ? 1 : 0);
    activeProgram->setInt("uRgbCoverageFallback",
                     data.rgbCoverageMask && !context.isClearTypeBlendModeActive() ? 1 : 0);
#endif
    if (!useFastProgram) {
        activeProgram->setInt(
            "uSourcePremultiplied", sourcePremultiplied ? 1 : 0);
    }
    if (!useFastProgram && !useCommonProgram) {
        activeProgram->setInt("uUseColorMatrix", data.hasColorMatrix ? 1 : 0);
        if (data.hasColorMatrix) {
            const glm::mat4 colorMatrix(
                data.colorMatrix[0], data.colorMatrix[1], data.colorMatrix[2], data.colorMatrix[3],
                data.colorMatrix[4], data.colorMatrix[5], data.colorMatrix[6], data.colorMatrix[7],
                data.colorMatrix[8], data.colorMatrix[9], data.colorMatrix[10], data.colorMatrix[11],
                data.colorMatrix[12], data.colorMatrix[13], data.colorMatrix[14], data.colorMatrix[15]);
            activeProgram->setMat4("uColorMatrix", colorMatrix);
            activeProgram->setVec4("uColorMatrixOffset", glm::vec4(
                data.colorMatrixOffset[0], data.colorMatrixOffset[1],
                data.colorMatrixOffset[2], data.colorMatrixOffset[3]));
        }
        int tileMode = 0;
        if (data.tileMode == DrawImageTileMode::Repeat) {
            tileMode = 1;
        } else if (data.tileMode == DrawImageTileMode::Mirror) {
            tileMode = 2;
        } else if (data.tileMode == DrawImageTileMode::Decal) {
            tileMode = 3;
        }
        activeProgram->setInt("uTileMode", tileMode);
    }
    if (!useFastProgram) {
        activeProgram->setInt(
            "uGradientType", static_cast<int>(data.gradientType));
    }
    if (!useFastProgram && data.gradientType != DrawGradientType::None) {
        activeProgram->setInt(
            "uGradientTileMode", static_cast<int>(data.gradientTileMode));
        if (data.gradientType == DrawGradientType::Linear) {
            activeProgram->setVec2(
                "uLinearStart",
                glm::vec2(data.gradientStart[0], data.gradientStart[1]));
            activeProgram->setVec2(
                "uLinearEnd",
                glm::vec2(data.gradientEnd[0], data.gradientEnd[1]));
        } else {
            activeProgram->setVec2(
                "uRadialCenter",
                glm::vec2(data.radialCenter[0], data.radialCenter[1]));
            activeProgram->setFloat("uRadialRadius", data.radialRadius);
        }
        activeProgram->setInt(
            "uGradientStopCount", data.gradientStopCount);
        for (std::size_t i = 0;
             i < DrawImageData::kMaxGradientStops
             && static_cast<int>(i) < data.gradientStopCount; ++i) {
            activeProgram->setFloat(
                "uGradientStopPositions[" + std::to_string(i) + "]",
                data.gradientStopPositions[i]);
            float stopColor[4] = {
                data.gradientStopColors[i * 4 + 0],
                data.gradientStopColors[i * 4 + 1],
                data.gradientStopColors[i * 4 + 2],
                data.gradientStopColors[i * 4 + 3]
            };
            GammaCorrect::srgbToLinear4(stopColor);
            activeProgram->setVec4(
                "uGradientStopColors[" + std::to_string(i) + "]",
                glm::vec4(
                    stopColor[0], stopColor[1],
                    stopColor[2], stopColor[3]));
        }
    }
    activeProgram->setInt("uTexture", 0);
    if (!useFastProgram) {
        wsc::opengl::applyClipMaskUniforms(activeProgram, context);
    }

    context.bindImageResource(data.imageResource, data.sampling, data.tileMode, data.mipmapsReady);

    glBindVertexArray(VAO_);
    vertexBuffer_.upload(vertices, 16);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GlobalIndexBuffers::quadBuffer());
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}
