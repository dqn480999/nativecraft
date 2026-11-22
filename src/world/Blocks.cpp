#include "world/Blocks.hpp"

#include <algorithm>
#include <set>

#include "core/Log.hpp"

namespace nc {
namespace {

const std::map<std::string, std::string> kBlockLangKeys = {
    {"stone", "tile.stone.stone.name"},
    {"granite", "tile.stone.granite.name"},
    {"diorite", "tile.stone.diorite.name"},
    {"andesite", "tile.stone.andesite.name"},
    {"cobblestone", "tile.stonebrick.name"},
    {"dirt", "tile.dirt.name"},
    {"coarse_dirt", "tile.dirt.coarse.name"},
    {"grass", "tile.grass.name"},
    {"grass_path", "tile.grassPath.name"},
    {"sand", "tile.sand.name"},
    {"red_sand", "tile.sand.red.name"},
    {"gravel", "tile.gravel.name"},
    {"clay", "tile.clay.name"},
    {"oak_planks", "tile.wood.oak.name"},
    {"spruce_planks", "tile.wood.spruce.name"},
    {"birch_planks", "tile.wood.birch.name"},
    {"jungle_planks", "tile.wood.jungle.name"},
    {"acacia_planks", "tile.wood.acacia.name"},
    {"dark_oak_planks", "tile.wood.big_oak.name"},
    {"oak_log", "tile.log.oak.name"},
    {"spruce_log", "tile.log.spruce.name"},
    {"birch_log", "tile.log.birch.name"},
    {"jungle_log", "tile.log.jungle.name"},
    {"acacia_log", "tile.log.acacia.name"},
    {"dark_oak_log", "tile.log.big_oak.name"},
    {"oak_leaves", "tile.leaves.oak.name"},
    {"spruce_leaves", "tile.leaves.spruce.name"},
    {"birch_leaves", "tile.leaves.birch.name"},
    {"jungle_leaves", "tile.leaves.jungle.name"},
    {"acacia_leaves", "tile.leaves.acacia.name"},
    {"dark_oak_leaves", "tile.leaves.big_oak.name"},
    {"bedrock", "tile.bedrock.name"},
    {"coal_ore", "tile.oreCoal.name"},
    {"iron_ore", "tile.oreIron.name"},
    {"gold_ore", "tile.oreGold.name"},
    {"diamond_ore", "tile.oreDiamond.name"},
    {"redstone_ore", "tile.oreRedstone.name"},
    {"lapis_ore", "tile.oreLapis.name"},
    {"emerald_ore", "tile.oreEmerald.name"},
    {"quartz_ore", "tile.netherquartz.name"},
    {"sandstone", "tile.sandStone.name"},
    {"stonebrick", "tile.stonebricksmooth.name"},
    {"brick_block", "tile.brick.name"},
    {"bookshelf", "tile.bookshelf.name"},
    {"crafting_table", "tile.workbench.name"},
    {"furnace", "tile.furnace.name"},
    {"obsidian", "tile.obsidian.name"},
    {"netherrack", "tile.hellrock.name"},
    {"glowstone", "tile.lightgem.name"},
    {"ice", "tile.ice.name"},
    {"snow", "tile.snow.name"},
    {"hardened_clay", "tile.clayHardened.name"},
    {"glass", "tile.glass.name"},
    {"tnt", "tile.tnt.name"},
    {"pumpkin", "tile.pumpkin.name"},
    {"melon_block", "tile.melon.name"},
    {"hay_block", "tile.hayBlock.name"},
    {"quartz_block", "tile.quartzBlock.name"},
    {"white_wool", "tile.cloth.white.name"},
    {"orange_wool", "tile.cloth.orange.name"},
    {"yellow_wool", "tile.cloth.yellow.name"},
    {"lime_wool", "tile.cloth.lime.name"},
    {"light_blue_wool", "tile.cloth.lightBlue.name"},
    {"blue_wool", "tile.cloth.blue.name"},
    {"purple_wool", "tile.cloth.purple.name"},
    {"magenta_wool", "tile.cloth.magenta.name"},
    {"pink_wool", "tile.cloth.pink.name"},
    {"red_wool", "tile.cloth.red.name"},
    {"gray_wool", "tile.cloth.gray.name"},
    {"silver_wool", "tile.cloth.silver.name"},
    {"cyan_wool", "tile.cloth.cyan.name"},
    {"brown_wool", "tile.cloth.brown.name"},
    {"green_wool", "tile.cloth.green.name"},
    {"black_wool", "tile.cloth.black.name"},
    {"water", "tile.water.name"},
    {"lava", "tile.lava.name"},
};

std::string stripNamespace(std::string value) {
    const auto colon = value.find(':');
    if (colon != std::string::npos) {
        value = value.substr(colon + 1);
    }
    return value;
}

std::string modelResourcePath(const std::string& modelName) {
    if (modelName.find('/') != std::string::npos) {
        return "models/" + modelName + ".json";
    }
    return "models/block/" + modelName + ".json";
}

std::string blockStateResourcePath(const std::string& blockName) {
    return "blockstates/" + blockName + ".json";
}

std::string textureResourcePath(const std::string& textureName) {
    std::string value = stripNamespace(textureName);
    if (value.rfind("textures/", 0) == 0) {
        return value + ".png";
    }
    if (value.find('/') == std::string::npos) {
        value = "blocks/" + value;
    }
    return "textures/" + value + ".png";
}

std::string resolveTextureRef(const std::map<std::string, std::string>& textures,
                              const std::string& value) {
    std::string current = value;
    for (int i = 0; i < 16; ++i) {
        if (current.empty() || current.front() != '#') {
            break;
        }
        const auto it = textures.find(current.substr(1));
        if (it == textures.end()) {
            return {};
        }
        current = it->second;
    }
    return current;
}

bool loadModelChain(Assets& assets, const std::string& modelName,
                    std::map<std::string, std::string>& textures,
                    std::vector<std::string>& ancestors, std::set<std::string>& visiting,
                    int depth) {
    if (depth > 16 || !visiting.insert(modelName).second) {
        return false;
    }

    const auto text = assets.textResource(modelResourcePath(modelName));
    if (!text) {
        visiting.erase(modelName);
        return false;
    }

    Json json;
    try {
        json = Json::parse(*text);
    } catch (const std::exception& ex) {
        logWarn("bad model %s: %s", modelName.c_str(), ex.what());
        visiting.erase(modelName);
        return false;
    }

    if (json["parent"].isString()) {
        loadModelChain(assets, json["parent"].asString(), textures, ancestors, visiting,
                       depth + 1);
    }

    for (const auto& [key, value] : json["textures"].object()) {
        if (value.isString()) {
            textures[key] = value.asString();
        }
    }

    ancestors.push_back(modelName);
    visiting.erase(modelName);
    return true;
}

std::string pickModelFromVariant(const Json& variant) {
    if (variant.isArray()) {
        return pickModelFromVariant(variant.at(0));
    }
    if (variant.isObject()) {
        return variant["model"].asString();
    }
    if (variant.isString()) {
        return variant.asString();
    }
    return {};
}

std::string pickBlockModel(Assets& assets, const std::string& blockName) {
    const auto text = assets.textResource(blockStateResourcePath(blockName));
    if (!text) {
        return blockName;
    }

    Json json;
    try {
        json = Json::parse(*text);
    } catch (const std::exception& ex) {
        logWarn("bad blockstate %s: %s", blockName.c_str(), ex.what());
        return blockName;
    }

    const Json& variants = json["variants"];
    if (variants.isObject()) {
        for (const char* preferred : {"normal", "axis=y", "inventory", "facing=north"}) {
            if (variants.contains(preferred)) {
                const std::string model = pickModelFromVariant(variants[preferred]);
                if (!model.empty()) {
                    return model;
                }
            }
        }
        for (const auto& [key, value] : variants.object()) {
            (void)key;
            const std::string model = pickModelFromVariant(value);
            if (!model.empty()) {
                return model;
            }
        }
    }

    const Json& multipart = json["multipart"];
    if (multipart.isArray()) {
        for (const Json& part : multipart.array()) {
            const std::string model = pickModelFromVariant(part["apply"]);
            if (!model.empty()) {
                return model;
            }
        }
    }

    return blockName;
}

std::vector<const char*> keysForFace(int face) {
    switch (face) {
    case static_cast<int>(Face::East): return {"east", "side", "all", "particle"};
    case static_cast<int>(Face::West): return {"west", "side", "all", "particle"};
    case static_cast<int>(Face::Up): return {"up", "top", "end", "all", "particle"};
    case static_cast<int>(Face::Down): return {"down", "bottom", "end", "all", "particle"};
    case static_cast<int>(Face::South): return {"south", "side", "all", "particle"};
    case static_cast<int>(Face::North): return {"north", "front", "side", "all", "particle"};
    default: return {"all", "particle"};
    }
}

}

bool BlockRegistry::load(Assets& assets, TextureArray& textures, std::string* error) {
    blocks_.clear();
    ids_.clear();
    textureLayers_.clear();
    hotbarNames_.clear();
    lang_.clear();

    if (const auto langText = assets.textResource("lang/en_us.lang")) {
        std::size_t position = 0;
        while (position < langText->size()) {
            std::size_t end = langText->find('\n', position);
            if (end == std::string::npos) {
                end = langText->size();
            }
            const std::string line = langText->substr(position, end - position);
            if (!line.empty() && line.front() != '#') {
                const auto equals = line.find('=');
                if (equals != std::string::npos) {
                    lang_[line.substr(0, equals)] = line.substr(equals + 1);
                }
            }
            position = end + 1;
        }
        logInfo("language loaded: %zu keys", lang_.size());
    }

    BlockDef air;
    air.name = "air";
    air.displayName = "Air";
    air.opaque = false;
    air.solid = false;
    blocks_.push_back(air);
    ids_["air"] = Air;

    Image checker;
    checker.width = 16;
    checker.height = 16;
    checker.rgba.resize(16 * 16 * 4);
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            std::uint8_t* pixel = checker.pixel(x, y);
            const bool magenta = ((x / 4) + (y / 4)) % 2 == 0;
            pixel[0] = magenta ? 255 : 20;
            pixel[1] = magenta ? 0 : 20;
            pixel[2] = magenta ? 255 : 20;
            pixel[3] = 255;
        }
    }
    fallbackLayer_ = std::max(0, textures.add(checker, "__fallback"));

    static const char* kBlockOrder[] = {
        "stone", "granite", "diorite", "andesite", "cobblestone", "mossy_cobblestone",
        "dirt", "coarse_dirt", "grass", "grass_path", "sand", "red_sand", "gravel", "clay",
        "oak_planks", "spruce_planks", "birch_planks", "jungle_planks", "acacia_planks",
        "dark_oak_planks", "oak_log", "spruce_log", "birch_log", "jungle_log", "acacia_log",
        "dark_oak_log", "oak_leaves", "spruce_leaves", "birch_leaves", "jungle_leaves",
        "acacia_leaves", "dark_oak_leaves", "bedrock", "coal_ore", "iron_ore", "gold_ore",
        "diamond_ore", "redstone_ore", "lapis_ore", "emerald_ore", "quartz_ore", "sandstone",
        "stonebrick", "brick_block", "bookshelf", "crafting_table", "furnace", "obsidian",
        "netherrack", "glowstone", "ice", "snow", "hardened_clay", "glass", "tnt", "pumpkin",
        "melon_block", "hay_block", "quartz_block", "white_wool", "orange_wool", "yellow_wool",
        "lime_wool", "light_blue_wool", "blue_wool", "purple_wool", "magenta_wool", "pink_wool",
        "red_wool", "gray_wool", "silver_wool", "cyan_wool", "brown_wool", "green_wool",
        "black_wool",
    };

    int failed = 0;
    for (const char* name : kBlockOrder) {
        if (!addFromModel(name, assets, textures, error)) {
            ++failed;
        }
    }

    const int stoneId = id("stone");
    if (stoneId > 0) {
        const int stoneLayer = def(stoneId).layers[static_cast<int>(Face::Up)];
        if (stoneLayer >= 0) {
            fallbackLayer_ = stoneLayer;
        }
    }

    addWaterOrLava("water", assets, textures);
    addWaterOrLava("lava", assets, textures);

    hotbarNames_ = {"grass", "dirt", "stone", "cobblestone", "oak_planks", "oak_log", "sand",
                    "glass", "oak_leaves"};

    logInfo("block registry: %zu blocks, %zu textures, %d failed", blocks_.size(),
            textureLayers_.size(), failed);
    return true;
}

bool BlockRegistry::addFromModel(const std::string& name, Assets& assets,
                                 TextureArray& textures, std::string* error) {
    (void)error;
    if (ids_.find(name) != ids_.end()) {
        return true;
    }

    const std::string modelName = pickBlockModel(assets, name);
    std::map<std::string, std::string> modelTextures;
    std::vector<std::string> ancestors;
    std::set<std::string> visiting;
    const bool loaded = loadModelChain(assets, modelName, modelTextures, ancestors, visiting, 0);

    BlockDef def;
    def.name = name;
    def.displayName = lookupDisplayName(name);

    if (!loaded) {
        logWarn("model for block '%s' not found (model '%s'), using fallback", name.c_str(),
                modelName.c_str());
        def.layers.fill(fallbackLayer_);
    } else {
        for (int face = 0; face < kFaceCount; ++face) {
            def.layers[face] = fallbackLayer_;
            for (const char* key : keysForFace(face)) {
                const auto it = modelTextures.find(key);
                if (it == modelTextures.end()) {
                    continue;
                }
                const std::string resolved = resolveTextureRef(modelTextures, it->second);
                if (resolved.empty()) {
                    continue;
                }
                const int layer = textureLayer(assets, textures, resolved);
                if (layer >= 0) {
                    def.layers[face] = layer;
                    break;
                }
            }
        }
    }

    if (name == "grass") {
        const int up = static_cast<int>(Face::Up);
        def.tints[up] = {0.55f, 0.86f, 0.45f};
        const auto side = assets.texture("textures/blocks/grass_side.png");
        const auto overlay = assets.texture("textures/blocks/grass_side_overlay.png");
        if (side && overlay) {
            Image composite = compositeOverlay(*side, *overlay, 0.55f, 0.86f, 0.45f);
            const int layer = textures.add(composite, "grass_side_tinted");
            if (layer >= 0) {
                for (int face : {static_cast<int>(Face::East), static_cast<int>(Face::West),
                                 static_cast<int>(Face::South),
                                 static_cast<int>(Face::North)}) {
                    def.layers[face] = layer;
                }
            }
        }
    }

    if (name.find("leaves") != std::string::npos) {
        def.opaque = false;
        for (auto& tint : def.tints) {
            tint = {0.45f, 0.75f, 0.35f};
        }
    }

    if (name == "glass") {
        def.opaque = false;
        def.transparentPass = true;
    } else if (name == "ice") {
        def.opaque = false;
        def.transparentPass = true;
        def.alpha = 0.75f;
        for (auto& tint : def.tints) {
            tint = {0.72f, 0.86f, 1.0f};
        }
    }

    const int blockId = static_cast<int>(blocks_.size());
    ids_[name] = blockId;
    blocks_.push_back(std::move(def));
    return true;
}

bool BlockRegistry::addWaterOrLava(const std::string& name, Assets& assets,
                                   TextureArray& textures) {
    if (ids_.find(name) != ids_.end()) {
        return true;
    }
    const bool water = name == "water";
    int still = textureLayer(assets, textures,
                             water ? "blocks/water_still" : "blocks/lava_still");
    int flow = textureLayer(assets, textures, water ? "blocks/water_flow" : "blocks/lava_flow");
    if (still < 0) {
        still = fallbackLayer_;
    }
    if (flow < 0) {
        flow = still;
    }

    BlockDef def;
    def.name = name;
    def.displayName = lookupDisplayName(name);
    def.layers = {flow, flow, still, still, flow, flow};
    def.opaque = false;
    def.solid = false;
    def.liquid = true;
    def.transparentPass = water;
    def.alpha = water ? 0.72f : 1.0f;
    if (water) {
        for (auto& tint : def.tints) {
            tint = {0.16f, 0.34f, 0.38f};
        }
    }

    ids_[name] = static_cast<int>(blocks_.size());
    blocks_.push_back(std::move(def));
    return true;
}

int BlockRegistry::textureLayer(Assets& assets, TextureArray& textures,
                                const std::string& textureName) {
    const auto cached = textureLayers_.find(textureName);
    if (cached != textureLayers_.end()) {
        return cached->second;
    }

    const std::string resource = textureResourcePath(textureName);
    std::string error;
    const auto image = assets.texture(resource, &error);
    int layer = -1;
    if (image) {
        layer = textures.add(*image, textureName);
    } else {
        logWarn("texture '%s' (%s) failed: %s, using fallback", textureName.c_str(),
                resource.c_str(), error.c_str());
    }
    if (layer < 0) {
        layer = fallbackLayer_;
    }
    textureLayers_[textureName] = layer;
    return layer;
}

std::string BlockRegistry::lookupDisplayName(const std::string& blockName) const {
    if (blockName == "mossy_cobblestone") {
        return "Mossy Cobblestone";
    }
    const auto mapping = kBlockLangKeys.find(blockName);
    if (mapping != kBlockLangKeys.end()) {
        const auto translated = lang_.find(mapping->second);
        if (translated != lang_.end()) {
            return translated->second;
        }
    }
    return blockName;
}

int BlockRegistry::id(std::string_view name) const {
    const auto it = ids_.find(std::string(name));
    return it == ids_.end() ? -1 : it->second;
}

const BlockDef& BlockRegistry::def(int blockId) const {
    if (blockId < 0 || static_cast<std::size_t>(blockId) >= blocks_.size()) {
        return blocks_[Air];
    }
    return blocks_[static_cast<std::size_t>(blockId)];
}

bool BlockRegistry::isOpaque(int blockId) const {
    return blockId > 0 && blockId < static_cast<int>(blocks_.size())
        && blocks_[static_cast<std::size_t>(blockId)].opaque;
}

bool BlockRegistry::isSolid(int blockId) const {
    return blockId > 0 && blockId < static_cast<int>(blocks_.size())
        && blocks_[static_cast<std::size_t>(blockId)].solid;
}

bool BlockRegistry::isLiquid(int blockId) const {
    return blockId > 0 && blockId < static_cast<int>(blocks_.size())
        && blocks_[static_cast<std::size_t>(blockId)].liquid;
}

bool BlockRegistry::usesTransparentPass(int blockId) const {
    return blockId > 0 && blockId < static_cast<int>(blocks_.size())
        && blocks_[static_cast<std::size_t>(blockId)].transparentPass;
}

}
