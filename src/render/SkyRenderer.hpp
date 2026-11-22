#pragma once

#include <string>

#include "core/Math.hpp"
#include "render/Environment.hpp"
#include "render/GL.hpp"
#include "render/Shader.hpp"

namespace nc {

class SkyRenderer {
public:
    bool init(std::string* error = nullptr);
    void destroy();

    void render(const Mat4& inverseViewProjection, const Vec3& cameraPosition,
                const Environment& environment, float cloudTime);

private:
    Shader shader_;
    GLuint vao_ = 0;
};

}
