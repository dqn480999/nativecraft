#include "render/WorldRenderer.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "core/Log.hpp"

namespace nc {
namespace {

struct FaceInfo {
    Vec3 corners[4];
    float u[4];
    float v[4];
    float shade;
};

const FaceInfo kFaces[6] = {
    {{{1, 0, 1}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}}, {0, 1, 1, 0}, {1, 1, 0, 0}, 0.72f},
    {{{0, 0, 0}, {0, 0, 1}, {0, 1, 1}, {0, 1, 0}}, {0, 1, 1, 0}, {1, 1, 0, 0}, 0.72f},
    {{{0, 1, 0}, {0, 1, 1}, {1, 1, 1}, {1, 1, 0}}, {0, 0, 1, 1}, {1, 0, 0, 1}, 1.0f},
    {{{0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}}, {0, 1, 1, 0}, {0, 0, 1, 1}, 0.5f},
    {{{0, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 1, 1}}, {0, 1, 1, 0}, {1, 1, 0, 0}, 0.86f},
    {{{1, 0, 0}, {0, 0, 0}, {0, 1, 0}, {1, 1, 0}}, {0, 1, 1, 0}, {1, 1, 0, 0}, 0.86f},
};

const int kOffsets[6][3] = {
    {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

const char* kVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in float aLayer;
layout(location = 3) in vec4 aColor;

uniform mat4 uViewProj;
uniform vec3 uCameraPos;

out vec3 vUV;
out vec4 vColor;
out float vDistance;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vUV = vec3(aUV, aLayer);
    vColor = aColor;
    vDistance = length(aPos - uCameraPos);
}
)";

const char* kFragmentShader = R"(
#version 330 core
in vec3 vUV;
in vec4 vColor;
in float vDistance;

uniform sampler2DArray uAtlas;
uniform vec3 uFogColor;
uniform float uFogStart;
uniform float uFogEnd;

out vec4 FragColor;

void main() {
    vec4 texel = texture(uAtlas, vUV);
    float alpha = texel.a * vColor.a;
    if (alpha < 0.05) {
        discard;
    }
    float fog = clamp((vDistance - uFogStart) / max(0.001, uFogEnd - uFogStart), 0.0, 1.0);
    vec3 color = mix(texel.rgb * vColor.rgb, uFogColor, fog);
    FragColor = vec4(color, alpha);
}
)";

}

bool WorldRenderer::init(TextureArray& textures, std::string* error) {
    textures_ = &textures;
    return shader_.compile(kVertexShader, kFragmentShader, error);
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
    shader_.destroy();
    triangleCount_ = 0;
}

void WorldRenderer::uploadMesh(ChunkMesh& mesh, const std::vector<Vertex>& vertices,
                               const std::vector<std::uint32_t>& indices,
                               int opaqueIndexCount) {
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
    gl::VertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, u)));
    gl::EnableVertexAttribArray(2);
    gl::VertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, layer)));
    gl::EnableVertexAttribArray(3);
    gl::VertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                            reinterpret_cast<void*>(offsetof(Vertex, r)));

    gl::BindVertexArray(0);
    gl::BindBuffer(GL_ARRAY_BUFFER, 0);
    gl::BindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

    mesh.opaqueIndices = opaqueIndexCount;
    mesh.transparentIndices = static_cast<int>(indices.size()) - opaqueIndexCount;
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
                std::vector<std::uint32_t>& target =
                    block.transparentPass ? transparentIndices : opaqueIndices;

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
                    const float shade = info.shade;
                    const float red = shade * block.tints[static_cast<std::size_t>(face)][0];
                    const float green = shade * block.tints[static_cast<std::size_t>(face)][1];
                    const float blue = shade * block.tints[static_cast<std::size_t>(face)][2];

                    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
                    for (int vertex = 0; vertex < 4; ++vertex) {
                        Vertex out;
                        out.x = static_cast<float>(worldX) + info.corners[vertex].x;
                        out.y = static_cast<float>(y) + info.corners[vertex].y;
                        out.z = static_cast<float>(worldZ) + info.corners[vertex].z;
                        out.u = info.u[vertex];
                        out.v = info.v[vertex];
                        out.layer = static_cast<float>(layer);
                        out.r = red;
                        out.g = green;
                        out.b = blue;
                        out.a = block.alpha;
                        vertices.push_back(out);
                    }

                    target.push_back(base + 0);
                    target.push_back(base + 1);
                    target.push_back(base + 2);
                    target.push_back(base + 0);
                    target.push_back(base + 2);
                    target.push_back(base + 3);
                }
            }
        }
    }

    const int opaqueIndexCount = static_cast<int>(opaqueIndices.size());
    std::vector<std::uint32_t> indices;
    indices.reserve(opaqueIndices.size() + transparentIndices.size());
    indices.insert(indices.end(), opaqueIndices.begin(), opaqueIndices.end());
    indices.insert(indices.end(), transparentIndices.begin(), transparentIndices.end());

    uploadMesh(mesh, vertices, indices, opaqueIndexCount);
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

void WorldRenderer::draw(const Mat4& viewProjection, const Vec3& cameraPosition,
                         const Vec3& fogColor, float fogStart, float fogEnd) {
    if (!textures_ || triangleCount_ == 0) {
        return;
    }

    shader_.use();
    shader_.setMat4("uViewProj", viewProjection);
    shader_.setVec3("uCameraPos", cameraPosition);
    shader_.setVec3("uFogColor", fogColor);
    shader_.setFloat("uFogStart", fogStart);
    shader_.setFloat("uFogEnd", fogEnd);
    shader_.setInt("uAtlas", 0);

    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, textures_->id());

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);

    for (const auto& mesh : meshes_) {
        if (mesh.opaqueIndices == 0) {
            continue;
        }
        gl::BindVertexArray(mesh.vao);
        glDrawElements(GL_TRIANGLES, mesh.opaqueIndices, GL_UNSIGNED_INT, nullptr);
    }

    std::vector<std::pair<float, const ChunkMesh*>> transparentOrder;
    for (const auto& mesh : meshes_) {
        if (mesh.transparentIndices == 0) {
            continue;
        }
        const Vec3 delta = mesh.center - cameraPosition;
        transparentOrder.emplace_back(lengthSquared(delta), &mesh);
    }
    std::sort(transparentOrder.begin(), transparentOrder.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    if (!transparentOrder.empty()) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        for (const auto& [distance, mesh] : transparentOrder) {
            (void)distance;
            gl::BindVertexArray(mesh->vao);
            const auto offset = static_cast<GLsizeiptr>(mesh->opaqueIndices * sizeof(std::uint32_t));
            glDrawElements(GL_TRIANGLES, mesh->transparentIndices, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(offset));
        }
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
    }

    gl::BindVertexArray(0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
    gl::UseProgram(0);
    glDisable(GL_CULL_FACE);
}

}
