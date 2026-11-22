#include "render/SkyRenderer.hpp"

namespace nc {
namespace {

const char* kSkyVertexShader = R"(
#version 330 core
out vec2 vNdc;

void main() {
    vec2 p = vec2(float((gl_VertexID << 1) & 2), float(gl_VertexID & 2));
    vNdc = p * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
)";

const char* kSkyFragmentShader = R"(
#version 330 core
in vec2 vNdc;

uniform mat4 uInvViewProj;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uSkyZenith;
uniform vec3 uSkyHorizon;
uniform vec3 uFogColor;
uniform float uCloudTime;
uniform float uDayFactor;

out vec4 FragColor;

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return fract(p.x * p.y);
}

float noise2(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = hash21(i);
    float b = hash21(i + vec2(1.0, 0.0));
    float c = hash21(i + vec2(0.0, 1.0));
    float d = hash21(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float hash33(vec3 p) {
    p = fract(p * 0.3183099 + vec3(0.1, 0.2, 0.3));
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float fbm2(vec2 p) {
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 5; ++i) {
        value += amplitude * noise2(p);
        p = p * 2.03 + 17.1;
        amplitude *= 0.5;
    }
    return value;
}

void main() {
    vec4 nearPoint = uInvViewProj * vec4(vNdc, -1.0, 1.0);
    vec4 farPoint = uInvViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 origin = nearPoint.xyz / nearPoint.w;
    vec3 direction = normalize(farPoint.xyz / farPoint.w - origin);

    float up = clamp(direction.y, 0.0, 1.0);
    vec3 sky = mix(uSkyHorizon, uSkyZenith, pow(up, 0.45));
    sky = mix(sky, uFogColor, pow(1.0 - up, 10.0) * 0.35);

    if (direction.y < 0.0) {
        float below = clamp(-direction.y * 3.5, 0.0, 1.0);
        sky = mix(uSkyHorizon * 0.9, uSkyZenith * 0.55, below);
    }

    float sunAmount = dot(direction, uSunDir);
    float disc = smoothstep(0.99978, 0.99994, sunAmount);
    float glow = pow(max(sunAmount, 0.0), 900.0);
    float halo = pow(max(sunAmount, 0.0), 70.0) * 0.08;
    sky += uSunColor * (disc * 3.2 + glow * 0.4 + halo);

    vec3 moonDir = -uSunDir;
    float moonAmount = dot(direction, moonDir);
    float moonDisc = smoothstep(0.99978, 0.99992, moonAmount);
    float moonGlow = pow(max(moonAmount, 0.0), 160.0);
    sky += vec3(0.72, 0.82, 1.0) * (moonDisc * 1.4 + moonGlow * 0.12)
         * (1.0 - uDayFactor);

    float star = step(0.9985, hash33(floor(direction * 380.0)));
    star *= smoothstep(0.0, 0.18, direction.y) * (1.0 - uDayFactor);
    sky += vec3(0.85, 0.9, 1.0) * star * 1.4;

    if (direction.y > 0.015) {
        float travel = (170.0 - uCameraPos.y) / direction.y;
        if (travel > 0.0) {
            vec2 uv = (uCameraPos.xz + direction.xz * travel) * 0.0019;
            uv += vec2(uCloudTime * 0.011, uCloudTime * 0.0045);
            float base = fbm2(uv * 2.1);
            float detail = fbm2(uv * 4.8 + 11.3);
            float density = base * 0.72 + detail * 0.28;
            float cover = smoothstep(0.44, 0.63, density);
            cover *= smoothstep(0.015, 0.18, direction.y);

            float cloudSun = max(uSunDir.y, 0.0);
            vec3 cloudColor = mix(vec3(0.32, 0.36, 0.45), vec3(1.0), cloudSun * 0.7 + 0.3);
            cloudColor = mix(cloudColor, uSunColor, cloudSun * 0.4);
            cloudColor *= 0.85 + 0.15 * smoothstep(0.4, 0.8, density);
            cloudColor *= 0.18 + 0.82 * uDayFactor;
            sky = mix(sky, cloudColor, cover * 0.92);
        }
    }

    FragColor = vec4(sky, 1.0);
}
)";

}

bool SkyRenderer::init(std::string* error) {
    if (!shader_.compile(kSkyVertexShader, kSkyFragmentShader, error)) {
        return false;
    }
    gl::GenVertexArrays(1, &vao_);
    return true;
}

void SkyRenderer::destroy() {
    if (vao_) {
        gl::DeleteVertexArrays(1, &vao_);
        vao_ = 0;
    }
    shader_.destroy();
}

void SkyRenderer::render(const Mat4& inverseViewProjection, const Vec3& cameraPosition,
                         const Environment& environment, float cloudTime) {
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    shader_.use();
    shader_.setMat4("uInvViewProj", inverseViewProjection);
    shader_.setVec3("uCameraPos", cameraPosition);
    shader_.setVec3("uSunDir", environment.sunDirection);
    shader_.setVec3("uSunColor", environment.sunColor);
    shader_.setVec3("uSkyZenith", environment.skyZenith);
    shader_.setVec3("uSkyHorizon", environment.skyHorizon);
    shader_.setVec3("uFogColor", environment.fogColor);
    shader_.setFloat("uCloudTime", cloudTime);
    shader_.setFloat("uDayFactor", environment.dayFactor);

    gl::BindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    gl::BindVertexArray(0);
    gl::UseProgram(0);

    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

}
