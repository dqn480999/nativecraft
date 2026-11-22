#include "world/World.hpp"

#include <algorithm>
#include <cmath>

#include "core/Log.hpp"
#include "core/Math.hpp"

namespace nc {
namespace {

std::uint64_t splitmix64(std::uint64_t value) {
    value += 0x9E3779B97F4A7C15ull;
    value = (value ^ (value >> 30u)) * 0xBF58476D1CE4E5B9ull;
    value = (value ^ (value >> 27u)) * 0x94D049BB133111EBull;
    return value ^ (value >> 31u);
}

std::uint32_t hash2(int x, int z, std::uint64_t seed) {
    const std::uint64_t key = seed
                            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 32u)
                            ^ static_cast<std::uint32_t>(z);
    return static_cast<std::uint32_t>(splitmix64(key) >> 32u);
}

std::uint32_t hash3(int x, int y, int z, std::uint64_t seed) {
    const std::uint64_t key = seed
                            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(x)) << 40u)
                            ^ (static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) << 8u)
                            ^ static_cast<std::uint32_t>(y);
    return static_cast<std::uint32_t>(splitmix64(key) >> 32u);
}

float toFloat(std::uint32_t hash) {
    return static_cast<float>(hash & 0x00FFFFFFu) / 16777216.0f;
}

float smoothStep(float t) {
    return t * t * (3.0f - 2.0f * t);
}

float valueNoise(float x, float z, std::uint64_t seed) {
    const int x0 = static_cast<int>(std::floor(x));
    const int z0 = static_cast<int>(std::floor(z));
    const float fx = smoothStep(x - static_cast<float>(x0));
    const float fz = smoothStep(z - static_cast<float>(z0));

    const float v00 = toFloat(hash2(x0, z0, seed));
    const float v10 = toFloat(hash2(x0 + 1, z0, seed));
    const float v01 = toFloat(hash2(x0, z0 + 1, seed));
    const float v11 = toFloat(hash2(x0 + 1, z0 + 1, seed));

    const float top = lerpf(v00, v10, fx);
    const float bottom = lerpf(v01, v11, fx);
    return lerpf(top, bottom, fz);
}

float fbm(float x, float z, std::uint64_t seed, int octaves) {
    float amplitude = 0.5f;
    float frequency = 1.0f;
    float total = 0.0f;
    float normalizer = 0.0f;
    for (int i = 0; i < octaves; ++i) {
        total += valueNoise(x * frequency, z * frequency, seed + static_cast<std::uint64_t>(i) * 7919ull)
               * amplitude;
        normalizer += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return normalizer > 0.0f ? total / normalizer : 0.0f;
}

}

World::World(int chunksX, int chunksZ, std::uint64_t seed)
    : chunksX_(std::max(1, chunksX))
    , chunksZ_(std::max(1, chunksZ))
    , seed_(seed) {
    chunks_.resize(static_cast<std::size_t>(chunksX_) * chunksZ_);
}

int World::localIndex(int x, int y, int z) {
    return (y * kChunkSize + z) * kChunkSize + x;
}

int World::chunkIndex(int chunkX, int chunkZ) const {
    return chunkZ * chunksX_ + chunkX;
}

bool World::inside(int x, int z) const {
    return x >= 0 && z >= 0 && x < width() && z < depth();
}

std::uint8_t World::get(int x, int y, int z) const {
    if (y < 0) {
        return 1;
    }
    if (y >= kHeight || !inside(x, z)) {
        return 0;
    }
    const int chunkX = x / kChunkSize;
    const int chunkZ = z / kChunkSize;
    const int localX = x % kChunkSize;
    const int localZ = z % kChunkSize;
    return chunks_[static_cast<std::size_t>(chunkIndex(chunkX, chunkZ))]
        .blocks[static_cast<std::size_t>(localIndex(localX, y, localZ))];
}

void World::set(int x, int y, int z, std::uint8_t block) {
    if (y < 0 || y >= kHeight || !inside(x, z)) {
        return;
    }
    const int chunkX = x / kChunkSize;
    const int chunkZ = z / kChunkSize;
    const int localX = x % kChunkSize;
    const int localZ = z % kChunkSize;
    chunks_[static_cast<std::size_t>(chunkIndex(chunkX, chunkZ))]
        .blocks[static_cast<std::size_t>(localIndex(localX, y, localZ))] = block;
}

int World::surfaceY(int x, int z) const {
    if (!inside(x, z)) {
        return 0;
    }
    for (int y = kHeight - 1; y >= 0; --y) {
        const std::uint8_t block = get(x, y, z);
        if (block != 0) {
            return y;
        }
    }
    return 0;
}

void World::generate(const BlockRegistry& registry) {
    const int stone = registry.id("stone");
    const int dirt = registry.id("dirt");
    const int grass = registry.id("grass");
    const int sand = registry.id("sand");
    const int water = registry.id("water");
    const int bedrock = registry.id("bedrock");
    const int coal = registry.id("coal_ore");
    const int iron = registry.id("iron_ore");
    const int log = registry.id("oak_log");
    const int leaves = registry.id("oak_leaves");
    const int gravel = registry.id("gravel");

    constexpr int seaLevel = 56;
    const int width = this->width();
    const int depth = this->depth();

    for (int z = 0; z < depth; ++z) {
        for (int x = 0; x < width; ++x) {
            const float base = fbm(static_cast<float>(x) * 0.014f, static_cast<float>(z) * 0.014f,
                                   seed_, 4);
            const float detail = fbm(static_cast<float>(x) * 0.06f, static_cast<float>(z) * 0.06f,
                                     seed_ ^ 0xA5A5A5A5ull, 3);
            int height = 44 + static_cast<int>(base * 24.0f) + static_cast<int>(detail * 7.0f);
            height = std::clamp(height, 8, kHeight - 12);

            const bool beach = height <= seaLevel + 1;
            for (int y = 0; y < kHeight; ++y) {
                int block = 0;
                if (y == 0) {
                    block = bedrock;
                } else if (y < height - 4) {
                    block = stone;
                    const std::uint32_t ore = hash3(x, y, z, seed_ ^ 0x12345678ull);
                    if (ore % 1409u == 0) {
                        block = coal;
                    } else if (ore % 2803u == 0) {
                        block = iron;
                    } else if (ore % 4099u == 0) {
                        block = gravel;
                    }
                } else if (y < height - 1) {
                    block = beach ? sand : dirt;
                } else if (y == height - 1) {
                    if (height <= seaLevel) {
                        block = sand;
                    } else if (beach) {
                        block = sand;
                    } else {
                        block = grass;
                    }
                } else if (y < seaLevel) {
                    block = water;
                }
                if (block != 0) {
                    set(x, y, z, static_cast<std::uint8_t>(block));
                }
            }
        }
    }

    for (int z = 3; z < depth - 3; ++z) {
        for (int x = 3; x < width - 3; ++x) {
            const std::uint32_t treeHash = hash3(x, 0, z, seed_ ^ 0xC0FFEEull);
            if (treeHash % 233u != 0) {
                continue;
            }
            const int ground = surfaceY(x, z);
            if (ground + 8 >= kHeight || get(x, ground, z) != grass) {
                continue;
            }

            const int trunkHeight = 4 + static_cast<int>((treeHash >> 8u) % 3u);
            for (int i = 1; i <= trunkHeight; ++i) {
                set(x, ground + i, z, static_cast<std::uint8_t>(log));
            }

            for (int dy = -2; dy <= 2; ++dy) {
                const int radius = (dy == 2) ? 1 : 2;
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) + std::abs(dy) > 3) {
                            continue;
                        }
                        const int lx = x + dx;
                        const int ly = ground + trunkHeight + dy;
                        const int lz = z + dz;
                        if (get(lx, ly, lz) == 0) {
                            set(lx, ly, lz, static_cast<std::uint8_t>(leaves));
                        }
                    }
                }
            }
        }
    }

    logInfo("world generated: %dx%dx%d, seed=%llu", width, kHeight, depth,
            static_cast<unsigned long long>(seed_));
}

}
