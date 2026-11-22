#include "render/Hud.hpp"

#include <cstddef>

#include "core/Log.hpp"

namespace nc {
namespace {

const char* kHudVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform float uScreenW;
uniform float uScreenH;

out vec2 vUV;
out vec4 vColor;

void main() {
    vec2 ndc = vec2(aPos.x / uScreenW * 2.0 - 1.0, 1.0 - aPos.y / uScreenH * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vColor = aColor;
}
)";

const char* kHudFragmentShader = R"(
#version 330 core
in vec2 vUV;
in vec4 vColor;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    vec4 texel = texture(uTexture, vUV);
    float alpha = texel.a * vColor.a;
    if (alpha < 0.01) {
        discard;
    }
    FragColor = vec4(texel.rgb * vColor.rgb, alpha);
}
)";

const char* kIconVertexShader = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aLayer;
layout(location = 3) in vec4 aColor;

uniform float uScreenW;
uniform float uScreenH;

out vec2 vUV;
out float vLayer;
out vec4 vColor;

void main() {
    vec2 ndc = vec2(aPos.x / uScreenW * 2.0 - 1.0, 1.0 - aPos.y / uScreenH * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
    vUV = aUV;
    vLayer = aLayer;
    vColor = aColor;
}
)";

const char* kIconFragmentShader = R"(
#version 330 core
in vec2 vUV;
in float vLayer;
in vec4 vColor;

uniform sampler2DArray uAtlas;

out vec4 FragColor;

void main() {
    vec4 texel = texture(uAtlas, vec3(vUV, vLayer));
    float alpha = texel.a * vColor.a;
    if (alpha < 0.01) {
        discard;
    }
    FragColor = vec4(texel.rgb * vColor.rgb, alpha);
}
)";

}

bool Hud::init(Assets& assets, TextureArray& textures, std::string* error) {
    textureArray_ = &textures;
    if (!shader_.compile(kHudVertexShader, kHudFragmentShader, error)) {
        return false;
    }
    if (!iconShader_.compile(kIconVertexShader, kIconFragmentShader, error)) {
        return false;
    }

    glGenTextures(1, &whiteTexture_);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    const std::uint8_t whitePixel[4] = {255, 255, 255, 255};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, whitePixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    std::string fontError;
    const auto font = assets.texture("textures/font/ascii.png", &fontError);
    if (font) {
        fontWidth_ = font->width;
        fontHeight_ = font->height;
        glGenTextures(1, &fontTexture_);
        glBindTexture(GL_TEXTURE_2D, fontTexture_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, font->width, font->height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, font->rgba.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        logInfo("font atlas loaded: %dx%d", fontWidth_, fontHeight_);
    } else {
        logWarn("font atlas failed: %s", fontError.c_str());
    }
    glBindTexture(GL_TEXTURE_2D, 0);

    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    gl::GenBuffers(1, &ebo_);

    return true;
}

void Hud::destroy() {
    if (whiteTexture_) {
        glDeleteTextures(1, &whiteTexture_);
        whiteTexture_ = 0;
    }
    if (fontTexture_) {
        glDeleteTextures(1, &fontTexture_);
        fontTexture_ = 0;
    }
    if (vao_) {
        gl::DeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vbo_) {
        gl::DeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    if (ebo_) {
        gl::DeleteBuffers(1, &ebo_);
        ebo_ = 0;
    }
    shader_.destroy();
    iconShader_.destroy();
}

void Hud::begin(int screenWidth, int screenHeight) {
    screenWidth_ = screenWidth;
    screenHeight_ = screenHeight;
    whiteVertices_.clear();
    whiteIndices_.clear();
    fontVertices_.clear();
    fontIndices_.clear();
    iconVertices_.clear();
    iconIndices_.clear();
}

void Hud::addRect(std::vector<UiVertex>& vertices, std::vector<std::uint32_t>& indices,
                  float x, float y, float width, float height, float u0, float v0, float u1,
                  float v1, const Vec3& color, float alpha) {
    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({x, y, u0, v0, color.x, color.y, color.z, alpha});
    vertices.push_back({x + width, y, u1, v0, color.x, color.y, color.z, alpha});
    vertices.push_back({x + width, y + height, u1, v1, color.x, color.y, color.z, alpha});
    vertices.push_back({x, y + height, u0, v1, color.x, color.y, color.z, alpha});
    indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
}

void Hud::drawRect(float x, float y, float width, float height, const Vec3& color, float alpha) {
    addRect(whiteVertices_, whiteIndices_, x, y, width, height, 0.0f, 0.0f, 1.0f, 1.0f, color,
            alpha);
}

float Hud::textWidth(std::string_view text, float scale) const {
    return static_cast<float>(text.size()) * 8.0f * scale;
}

void Hud::drawText(float x, float y, std::string_view text, float scale, const Vec3& color,
                   float alpha) {
    if (!fontTexture_ || fontWidth_ <= 0 || fontHeight_ <= 0) {
        return;
    }
    float cursor = x;
    for (const char rawChar : text) {
        const int code = static_cast<unsigned char>(rawChar);
        if (code >= 32) {
            const int cellX = (code % 16) * 8;
            const int cellY = (code / 16) * 8;
            const float u0 = static_cast<float>(cellX) / static_cast<float>(fontWidth_);
            const float v0 = static_cast<float>(cellY) / static_cast<float>(fontHeight_);
            const float u1 = static_cast<float>(cellX + 8) / static_cast<float>(fontWidth_);
            const float v1 = static_cast<float>(cellY + 8) / static_cast<float>(fontHeight_);
            addRect(fontVertices_, fontIndices_, cursor, y, 8.0f * scale, 8.0f * scale, u0, v0,
                    u1, v1, color, alpha);
        }
        cursor += 8.0f * scale;
    }
}

void Hud::drawBlockIcon(float x, float y, float size, int layer, const Vec3& tint, float alpha) {
    if (!textureArray_ || layer < 0) {
        return;
    }
    const std::uint32_t base = static_cast<std::uint32_t>(iconVertices_.size());
    const float layerValue = static_cast<float>(layer);
    iconVertices_.push_back({x, y, 0.0f, 0.0f, layerValue, tint.x, tint.y, tint.z, alpha});
    iconVertices_.push_back(
        {x + size, y, 1.0f, 0.0f, layerValue, tint.x, tint.y, tint.z, alpha});
    iconVertices_.push_back(
        {x + size, y + size, 1.0f, 1.0f, layerValue, tint.x, tint.y, tint.z, alpha});
    iconVertices_.push_back({x, y + size, 0.0f, 1.0f, layerValue, tint.x, tint.y, tint.z, alpha});
    iconIndices_.insert(iconIndices_.end(),
                        {base, base + 1, base + 2, base, base + 2, base + 3});
}

void Hud::drawCrosshair() {
    const float centerX = static_cast<float>(screenWidth_) * 0.5f;
    const float centerY = static_cast<float>(screenHeight_) * 0.5f;
    drawRect(centerX - 7.0f, centerY - 1.0f, 14.0f, 2.0f, {0.0f, 0.0f, 0.0f}, 0.45f);
    drawRect(centerX - 1.0f, centerY - 7.0f, 2.0f, 14.0f, {0.0f, 0.0f, 0.0f}, 0.45f);
    drawRect(centerX - 6.0f, centerY - 0.5f, 12.0f, 1.0f, {1.0f, 1.0f, 1.0f}, 0.9f);
    drawRect(centerX - 0.5f, centerY - 6.0f, 1.0f, 12.0f, {1.0f, 1.0f, 1.0f}, 0.9f);
}

void Hud::flush(GLuint texture, std::vector<UiVertex>& vertices,
                std::vector<std::uint32_t>& indices) {
    if (vertices.empty() || indices.empty()) {
        return;
    }

    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(vertices.size() * sizeof(UiVertex)), vertices.data(),
                   GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                   indices.data(), GL_DYNAMIC_DRAW);

    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                            reinterpret_cast<void*>(offsetof(UiVertex, x)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                            reinterpret_cast<void*>(offsetof(UiVertex, u)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(UiVertex),
                            reinterpret_cast<void*>(offsetof(UiVertex, r)));

    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);

    gl::BindVertexArray(0);
    vertices.clear();
    indices.clear();
}

void Hud::flushIcons() {
    if (iconVertices_.empty() || !textureArray_) {
        return;
    }
    iconShader_.use();
    iconShader_.setFloat("uScreenW", static_cast<float>(screenWidth_));
    iconShader_.setFloat("uScreenH", static_cast<float>(screenHeight_));
    iconShader_.setInt("uAtlas", 0);

    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(iconVertices_.size() * sizeof(IconVertex)),
                   iconVertices_.data(), GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(iconIndices_.size() * sizeof(std::uint32_t)),
                   iconIndices_.data(), GL_DYNAMIC_DRAW);

    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(IconVertex),
                            reinterpret_cast<void*>(offsetof(IconVertex, x)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(IconVertex),
                            reinterpret_cast<void*>(offsetof(IconVertex, u)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(IconVertex),
                            reinterpret_cast<void*>(offsetof(IconVertex, layer)));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(IconVertex),
                            reinterpret_cast<void*>(offsetof(IconVertex, r)));

    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textureArray_->id());
    glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(iconIndices_.size()), GL_UNSIGNED_INT,
                   nullptr);
    gl::BindVertexArray(0);
    iconVertices_.clear();
    iconIndices_.clear();
}

void Hud::end() {
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    shader_.use();
    shader_.setFloat("uScreenW", static_cast<float>(screenWidth_));
    shader_.setFloat("uScreenH", static_cast<float>(screenHeight_));
    shader_.setInt("uTexture", 0);
    flush(whiteTexture_, whiteVertices_, whiteIndices_);

    flushIcons();

    shader_.use();
    shader_.setFloat("uScreenW", static_cast<float>(screenWidth_));
    shader_.setFloat("uScreenH", static_cast<float>(screenHeight_));
    shader_.setInt("uTexture", 0);
    flush(fontTexture_, fontVertices_, fontIndices_);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glBindTexture(GL_TEXTURE_2D, 0);
    gl::UseProgram(0);
}

}
