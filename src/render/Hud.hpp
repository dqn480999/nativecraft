#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "assets/Assets.hpp"
#include "core/Math.hpp"
#include "render/GL.hpp"
#include "render/Shader.hpp"
#include "render/TextureArray.hpp"

namespace nc {

class Hud {
public:
    bool init(Assets& assets, TextureArray& textures, std::string* error = nullptr);
    void destroy();

    void begin(int screenWidth, int screenHeight);
    void end();

    void drawRect(float x, float y, float width, float height, const Vec3& color,
                  float alpha = 1.0f);
    void drawText(float x, float y, std::string_view text, float scale, const Vec3& color,
                  float alpha = 1.0f);
    float textWidth(std::string_view text, float scale) const;
    void drawCrosshair();
    void drawBlockIcon(float x, float y, float size, int layer, const Vec3& tint,
                       float alpha = 1.0f);

private:
    struct UiVertex {
        float x;
        float y;
        float u;
        float v;
        float r;
        float g;
        float b;
        float a;
    };

    struct IconVertex {
        float x;
        float y;
        float u;
        float v;
        float layer;
        float r;
        float g;
        float b;
        float a;
    };

    void addRect(std::vector<UiVertex>& vertices, std::vector<std::uint32_t>& indices,
                 float x, float y, float width, float height, float u0, float v0, float u1,
                 float v1, const Vec3& color, float alpha);
    void flush(GLuint texture, std::vector<UiVertex>& vertices,
               std::vector<std::uint32_t>& indices);
    void flushIcons();

    Shader shader_;
    Shader iconShader_;
    TextureArray* textureArray_ = nullptr;
    GLuint whiteTexture_ = 0;
    GLuint fontTexture_ = 0;
    int fontWidth_ = 0;
    int fontHeight_ = 0;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint ebo_ = 0;
    int screenWidth_ = 1;
    int screenHeight_ = 1;

    std::vector<UiVertex> whiteVertices_;
    std::vector<std::uint32_t> whiteIndices_;
    std::vector<UiVertex> fontVertices_;
    std::vector<std::uint32_t> fontIndices_;
    std::vector<IconVertex> iconVertices_;
    std::vector<std::uint32_t> iconIndices_;
};

}
