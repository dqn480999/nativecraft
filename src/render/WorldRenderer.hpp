#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.hpp"
#include "render/GL.hpp"
#include "render/Shader.hpp"
#include "render/TextureArray.hpp"
#include "world/Blocks.hpp"
#include "world/World.hpp"

namespace nc {

class WorldRenderer {
public:
    bool init(TextureArray& textures, std::string* error = nullptr);
    void destroy();

    void build(const World& world, const BlockRegistry& registry);
    void rebuildChunk(const World& world, const BlockRegistry& registry, int chunkX,
                      int chunkZ);
    void rebuildAround(const World& world, const BlockRegistry& registry, int x, int z);

    void draw(const Mat4& viewProjection, const Vec3& cameraPosition, const Vec3& fogColor,
              float fogStart, float fogEnd);

    int triangleCount() const { return triangleCount_; }
    int chunkCount() const { return static_cast<int>(meshes_.size()); }

private:
    struct Vertex {
        float x;
        float y;
        float z;
        float u;
        float v;
        float layer;
        float r;
        float g;
        float b;
        float a;
    };

    struct ChunkMesh {
        GLuint vao = 0;
        GLuint vbo = 0;
        GLuint ebo = 0;
        int opaqueIndices = 0;
        int transparentIndices = 0;
        int triangles = 0;
        Vec3 center{};
        bool hasData = false;
    };

    void uploadMesh(ChunkMesh& mesh, const std::vector<Vertex>& vertices,
                    const std::vector<std::uint32_t>& indices, int opaqueIndexCount);

    Shader shader_;
    TextureArray* textures_ = nullptr;
    std::vector<ChunkMesh> meshes_;
    int chunksX_ = 0;
    int chunksZ_ = 0;
    int triangleCount_ = 0;
};

}
