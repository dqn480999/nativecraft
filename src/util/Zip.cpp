#include "util/Zip.hpp"

#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <zlib.h>

namespace nc {
namespace {

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50u;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50u;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50u;

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset])
         | static_cast<std::uint16_t>(data[offset + 1] << 8u);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset])
         | (static_cast<std::uint32_t>(data[offset + 1]) << 8u)
         | (static_cast<std::uint32_t>(data[offset + 2]) << 16u)
         | (static_cast<std::uint32_t>(data[offset + 3]) << 24u);
}

void setError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

}

bool ZipArchive::open(const std::string& path, std::string* error) {
    path_ = path;
    data_.clear();
    entries_.clear();

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        setError(error, "cannot open " + path);
        return false;
    }
    file.seekg(0, std::ios::end);
    const std::streamoff size = file.tellg();
    if (size <= 0) {
        setError(error, "empty file " + path);
        return false;
    }
    file.seekg(0, std::ios::beg);
    data_.resize(static_cast<std::size_t>(size));
    if (!file.read(reinterpret_cast<char*>(data_.data()), size)) {
        setError(error, "cannot read " + path);
        return false;
    }

    if (data_.size() < 22) {
        setError(error, "zip too small");
        return false;
    }

    std::size_t eocd = std::numeric_limits<std::size_t>::max();
    const std::size_t searchStart = data_.size() >= 22 + 65535 ? data_.size() - 22 - 65535 : 0;
    for (std::size_t i = data_.size() - 22;; --i) {
        if (readU32(data_, i) == kEndOfCentralDirectorySignature) {
            eocd = i;
            break;
        }
        if (i == searchStart) {
            break;
        }
    }
    if (eocd == std::numeric_limits<std::size_t>::max()) {
        setError(error, "end of central directory not found");
        return false;
    }

    const std::uint16_t totalEntries = readU16(data_, eocd + 10);
    const std::uint32_t centralSize = readU32(data_, eocd + 12);
    const std::uint32_t centralOffset = readU32(data_, eocd + 16);
    if (totalEntries == 0xFFFFu || centralSize == 0xFFFFFFFFu || centralOffset == 0xFFFFFFFFu) {
        setError(error, "zip64 archives are not supported");
        return false;
    }
    if (static_cast<std::size_t>(centralOffset) + centralSize > data_.size()) {
        setError(error, "central directory out of range");
        return false;
    }

    std::size_t cursor = centralOffset;
    const std::size_t centralEnd = static_cast<std::size_t>(centralOffset) + centralSize;
    for (std::uint16_t i = 0; i < totalEntries; ++i) {
        if (cursor + 46 > centralEnd || readU32(data_, cursor) != kCentralHeaderSignature) {
            setError(error, "corrupt central directory");
            return false;
        }
        Entry entry;
        entry.method = readU16(data_, cursor + 10);
        entry.compressedSize = readU32(data_, cursor + 20);
        entry.uncompressedSize = readU32(data_, cursor + 24);
        const std::uint16_t nameLength = readU16(data_, cursor + 28);
        const std::uint16_t extraLength = readU16(data_, cursor + 30);
        const std::uint16_t commentLength = readU16(data_, cursor + 32);
        entry.localOffset = readU32(data_, cursor + 42);
        if (cursor + 46 + nameLength > centralEnd) {
            setError(error, "corrupt entry name");
            return false;
        }
        const std::string name(reinterpret_cast<const char*>(data_.data() + cursor + 46),
                               nameLength);
        entries_[name] = entry;
        cursor += 46 + nameLength + extraLength + commentLength;
    }

    return true;
}

bool ZipArchive::exists(std::string_view name) const {
    return entries_.find(std::string(name)) != entries_.end();
}

std::optional<std::vector<std::uint8_t>> ZipArchive::read(std::string_view name,
                                                          std::string* error) const {
    const auto it = entries_.find(std::string(name));
    if (it == entries_.end()) {
        setError(error, "entry not found: " + std::string(name));
        return std::nullopt;
    }
    std::vector<std::uint8_t> out;
    if (!readEntry(it->second, out, error)) {
        return std::nullopt;
    }
    return out;
}

bool ZipArchive::readEntry(const Entry& entry, std::vector<std::uint8_t>& out,
                           std::string* error) const {
    const std::size_t localOffset = entry.localOffset;
    if (localOffset + 30 > data_.size()
        || readU32(data_, localOffset) != kLocalHeaderSignature) {
        setError(error, "corrupt local header");
        return false;
    }
    const std::uint16_t nameLength = readU16(data_, localOffset + 26);
    const std::uint16_t extraLength = readU16(data_, localOffset + 28);
    const std::size_t dataOffset = localOffset + 30 + nameLength + extraLength;
    if (dataOffset + entry.compressedSize > data_.size()) {
        setError(error, "entry data out of range");
        return false;
    }

    if (entry.uncompressedSize > 256u * 1024u * 1024u) {
        setError(error, "entry too large");
        return false;
    }

    if (entry.method == 0) {
        out.assign(data_.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                   data_.begin() + static_cast<std::ptrdiff_t>(dataOffset + entry.compressedSize));
        return true;
    }

    if (entry.method != 8) {
        setError(error, "unsupported compression method " + std::to_string(entry.method));
        return false;
    }

    out.resize(entry.uncompressedSize);
    if (out.empty()) {
        return true;
    }

    z_stream stream{};
    if (inflateInit2(&stream, -MAX_WBITS) != Z_OK) {
        setError(error, "inflateInit2 failed");
        return false;
    }
    stream.next_in = const_cast<Bytef*>(data_.data() + dataOffset);
    stream.avail_in = entry.compressedSize;
    stream.next_out = out.data();
    stream.avail_out = entry.uncompressedSize;

    int result = Z_OK;
    while (result == Z_OK) {
        result = inflate(&stream, Z_FINISH);
    }
    inflateEnd(&stream);

    if (result != Z_STREAM_END) {
        setError(error, "inflate failed");
        out.clear();
        return false;
    }
    if (stream.total_out != entry.uncompressedSize) {
        setError(error, "inflated size mismatch");
        out.clear();
        return false;
    }
    return true;
}

}
