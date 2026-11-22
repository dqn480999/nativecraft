#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nc {

class ZipArchive {
public:
    bool open(const std::string& path, std::string* error = nullptr);

    bool exists(std::string_view name) const;
    std::optional<std::vector<std::uint8_t>> read(std::string_view name,
                                                  std::string* error = nullptr) const;
    std::size_t entryCount() const { return entries_.size(); }
    const std::string& path() const { return path_; }

private:
    struct Entry {
        std::uint16_t method = 0;
        std::uint32_t compressedSize = 0;
        std::uint32_t uncompressedSize = 0;
        std::uint32_t localOffset = 0;
    };

    bool readEntry(const Entry& entry, std::vector<std::uint8_t>& out,
                   std::string* error) const;

    std::vector<std::uint8_t> data_;
    std::unordered_map<std::string, Entry> entries_;
    std::string path_;
};

}
