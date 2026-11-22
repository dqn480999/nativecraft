#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "util/Image.hpp"
#include "util/Json.hpp"
#include "util/Zip.hpp"

namespace nc {

class Assets {
public:
    bool init(const std::string& minecraftDir, std::string* error = nullptr);

    const std::vector<std::uint8_t>* resource(std::string_view name);
    std::optional<std::string> textResource(std::string_view name);
    std::optional<Image> texture(std::string_view name, std::string* error = nullptr);

    std::optional<std::string> assetPath(std::string_view name) const;
    std::optional<std::string> soundPath(std::string_view name) const {
        return assetPath(name);
    }

    const std::string& minecraftDir() const { return minecraftDir_; }
    const std::string& jarPath() const { return jarPath_; }
    std::size_t jarEntryCount() const { return jar_.entryCount(); }
    std::size_t assetsIndexCount() const { return assetsIndex_["objects"].size(); }

private:
    std::string normalizeResourceName(std::string_view name) const;

    std::string minecraftDir_;
    std::string jarPath_;
    ZipArchive jar_;
    Json assetsIndex_;
    std::map<std::string, std::vector<std::uint8_t>> cache_;
};

}
