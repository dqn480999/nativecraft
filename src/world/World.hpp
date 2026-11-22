#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "core/Math.hpp"
#include "core/ThreadPool.hpp"
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

struct BlockPos {
    int x = 0;
    int y = 0;
    int z = 0;

    bool operator==(const BlockPos& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct BlockPosHash {
    std::size_t operator()(const BlockPos& pos) const noexcept;
};

class World {
public:
    static constexpr int kChunkSize = 16;
    static constexpr int kHeight = 128;
    static constexpr int kSeaLevel = 56;
    static constexpr int kChunkVolume = kChunkSize * kChunkSize * kHeight;

    World(std::uint64_t seed, std::string saveDirectory = {}, int workerThreads = 0);
    ~World();

    void updateStreaming(const Vec3& playerPosition, int radius,
                         const BlockRegistry& registry);
    void pumpCompleted();
    bool isStreamingIdle() const { return pending_.empty(); }
    void scheduleFluid(int x, int y, int z);
    std::vector<ChunkPos> updateFluids(const BlockRegistry& registry, int budget);

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

    struct ChunkData {
        ChunkPos pos;
        std::array<std::uint8_t, kChunkVolume> blocks{};
        bool loadedFromDisk = false;
    };

    struct PendingJob {
        ChunkPos pos;
        std::future<ChunkData> future;
    };

    static int localIndex(int x, int y, int z);
    static int floorDiv(int value, int divisor);
    static void generateChunkBlocks(const ChunkPos& pos,
                                    std::array<std::uint8_t, kChunkVolume>& blocks,
                                    std::uint64_t seed, const BlockRegistry& registry);
    Chunk* findChunk(const ChunkPos& pos);
    const Chunk* findChunk(const ChunkPos& pos) const;
    bool loadChunkBlocks(const ChunkPos& pos,
                         std::array<std::uint8_t, kChunkVolume>& blocks) const;
    void saveChunk(const ChunkPos& pos, const Chunk& chunk);
    std::string chunkFilePath(const ChunkPos& pos) const;

    std::uint64_t seed_ = 0;
    std::string saveDirectory_;
    std::unordered_map<ChunkPos, Chunk, ChunkPosHash> chunks_;
    std::unordered_set<ChunkPos, ChunkPosHash> pending_;
    std::deque<BlockPos> fluidQueue_;
    std::unordered_set<BlockPos, BlockPosHash> fluidQueued_;
    std::vector<PendingJob> pendingJobs_;
    ChunkPos streamCenter_{};
    int streamRadius_ = 6;
    bool hasStreamCenter_ = false;
    std::unique_ptr<ThreadPool> pool_;
};

}
