#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/Math.hpp"
#include "world/Blocks.hpp"

namespace nc {

struct ChunkPos {
    int x = 0;
    int z = 0;

    bool operator==(const ChunkPos& other) const {
        return x == other.x && z == other.z;
    }
};

struct ChunkPosHash {
    std::size_t operator()(const ChunkPos& pos) const noexcept;
};

class World {
public:
    static constexpr int kChunkSize = 16;
    static constexpr int kHeight = 128;
    static constexpr int kSeaLevel = 56;
    static constexpr int kChunkVolume = kChunkSize * kChunkSize * kHeight;

    World(std::uint64_t seed, std::string saveDirectory = {});
    ~World();

    void updateStreaming(const Vec3& playerPosition, int radius,
                         const BlockRegistry& registry);

    std::uint8_t get(int x, int y, int z) const;
    void set(int x, int y, int z, std::uint8_t block);
    int surfaceY(int x, int z) const;

    bool isChunkLoaded(const ChunkPos& pos) const;
    std::vector<ChunkPos> loadedChunks() const;
    std::uint64_t seed() const { return seed_; }

    static ChunkPos chunkOf(int x, int z);
    static int chunkCoord(int worldCoord);
    static int localCoord(int worldCoord);

    void saveAll();
    void saveChunkIfModified(const ChunkPos& pos);
    void unload(const ChunkPos& pos);

private:
    struct Chunk {
        std::array<std::uint8_t, kChunkVolume> blocks{};
        bool generated = false;
        bool modified = false;
    };

    static int localIndex(int x, int y, int z);
    static int floorDiv(int value, int divisor);
    Chunk* findChunk(const ChunkPos& pos);
    const Chunk* findChunk(const ChunkPos& pos) const;
    Chunk& ensureChunk(const ChunkPos& pos, const BlockRegistry& registry);
    void generateChunk(const ChunkPos& pos, Chunk& chunk, const BlockRegistry& registry);
    bool loadChunk(const ChunkPos& pos, Chunk& chunk);
    void saveChunk(const ChunkPos& pos, const Chunk& chunk);
    std::string chunkFilePath(const ChunkPos& pos) const;

    std::uint64_t seed_ = 0;
    std::string saveDirectory_;
    std::unordered_map<ChunkPos, Chunk, ChunkPosHash> chunks_;
};

}
