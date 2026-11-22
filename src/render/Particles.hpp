#pragma once

#include <string>
#include <vector>

#include "core/Math.hpp"
#include "render/Environment.hpp"
#include "render/GL.hpp"
#include "render/Shader.hpp"

namespace nc {

struct Particle {
    Vec3 position{};
    Vec3 velocity{};
    Vec3 startColor{1.0f, 1.0f, 1.0f};
    Vec3 endColor{1.0f, 1.0f, 1.0f};
    float life = 1.0f;
    float maxLife = 1.0f;
    float startSize = 0.2f;
    float endSize = 0.2f;
    float gravity = 0.0f;
    float drag = 0.0f;
};

class ParticleRenderer {
public:
    bool init(std::string* error = nullptr);
    void destroy();

    void render(const Mat4& viewProjection, const Vec3& cameraPosition,
                const Environment& environment, float fogStart, float fogEnd,
                int screenHeight, const std::vector<Particle>& particles);

private:
    struct Vertex {
        float x;
        float y;
        float z;
        float size;
        float r;
        float g;
        float b;
        float a;
    };

    Shader shader_;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
};

}
