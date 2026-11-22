#pragma once

#include <array>
#include <string>

#include "assets/Assets.hpp"
#include "core/Math.hpp"
#include "render/Environment.hpp"
#include "render/GL.hpp"
#include "render/Shader.hpp"

namespace nc {

struct PlayerRenderState {
    Vec3 position{};
    float bodyYaw = 0.0f;
    float headPitch = 0.0f;
    float walkPhase = 0.0f;
    float walkAmount = 0.0f;
    float attackSwing = 0.0f;
    bool onGround = true;
    bool inWater = false;
};

class PlayerRenderer {
public:
    bool init(Assets& assets, std::string* error = nullptr);
    void destroy();

    void render(const Mat4& viewProjection, const Vec3& cameraPosition,
                const Environment& environment, float fogStart, float fogEnd,
                const PlayerRenderState& state);
    void renderShadow(const Mat4& lightViewProjection, const PlayerRenderState& state);

public:
    struct UvRect {
        float u0;
        float v0;
        float u1;
        float v1;
    };

private:
    struct Part {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        int indexCount = 0;
        Vec3 pivot{};
    };

    enum PartIndex {
        Body = 0,
        Head,
        ArmLeft,
        ArmRight,
        LegLeft,
        LegRight,
        PartCount,
    };

    bool createBox(Part& part, const Vec3& min, const Vec3& max,
                   const std::array<UvRect, 6>& faces, const Vec3& pivot);
    void drawPart(const Part& part, const Mat4& model, Shader& shader);
    std::array<Mat4, PartCount> buildPartMatrices(const PlayerRenderState& state) const;

    Shader shader_;
    Shader shadowShader_;
    GLuint skinTexture_ = 0;
    std::array<Part, PartCount> parts_{};
};

}
