#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nc {

struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgba;

    bool valid() const {
        return width > 0 && height > 0
            && rgba.size() == static_cast<std::size_t>(width) * height * 4;
    }

    std::uint8_t* pixel(int x, int y) {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }

    const std::uint8_t* pixel(int x, int y) const {
        return rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4;
    }
};

bool loadPng(const std::uint8_t* data, std::size_t size, Image& out,
             std::string* error = nullptr);

bool writePng(const std::string& path, int width, int height,
              const std::uint8_t* rgba, std::string* error = nullptr);

Image cropTop(const Image& source, int height);
Image resizeNearest(const Image& source, int width, int height);
Image compositeOverlay(const Image& base, const Image& overlay, float tintR, float tintG,
                       float tintB);

}
