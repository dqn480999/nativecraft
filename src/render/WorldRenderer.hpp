#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.hpp"
#include "render/Environment.hpp"
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

    void renderShadowMap(const World& world, const BlockRegistry& registry,
                         const Vec3& sunDirection);

    void drawOpaque(const Mat4& viewProjection, const Vec3& cameraPosition,
                    const Environment& environment, float fogStart, float fogEnd,
                    bool useClip, float clipY, bool mirrored);

    void drawTransparent(const Mat4& viewProjection, const Vec3& cameraPosition,
                         const Environment& environment, float fogStart, float fogEnd);

    void drawWater(const Mat4& viewProjection, const Vec3& cameraPosition,
                   const Environment& environment, float fogStart, float fogEnd,
                   GLuint reflectionTexture, const Mat4& reflectionViewProjection,
                   float time, float waterLevel);

    int triangleCount() const { return triangleCount_; }
    int chunkCount() const { return static_cast<int>(meshes_.size()); }
    GLuint shadowTexture() const { return shadowTexture_; }

private:
    struct Vertex {
        float x;
        float y;
        float z;
        float nx;
        float ny;
        float nz;
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
        int waterIndices = 0;
        int triangles = 0;
        Vec3 center{};
        bool hasData = false;
    };

    void uploadMesh(ChunkMesh& mesh, const std::vector<Vertex>& vertices,
                    const std::vector<std::uint32_t>& indices, int opaqueIndexCount,
                    int transparentIndexCount);
    void setWorldUniforms(const Mat4& viewProjection, const Vec3& cameraPosition,
                          const Environment& environment, float fogStart, float fogEnd,
                          bool useClip, float clipY);

    Shader shader_;
    Shader shadowShader_;
    Shader waterShader_;
    TextureArray* textures_ = nullptr;
    std::vector<ChunkMesh> meshes_;
    int chunksX_ = 0;
    int chunksZ_ = 0;
    int triangleCount_ = 0;

    GLuint shadowFbo_ = 0;
    GLuint shadowTexture_ = 0;
    int shadowSize_ = 2048;
    Mat4 lightViewProjection_;
};

}
