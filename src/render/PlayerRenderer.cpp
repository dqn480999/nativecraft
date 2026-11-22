#include "render/PlayerRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

#include "core/Log.hpp"

namespace nc {
namespace {

const char* kSkinVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uViewProj;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 world = uModel * vec4(aPos, 1.0);
    gl_Position = uViewProj * world;
    vWorldPos = world.xyz;
    vNormal = normalize(mat3(uModel) * aNormal);
    vUV = aUV;
}
)";

const char* kSkinFragmentShader = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform sampler2D uSkin;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

out vec4 FragColor;

void main() {
    vec4 texel = texture(uSkin, vUV);
    if (texel.a < 0.1) {
        discard;
    }
    vec3 normal = normalize(vNormal);
    float sun = max(dot(normal, uSunDir), 0.0);
    vec3 ambient = mix(uAmbientGround, uAmbientSky, normal.y * 0.5 + 0.5);
    vec3 color = texel.rgb * (ambient + uSunColor * sun);
    float distanceToCamera = length(vWorldPos - uCameraPos);
    float fog = clamp((distanceToCamera - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
    FragColor = vec4(mix(color, uFogColor, fog), 1.0);
}
)";

const char* kPlayerShadowVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uLightVP;
void main() {
    gl_Position = uLightVP * (uModel * vec4(aPos, 1.0));
}
)";

const char* kPlayerShadowFragmentShader = R"(
#version 330 core
void main() {
}
)";

PlayerRenderer::UvRect rect(int x, int y, int width, int height) {
    constexpr float size = 64.0f;
    return {x / size, y / size, (x + width) / size, (y + height) / size};
}

}

bool PlayerRenderer::init(Assets& assets, std::string* error) {
    if (!shader_.compile(kSkinVertexShader, kSkinFragmentShader, error)) {
        return false;
    }
    if (!shadowShader_.compile(kPlayerShadowVertexShader, kPlayerShadowFragmentShader,
                               error)) {
        return false;
    }

    std::string textureError;
    auto skin = assets.texture("textures/entity/steve.png", &textureError);
    if (!skin) {
        logWarn("player skin failed: %s", textureError.c_str());
        Image fallback;
        fallback.width = 64;
        fallback.height = 64;
        fallback.rgba.resize(64 * 64 * 4);
        for (std::size_t i = 0; i < fallback.rgba.size(); i += 4) {
            fallback.rgba[i] = 210;
            fallback.rgba[i + 1] = 160;
            fallback.rgba[i + 2] = 130;
            fallback.rgba[i + 3] = 255;
        }
        skin = fallback;
    }

    glGenTextures(1, &skinTexture_);
    glBindTexture(GL_TEXTURE_2D, skinTexture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, skin->width, skin->height, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, skin->rgba.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);


    const std::array<UvRect, 6> headFaces = {
        rect(0, 8, 8, 8), rect(16, 8, 8, 8), rect(8, 0, 8, 8),
        rect(16, 0, 8, 8), rect(24, 8, 8, 8), rect(8, 8, 8, 8),
    };
    if (!createBox(parts_[Head], {-0.25f, 0.0f, -0.25f}, {0.25f, 0.5f, 0.25f},
                   headFaces, {0.0f, 1.5f, 0.0f})) {
        return false;
    }

    const std::array<UvRect, 6> bodyFaces = {
        rect(16, 20, 4, 12), rect(28, 20, 4, 12), rect(20, 16, 8, 4),
        rect(28, 16, 8, 4), rect(32, 20, 8, 12), rect(20, 20, 8, 12),
    };
    if (!createBox(parts_[Body], {-0.25f, 0.0f, -0.125f}, {0.25f, 0.75f, 0.125f},
                   bodyFaces, {0.0f, 0.75f, 0.0f})) {
        return false;
    }

    const std::array<UvRect, 6> rightArmFaces = {
        rect(40, 20, 4, 12), rect(48, 20, 4, 12), rect(44, 16, 4, 4),
        rect(48, 16, 4, 4), rect(52, 20, 4, 12), rect(44, 20, 4, 12),
    };
    if (!createBox(parts_[ArmRight], {-0.125f, -0.75f, -0.125f}, {0.125f, 0.0f, 0.125f},
                   rightArmFaces, {0.375f, 1.5f, 0.0f})) {
        return false;
    }

    const std::array<UvRect, 6> leftArmFaces = {
        rect(32, 52, 4, 12), rect(40, 52, 4, 12), rect(36, 48, 4, 4),
        rect(40, 48, 4, 4), rect(44, 52, 4, 12), rect(36, 52, 4, 12),
    };
    if (!createBox(parts_[ArmLeft], {-0.125f, -0.75f, -0.125f}, {0.125f, 0.0f, 0.125f},
                   leftArmFaces, {-0.375f, 1.5f, 0.0f})) {
        return false;
    }

    const std::array<UvRect, 6> rightLegFaces = {
        rect(0, 20, 4, 12), rect(8, 20, 4, 12), rect(4, 16, 4, 4),
        rect(8, 16, 4, 4), rect(12, 20, 4, 12), rect(4, 20, 4, 12),
    };
    if (!createBox(parts_[LegRight], {-0.125f, -0.75f, -0.125f}, {0.125f, 0.0f, 0.125f},
                   rightLegFaces, {0.125f, 0.75f, 0.0f})) {
        return false;
    }

    const std::array<UvRect, 6> leftLegFaces = {
        rect(16, 52, 4, 12), rect(24, 52, 4, 12), rect(20, 48, 4, 4),
        rect(24, 48, 4, 4), rect(28, 52, 4, 12), rect(20, 52, 4, 12),
    };
    if (!createBox(parts_[LegLeft], {-0.125f, -0.75f, -0.125f}, {0.125f, 0.0f, 0.125f},
                   leftLegFaces, {-0.125f, 0.75f, 0.0f})) {
        return false;
    }

    return true;
}

bool PlayerRenderer::createBox(Part& part, const Vec3& min, const Vec3& max,
                               const std::array<UvRect, 6>& faces, const Vec3& pivot) {
    struct Vertex {
        float x;
        float y;
        float z;
        float nx;
        float ny;
        float nz;
        float u;
        float v;
    };

    const Vec3 corners[6][4] = {
        {{max.x, min.y, max.z}, {max.x, min.y, min.z}, {max.x, max.y, min.z},
         {max.x, max.y, max.z}},
        {{min.x, min.y, min.z}, {min.x, min.y, max.z}, {min.x, max.y, max.z},
         {min.x, max.y, min.z}},
        {{min.x, max.y, min.z}, {min.x, max.y, max.z}, {max.x, max.y, max.z},
         {max.x, max.y, min.z}},
        {{min.x, min.y, min.z}, {max.x, min.y, min.z}, {max.x, min.y, max.z},
         {min.x, min.y, max.z}},
        {{min.x, min.y, max.z}, {max.x, min.y, max.z}, {max.x, max.y, max.z},
         {min.x, max.y, max.z}},
        {{max.x, min.y, min.z}, {min.x, min.y, min.z}, {min.x, max.y, min.z},
         {max.x, max.y, min.z}},
    };
    const Vec3 normals[6] = {
        {1.0f, 0.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f},
        {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f, -1.0f},
    };

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    for (int face = 0; face < 6; ++face) {
        const UvRect& uv = faces[static_cast<std::size_t>(face)];
        const float us[4] = {uv.u0, uv.u1, uv.u1, uv.u0};
        const float vs[4] = {uv.v1, uv.v1, uv.v0, uv.v0};
        const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
        for (int i = 0; i < 4; ++i) {
            Vertex vertex;
            vertex.x = corners[face][i].x;
            vertex.y = corners[face][i].y;
            vertex.z = corners[face][i].z;
            vertex.nx = normals[face].x;
            vertex.ny = normals[face].y;
            vertex.nz = normals[face].z;
            vertex.u = us[i];
            vertex.v = vs[i];
            vertices.push_back(vertex);
        }
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }

    part.pivot = pivot;
    part.indexCount = static_cast<int>(indices.size());
    gl::GenVertexArrays(1, &part.vao);
    gl::GenBuffers(1, &part.vbo);
    gl::GenBuffers(1, &part.ebo);
    gl::BindVertexArray(part.vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, part.vbo);
    gl::BufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                   GL_STATIC_DRAW);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, part.ebo);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                   indices.data(), GL_STATIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, nx)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, u)));
    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return true;
}

std::array<Mat4, PlayerRenderer::PartCount> PlayerRenderer::buildPartMatrices(
    const PlayerRenderState& state) const {
    const float walk = state.walkAmount;
    const float idle = std::sin(state.time * 1.7f);
    const float bob = std::sin(state.walkPhase * 2.0f) * 0.02f * walk + idle * 0.004f;
    const float sway = std::sin(state.walkPhase) * 1.2f * walk;
    const Mat4 base = Mat4::translation(state.position + Vec3{0.0f, bob, 0.0f})
                    * Mat4::rotationY(radians(-state.bodyYaw + sway))
                    * Mat4::rotationX(radians(-walk * 2.5f))
                    * Mat4::scale(0.9f);

    float swing = std::sin(state.walkPhase) * 0.75f * walk;
    float leftArm = -swing * 0.85f;
    float rightArm = swing * 0.85f;
    float leftLeg = swing;
    float rightLeg = -swing;
    float leftArmOut = radians(-3.5f - walk * 2.0f - idle * 1.2f);
    float rightArmOut = radians(3.5f + walk * 2.0f + idle * 1.2f);

    if (state.inWater) {
        const float swim = std::sin(state.walkPhase * 1.4f) * 0.4f;
        leftArm = -swim * 1.2f;
        rightArm = swim * 1.2f;
        leftLeg = swim;
        rightLeg = -swim;
        leftArmOut = radians(-12.0f);
        rightArmOut = radians(12.0f);
    } else if (!state.onGround) {
        const float rise = clampf(state.verticalVelocity / 8.0f, -1.0f, 1.0f);
        leftArm = -0.55f + rise * 0.35f;
        rightArm = -0.55f + rise * 0.35f;
        leftLeg = 0.30f + rise * 0.15f;
        rightLeg = -0.30f - rise * 0.15f;
        leftArmOut = radians(-9.0f);
        rightArmOut = radians(9.0f);
    }

    leftArm += std::sin(state.time * 1.5f) * 0.02f * (1.0f - walk);
    rightArm -= std::sin(state.time * 1.5f) * 0.02f * (1.0f - walk);

    if (state.attackSwing > 0.0f) {
        const float attack = std::sin(3.1415926f * clampf(state.attackSwing, 0.0f, 1.0f))
                           * 1.6f;
        rightArm -= attack;
        rightArmOut += radians(4.0f);
    }

    const float headYaw = (walk < 0.12f && state.attackSwing <= 0.0f)
                            ? std::sin(state.time * 0.35f) * 2.0f
                            : 0.0f;

    std::array<Mat4, PartCount> matrices{};
    matrices[Body] = base * Mat4::translation({0.0f, 0.75f, 0.0f});
    matrices[Head] = base * Mat4::translation({0.0f, 1.5f, 0.0f})
                   * Mat4::rotationY(radians(headYaw))
                   * Mat4::rotationX(radians(state.headPitch));
    matrices[ArmLeft] = base * Mat4::translation({-0.375f, 1.5f, 0.0f})
                      * Mat4::rotationZ(leftArmOut)
                      * Mat4::rotationX(leftArm);
    matrices[ArmRight] = base * Mat4::translation({0.375f, 1.5f, 0.0f})
                       * Mat4::rotationZ(rightArmOut)
                       * Mat4::rotationX(rightArm);
    matrices[LegLeft] = base * Mat4::translation({-0.125f, 0.75f, 0.0f})
                      * Mat4::rotationX(leftLeg);
    matrices[LegRight] = base * Mat4::translation({0.125f, 0.75f, 0.0f})
                       * Mat4::rotationX(rightLeg);
    return matrices;
}
void PlayerRenderer::drawPart(const Part& part, const Mat4& model, Shader& shader) {
    if (part.vao == 0) {
        return;
    }
    shader.setMat4("uModel", model);
    gl::BindVertexArray(part.vao);
    glDrawElements(GL_TRIANGLES, part.indexCount, GL_UNSIGNED_INT, nullptr);
}

void PlayerRenderer::render(const Mat4& viewProjection, const Vec3& cameraPosition,
                            const Environment& environment, float fogStart, float fogEnd,
                            const PlayerRenderState& state) {
    if (!skinTexture_) {
        return;
    }
    shader_.use();
    shader_.setMat4("uViewProj", viewProjection);
    shader_.setVec3("uCameraPos", cameraPosition);
    shader_.setVec3("uSunDir", environment.sunDirection);
    shader_.setVec3("uSunColor", environment.sunColor);
    shader_.setVec3("uAmbientSky", environment.ambientSky);
    shader_.setVec3("uAmbientGround", environment.ambientGround);
    shader_.setVec3("uFogColor", environment.fogColor);
    shader_.setFloat("uFogStart", fogStart);
    shader_.setFloat("uFogEnd", fogEnd);
    shader_.setInt("uSkin", 0);

    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, skinTexture_);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glDisable(GL_BLEND);

    const auto matrices = buildPartMatrices(state);
    for (int i = 0; i < PartCount; ++i) {
        drawPart(parts_[i], matrices[i], shader_);
    }

    gl::BindVertexArray(0);
    gl::UseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void PlayerRenderer::renderShadow(const Mat4& lightViewProjection,
                                  const PlayerRenderState& state) {
    shadowShader_.use();
    shadowShader_.setMat4("uLightVP", lightViewProjection);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    const auto matrices = buildPartMatrices(state);
    for (int i = 0; i < PartCount; ++i) {
        drawPart(parts_[i], matrices[i], shadowShader_);
    }
    gl::BindVertexArray(0);
    gl::UseProgram(0);
}

void PlayerRenderer::destroy() {
    for (Part& part : parts_) {
        if (part.vao) {
            gl::DeleteVertexArrays(1, &part.vao);
            part.vao = 0;
        }
        if (part.vbo) {
            gl::DeleteBuffers(1, &part.vbo);
            part.vbo = 0;
        }
        if (part.ebo) {
            gl::DeleteBuffers(1, &part.ebo);
            part.ebo = 0;
        }
    }
    if (skinTexture_) {
        glDeleteTextures(1, &skinTexture_);
        skinTexture_ = 0;
    }
    shader_.destroy();
    shadowShader_.destroy();
}

}
