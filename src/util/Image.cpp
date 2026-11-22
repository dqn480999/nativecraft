#include "util/Image.hpp"

#include <algorithm>
#include <cstring>
#include <png.h>

namespace nc {
namespace {

void setError(std::string* error, const std::string& message) {
    if (error) {
        *error = message;
    }
}

}

bool loadPng(const std::uint8_t* data, std::size_t size, Image& out, std::string* error) {
    out = {};
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    if (!png_image_begin_read_from_memory(&image, data, size)) {
        setError(error, image.message);
        return false;
    }

    image.format = PNG_FORMAT_RGBA;
    out.width = static_cast<int>(image.width);
    out.height = static_cast<int>(image.height);
    out.rgba.resize(PNG_IMAGE_SIZE(image));

    if (!png_image_finish_read(&image, nullptr, out.rgba.data(), 0, nullptr)) {
        setError(error, image.message);
        out = {};
        png_image_free(&image);
        return false;
    }
    png_image_free(&image);
    return true;
}

bool writePng(const std::string& path, int width, int height, const std::uint8_t* rgba,
              std::string* error) {
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    image.width = static_cast<png_uint_32>(width);
    image.height = static_cast<png_uint_32>(height);
    image.format = PNG_FORMAT_RGBA;
    if (!png_image_write_to_file(&image, path.c_str(), 0, rgba, 0, nullptr)) {
        setError(error, image.message);
        return false;
    }
    return true;
}

Image cropTop(const Image& source, int height) {
    Image out;
    if (!source.valid() || height <= 0) {
        return out;
    }
    height = std::min(height, source.height);
    out.width = source.width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(out.width) * out.height * 4);
    std::memcpy(out.rgba.data(), source.rgba.data(), out.rgba.size());
    return out;
}

Image resizeNearest(const Image& source, int width, int height) {
    Image out;
    if (!source.valid() || width <= 0 || height <= 0) {
        return out;
    }
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const int sy = std::min(source.height - 1, y * source.height / height);
        for (int x = 0; x < width; ++x) {
            const int sx = std::min(source.width - 1, x * source.width / width);
            std::memcpy(out.pixel(x, y), source.pixel(sx, sy), 4);
        }
    }
    return out;
}

Image compositeOverlay(const Image& base, const Image& overlay, float tintR, float tintG,
                       float tintB) {
    if (!base.valid()) {
        return {};
    }
    Image out = base;
    if (!overlay.valid()) {
        return out;
    }
    const int width = std::min(base.width, overlay.width);
    const int height = std::min(base.height, overlay.height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const std::uint8_t* src = overlay.pixel(x, y);
            std::uint8_t* dst = out.pixel(x, y);
            const float alpha = static_cast<float>(src[3]) / 255.0f;
            if (alpha <= 0.0f) {
                continue;
            }
            const float sr = static_cast<float>(src[0]) / 255.0f * tintR;
            const float sg = static_cast<float>(src[1]) / 255.0f * tintG;
            const float sb = static_cast<float>(src[2]) / 255.0f * tintB;
            const float inv = 1.0f - alpha;
            dst[0] = static_cast<std::uint8_t>(
                std::clamp((static_cast<float>(dst[0]) / 255.0f * inv + sr * alpha) * 255.0f,
                           0.0f, 255.0f));
            dst[1] = static_cast<std::uint8_t>(
                std::clamp((static_cast<float>(dst[1]) / 255.0f * inv + sg * alpha) * 255.0f,
                           0.0f, 255.0f));
            dst[2] = static_cast<std::uint8_t>(
                std::clamp((static_cast<float>(dst[2]) / 255.0f * inv + sb * alpha) * 255.0f,
                           0.0f, 255.0f));
            dst[3] = 255;
        }
    }
    return out;
}

}
