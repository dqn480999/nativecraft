#include "assets/Assets.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include "core/Log.hpp"

namespace nc {
namespace fs = std::filesystem;

namespace {

bool fileExists(const std::string& path) {
    std::error_code error;
    return fs::is_regular_file(path, error);
}

}

bool Assets::init(const std::string& minecraftDir, std::string* error) {
    minecraftDir_ = minecraftDir;
    cache_.clear();

    jarPath_ = minecraftDir_ + "/versions/1.12.2/1.12.2.jar";
    if (!fileExists(jarPath_)) {
        const fs::path versions = fs::path(minecraftDir_) / "versions" / "1.12.2";
        std::error_code errorCode;
        for (const auto& entry : fs::directory_iterator(versions, errorCode)) {
            if (errorCode) {
                break;
            }
            if (entry.is_regular_file() && entry.path().extension() == ".jar") {
                jarPath_ = entry.path().string();
                break;
            }
        }
    }

    if (!fileExists(jarPath_)) {
        if (error) {
            *error = "Minecraft 1.12.2 jar not found under " + minecraftDir_;
        }
        return false;
    }
    if (!jar_.open(jarPath_, error)) {
        return false;
    }

    const std::string index1_12 = minecraftDir_ + "/assets/indexes/1.12.json";
    const std::string index1_12_2 = minecraftDir_ + "/assets/indexes/1.12.2.json";
    const std::string indexPath = fileExists(index1_12) ? index1_12 : index1_12_2;
    if (fileExists(indexPath)) {
        std::ifstream file(indexPath, std::ios::binary);
        std::stringstream buffer;
        buffer << file.rdbuf();
        try {
            assetsIndex_ = Json::parse(buffer.str());
        } catch (const std::exception& ex) {
            logWarn("failed to parse assets index %s: %s", indexPath.c_str(), ex.what());
            assetsIndex_ = Json();
        }
    } else {
        logWarn("assets index not found, sounds will be unavailable");
    }

    logInfo("assets: jar=%s (%zu entries), index objects=%zu", jarPath_.c_str(),
            jar_.entryCount(), assetsIndexCount());
    return true;
}

std::string Assets::normalizeResourceName(std::string_view name) const {
    std::string result(name);
    while (!result.empty() && result.front() == '/') {
        result.erase(result.begin());
    }
    if (result.rfind("assets/", 0) == 0) {
        return result;
    }
    if (result.rfind("minecraft/", 0) == 0) {
        return "assets/" + result;
    }
    return "assets/minecraft/" + result;
}

const std::vector<std::uint8_t>* Assets::resource(std::string_view name) {
    const std::string key = normalizeResourceName(name);
    const auto cached = cache_.find(key);
    if (cached != cache_.end()) {
        return cached->second.empty() ? nullptr : &cached->second;
    }

    std::string error;
    auto data = jar_.read(key, &error);
    if (!data) {
        logWarn("resource missing: %s (%s)", key.c_str(), error.c_str());
        cache_.emplace(key, std::vector<std::uint8_t>{});
        return nullptr;
    }
    const auto [it, inserted] = cache_.emplace(key, std::move(*data));
    (void)inserted;
    return it->second.empty() ? nullptr : &it->second;
}

std::optional<std::string> Assets::textResource(std::string_view name) {
    const auto* data = resource(name);
    if (!data) {
        return std::nullopt;
    }
    return std::string(reinterpret_cast<const char*>(data->data()), data->size());
}

std::optional<Image> Assets::texture(std::string_view name, std::string* error) {
    const auto* data = resource(name);
    if (!data) {
        if (error) {
            *error = "texture not found: " + std::string(name);
        }
        return std::nullopt;
    }
    Image image;
    if (!loadPng(data->data(), data->size(), image, error)) {
        return std::nullopt;
    }
    return image;
}

std::optional<std::string> Assets::assetPath(std::string_view name) const {
    if (!assetsIndex_.isObject()) {
        return std::nullopt;
    }
    const Json& objects = assetsIndex_["objects"];
    if (!objects.contains(name)) {
        return std::nullopt;
    }
    const std::string hash = objects[name]["hash"].asString();
    if (hash.size() < 2) {
        return std::nullopt;
    }
    const std::string path = minecraftDir_ + "/assets/objects/" + hash.substr(0, 2) + "/" + hash;
    if (!fileExists(path)) {
        return std::nullopt;
    }
    return path;
}

}
