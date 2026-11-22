#include "world/World.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <utility>

#include "core/Log.hpp"

namespace nc {
namespace {

constexpr std::uint32_t kChunkMagic = 0x3148434Eu;
constexpr std::uint32_t kChunkVersion = 1;

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
        total += valueNoise(x * frequency, z * frequency,
                            seed + static_cast<std::uint64_t>(i) * 7919ull)
               * amplitude;
        normalizer += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }
    return normalizer > 0.0f ? total / normalizer : 0.0f;
}

}

std::size_t ChunkPosHash::operator()(const ChunkPos& pos) const noexcept {
    std::uint64_t value = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(pos.x)) << 32u)
                        ^ static_cast<std::uint32_t>(pos.z);
    value ^= value >> 33u;
    value *= 0xff51afd7ed558ccdull;
    value ^= value >> 33u;
    return static_cast<std::size_t>(value >> 32u);
}

World::World(std::uint64_t seed, std::string saveDirectory, int workerThreads)
    : seed_(seed)
    , saveDirectory_(std::move(saveDirectory)) {
    if (workerThreads <= 0) {
        const unsigned int hardware = std::thread::hardware_concurrency();
        workerThreads = hardware > 2 ? std::min(static_cast<int>(hardware) - 1, 8) : 1;
    }
    workerThreads = std::clamp(workerThreads, 1, 16);
    pool_ = std::make_unique<ThreadPool>(static_cast<std::size_t>(workerThreads));
    logInfo("world: %d chunk worker threads", workerThreads);
}

World::~World() {
    saveAll();
}

int World::floorDiv(int value, int divisor) {
    const int quotient = value / divisor;
    const int remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

int World::chunkCoord(int worldCoord) {
    return floorDiv(worldCoord, kChunkSize);
}

int World::localCoord(int worldCoord) {
    const int remainder = worldCoord % kChunkSize;
    return remainder < 0 ? remainder + kChunkSize : remainder;
}

ChunkPos World::chunkOf(int x, int z) {
    return {chunkCoord(x), chunkCoord(z)};
}

int World::localIndex(int x, int y, int z) {
    return (y * kChunkSize + z) * kChunkSize + x;
}

World::Chunk* World::findChunk(const ChunkPos& pos) {
    const auto it = chunks_.find(pos);
    return it == chunks_.end() ? nullptr : &it->second;
}

const World::Chunk* World::findChunk(const ChunkPos& pos) const {
    const auto it = chunks_.find(pos);
    return it == chunks_.end() ? nullptr : &it->second;
}

bool World::isChunkLoaded(const ChunkPos& pos) const {
    const Chunk* chunk = findChunk(pos);
    return chunk != nullptr && chunk->generated;
}

std::vector<ChunkPos> World::loadedChunks() const {
    std::vector<ChunkPos> result;
    result.reserve(chunks_.size());
    for (const auto& [pos, chunk] : chunks_) {
        if (chunk.generated) {
            result.push_back(pos);
        }
    }
    return result;
}

std::string World::chunkFilePath(const ChunkPos& pos) const {
    if (saveDirectory_.empty()) {
        return {};
    }
    return saveDirectory_ + "/chunks/" + std::to_string(pos.x) + "_"
         + std::to_string(pos.z) + ".nc";
}

bool World::loadChunkBlocks(const ChunkPos& pos,
                             std::array<std::uint8_t, kChunkVolume>& blocks) const {
    const std::string path = chunkFilePath(pos);
    if (path.empty()) {
        return false;
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    std::uint32_t magic = 0;
    std::uint32_t version = 0;
    std::int32_t x = 0;
    std::int32_t z = 0;
    std::uint32_t volume = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    file.read(reinterpret_cast<char*>(&x), sizeof(x));
    file.read(reinterpret_cast<char*>(&z), sizeof(z));
    file.read(reinterpret_cast<char*>(&volume), sizeof(volume));
    if (!file || magic != kChunkMagic || version != kChunkVersion || x != pos.x
        || z != pos.z || volume != kChunkVolume) {
        return false;
    }
    file.read(reinterpret_cast<char*>(blocks.data()), kChunkVolume);
    return static_cast<bool>(file);
}

void World::saveChunk(const ChunkPos& pos, const Chunk& chunk) {
    const std::string path = chunkFilePath(pos);
    if (path.empty()) {
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
    if (error) {
        logWarn("cannot create chunk directory: %s", error.message().c_str());
        return;
    }

    const std::string temporary = path + ".tmp";
    std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
    if (!file) {
        logWarn("cannot write chunk file: %s", temporary.c_str());
        return;
    }
    const std::int32_t x = pos.x;
    const std::int32_t z = pos.z;
    const std::uint32_t volume = kChunkVolume;
    file.write(reinterpret_cast<const char*>(&kChunkMagic), sizeof(kChunkMagic));
    file.write(reinterpret_cast<const char*>(&kChunkVersion), sizeof(kChunkVersion));
    file.write(reinterpret_cast<const char*>(&x), sizeof(x));
    file.write(reinterpret_cast<const char*>(&z), sizeof(z));
    file.write(reinterpret_cast<const char*>(&volume), sizeof(volume));
    file.write(reinterpret_cast<const char*>(chunk.blocks.data()), kChunkVolume);
    file.close();
    if (!file) {
        logWarn("failed to write chunk file: %s", temporary.c_str());
        return;
    }

    std::filesystem::rename(temporary, path, error);
    if (error) {
        logWarn("cannot rename chunk file: %s", error.message().c_str());
    }
}

void World::saveChunkIfModified(const ChunkPos& pos) {
    const Chunk* chunk = findChunk(pos);
    if (chunk && chunk->generated && chunk->modified) {
        saveChunk(pos, *chunk);
    }
}

void World::unload(const ChunkPos& pos) {
    const auto it = chunks_.find(pos);
    if (it == chunks_.end()) {
        return;
    }
    if (it->second.modified) {
        saveChunk(pos, it->second);
    }
    chunks_.erase(it);
}

void World::saveAll() {
    for (auto& [pos, chunk] : chunks_) {
        if (chunk.generated && chunk.modified) {
            saveChunk(pos, chunk);
            chunk.modified = false;
        }
    }
}

void World::generateChunkBlocks(const ChunkPos& pos,
                               std::array<std::uint8_t, kChunkVolume>& blocks,
                               std::uint64_t seed, const BlockRegistry& registry) {
    const int stone = registry.id("stone");
    const int dirt = registry.id("dirt");
    const int grass = registry.id("grass");
    const int sand = registry.id("sand");
    const int water = registry.id("water");
    const int bedrock = registry.id("bedrock");
    const int coal = registry.id("coal_ore");
    const int iron = registry.id("iron_ore");
    const int gravel = registry.id("gravel");
    const int log = registry.id("oak_log");
    const int leaves = registry.id("oak_leaves");
    const int tallGrass = registry.id("tall_grass");
    const int meadowGrass = registry.id("meadow_grass");
    const int dryGrass = registry.id("dry_grass");
    const int fern = registry.id("fern");

    std::array<int, kChunkSize * kChunkSize> heights{};
    const int baseX = pos.x * kChunkSize;
    const int baseZ = pos.z * kChunkSize;

    for (int localZ = 0; localZ < kChunkSize; ++localZ) {
        for (int localX = 0; localX < kChunkSize; ++localX) {
            const int worldX = baseX + localX;
            const int worldZ = baseZ + localZ;
            const float base = fbm(static_cast<float>(worldX) * 0.014f,
                                   static_cast<float>(worldZ) * 0.014f, seed, 4);
            const float detail = fbm(static_cast<float>(worldX) * 0.06f,
                                     static_cast<float>(worldZ) * 0.06f,
                                     seed ^ 0xA5A5A5A5ull, 3);
            const float continent = fbm(static_cast<float>(worldX) * 0.0038f,
                                        static_cast<float>(worldZ) * 0.0038f,
                                        seed ^ 0xC0A57ull, 3);
            int height = 48 + static_cast<int>(base * 22.0f)
                       + static_cast<int>(detail * 6.0f)
                       + static_cast<int>((continent - 0.5f) * 28.0f);
            height = std::clamp(height, 8, kHeight - 12);
            heights[localZ * kChunkSize + localX] = height;

            const bool beach = height <= kSeaLevel + 1;
            for (int y = 0; y < kHeight; ++y) {
                int block = 0;
                if (y == 0) {
                    block = bedrock;
                } else if (y < height - 4) {
                    block = stone;
                    const std::uint32_t ore = hash3(worldX, y, worldZ, seed ^ 0x12345678ull);
                    if (ore % 1409u == 0u) {
                        block = coal;
                    } else if (ore % 2803u == 0u) {
                        block = iron;
                    } else if (ore % 4099u == 0u) {
                        block = gravel;
                    }
                } else if (y < height - 1) {
                    block = beach ? sand : dirt;
                } else if (y == height - 1) {
                    block = (height <= kSeaLevel || beach) ? sand : grass;
                } else if (y < kSeaLevel) {
                    block = water;
                }
                if (block != 0) {
                    blocks[static_cast<std::size_t>(localIndex(localX, y, localZ))] =
                        static_cast<std::uint8_t>(block);
                }
            }
        }
    }

    for (int localZ = 3; localZ < kChunkSize - 3; ++localZ) {
        for (int localX = 3; localX < kChunkSize - 3; ++localX) {
            const int worldX = baseX + localX;
            const int worldZ = baseZ + localZ;
            const std::uint32_t treeHash = hash3(worldX, 0, worldZ, seed ^ 0xC0FFEEull);
            if (treeHash % 233u != 0u) {
                continue;
            }
            const int ground = heights[localZ * kChunkSize + localX] - 1;
            if (ground + 9 >= kHeight
                || blocks[static_cast<std::size_t>(localIndex(localX, ground, localZ))]
                       != grass) {
                continue;
            }

            const int trunkHeight = 4 + static_cast<int>((treeHash >> 8u) % 3u);
            for (int i = 1; i <= trunkHeight; ++i) {
                blocks[static_cast<std::size_t>(localIndex(localX, ground + i, localZ))] =
                    static_cast<std::uint8_t>(log);
            }

            for (int dy = -2; dy <= 2; ++dy) {
                const int radius = (dy == 2) ? 1 : 2;
                for (int dz = -radius; dz <= radius; ++dz) {
                    for (int dx = -radius; dx <= radius; ++dx) {
                        if (std::abs(dx) + std::abs(dz) + std::abs(dy) > 3) {
                            continue;
                        }
                        const int lx = localX + dx;
                        const int ly = ground + trunkHeight + dy;
                        const int lz = localZ + dz;
                        if (lx < 0 || lx >= kChunkSize || lz < 0 || lz >= kChunkSize
                            || ly < 0 || ly >= kHeight) {
                            continue;
                        }
                        std::uint8_t& target =
                            blocks[static_cast<std::size_t>(localIndex(lx, ly, lz))];
                        if (target == 0) {
                            target = static_cast<std::uint8_t>(leaves);
                        }
                    }
                }
            }
        }
    }

    for (int localZ = 1; localZ < kChunkSize - 1; ++localZ) {
        for (int localX = 1; localX < kChunkSize - 1; ++localX) {
            const int worldX = baseX + localX;
            const int worldZ = baseZ + localZ;
            if (hash3(worldX, 1, worldZ, seed ^ 0x51A55ull) % 3u == 0u) {
                continue;
            }
            const int ground = heights[localZ * kChunkSize + localX] - 1;
            if (ground < 1 || ground + 1 >= kHeight) {
                continue;
            }
            if (blocks[static_cast<std::size_t>(localIndex(localX, ground, localZ))]
                    != grass
                || blocks[static_cast<std::size_t>(localIndex(localX, ground + 1, localZ))]
                       != 0) {
                continue;
            }

            const float biome = fbm(static_cast<float>(worldX) * 0.014f,
                                    static_cast<float>(worldZ) * 0.014f,
                                    seed ^ 0xB10Eull, 3);
            int vegetation = tallGrass;
            if (biome < 0.32f) {
                vegetation = dryGrass;
            } else if (biome < 0.55f) {
                vegetation = tallGrass;
            } else if (biome < 0.78f) {
                vegetation = meadowGrass;
            } else {
                vegetation = fern;
            }

            const std::uint32_t grassHash = hash3(worldX, 2, worldZ, seed ^ 0xA11CEull);
            if (grassHash % 11u == 0u) {
                vegetation = dryGrass;
            } else if (grassHash % 13u == 0u) {
                vegetation = meadowGrass;
            } else if (grassHash % 17u == 0u) {
                vegetation = fern;
            }
            if (vegetation <= 0) {
                vegetation = tallGrass;
            }
            if (vegetation > 0) {
                blocks[static_cast<std::size_t>(localIndex(localX, ground + 1, localZ))] =
                    static_cast<std::uint8_t>(vegetation);
            }
        }
    }

}

void World::updateStreaming(const Vec3& playerPosition, int radius,
                            const BlockRegistry& registry) {
    radius = std::max(1, radius);
    const int playerX = static_cast<int>(std::floor(playerPosition.x));
    const int playerZ = static_cast<int>(std::floor(playerPosition.z));
    const ChunkPos center = chunkOf(playerX, playerZ);
    streamCenter_ = center;
    streamRadius_ = radius;
    hasStreamCenter_ = true;
    const int generateRadius = radius + 1;

    int queued = 0;
    for (int dz = -generateRadius; dz <= generateRadius; ++dz) {
        for (int dx = -generateRadius; dx <= generateRadius; ++dx) {
            const ChunkPos pos{center.x + dx, center.z + dz};
            if (chunks_.find(pos) != chunks_.end() || pending_.find(pos) != pending_.end()) {
                continue;
            }
            pending_.insert(pos);
            auto future = pool_->submit([this, pos, &registry]() {
                ChunkData data;
                data.pos = pos;
                if (loadChunkBlocks(pos, data.blocks)) {
                    data.loadedFromDisk = true;
                } else {
                    generateChunkBlocks(pos, data.blocks, seed_, registry);
                }
                return data;
            });
            pendingJobs_.push_back(PendingJob{pos, std::move(future)});
            ++queued;
        }
    }

    std::vector<ChunkPos> toUnload;
    for (const auto& [pos, chunk] : chunks_) {
        (void)chunk;
        if (std::abs(pos.x - center.x) > radius + 2
            || std::abs(pos.z - center.z) > radius + 2) {
            toUnload.push_back(pos);
        }
    }
    for (const ChunkPos& pos : toUnload) {
        unload(pos);
    }

    if (queued > 0) {
        logInfo("streaming: queued=%d pending=%zu loaded=%zu center=%d,%d", queued,
                pending_.size(), chunks_.size(), center.x, center.z);
    }
}

void World::pumpCompleted() {
    int completed = 0;
    for (auto it = pendingJobs_.begin(); it != pendingJobs_.end();) {
        if (it->future.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
            ++it;
            continue;
        }

        ChunkData data;
        try {
            data = it->future.get();
        } catch (const std::exception& ex) {
            logWarn("chunk job failed: %s", ex.what());
            pending_.erase(it->pos);
            it = pendingJobs_.erase(it);
            continue;
        }

        pending_.erase(data.pos);
        if (hasStreamCenter_
            && (std::abs(data.pos.x - streamCenter_.x) > streamRadius_ + 2
                || std::abs(data.pos.z - streamCenter_.z) > streamRadius_ + 2)) {
            it = pendingJobs_.erase(it);
            continue;
        }
        Chunk& chunk = chunks_[data.pos];
        chunk.blocks = data.blocks;
        chunk.generated = true;
        chunk.modified = false;
        if (data.loadedFromDisk) {
            logInfo("loaded chunk %d,%d from disk", data.pos.x, data.pos.z);
        }
        it = pendingJobs_.erase(it);
        ++completed;
    }

    if (completed > 0) {
        logInfo("streaming: +%d chunks, loaded=%zu, pending=%zu", completed, chunks_.size(),
                pending_.size());
    }
}

std::uint8_t World::get(int x, int y, int z) const {
    if (y < 0) {
        return 1;
    }
    if (y >= kHeight) {
        return 0;
    }
    const ChunkPos pos = chunkOf(x, z);
    const Chunk* chunk = findChunk(pos);
    if (!chunk || !chunk->generated) {
        return 0;
    }
    return chunk->blocks[static_cast<std::size_t>(
        localIndex(localCoord(x), y, localCoord(z)))];
}

void World::set(int x, int y, int z, std::uint8_t block) {
    if (y < 0 || y >= kHeight) {
        return;
    }
    const ChunkPos pos = chunkOf(x, z);
    Chunk* chunk = findChunk(pos);
    if (!chunk || !chunk->generated) {
        return;
    }
    chunk->blocks[static_cast<std::size_t>(localIndex(localCoord(x), y, localCoord(z)))] =
        block;
    chunk->modified = true;
}

int World::surfaceY(int x, int z) const {
    for (int y = kHeight - 1; y >= 0; --y) {
        if (get(x, y, z) != 0) {
            return y;
        }
    }
    return 0;
}

}
