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

const char* kWorldVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in float aLayer;
layout(location = 4) in vec4 aColor;

uniform mat4 uViewProj;
uniform mat4 uLightVP;

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
    vShadowCoord = uLightVP * world;
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
uniform sampler2D uShadowMap;
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
    float bias = max(0.0028 * (1.0 - max(dot(normal, uSunDir), 0.0)), 0.0005);
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float sum = 0.0;
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            float depth = texture(uShadowMap, projected.xy + vec2(x, y) * texel).r;
            sum += (projected.z - bias > depth) ? 0.0 : 1.0;
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
    float phase1 = aPos.x * 0.62 + uTime * 0.95;
    float phase2 = aPos.z * 0.71 + uTime * 0.82;
    float phase3 = (aPos.x + aPos.z) * 0.33 + uTime * 0.55;
    float height = (sin(phase1) * 0.5 + sin(phase2) * 0.5) * 0.7 + sin(phase3) * 0.3;
    float ripplePhaseX = aPos.x * 2.6 + uTime * 2.0;
    float ripplePhaseZ = aPos.z * 2.2 - uTime * 1.5;
    float ripple = sin(ripplePhaseX) * sin(ripplePhaseZ);
    float waveHeight = (height * uWaveAmplitude + ripple * 0.05) * mask;
    position.y += waveHeight;
    float dhdx = (cos(phase1) * 0.62 * 0.5 * 0.7 + cos(phase3) * 0.33 * 0.3)
               * uWaveAmplitude * mask;
    float dhdz = (cos(phase2) * 0.71 * 0.5 * 0.7 + cos(phase3) * 0.33 * 0.3)
               * uWaveAmplitude * mask;
    dhdx += cos(ripplePhaseX) * 2.6 * sin(ripplePhaseZ) * 0.05 * mask;
    dhdz += sin(ripplePhaseX) * cos(ripplePhaseZ) * 2.2 * 0.05 * mask;
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
    vec2 flow = vec2(uTime * 0.012, uTime * 0.008);
    vec3 normal = normalize(vNormal);
    normal.x += sin(vWorldPos.x * 3.0 + uTime * 2.2) * 0.055;
    normal.z += cos(vWorldPos.z * 2.8 - uTime * 1.8) * 0.055;
    normal = normalize(normal);

    vec3 texel = texture(uAtlas, vec3(vUV.xy + flow, vUV.z)).rgb;
    vec3 deep = texel * vColor.rgb * (0.62 + 0.3 * uDayFactor);

    vec4 clip = uReflectionVP * vec4(vWorldPos, 1.0);
    vec2 reflectionUv = clip.xy / clip.w * 0.5 + 0.5;
    reflectionUv += normal.xz * 0.042;
    reflectionUv = clamp(reflectionUv, vec2(0.002), vec2(0.998));
    vec3 reflection = texture(uReflectionTex, reflectionUv).rgb;

    vec3 viewDir = normalize(uCameraPos - vWorldPos);
    float fresnel = pow(1.0 - max(dot(normal, viewDir), 0.0), 3.0);
    vec3 color = mix(deep, reflection, clamp(0.42 + 0.58 * fresnel, 0.0, 0.92));

    vec3 halfVector = normalize(uSunDir + viewDir);
    float specular = pow(max(dot(normal, halfVector), 0.0), 220.0);
    color += uSunColor * specular * 2.2;
    vec3 reflectedSun = reflect(-uSunDir, normal);
    float sunPath = pow(max(dot(reflectedSun, viewDir), 0.0), 12.0);
    color += uSunColor * sunPath * 0.16;
    float foam = smoothstep(0.14, 0.26, vWaveHeight) * (0.22 + 0.22 * uDayFactor);
    color = mix(color, vec3(0.92, 0.97, 1.0), foam);

    float distanceToCamera = length(vWorldPos - uCameraPos);
    float fog = clamp((distanceToCamera - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
    color = mix(color, uFogColor, fog);
    FragColor = vec4(color, clamp(0.72 + fresnel * 0.2, 0.0, 0.92));
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
    if (!waterShader_.compile(kWaterVertexShader, kWaterFragmentShader, error)) {
        return false;
    }

    glGenTextures(1, &shadowTexture_);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0,
                 GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
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

void WorldRenderer::destroy() {
    for (auto& mesh : meshes_) {
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
    waterShader_.destroy();
    triangleCount_ = 0;
}

void WorldRenderer::uploadMesh(ChunkMesh& mesh, const std::vector<Vertex>& vertices,
                               const std::vector<std::uint32_t>& indices, int opaqueIndexCount,
                               int transparentIndexCount) {
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
    mesh.transparentIndices = transparentIndexCount;
    mesh.waterIndices = static_cast<int>(indices.size()) - opaqueIndexCount
                      - transparentIndexCount;
    mesh.triangles = static_cast<int>(indices.size() / 3);
    mesh.hasData = !indices.empty();
}

void WorldRenderer::rebuildChunk(const World& world, const BlockRegistry& registry, int chunkX,
                                 int chunkZ) {
    if (chunkX < 0 || chunkZ < 0 || chunkX >= world.chunksX() || chunkZ >= world.chunksZ()) {
        return;
    }
    const int index = chunkZ * world.chunksX() + chunkX;
    if (index < 0 || static_cast<std::size_t>(index) >= meshes_.size()) {
        return;
    }

    ChunkMesh& mesh = meshes_[static_cast<std::size_t>(index)];
    triangleCount_ -= mesh.triangles;

    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> opaqueIndices;
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
    const int transparentIndexCount = static_cast<int>(transparentIndices.size());
    std::vector<std::uint32_t> indices;
    indices.reserve(opaqueIndices.size() + transparentIndices.size() + waterIndices.size());
    indices.insert(indices.end(), opaqueIndices.begin(), opaqueIndices.end());
    indices.insert(indices.end(), transparentIndices.begin(), transparentIndices.end());
    indices.insert(indices.end(), waterIndices.begin(), waterIndices.end());

    uploadMesh(mesh, vertices, indices, opaqueIndexCount, transparentIndexCount);
    mesh.center = {static_cast<float>(baseX) + 8.0f, 64.0f, static_cast<float>(baseZ) + 8.0f};
    triangleCount_ += mesh.triangles;
}

void WorldRenderer::build(const World& world, const BlockRegistry& registry) {
    chunksX_ = world.chunksX();
    chunksZ_ = world.chunksZ();
    meshes_.resize(static_cast<std::size_t>(chunksX_) * chunksZ_);
    triangleCount_ = 0;
    for (int chunkZ = 0; chunkZ < chunksZ_; ++chunkZ) {
        for (int chunkX = 0; chunkX < chunksX_; ++chunkX) {
            rebuildChunk(world, registry, chunkX, chunkZ);
        }
    }
    logInfo("world meshes: %d chunks, %d triangles", chunksX_ * chunksZ_, triangleCount_);
}

void WorldRenderer::rebuildAround(const World& world, const BlockRegistry& registry, int x,
                                  int z) {
    const int chunkX = x / World::kChunkSize;
    const int chunkZ = z / World::kChunkSize;
    rebuildChunk(world, registry, chunkX, chunkZ);
    if (x % World::kChunkSize == 0) {
        rebuildChunk(world, registry, chunkX - 1, chunkZ);
    }
    if (x % World::kChunkSize == World::kChunkSize - 1) {
        rebuildChunk(world, registry, chunkX + 1, chunkZ);
    }
    if (z % World::kChunkSize == 0) {
        rebuildChunk(world, registry, chunkX, chunkZ - 1);
    }
    if (z % World::kChunkSize == World::kChunkSize - 1) {
        rebuildChunk(world, registry, chunkX, chunkZ + 1);
    }
}

void WorldRenderer::renderShadowMap(const World& world, const BlockRegistry& registry,
                                    const Vec3& sunDirection) {
    (void)registry;
    if (!shadowFbo_ || meshes_.empty()) {
        return;
    }

    const Vec3 center{static_cast<float>(world.width()) * 0.5f, 64.0f,
                      static_cast<float>(world.depth()) * 0.5f};
    const Vec3 up = std::abs(sunDirection.y) > 0.98f ? Vec3{1.0f, 0.0f, 0.0f}
                                                     : Vec3{0.0f, 1.0f, 0.0f};
    const Mat4 lightView = Mat4::lookAt(center + sunDirection * 140.0f, center, up);
    const float extent = static_cast<float>(std::max(world.width(), world.depth())) * 0.75f
                       + 12.0f;
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

    for (const auto& mesh : meshes_) {
        if (mesh.opaqueIndices == 0) {
            continue;
        }
        gl::BindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.opaqueIndices, GL_UNSIGNED_INT, nullptr);
    }

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

    for (const auto& mesh : meshes_) {
        if (mesh.opaqueIndices == 0) {
            continue;
        }
        gl::BindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.opaqueIndices, GL_UNSIGNED_INT, nullptr);
    }

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

    std::vector<std::pair<float, const ChunkMesh*>> order;
    for (const auto& mesh : meshes_) {
        if (mesh.transparentIndices == 0) {
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
        const auto offset =
            static_cast<GLsizeiptr>(mesh->opaqueIndices * sizeof(std::uint32_t));
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

    std::vector<std::pair<float, const ChunkMesh*>> order;
    for (const auto& mesh : meshes_) {
        if (mesh.waterIndices == 0) {
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
    waterShader_.setFloat("uWaveAmplitude", 0.32f);
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
            (mesh->opaqueIndices + mesh->transparentIndices) * sizeof(std::uint32_t));
        glDrawElements(GL_TRIANGLES, mesh->waterIndices, GL_UNSIGNED_INT,
                       reinterpret_cast<const void*>(offset));
    }

    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    gl::BindVertexArray(0);
    gl::UseProgram(0);
}

}
