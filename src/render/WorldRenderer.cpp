#include "render/WorldRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "core/Log.hpp"

namespace nc {
namespace {

struct FaceInfo {
    Vec3 corners[4];
    float u[4];
    float v[4];
};

const FaceInfo kFaces[6] = {
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {0, 1, 1, 0}, {1, 1, 0, 0}},
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {0, 1, 1, 0}, {1, 1, 0, 0}},
    {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {0, 0, 1, 1}, {1, 0, 0, 1}},
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, 1, 1, 0}, {0, 0, 1, 1}},
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {0, 1, 1, 0}, {1, 1, 0, 0}},
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 1, 1, 0}, {1, 1, 0, 0}},
};

const int kOffsets[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

struct FrustumPlane {
    Vec3 normal;
    float distance = 0.0f;
};

struct Frustum {
    FrustumPlane planes[6];
};

Frustum extractFrustum(const Mat4& matrix) {
    const float* e = matrix.m.data();
    auto row = [&](int index) {
        return Vec4{e[index], e[4 + index], e[8 + index], e[12 + index]};
    };
    const Vec4 row0 = row(0);
    const Vec4 row1 = row(1);
    const Vec4 row2 = row(2);
    const Vec4 row3 = row(3);

    const Vec4 raw[6] = {
        {row3.x + row0.x, row3.y + row0.y, row3.z + row0.z, row3.w + row0.w},
        {row3.x - row0.x, row3.y - row0.y, row3.z - row0.z, row3.w - row0.w},
        {row3.x + row1.x, row3.y + row1.y, row3.z + row1.z, row3.w + row1.w},
        {row3.x - row1.x, row3.y - row1.y, row3.z - row1.z, row3.w - row1.w},
        {row3.x + row2.x, row3.y + row2.y, row3.z + row2.z, row3.w + row2.w},
        {row3.x - row2.x, row3.y - row2.y, row3.z - row2.z, row3.w - row2.w},
    };

    Frustum frustum;
    for (int i = 0; i < 6; ++i) {
        const float length = std::sqrt(raw[i].x * raw[i].x + raw[i].y * raw[i].y
                                       + raw[i].z * raw[i].z);
        const float inverse = length > 0.0f ? 1.0f / length : 0.0f;
        frustum.planes[i].normal = {raw[i].x * inverse, raw[i].y * inverse,
                                    raw[i].z * inverse};
        frustum.planes[i].distance = raw[i].w * inverse;
    }
    return frustum;
}

std::uint32_t hashCoords(int x, int y, int z, std::uint32_t seed) {
    std::uint64_t value = seed
                        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32u)
                        ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) << 16u)
                        ^ static_cast<std::uint32_t>(y);
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33u;
    value *= 0xc4ceb9fe1a85ec53ull;
    value ^= value >> 33u;
    return static_cast<std::uint32_t>(value >> 32u);
}

float rand01(std::uint32_t value) {
    return static_cast<float>(value & 0x00FFFFFFu) / 16777216.0f;
}

bool intersectsFrustum(const Frustum& frustum, const Aabb& bounds) {
    for (const FrustumPlane& plane : frustum.planes) {
        const Vec3 positive{
            plane.normal.x >= 0.0f ? bounds.max.x : bounds.min.x,
            plane.normal.y >= 0.0f ? bounds.max.y : bounds.min.y,
            plane.normal.z >= 0.0f ? bounds.max.z : bounds.min.z,
        };
        if (dot(plane.normal, positive) + plane.distance < 0.0f) {
            return false;
        }
    }
    return true;
}

const char* kWorldVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aLayer;
layout(location = 4) in vec4 aColor;

uniform mat4 uViewProj;
uniform mat4 uLightVP;
uniform float uShadowNormalOffset;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vUV;
out vec4 vColor;
out vec4 vShadowCoord;

void main() {
    vec4 world = vec4(aPos, 1.0);
    gl_Position = uViewProj * world;
    vWorldPos = aPos;
    vNormal = aNormal;
    vUV = vec3(aUV, aLayer);
    vColor = aColor;
    vShadowCoord = uLightVP * vec4(aPos + aNormal * uShadowNormalOffset, 1.0);
}
)";

const char* kWorldFragmentShader = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vUV;
in vec4 vColor;
in vec4 vShadowCoord;

uniform sampler2DArray uAtlas;
uniform sampler2DShadow uShadowMap;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uUseClip;
uniform float uClipY;
uniform float uShadowStrength;

out vec4 FragColor;

float shadowFactor(vec4 shadowCoord, vec3 normal) {
    if (shadowCoord.w <= 0.0) {
        return 1.0;
    }
    vec3 projected = shadowCoord.xyz / shadowCoord.w * 0.5 + 0.5;
    if (projected.z > 1.0 || projected.z < 0.0 || projected.x < 0.0 || projected.x > 1.0
        || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }
    float bias = max(0.0008 * (1.0 - max(dot(normal, uSunDir), 0.0)), 0.0002);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            sum += texture(uShadowMap,
                           vec3(projected.xy + vec2(x, y) * texel, projected.z - bias));
        }
    }
    return sum / 9.0;
}

void main() {
    if (uUseClip > 0.5 && vWorldPos.y < uClipY) {
        discard;
    }
    vec4 texel = texture(uAtlas, vUV);
    float alpha = texel.a * vColor.a;
    if (alpha < 0.05) {
        discard;
    }
    vec3 normal = normalize(vNormal);
    float sun = max(dot(normal, uSunDir), 0.0);
    float shadow = mix(1.0, shadowFactor(vShadowCoord, normal), uShadowStrength);
    vec3 ambient = mix(uAmbientGround, uAmbientSky, normal.y * 0.5 + 0.5);
    vec3 light = ambient + uSunColor * sun * shadow;
    vec3 color = texel.rgb * vColor.rgb * light;
    float distanceToCamera = length(vWorldPos - uCameraPos);
    float fog = clamp((distanceToCamera - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
    color = mix(color, uFogColor, fog);
    FragColor = vec4(color, alpha);
}
)";

const char* kShadowVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 uLightVP;
void main() {
    gl_Position = uLightVP * vec4(aPos, 1.0);
}
)";

const char* kShadowFragmentShader = R"(
#version 330 core
void main() {
}
)";


const char* kGrassVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aLayer;
layout(location = 4) in vec4 aColor;

uniform mat4 uViewProj;
uniform mat4 uLightVP;
uniform float uShadowNormalOffset;
uniform float uTime;
uniform vec3 uWindDir;
uniform float uWindStrength;
uniform vec3 uPlayerPos;
uniform float uPlayerRadius;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vUV;
out vec4 vColor;
out vec4 vShadowCoord;

void main() {
    float base = floor(aPos.y);
    float bend = clamp(aPos.y - base, 0.0, 1.0);

    float wave = sin(uTime * 1.7 + aPos.x * 0.55 + aPos.z * 0.42) * 0.55;
    wave += sin(uTime * 2.9 + aPos.x * 1.1 - aPos.z * 0.8) * 0.25;
    float gust = 0.6 + 0.4 * sin(uTime * 0.37 + aPos.x * 0.03 + aPos.z * 0.02);
    vec3 offset = uWindDir * (wave * gust * uWindStrength * bend);

    vec2 toPlayer = aPos.xz - uPlayerPos.xz;
    float distanceToPlayer = length(toPlayer);
    float push = smoothstep(uPlayerRadius, uPlayerRadius * 0.15, distanceToPlayer) * bend;
    if (distanceToPlayer > 0.0001) {
        offset += vec3(normalize(toPlayer).x, 0.0, normalize(toPlayer).y) * push * 0.8;
    }

    vec3 position = aPos + offset;
    position.y -= length(offset) * 0.3;

    vWorldPos = position;
    vNormal = aNormal;
    vUV = vec3(aUV, aLayer);
    vColor = aColor;
    vShadowCoord = uLightVP * vec4(position + aNormal * uShadowNormalOffset, 1.0);
    gl_Position = uViewProj * vec4(position, 1.0);
}
)";

const char* kGrassFragmentShader = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vUV;
in vec4 vColor;
in vec4 vShadowCoord;

uniform sampler2DShadow uShadowMap;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uAmbientSky;
uniform vec3 uAmbientGround;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uShadowStrength;

out vec4 FragColor;

float shadowFactor(vec4 shadowCoord, vec3 normal) {
    if (shadowCoord.w <= 0.0) {
        return 1.0;
    }
    vec3 projected = shadowCoord.xyz / shadowCoord.w * 0.5 + 0.5;
    if (projected.z > 1.0 || projected.z < 0.0 || projected.x < 0.0 || projected.x > 1.0
        || projected.y < 0.0 || projected.y > 1.0) {
        return 1.0;
    }
    float bias = max(0.0008 * (1.0 - max(dot(normal, uSunDir), 0.0)), 0.0002);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            sum += texture(uShadowMap,
                           vec3(projected.xy + vec2(x, y) * texel, projected.z - bias));
        }
    }
    return sum / 9.0;
}

void main() {
    vec3 normal = normalize(vNormal);
    float sun = max(dot(normal, uSunDir), 0.0);
    float shadow = mix(1.0, shadowFactor(vShadowCoord, normal), uShadowStrength);
    vec3 ambient = mix(uAmbientGround, uAmbientSky, normal.y * 0.5 + 0.5);
    vec3 light = ambient * 0.85 + uSunColor * sun * shadow * 0.8;
    float blade = clamp(1.0 - vUV.y, 0.0, 1.0);
    vec3 color = vColor.rgb * light * mix(0.42, 1.05, blade);
    float distanceToCamera = length(vWorldPos - uCameraPos);
    float fog = clamp((distanceToCamera - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
    color = mix(color, uFogColor, fog);
    FragColor = vec4(color, 1.0);
}
)";

const char* kWaterVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aLayer;
layout(location = 4) in vec4 aColor;

uniform mat4 uViewProj;
uniform float uTime;
uniform float uWaterLevel;
uniform float uWaveAmplitude;

out vec3 vWorldPos;
out vec3 vNormal;
out vec3 vUV;
out vec4 vColor;
out float vWaveHeight;

void main() {
    vec3 position = aPos;
    float mask = step(uWaterLevel - 0.01, aPos.y);
    float phase1 = aPos.x * 0.45 + uTime * 0.7;
    float phase2 = aPos.z * 0.52 + uTime * 0.6;
    float phase3 = (aPos.x + aPos.z) * 0.22 + uTime * 0.4;
    float height = (sin(phase1) * 0.5 + sin(phase2) * 0.5) * 0.7 + sin(phase3) * 0.3;
    float ripplePhaseX = aPos.x * 1.6 + uTime * 1.1;
    float ripplePhaseZ = aPos.z * 1.4 - uTime * 0.9;
    float ripple = sin(ripplePhaseX) * sin(ripplePhaseZ);
    float waveHeight = (height * uWaveAmplitude + ripple * 0.018) * mask;
    position.y += waveHeight;
    float dhdx = (cos(phase1) * 0.45 * 0.5 * 0.7 + cos(phase3) * 0.22 * 0.3)
               * uWaveAmplitude * mask;
    float dhdz = (cos(phase2) * 0.52 * 0.5 * 0.7 + cos(phase3) * 0.22 * 0.3)
               * uWaveAmplitude * mask;
    dhdx += cos(ripplePhaseX) * 1.6 * sin(ripplePhaseZ) * 0.018 * mask;
    dhdz += sin(ripplePhaseX) * cos(ripplePhaseZ) * 1.4 * 0.018 * mask;
    vec3 normal = mask > 0.5 ? normalize(vec3(-dhdx, 1.0, -dhdz)) : normalize(aNormal);
    vWorldPos = position;
    vNormal = normal;
    vUV = vec3(aUV, aLayer);
    vColor = aColor;
    vWaveHeight = waveHeight;
    gl_Position = uViewProj * vec4(position, 1.0);
}
)";

const char* kWaterFragmentShader = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec3 vUV;
in vec4 vColor;
in float vWaveHeight;

uniform sampler2DArray uAtlas;
uniform sampler2D uReflectionTex;
uniform mat4 uReflectionVP;
uniform vec3 uCameraPos;
uniform vec3 uSunDir;
uniform vec3 uSunColor;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;
uniform float uTime;
uniform float uDayFactor;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    normal.x += sin(vWorldPos.x * 0.65 + uTime * 0.7) * 0.018;
    normal.z += cos(vWorldPos.z * 0.72 - uTime * 0.6) * 0.018;
    normal = normalize(normal);

    vec2 flow = vec2(uTime * 0.006, uTime * 0.004);
    vec3 texel = texture(uAtlas, vec3(vUV.xy + flow, vUV.z)).rgb;
    vec3 waterBody = vec3(0.018, 0.075, 0.095) + texel * 0.08 + vColor.rgb * 0.06;
    waterBody *= 0.55 + 0.45 * uDayFactor;

    vec4 clip = uReflectionVP * vec4(vWorldPos, 1.0);
    vec2 reflectionUv = clip.xy / clip.w * 0.5 + 0.5;
    reflectionUv += normal.xz * 0.018;
    reflectionUv = clamp(reflectionUv, vec2(0.002), vec2(0.998));

    float distanceToCamera = length(vWorldPos - uCameraPos);
    vec2 texelSize = 1.0 / vec2(textureSize(uReflectionTex, 0));
    float blur = mix(0.6, 3.0, clamp(distanceToCamera / 140.0, 0.0, 1.0));
    vec2 offset = texelSize * blur;
    vec3 reflection = texture(uReflectionTex, reflectionUv).rgb * 0.4;
    reflection += texture(uReflectionTex, reflectionUv + vec2(offset.x, 0.0)).rgb * 0.15;
    reflection += texture(uReflectionTex, reflectionUv - vec2(offset.x, 0.0)).rgb * 0.15;
    reflection += texture(uReflectionTex, reflectionUv + vec2(0.0, offset.y)).rgb * 0.15;
    reflection += texture(uReflectionTex, reflectionUv - vec2(0.0, offset.y)).rgb * 0.15;
    reflection = mix(reflection, reflection * vec3(0.82, 0.92, 0.96), 0.25);

    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float fresnel = 0.02 + 0.98 * pow(1.0 - max(dot(normal, viewDir), 0.0), 5.0);
    vec3 color = mix(waterBody, reflection, clamp(fresnel + 0.06, 0.0, 1.0));

    vec3 halfVector = normalize(uSunDir + viewDir);
    float specular = pow(max(dot(normal, halfVector), 0.0), 180.0);
    color += uSunColor * specular * 1.4;
    vec3 reflectedSun = reflect(-uSunDir, normal);
    float sunPath = pow(max(dot(reflectedSun, viewDir), 0.0), 24.0);
    color += uSunColor * sunPath * 0.08;
    float crest = smoothstep(0.06, 0.14, vWaveHeight) * 0.03;
    color += vec3(0.4, 0.55, 0.6) * crest * uDayFactor;

    float fog = clamp((distanceToCamera - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
    color = mix(color, uFogColor, fog);
    FragColor = vec4(color, mix(0.82, 0.95, fresnel));
}
)";

}

bool WorldRenderer::init(TextureArray& textures, std::string* error) {
    textures_ = &textures;

    if (!shader_.compile(kWorldVertexShader, kWorldFragmentShader, error)) {
        return false;
    }
    if (!shadowShader_.compile(kShadowVertexShader, kShadowFragmentShader, error)) {
        return false;
    }
    if (!grassShader_.compile(kGrassVertexShader, kGrassFragmentShader, error)) {
        return false;
    }
    if (!waterShader_.compile(kWaterVertexShader, kWaterFragmentShader, error)) {
        return false;
    }

    GLint maxTextureSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTextureSize);
    if (maxTextureSize > 0) {
        shadowSize_ = std::min(shadowSize_, static_cast<int>(maxTextureSize));
    }

    glGenTextures(1, &shadowTexture_);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    const GLfloat border[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);

    gl::GenFramebuffers(1, &shadowFbo_);
    gl::BindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    gl::FramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTexture_,
                             0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    const GLenum status = gl::CheckFramebufferStatus(GL_FRAMEBUFFER);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (status != GL_FRAMEBUFFER_COMPLETE) {
        if (error) {
            *error = "shadow framebuffer incomplete: " + std::to_string(status);
        }
        return false;
    }

    return true;
}

void WorldRenderer::destroyMesh(ChunkMesh& mesh) {
    if (mesh.vao) {
        gl::DeleteVertexArrays(1, &mesh.vao);
        mesh.vao = 0;
    }
    if (mesh.vbo) {
        gl::DeleteBuffers(1, &mesh.vbo);
        mesh.vbo = 0;
    }
    if (mesh.ebo) {
        gl::DeleteBuffers(1, &mesh.ebo);
        mesh.ebo = 0;
    }
    mesh.triangles = 0;
    mesh.hasData = false;
}

void WorldRenderer::destroy() {
    for (auto& [pos, mesh] : meshes_) {
        (void)pos;
        destroyMesh(mesh);
    }
    meshes_.clear();

    if (shadowFbo_) {
        gl::DeleteFramebuffers(1, &shadowFbo_);
        shadowFbo_ = 0;
    }
    if (shadowTexture_) {
        glDeleteTextures(1, &shadowTexture_);
        shadowTexture_ = 0;
    }

    shader_.destroy();
    shadowShader_.destroy();
    grassShader_.destroy();
    waterShader_.destroy();
    triangleCount_ = 0;
    shadowMapValid_ = false;
}

void WorldRenderer::uploadMesh(ChunkMesh& mesh, const std::vector<Vertex>& vertices,
                               const std::vector<std::uint32_t>& indices, int opaqueIndexCount,
                               int grassIndexCount, int transparentIndexCount) {
    if (!mesh.vao) {
        gl::GenVertexArrays(1, &mesh.vao);
        gl::GenBuffers(1, &mesh.vbo);
        gl::GenBuffers(1, &mesh.ebo);
    }

    gl::BindVertexArray(mesh.vao);
    gl::BindBuffer(GL_ARRAY_BUFFER, mesh.vbo);
    gl::BufferData(GL_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)),
                   vertices.empty() ? nullptr : vertices.data(), GL_DYNAMIC_DRAW);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.ebo);
    gl::BufferData(GL_ELEMENT_ARRAY_BUFFER,
                   static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
                   indices.empty() ? nullptr : indices.data(), GL_DYNAMIC_DRAW);

    gl::EnableVertexAttribArray(0);
    gl::VertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, x)));
    gl::EnableVertexAttribArray(1);
    gl::VertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, nx)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, u)));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, layer)));
    gl::EnableVertexAttribArray(4);
    gl::VertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, r)));

    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    mesh.opaqueIndices = opaqueIndexCount;
    mesh.grassIndices = grassIndexCount;
    mesh.transparentIndices = transparentIndexCount;
    mesh.waterIndices = static_cast<int>(indices.size()) - opaqueIndexCount
                      - grassIndexCount - transparentIndexCount;
    mesh.triangles = static_cast<int>(indices.size() / 3);
    mesh.hasData = !indices.empty();
}

void WorldRenderer::rebuildChunk(const World& world, const BlockRegistry& registry, int chunkX,
                                 int chunkZ, bool includeGrass) {
    const ChunkPos pos{chunkX, chunkZ};
    ChunkMesh& mesh = meshes_[pos];
    triangleCount_ -= mesh.triangles;
    mesh.grassEnabled = includeGrass;
    shadowMapValid_ = false;

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> opaqueIndices;
    std::vector<std::uint32_t> grassIndices;
    std::vector<std::uint32_t> transparentIndices;
    std::vector<std::uint32_t> waterIndices;

    const int baseX = chunkX * World::kChunkSize;
    const int baseZ = chunkZ * World::kChunkSize;

    for (int y = 0; y < World::kHeight; ++y) {
        for (int z = 0; z < World::kChunkSize; ++z) {
            for (int x = 0; x < World::kChunkSize; ++x) {
                const int worldX = baseX + x;
                const int worldZ = baseZ + z;
                const std::uint8_t blockId = world.get(worldX, y, worldZ);
                if (blockId == 0) {
                    continue;
                }

                const BlockDef& block = registry.def(blockId);
                if (block.cross) {
                    if (!includeGrass) {
                        continue;
                    }
                    const int tintIndex = static_cast<int>(Face::Up);
                    const float tintRed = block.tints[tintIndex][0];
                    const float tintGreen = block.tints[tintIndex][1];
                    const float tintBlue = block.tints[tintIndex][2];
                    const std::uint32_t blockHash = hashCoords(worldX, y, worldZ, 0x9E3779B9u);
                    const int bladeRange =
                        std::max(1, block.grassBladeMax - block.grassBladeMin + 1);
                    const int bladeCount =
                        block.grassBladeMin + static_cast<int>(blockHash % static_cast<std::uint32_t>(bladeRange));

                    for (int blade = 0; blade < bladeCount; ++blade) {
                        const std::uint32_t h = hashCoords(
                            worldX, y, worldZ,
                            blockHash ^ (0x85EBCA6Bu * static_cast<std::uint32_t>(blade + 1)));
                        const float localX = 0.12f + rand01(h) * 0.76f;
                        const float localZ = 0.12f + rand01(h ^ 0xC2B2AE35u) * 0.76f;
                        const float angle = rand01(h ^ 0x27D4EB2Fu) * 2.0f * kPi;
                        const float height =
                            block.grassHeightMin
                            + rand01(h ^ 0x165667B1u)
                                  * (block.grassHeightMax - block.grassHeightMin);
                        const float halfWidth = 0.016f + rand01(h ^ 0x9E3779B9u) * 0.022f;
                        const float lean = 0.06f + rand01(h ^ 0xD3A2646Cu) * 0.18f;
                        const float shade = 0.75f + rand01(h ^ 0xFD7046C5u) * 0.45f;

                        const Vec3 basePoint{static_cast<float>(worldX) + localX,
                                             static_cast<float>(y),
                                             static_cast<float>(worldZ) + localZ};
                        const Vec3 side{std::cos(angle) * halfWidth, 0.0f,
                                        std::sin(angle) * halfWidth};
                        const Vec3 leanDir{std::sin(angle + 0.8f), 0.0f,
                                           std::cos(angle + 0.8f)};
                        const Vec3 midCenter = basePoint
                                             + Vec3{0.0f, height * 0.55f, 0.0f}
                                             + leanDir * (lean * height * 0.4f);
                        const Vec3 topCenter = basePoint
                                             + Vec3{0.0f, height, 0.0f}
                                             + leanDir * (lean * height);
                        const Vec3 sideMid = side * 0.55f;
                        const Vec3 sideTop = side * 0.18f;
                        const Vec3 corners[6] = {
                            basePoint - side, basePoint + side,
                            midCenter - sideMid, midCenter + sideMid,
                            topCenter - sideTop, topCenter + sideTop,
                        };
                        const float u[6] = {0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f};
                        const float v[6] = {1.0f, 1.0f, 0.45f, 0.45f, 0.0f, 0.0f};

                        const std::uint32_t base =
                            static_cast<std::uint32_t>(vertices.size());
                        for (int vertex = 0; vertex < 6; ++vertex) {
                            Vertex out;
                            out.x = corners[vertex].x;
                            out.y = corners[vertex].y;
                            out.z = corners[vertex].z;
                            out.nx = 0.0f;
                            out.ny = 1.0f;
                            out.nz = 0.0f;
                            out.u = u[vertex];
                            out.v = v[vertex];
                            out.layer = 0.0f;
                            out.r = tintRed * shade;
                            out.g = tintGreen * shade;
                            out.b = tintBlue * shade;
                            out.a = 1.0f;
                            vertices.push_back(out);
                        }

                        grassIndices.push_back(base + 0);
                        grassIndices.push_back(base + 1);
                        grassIndices.push_back(base + 3);
                        grassIndices.push_back(base + 0);
                        grassIndices.push_back(base + 3);
                        grassIndices.push_back(base + 2);
                        grassIndices.push_back(base + 2);
                        grassIndices.push_back(base + 3);
                        grassIndices.push_back(base + 5);
                        grassIndices.push_back(base + 2);
                        grassIndices.push_back(base + 5);
                        grassIndices.push_back(base + 4);
                    }
                    continue;
                }

                std::vector<std::uint32_t>* target = &opaqueIndices;
                if (block.name == "water") {
                    target = &waterIndices;
                } else if (block.transparentPass) {
                    target = &transparentIndices;
                }

                for (int face = 0; face < kFaceCount; ++face) {
                    const int nx = worldX + kOffsets[face][0];
                    const int ny = y + kOffsets[face][1];
                    const int nz = worldZ + kOffsets[face][2];
                    const std::uint8_t neighbor = world.get(nx, ny, nz);

                    if (registry.isOpaque(neighbor)) {
                        continue;
                    }
                    if (neighbor == blockId && !block.opaque) {
                        continue;
                    }

                    const int layer = block.layers[static_cast<std::size_t>(face)];
                    if (layer < 0) {
                        continue;
                    }

                    const FaceInfo& info = kFaces[face];
                    const float red = block.tints[static_cast<std::size_t>(face)][0];
                    const float green = block.tints[static_cast<std::size_t>(face)][1];
                    const float blue = block.tints[static_cast<std::size_t>(face)][2];

                    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
                    for (int vertex = 0; vertex < 4; ++vertex) {
                        Vertex out;
                        out.x = static_cast<float>(worldX) + info.corners[vertex].x;
                        out.y = static_cast<float>(y) + info.corners[vertex].y;
                        out.z = static_cast<float>(worldZ) + info.corners[vertex].z;
                        out.nx = static_cast<float>(kOffsets[face][0]);
                        out.ny = static_cast<float>(kOffsets[face][1]);
                        out.nz = static_cast<float>(kOffsets[face][2]);
                        out.u = info.u[vertex];
                        out.v = info.v[vertex];
                        out.layer = static_cast<float>(layer);
                        out.r = red;
                        out.g = green;
                        out.b = blue;
                        out.a = block.alpha;
                        vertices.push_back(out);
                    }

                    target->push_back(base + 0);
                    target->push_back(base + 1);
                    target->push_back(base + 2);
                    target->push_back(base + 0);
                    target->push_back(base + 2);
                    target->push_back(base + 3);
                }
            }
        }
    }

    const int opaqueIndexCount = static_cast<int>(opaqueIndices.size());
    const int grassIndexCount = static_cast<int>(grassIndices.size());
    const int transparentIndexCount = static_cast<int>(transparentIndices.size());
    std::vector<std::uint32_t> indices;
    indices.reserve(opaqueIndices.size() + grassIndices.size() + transparentIndices.size()
                    + waterIndices.size());
    indices.insert(indices.end(), opaqueIndices.begin(), opaqueIndices.end());
    indices.insert(indices.end(), grassIndices.begin(), grassIndices.end());
    indices.insert(indices.end(), transparentIndices.begin(), transparentIndices.end());
    indices.insert(indices.end(), waterIndices.begin(), waterIndices.end());

    uploadMesh(mesh, vertices, indices, opaqueIndexCount, grassIndexCount,
               transparentIndexCount);
    mesh.center = {static_cast<float>(baseX) + 8.0f, 64.0f, static_cast<float>(baseZ) + 8.0f};
    mesh.bounds = {{static_cast<float>(baseX), 0.0f, static_cast<float>(baseZ)},
                   {static_cast<float>(baseX + World::kChunkSize),
                    static_cast<float>(World::kHeight),
                    static_cast<float>(baseZ + World::kChunkSize)}};
    triangleCount_ += mesh.triangles;
}

void WorldRenderer::syncMeshes(const World& world, const BlockRegistry& registry,
                               const ChunkPos& center, int renderRadius, int grassRadius) {
    std::unordered_map<ChunkPos, bool, ChunkPosHash> desired;
    for (int dz = -renderRadius; dz <= renderRadius; ++dz) {
        for (int dx = -renderRadius; dx <= renderRadius; ++dx) {
            const ChunkPos pos{center.x + dx, center.z + dz};
            if (!world.isChunkLoaded(pos)) {
                continue;
            }
            const bool includeGrass =
                std::abs(dx) <= grassRadius && std::abs(dz) <= grassRadius;
            desired.emplace(pos, includeGrass);
        }
    }

    for (auto it = meshes_.begin(); it != meshes_.end();) {
        const auto target = desired.find(it->first);
        if (target == desired.end() || !world.isChunkLoaded(it->first)) {
            triangleCount_ -= it->second.triangles;
            destroyMesh(it->second);
            it = meshes_.erase(it);
        } else {
            ++it;
        }
    }

    for (const auto& [pos, includeGrass] : desired) {
        const auto existing = meshes_.find(pos);
        if (existing == meshes_.end()) {
            rebuildChunk(world, registry, pos.x, pos.z, includeGrass);
        } else if (existing->second.grassEnabled != includeGrass) {
            rebuildChunk(world, registry, pos.x, pos.z, includeGrass);
        }
    }
}

void WorldRenderer::rebuildAround(const World& world, const BlockRegistry& registry, int x,
                                  int z) {
    const ChunkPos center = World::chunkOf(x, z);
    const int localX = World::localCoord(x);
    const int localZ = World::localCoord(z);

    auto rebuild = [&](int chunkX, int chunkZ) {
        const ChunkPos pos{chunkX, chunkZ};
        if (!world.isChunkLoaded(pos)) {
            return;
        }
        const auto existing = meshes_.find(pos);
        if (existing == meshes_.end()) {
            return;
        }
        rebuildChunk(world, registry, chunkX, chunkZ, existing->second.grassEnabled);
    };

    rebuild(center.x, center.z);
    if (localX == 0) {
        rebuild(center.x - 1, center.z);
    }
    if (localX == World::kChunkSize - 1) {
        rebuild(center.x + 1, center.z);
    }
    if (localZ == 0) {
        rebuild(center.x, center.z - 1);
    }
    if (localZ == World::kChunkSize - 1) {
        rebuild(center.x, center.z + 1);
    }
}

void WorldRenderer::renderShadowMap(const Vec3& focus, const Vec3& sunDirection) {
    if (!shadowFbo_ || meshes_.empty()) {
        return;
    }
    if (shadowMapValid_ && dot(sunDirection, lastShadowSun_) > 0.99997f
        && lengthSquared(focus - lastShadowFocus_) < 36.0f) {
        return;
    }

    const Vec3 center{focus.x, 64.0f, focus.z};
    const Vec3 up = std::abs(sunDirection.y) > 0.98f ? Vec3{1.0f, 0.0f, 0.0f}
                                                     : Vec3{0.0f, 1.0f, 0.0f};
    const Mat4 lightView = Mat4::lookAt(center + sunDirection * 140.0f, center, up);
    const float extent = 90.0f;
    const Mat4 lightProjection =
        Mat4::orthographic(-extent, extent, -extent, extent, 1.0f, 320.0f);
    lightViewProjection_ = lightProjection * lightView;

    GLint viewport[4] = {0, 0, 1, 1};
    glGetIntegerv(GL_VIEWPORT, viewport);

    gl::BindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);

    shadowShader_.use();
    shadowShader_.setMat4("uLightVP", lightViewProjection_);

    const Frustum lightFrustum = extractFrustum(lightViewProjection_);
    for (const auto& [pos, mesh] : meshes_) {
        (void)pos;
        if (mesh.opaqueIndices == 0 || !intersectsFrustum(lightFrustum, mesh.bounds)) {
            continue;
        }
        gl::BindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.opaqueIndices, GL_UNSIGNED_INT, nullptr);
    }

    shadowMapValid_ = true;
    lastShadowSun_ = sunDirection;
    lastShadowFocus_ = focus;

    glDisable(GL_POLYGON_OFFSET_FILL);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
    gl::BindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void WorldRenderer::setWorldUniforms(const Mat4& viewProjection, const Vec3& cameraPosition,
                                     const Environment& environment, float fogStart,
                                     float fogEnd, bool useClip, float clipY) {
    shader_.setMat4("uViewProj", viewProjection);
    shader_.setMat4("uLightVP", lightViewProjection_);
    shader_.setVec3("uCameraPos", cameraPosition);
    shader_.setVec3("uSunDir", environment.sunDirection);
    shader_.setVec3("uSunColor", environment.sunColor);
    shader_.setVec3("uAmbientSky", environment.ambientSky);
    shader_.setVec3("uAmbientGround", environment.ambientGround);
    shader_.setVec3("uFogColor", environment.fogColor);
    shader_.setFloat("uFogStart", fogStart);
    shader_.setFloat("uFogEnd", fogEnd);
    shader_.setFloat("uUseClip", useClip ? 1.0f : 0.0f);
    shader_.setFloat("uClipY", clipY);
    shader_.setFloat("uShadowStrength", 0.82f);
    shader_.setFloat("uShadowNormalOffset", 0.03f);
    shader_.setInt("uAtlas", 0);
    shader_.setInt("uShadowMap", 1);
}

void WorldRenderer::drawOpaque(const Mat4& viewProjection, const Vec3& cameraPosition,
                               const Environment& environment, float fogStart, float fogEnd,
                               bool useClip, float clipY, bool mirrored) {
    if (!textures_ || triangleCount_ == 0) {
        return;
    }

    shader_.use();
    setWorldUniforms(viewProjection, cameraPosition, environment, fogStart, fogEnd, useClip,
                     clipY);

    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures_->id());
    gl::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(mirrored ? GL_CW : GL_CCW);
    glDisable(GL_BLEND);

    const Frustum frustum = extractFrustum(viewProjection);
    for (const auto& [pos, mesh] : meshes_) {
        (void)pos;
        if (mesh.opaqueIndices == 0 || !intersectsFrustum(frustum, mesh.bounds)) {
            continue;
        }
        gl::BindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.opaqueIndices, GL_UNSIGNED_INT, nullptr);
    }

    glFrontFace(GL_CCW);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
}

void WorldRenderer::drawGrass(const Mat4& viewProjection, const Vec3& cameraPosition,
                              const Environment& environment, float fogStart, float fogEnd,
                              float time, const Vec3& playerPosition,
                              const Vec3& windDirection, float windStrength) {
    if (!textures_ || triangleCount_ == 0) {
        return;
    }

    grassShader_.use();
    grassShader_.setMat4("uViewProj", viewProjection);
    grassShader_.setMat4("uLightVP", lightViewProjection_);
    grassShader_.setVec3("uCameraPos", cameraPosition);
    grassShader_.setVec3("uSunDir", environment.sunDirection);
    grassShader_.setVec3("uSunColor", environment.sunColor);
    grassShader_.setVec3("uAmbientSky", environment.ambientSky);
    grassShader_.setVec3("uAmbientGround", environment.ambientGround);
    grassShader_.setVec3("uFogColor", environment.fogColor);
    grassShader_.setFloat("uFogStart", fogStart);
    grassShader_.setFloat("uFogEnd", fogEnd);
    grassShader_.setFloat("uShadowStrength", 0.82f);
    grassShader_.setFloat("uShadowNormalOffset", 0.03f);
    grassShader_.setFloat("uTime", time);
    grassShader_.setVec3("uWindDir", windDirection);
    grassShader_.setFloat("uWindStrength", windStrength);
    grassShader_.setVec3("uPlayerPos", playerPosition);
    grassShader_.setFloat("uPlayerRadius", 1.7f);
    grassShader_.setInt("uShadowMap", 1);

    gl::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);

    const Frustum frustum = extractFrustum(viewProjection);
    for (const auto& [pos, mesh] : meshes_) {
        (void)pos;
        if (mesh.grassIndices == 0 || !intersectsFrustum(frustum, mesh.bounds)) {
            continue;
        }
        gl::BindVertexArray(mesh.vao);
        const auto offset =
            static_cast<GLsizeiptr>(mesh.opaqueIndices * sizeof(std::uint32_t));
        glDrawElements(GL_TRIANGLES, mesh.grassIndices, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(offset));
    }

    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
}

void WorldRenderer::drawTransparent(const Mat4& viewProjection, const Vec3& cameraPosition,
                                    const Environment& environment, float fogStart,
                                    float fogEnd) {
    if (!textures_ || triangleCount_ == 0) {
        return;
    }

    const Frustum frustum = extractFrustum(viewProjection);
    std::vector<std::pair<float, const ChunkMesh*>> order;
    for (const auto& [pos, mesh] : meshes_) {
        (void)pos;
        if (mesh.transparentIndices == 0 || !intersectsFrustum(frustum, mesh.bounds)) {
            continue;
        }
        order.emplace_back(lengthSquared(mesh.center - cameraPosition), &mesh);
    }
    if (order.empty()) {
        return;
    }
    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    shader_.use();
    setWorldUniforms(viewProjection, cameraPosition, environment, fogStart, fogEnd, false,
                     0.0f);

    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures_->id());
    gl::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const auto& [distance, mesh] : order) {
        (void)distance;
        gl::BindVertexArray(mesh->vao);
        const auto offset = static_cast<GLsizeiptr>(
            (mesh->opaqueIndices + mesh->grassIndices) * sizeof(std::uint32_t));
        glDrawElements(GL_TRIANGLES, mesh->transparentIndices, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(offset));
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
}

void WorldRenderer::drawWater(const Mat4& viewProjection, const Vec3& cameraPosition,
                              const Environment& environment, float fogStart, float fogEnd,
                              GLuint reflectionTexture, const Mat4& reflectionViewProjection,
                              float time, float waterLevel) {
    if (!textures_ || reflectionTexture == 0 || triangleCount_ == 0) {
        return;
    }

    const Frustum frustum = extractFrustum(viewProjection);
    std::vector<std::pair<float, const ChunkMesh*>> order;
    for (const auto& [pos, mesh] : meshes_) {
        (void)pos;
        if (mesh.waterIndices == 0 || !intersectsFrustum(frustum, mesh.bounds)) {
            continue;
        }
        order.emplace_back(lengthSquared(mesh.center - cameraPosition), &mesh);
    }
    if (order.empty()) {
        return;
    }
    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    waterShader_.use();
    waterShader_.setMat4("uViewProj", viewProjection);
    waterShader_.setMat4("uReflectionVP", reflectionViewProjection);
    waterShader_.setVec3("uCameraPos", cameraPosition);
    waterShader_.setVec3("uSunDir", environment.sunDirection);
    waterShader_.setVec3("uSunColor", environment.sunColor);
    waterShader_.setVec3("uFogColor", environment.fogColor);
    waterShader_.setFloat("uFogStart", fogStart);
    waterShader_.setFloat("uFogEnd", fogEnd);
    waterShader_.setFloat("uTime", time);
    waterShader_.setFloat("uWaterLevel", waterLevel);
    waterShader_.setFloat("uWaveAmplitude", 0.16f);
    waterShader_.setFloat("uDayFactor", environment.dayFactor);
    waterShader_.setInt("uAtlas", 0);
    waterShader_.setInt("uReflectionTex", 1);

    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures_->id());
    gl::ActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, reflectionTexture);

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (const auto& [distance, mesh] : order) {
        (void)distance;
        gl::BindVertexArray(mesh->vao);
        const auto offset = static_cast<GLsizeiptr>(
            (mesh->opaqueIndices + mesh->grassIndices + mesh->transparentIndices)
            * sizeof(std::uint32_t));
        glDrawElements(GL_TRIANGLES, mesh->waterIndices, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(offset));
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
}

}
