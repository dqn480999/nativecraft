#include "render/Particles.hpp"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace nc {
namespace {

const char* kParticleVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in float aSize;
layout(location = 2) in vec4 aColor;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;
uniform float uScreenHeight;
uniform float uFogStart;
uniform float uFogEnd;

out vec4 vColor;
out float vFog;

void main() {
    vec4 clip = uViewProj * vec4(aPos, 1.0);
    gl_Position = clip;
    float distanceToCamera = length(aPos - uCameraPos);
    gl_PointSize = clamp(uScreenHeight * aSize / max(distanceToCamera, 0.5), 1.0, 96.0);
    vColor = aColor;
    vFog = clamp((distanceToCamera - uFogStart) / max(0.001, uFogEnd - uFogStart),
                 0.0, 1.0);
}
)";

const char* kParticleFragmentShader = R"(
#version 330 core
in vec4 vColor;
in float vFog;

uniform vec3 uFogColor;

out vec4 FragColor;

void main() {
    vec2 offset = gl_PointCoord - vec2(0.5);
    float radius = length(offset);
    if (radius > 0.5) {
        discard;
    }
    float alpha = smoothstep(0.5, 0.10, radius) * vColor.a;
    if (alpha < 0.01) {
        discard;
    }
    FragColor = vec4(mix(vColor.rgb, uFogColor, vFog), alpha);
}
)";

}

bool ParticleRenderer::init(std::string* error) {
    if (!shader_.compile(kParticleVertexShader, kParticleFragmentShader, error)) {
        return false;
    }
    gl::GenVertexArrays(1, &vao_);
    gl::GenBuffers(1, &vbo_);
    return true;
}

void ParticleRenderer::destroy() {
    if (vao_) {
        gl::DeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    if (vbo_) {
        gl::DeleteBuffers(1, &vbo_);
        vbo_ = 0;
    }
    shader_.destroy();
}

void ParticleRenderer::render(const Mat4& viewProjection, const Vec3& cameraPosition,
                              const Environment& environment, float fogStart, float fogEnd,
                              int screenHeight, const std::vector<Particle>& particles) {
    if (particles.empty()) {
        return;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(particles.size());
    for (const Particle& particle : particles) {
        if (particle.life <= 0.0f) {
            continue;
        }
        const float progress = 1.0f
                             - clampf(particle.life / std::max(particle.maxLife, 0.001f),
                                      0.0f, 1.0f);
        const float fadeIn = clampf(progress / 0.12f, 0.0f, 1.0f);
        const float fadeOut = 1.0f - clampf((progress - 0.65f) / 0.35f, 0.0f, 1.0f);
        const float alpha = fadeIn * fadeOut;
        const Vec3 color = particle.startColor
                         + (particle.endColor - particle.startColor) * progress;
        const float size = particle.startSize
                         + (particle.endSize - particle.startSize) * progress;
        vertices.push_back({particle.position.x, particle.position.y, particle.position.z, size,
                            color.x, color.y, color.z, alpha});
    }
    if (vertices.empty()) {
        return;
    }

    shader_.use();
    shader_.setMat4("uViewProj", viewProjection);
    shader_.setVec3("uCameraPos", cameraPosition);
    shader_.setFloat("uScreenHeight", static_cast<float>(screenHeight));
    shader_.setFloat("uFogStart", fogStart);
    shader_.setFloat("uFogEnd", fogEnd);
    shader_.setVec3("uFogColor", environment.fogColor);

    gl::BindVertexArray(vao_);
    gl::BindBuffer(GL_ARRAY_BUFFER, vbo_);
    gl::BufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(),
                   GL_DYNAMIC_DRAW);
    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, size)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, r)));

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_PROGRAM_POINT_SIZE);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(vertices.size()));
    glDisable(GL_PROGRAM_POINT_SIZE);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
}

}
