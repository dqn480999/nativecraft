#pragma once

#include <array>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "assets/Assets.hpp"
#include "core/Math.hpp"
#include "render/TextureArray.hpp"

namespace nc {

enum class Face : int {
    East = 0,
    West = 1,
    Up = 2,
    Down = 3,
    South = 4,
    North = 5,
};

constexpr int kFaceCount = 6;

struct BlockDef {
    std::string name;
    std::string displayName;
    std::array<int, kFaceCount> layers{-1, -1, -1, -1, -1, -1};
    std::array<std::array<float, 3>, kFaceCount> tints{{
        {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
        {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f}, {1.0f, 1.0f, 1.0f},
    }};
    float alpha = 1.0f;
    bool opaque = true;
    bool solid = true;
    bool transparentPass = false;
    bool liquid = false;
    bool cross = false;
    bool slab = false;
    float lightEmission = 0.0f;
    int grassBladeMin = 9;
    int grassBladeMax = 13;
    float grassHeightMin = 0.32f;
    float grassHeightMax = 0.94f;
};

class BlockRegistry {
public:
    bool load(Assets& assets, TextureArray& textures, std::string* error = nullptr);

    int id(std::string_view name) const;
    const BlockDef& def(int id) const;
    std::size_t count() const { return blocks_.size(); }

    static constexpr int Air = 0;

    int air() const { return Air; }
    bool isOpaque(int id) const;
    bool isSolid(int id) const;
    bool isLiquid(int id) const;
    bool usesTransparentPass(int id) const;

    const std::vector<std::string>& hotbarNames() const { return hotbarNames_; }

private:
    bool addFromModel(const std::string& name, Assets& assets, TextureArray& textures,
                      std::string* error);
    bool addWaterOrLava(const std::string& name, Assets& assets, TextureArray& textures);
    bool addCrossVegetation(const std::string& name, const Vec3& tint, int bladeMin,
                            int bladeMax, float heightMin, float heightMax);
    bool addSlabBlock(const std::string& name, const std::string& baseName,
                      const std::string& displayName);
    int textureLayer(Assets& assets, TextureArray& textures, const std::string& textureName);
    std::string lookupDisplayName(const std::string& blockName) const;
    std::map<std::string, std::string> lang_;

    std::vector<BlockDef> blocks_;
    std::unordered_map<std::string, int> ids_;
    std::unordered_map<std::string, int> textureLayers_;
    std::vector<std::string> hotbarNames_;
    int fallbackLayer_ = 0;
};

}
