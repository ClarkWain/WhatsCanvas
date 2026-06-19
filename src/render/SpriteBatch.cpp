#include "SpriteBatch.h"

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>

#include "command/DrawImage.h"
#include "render/RenderContext.h"
#include "render/RenderTypes.h"

SpriteBatch::SpriteBatch()
{
}

SpriteBatch::~SpriteBatch()
{
    if (glInitialized_) {
        if (VAO_ != static_cast<unsigned int>(-1)) {
            glDeleteVertexArrays(1, &VAO_);
        }
        if (VBO_ != static_cast<unsigned int>(-1)) {
            glDeleteBuffers(1, &VBO_);
        }
    }
}

void SpriteBatch::setTexture(std::shared_ptr<ImageResource> texture)
{
    texture_ = std::move(texture);
}

void SpriteBatch::add(float x, float y, float width, float height,
                       float u0, float v0, float u1, float v1,
                       float r, float g, float b, float a,
                       const glm::mat4 &transform)
{
    // Apply transform to the 4 corner positions.
    const glm::vec4 tl = transform * glm::vec4(x, y, 0.0f, 1.0f);
    const glm::vec4 tr = transform * glm::vec4(x + width, y, 0.0f, 1.0f);
    const glm::vec4 br = transform * glm::vec4(x + width, y + height, 0.0f, 1.0f);
    const glm::vec4 bl = transform * glm::vec4(x, y + height, 0.0f, 1.0f);

    // Triangle 1: tl, tr, br
    vertexData_.insert(vertexData_.end(), {tl.x, tl.y, u0, v0, r, g, b, a});
    vertexData_.insert(vertexData_.end(), {tr.x, tr.y, u1, v0, r, g, b, a});
    vertexData_.insert(vertexData_.end(), {br.x, br.y, u1, v1, r, g, b, a});

    // Triangle 2: tl, br, bl
    vertexData_.insert(vertexData_.end(), {tl.x, tl.y, u0, v0, r, g, b, a});
    vertexData_.insert(vertexData_.end(), {br.x, br.y, u1, v1, r, g, b, a});
    vertexData_.insert(vertexData_.end(), {bl.x, bl.y, u0, v1, r, g, b, a});
}

void SpriteBatch::flush(RenderContext &context)
{
    if (vertexData_.empty() || !texture_ || !texture_->isValid()) {
        return;
    }

    ensureGLInitialized();

    // Use DrawImageProgram for rendering.
    DrawImageProgram *program = DrawImageProgram::getInstance();
    // The SpriteBatch uses its own shader path — for now, we'll
    // submit individual DrawImageCommands. A proper implementation
    // would use a dedicated sprite shader. This is a simplified version
    // that demonstrates the batch concept.

    // Upload vertex data.
    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(vertexData_.size() * sizeof(float)),
                 vertexData_.data(),
                 GL_DYNAMIC_DRAW);

    // Bind texture.
    context.bindImageResource(texture_, DrawImageSampling::Linear, DrawImageTileMode::Clamp, false);

    // Draw all sprites in one call.
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertexData_.size() / 8));

    glBindVertexArray(0);
}

void SpriteBatch::clear()
{
    vertexData_.clear();
}

void SpriteBatch::ensureGLInitialized()
{
    if (glInitialized_) {
        return;
    }

    glGenVertexArrays(1, &VAO_);
    glGenBuffers(1, &VBO_);

    glBindVertexArray(VAO_);
    glBindBuffer(GL_ARRAY_BUFFER, VBO_);

    // Position: 2 floats at offset 0.
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(0);

    // UV: 2 floats at offset 8.
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);

    // Color: 4 floats at offset 16.
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void *)(4 * sizeof(float)));
    glEnableVertexAttribArray(2);

    glBindVertexArray(0);
    glInitialized_ = true;
}
