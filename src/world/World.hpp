#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "world/Blocks.hpp"

namespace nc {

class World {
public:
    static constexpr int kChunkSize = 16;
    static constexpr int kHeight = 128;
    static constexpr int kSeaLevel = 56;
    static constexpr int kChunkVolume = kChunkSize * kChunkSize * kHeight;

    World(int chunksX, int chunksZ, std::uint64_t seed);

    void generate(const BlockRegistry& registry);

    std::uint8_t get(int x, int y, int z) const;
    void set(int x, int y, int z, std::uint8_t block);

    int chunksX() const { return chunksX_; }
    int chunksZ() const { return chunksZ_; }
    int width() const { return chunksX_ * kChunkSize; }
    int depth() const { return chunksZ_ * kChunkSize; }
    std::uint64_t seed() const { return seed_; }

    int surfaceY(int x, int z) const;

private:
    struct Chunk {
        std::array<std::uint8_t, kChunkVolume> blocks{};
    };

    static int localIndex(int x, int y, int z);
    int chunkIndex(int chunkX, int chunkZ) const;
    bool inside(int x, int z) const;

    int chunksX_ = 0;
    int chunksZ_ = 0;
    std::uint64_t seed_ = 0;
    std::vector<Chunk> chunks_;
};

}
