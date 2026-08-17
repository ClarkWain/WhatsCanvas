#include "SpriteBatch.h"

#include <algorithm>
#include <glad/glad.h>
#include <cstdint>
#include <glm/gtc/matrix_transform.hpp>

#include "opengl/GLProgram.h"
#include "opengl/GLShaderSource.h"
#include "render/RenderContext.h"
#include "render/RenderTypes.h"

SpriteBatch::SpriteBatch()
{
}

SpriteBatch::~SpriteBatch()
{
    releaseGLResources();
}

void SpriteBatch::beginFrame()
{
    endBatch();
    boundTextures_.fill(0u);
}

void SpriteBatch::endBatch()
{
    if (boundProgram_ == nullptr) {
        return;
    }
    if (boundProgram_ == program_) {
#if !defined(WHATSCANVAS_OPENGL_ES)
        for (std::size_t slot = 0;
             slot < boundSamplerCount_; ++slot) {
            glBindSampler(static_cast<GLuint>(slot), 0);
        }
#endif
    }
    glActiveTexture(GL_TEXTURE0);
    glBindVertexArray(0);
    boundProgram_ = nullptr;
    boundSamplerCount_ = 0;
    boundTextures_.fill(0u);
}

void SpriteBatch::setTexture(std::shared_ptr<ImageResource> texture)
{
    texture_ = std::move(texture);
    textures_.clear();
    if (texture_) {
        textures_.push_back(texture_);
    }
}

int SpriteBatch::addTexture(
    const std::shared_ptr<ImageResource> &texture)
{
    if (!texture || !texture->isValid()
        || !texture->nativeHandle().isValid()) {
        return -1;
    }
    const auto existing = std::find(
        textures_.begin(), textures_.end(), texture);
    if (existing != textures_.end()) {
        return static_cast<int>(
            std::distance(textures_.begin(), existing));
    }
    if (textures_.size() >= kMaxTextures) {
        return -1;
    }
    textures_.push_back(texture);
    if (!texture_) {
        texture_ = texture;
    }
    return static_cast<int>(textures_.size() - 1u);
}

void SpriteBatch::add(float x, float y, float width, float height,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a,
                       const glm::mat4 &transform, float roundedRadius,
                       int textureSlot)
{
    // Apply transform to the 4 corner positions.
    const glm::vec4 tl = transform * glm::vec4(x, y, 0.0f, 1.0f);
    const glm::vec4 tr = transform * glm::vec4(x + width, y, 0.0f, 1.0f);
    const glm::vec4 br = transform * glm::vec4(x + width, y + height, 0.0f, 1.0f);
    const glm::vec4 bl = transform * glm::vec4(x, y + height, 0.0f, 1.0f);

    // Four corners are shared by the two indexed triangles.
    vertexData_.insert(vertexData_.end(), {
        tl.x, tl.y, u0, v0, r, g, b, a,
        0.0f, 0.0f, roundedRadius, width, height,
        static_cast<float>(textureSlot)});
    vertexData_.insert(vertexData_.end(), {
        tr.x, tr.y, u1, v0, r, g, b, a,
        1.0f, 0.0f, roundedRadius, width, height,
        static_cast<float>(textureSlot)});
    vertexData_.insert(vertexData_.end(), {
        br.x, br.y, u1, v1, r, g, b, a,
        1.0f, 1.0f, roundedRadius, width, height,
        static_cast<float>(textureSlot)});
    vertexData_.insert(vertexData_.end(), {
        bl.x, bl.y, u0, v1, r, g, b, a,
        0.0f, 1.0f, roundedRadius, width, height,
        static_cast<float>(textureSlot)});
}

void SpriteBatch::addInstance(
    float x, float y, float width, float height,
    float u0, float v0, float u1, float v1,
    float r, float g, float b, float a,
    const glm::mat4 &transform)
{
    const glm::vec4 tl =
        transform * glm::vec4(x, y, 0.0f, 1.0f);
    const glm::vec4 br =
        transform
        * glm::vec4(x + width, y + height, 0.0f, 1.0f);

    instanceData_.insert(instanceData_.end(), {
        tl.x, tl.y, br.x, br.y,
        u0, v0, u1, v1,
        r, g, b, a});
}

void SpriteBatch::flush(RenderContext &context, DrawBlendMode blendMode)
{
    const bool instanced =
#if defined(WHATSCANVAS_OPENGL_ES)
        false;
#else
        !instanceData_.empty();
#endif
    if (empty()
        || (instanced
            ? (!texture_ || !texture_->isValid())
            : textures_.empty())) {
        return;
    }

    ensureGLInitialized();

    GLProgram *activeProgram =
        instanced ? instanceProgram_ : program_;
    if (activeProgram == nullptr) {
        return;
    }

    // SpriteBatch currently batches only unclipped images. Apply state
    // explicitly instead of inheriting GL state left by the previous command.
    context.applyBlendMode(blendMode);
    context.applyClipState(ScissorState{}, ClipMaskState{});

    if (boundProgram_ != activeProgram) {
        endBatch();
        activeProgram->use();
        const glm::mat4 projection = glm::ortho(
            0.0f, static_cast<float>(context.getWidth()),
            static_cast<float>(context.getHeight()), 0.0f);
        activeProgram->setMat4("uProjection", projection);
        if (instanced) {
            if (!instanceSamplerInitialized_) {
                activeProgram->setInt("uTexture", 0);
                instanceSamplerInitialized_ = true;
            }
            glBindVertexArray(instanceVAO_);
        } else {
            if (!samplerUniformsInitialized_) {
                for (std::size_t slot = 0;
                     slot < kMaxTextures; ++slot) {
                    activeProgram->setInt(
                        "uTexture" + std::to_string(slot),
                        static_cast<int>(slot));
                }
                samplerUniformsInitialized_ = true;
            }
            glBindVertexArray(VAO_);
        }
        boundProgram_ = activeProgram;
    }

    const std::size_t sprites = spriteCount();
    if (instanced) {
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(
                instanceData_.size() * sizeof(float)),
            instanceData_.data(), GL_DYNAMIC_DRAW);
    } else {
        glBindBuffer(GL_ARRAY_BUFFER, VBO_);
        ensureIndexCapacity(sprites);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(
                vertexData_.size() * sizeof(float)),
            vertexData_.data(), GL_DYNAMIC_DRAW);
    }

    if (instanced) {
        context.bindImageResource(
            texture_, DrawImageSampling::Linear,
            DrawImageTileMode::Clamp, false);
    } else {
        for (std::size_t slot = 0;
             slot < textures_.size(); ++slot) {
            const GLuint handle = static_cast<GLuint>(
                textures_[slot]->nativeHandle().value);
            if (boundTextures_[slot] != handle) {
                glActiveTexture(
                    GL_TEXTURE0 + static_cast<GLenum>(slot));
                glBindTexture(GL_TEXTURE_2D, handle);
                boundTextures_[slot] = handle;
            }
#if !defined(WHATSCANVAS_OPENGL_ES)
            if (slot >= boundSamplerCount_) {
                glBindSampler(
                    static_cast<GLuint>(slot), sampler_);
            }
#endif
        }
        boundSamplerCount_ =
            std::max(boundSamplerCount_, textures_.size());
        glActiveTexture(GL_TEXTURE0);
        context.invalidateImageBinding();
    }

    if (instanced) {
        glDrawArraysInstanced(
            GL_TRIANGLE_STRIP, 0, 4,
            static_cast<GLsizei>(sprites));
    } else {
        glDrawElements(
            GL_TRIANGLES, static_cast<GLsizei>(sprites * 6u),
            GL_UNSIGNED_INT, nullptr);
    }
}

void SpriteBatch::clear()
{
    texture_.reset();
    textures_.clear();
    vertexData_.clear();
    instanceData_.clear();
}

void SpriteBatch::ensureGLInitialized()
{
    if (glInitialized_) {
        return;
    }

    const std::string vertexSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec2 aPos;
        layout (location = 1) in vec2 aUv;
        layout (location = 2) in vec4 aColor;
        layout (location = 3) in vec2 aQuadUv;
        layout (location = 4) in vec3 aRounded;
        layout (location = 5) in float aTextureSlot;

        uniform mat4 uProjection;

        out vec2 vUv;
        out vec4 vColor;
        out vec2 vQuadUv;
        out vec3 vRounded;
        flat out int vTextureSlot;

        void main()
        {
            gl_Position = uProjection * vec4(aPos, 0.0, 1.0);
            vUv = aUv;
            vColor = aColor;
            vQuadUv = aQuadUv;
            vRounded = aRounded;
            vTextureSlot = int(aTextureSlot + 0.5);
        }
    )";

    const std::string fragmentSrc = std::string(wsc::opengl::shaderVersionDirective()) + R"(
        in vec2 vUv;
        in vec4 vColor;
        in vec2 vQuadUv;
        in vec3 vRounded;
        flat in int vTextureSlot;

        uniform sampler2D uTexture0;
        uniform sampler2D uTexture1;
        uniform sampler2D uTexture2;
        uniform sampler2D uTexture3;
        uniform sampler2D uTexture4;
        uniform sampler2D uTexture5;
        uniform sampler2D uTexture6;
        uniform sampler2D uTexture7;

        out vec4 FragColor;

        float roundedRectCoverage()
        {
            float radius = vRounded.x;
            if (radius <= 0.0) {
                return 1.0;
            }
            vec2 size = vRounded.yz;
            vec2 halfSize = size * 0.5;
            radius = min(radius, min(halfSize.x, halfSize.y));
            vec2 point = vQuadUv * size;
            vec2 q = abs(point - halfSize) - (halfSize - vec2(radius));
            float distanceToEdge =
                length(max(q, vec2(0.0)))
                + min(max(q.x, q.y), 0.0) - radius;
            float aa = max(fwidth(distanceToEdge), 0.0001);
            return smoothstep(aa * 0.5, -aa * 0.5, distanceToEdge);
        }

        vec4 sampleBatchTexture()
        {
            if (vTextureSlot == 0) return texture(uTexture0, vUv);
            if (vTextureSlot == 1) return texture(uTexture1, vUv);
            if (vTextureSlot == 2) return texture(uTexture2, vUv);
            if (vTextureSlot == 3) return texture(uTexture3, vUv);
            if (vTextureSlot == 4) return texture(uTexture4, vUv);
            if (vTextureSlot == 5) return texture(uTexture5, vUv);
            if (vTextureSlot == 6) return texture(uTexture6, vUv);
            return texture(uTexture7, vUv);
        }

        void main()
        {
            FragColor = sampleBatchTexture() * vColor;
            FragColor.a *= roundedRectCoverage();
        }
    )";

    program_ = new GLProgram(vertexSrc, fragmentSrc);
    const std::string instanceVertexSrc =
        std::string(wsc::opengl::shaderVersionDirective()) + R"(
        layout (location = 0) in vec4 aBounds;
        layout (location = 1) in vec4 aUvRect;
        layout (location = 2) in vec4 aColor;

        uniform mat4 uProjection;

        out vec2 vUv;
        out vec4 vColor;

        void main()
        {
            int vertex = gl_VertexID & 3;
            vec2 corner;
            if (vertex == 0) {
                corner = vec2(0.0, 0.0);
            } else if (vertex == 1) {
                corner = vec2(1.0, 0.0);
            } else if (vertex == 2) {
                corner = vec2(0.0, 1.0);
            } else {
                corner = vec2(1.0, 1.0);
            }
            vec2 position =
                mix(aBounds.xy, aBounds.zw, corner);
            gl_Position =
                uProjection * vec4(position, 0.0, 1.0);
            vUv = mix(aUvRect.xy, aUvRect.zw, corner);
            vColor = aColor;
        }
    )";
    const std::string instanceFragmentSrc =
        std::string(wsc::opengl::shaderVersionDirective()) + R"(
        in vec2 vUv;
        in vec4 vColor;

        uniform sampler2D uTexture;

        out vec4 FragColor;

        void main()
        {
            FragColor = texture(uTexture, vUv) * vColor;
        }
    )";
#if !defined(WHATSCANVAS_OPENGL_ES)
    instanceProgram_ =
        new GLProgram(instanceVertexSrc, instanceFragmentSrc);
#endif

    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);
    glGenBuffers(1, &EBO_);
#if !defined(WHATSCANVAS_OPENGL_ES)
    glGenSamplers(1, &sampler_);
    glSamplerParameteri(
        sampler_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(
        sampler_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glSamplerParameteri(
        sampler_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glSamplerParameteri(
        sampler_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
#endif
#if !defined(WHATSCANVAS_OPENGL_ES)
    glGenVertexArrays(1, &instanceVAO_);
    glGenBuffers(1, &instanceVBO_);
#endif

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_);

    // Position: 2 floats at offset 0.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // UV: 2 floats at offset 8.
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Color: 4 floats at offset 16.
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 14 * sizeof(float), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glVertexAttribPointer(
        3, 2, GL_FLOAT, GL_FALSE, 14 * sizeof(float),
        (void *)(8 * sizeof(float)));
    glEnableVertexAttribArray(3);

    glVertexAttribPointer(
        4, 3, GL_FLOAT, GL_FALSE, 14 * sizeof(float),
        (void *)(10 * sizeof(float)));
    glEnableVertexAttribArray(4);

    glVertexAttribPointer(
        5, 1, GL_FLOAT, GL_FALSE, 14 * sizeof(float),
        (void *)(13 * sizeof(float)));
    glEnableVertexAttribArray(5);

#if !defined(WHATSCANVAS_OPENGL_ES)
    glBindVertexArray(instanceVAO_);
    glBindBuffer(GL_ARRAY_BUFFER, instanceVBO_);
    constexpr GLsizei instanceStride = 12 * sizeof(float);
    for (GLuint attribute = 0; attribute < 3; ++attribute) {
        glVertexAttribPointer(
            attribute, 4, GL_FLOAT, GL_FALSE,
            instanceStride,
            reinterpret_cast<void *>(
                static_cast<std::uintptr_t>(
                    attribute * 4u * sizeof(float))));
        glEnableVertexAttribArray(attribute);
        glVertexAttribDivisor(attribute, 1);
    }
#endif

    glBindVertexArray(0);
    glInitialized_ = true;
}

void SpriteBatch::ensureIndexCapacity(std::size_t spriteCount)
{
    if (spriteCount <= indexSpriteCapacity_) {
        return;
    }

    std::size_t nextCapacity =
        std::max<std::size_t>(256u, indexSpriteCapacity_);
    while (nextCapacity < spriteCount) {
        nextCapacity *= 2u;
    }

    std::vector<std::uint32_t> indices(nextCapacity * 6u);
    for (std::size_t sprite = 0; sprite < nextCapacity; ++sprite) {
        const std::uint32_t vertex =
            static_cast<std::uint32_t>(sprite * 4u);
        const std::size_t index = sprite * 6u;
        indices[index + 0u] = vertex + 0u;
        indices[index + 1u] = vertex + 1u;
        indices[index + 2u] = vertex + 2u;
        indices[index + 3u] = vertex + 0u;
        indices[index + 4u] = vertex + 2u;
        indices[index + 5u] = vertex + 3u;
    }

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO_);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
        indices.data(), GL_STATIC_DRAW);
    indexSpriteCapacity_ = nextCapacity;
}

void SpriteBatch::abandonGLResources()
{
    releaseGLResources(true);
}

void SpriteBatch::releaseGLResources(bool abandon)
{
    if (!abandon) {
        endBatch();
    }
    if (program_ != nullptr) {
        if (abandon) program_->abandonVolatile();
        delete program_;
        program_ = nullptr;
    }
    if (instanceProgram_ != nullptr) {
        if (abandon) instanceProgram_->abandonVolatile();
        delete instanceProgram_;
        instanceProgram_ = nullptr;
    }

    if (VAO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteVertexArrays(1, &VAO_);
        VAO_ = static_cast<unsigned int>(-1);
    }

    if (VBO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteBuffers(1, &VBO_);
        VBO_ = static_cast<unsigned int>(-1);
    }
    if (EBO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteBuffers(1, &EBO_);
        EBO_ = static_cast<unsigned int>(-1);
    }
    if (sampler_ != static_cast<unsigned int>(-1)) {
#if !defined(WHATSCANVAS_OPENGL_ES)
        if (!abandon) glDeleteSamplers(1, &sampler_);
#endif
        sampler_ = static_cast<unsigned int>(-1);
    }
    if (instanceVAO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteVertexArrays(1, &instanceVAO_);
        instanceVAO_ = static_cast<unsigned int>(-1);
    }
    if (instanceVBO_ != static_cast<unsigned int>(-1)) {
        if (!abandon) glDeleteBuffers(1, &instanceVBO_);
        instanceVBO_ = static_cast<unsigned int>(-1);
    }
    indexSpriteCapacity_ = 0;
    boundProgram_ = nullptr;
    boundSamplerCount_ = 0;
    boundTextures_.fill(0u);
    samplerUniformsInitialized_ = false;
    instanceSamplerInitialized_ = false;

    texture_.reset();
    textures_.clear();

    glInitialized_ = false;
}
