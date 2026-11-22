#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
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

    void syncMeshes(const World& world, const BlockRegistry& registry, const ChunkPos& center,
                    int renderRadius, int grassRadius);
    bool isSyncedFor(const World& world, const ChunkPos& center, int renderRadius,
                     int grassRadius) const;
    void rebuildChunk(const World& world, const BlockRegistry& registry, int chunkX,
                      int chunkZ, bool includeGrass);
    void rebuildChunkAt(const World& world, const BlockRegistry& registry,
                        const ChunkPos& pos);
    void rebuildAround(const World& world, const BlockRegistry& registry, int x, int z);

    void renderShadowMap(const Vec3& focus, const Vec3& sunDirection, float time,
                         const Vec3& windDirection, float windStrength,
                         const Vec3& playerPosition);
    void setWaterState(float waterLevel, float time, bool underwater);

    void drawOpaque(const Mat4& viewProjection, const Vec3& cameraPosition,
                    const Environment& environment, float fogStart, float fogEnd,
                    bool useClip, float clipY, bool mirrored);

    void drawGrass(const Mat4& viewProjection, const Vec3& cameraPosition,
                   const Environment& environment, float fogStart, float fogEnd, float time,
                   const Vec3& playerPosition, const Vec3& windDirection,
                   float windStrength);

    void drawTransparent(const Mat4& viewProjection, const Vec3& cameraPosition,
                         const Environment& environment, float fogStart, float fogEnd);

    void drawWater(const Mat4& viewProjection, const Vec3& cameraPosition,
                   const Environment& environment, float fogStart, float fogEnd,
                   GLuint reflectionTexture, const Mat4& reflectionViewProjection,
                   float time, float waterLevel);

    int triangleCount() const { return triangleCount_; }
    int chunkCount() const { return static_cast<int>(meshes_.size()); }
    GLuint shadowTexture() const { return shadowTexture_; }
    void invalidateShadowMap() { shadowMapValid_ = false; shadowMapNearValid_ = false; }

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
        int grassIndices = 0;
        int transparentIndices = 0;
        int waterIndices = 0;
        int triangles = 0;
        Vec3 center{};
        Aabb bounds{};
        bool grassEnabled = false;
        bool hasData = false;
    };

    void uploadMesh(ChunkMesh& mesh, const std::vector<Vertex>& vertices,
                    const std::vector<std::uint32_t>& indices, int opaqueIndexCount,
                    int grassIndexCount, int transparentIndexCount);
    void setWorldUniforms(const Mat4& viewProjection, const Vec3& cameraPosition,
                          const Environment& environment, float fogStart, float fogEnd,
                          bool useClip, float clipY);
    void destroyMesh(ChunkMesh& mesh);
    void renderShadowTarget(GLuint framebuffer, int size, const Mat4& lightViewProjection,
                            bool includeGrass, float time, const Vec3& windDirection,
                            float windStrength, const Vec3& playerPosition);

    Shader shader_;
    Shader shadowShader_;
    Shader grassShadowShader_;
    Shader grassShader_;
    Shader waterShader_;
    TextureArray* textures_ = nullptr;
    std::unordered_map<ChunkPos, ChunkMesh, ChunkPosHash> meshes_;
    int triangleCount_ = 0;
    float waterLevel_ = 56.0f;
    float time_ = 0.0f;
    bool underwater_ = false;

    GLuint shadowFbo_ = 0;
    GLuint shadowTexture_ = 0;
    int shadowSize_ = 4096;
    Mat4 lightViewProjection_;
    Vec3 lastShadowSun_{};
    Vec3 lastShadowFocus_{};
    bool shadowMapValid_ = false;

    GLuint shadowFboNear_ = 0;
    GLuint shadowTextureNear_ = 0;
    int shadowSizeNear_ = 4096;
    Mat4 lightViewProjectionNear_;
    Vec3 lastNearShadowSun_{};
    Vec3 lastNearShadowFocus_{};
    float lastNearShadowTime_ = 0.0f;
    bool shadowMapNearValid_ = false;
};

}
