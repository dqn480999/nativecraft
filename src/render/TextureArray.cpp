#include "render/TextureArray.hpp"

#include <cstring>

#include "core/Log.hpp"

namespace nc {
namespace {

std::vector<std::uint8_t> toTile(const Image& image, int tileSize) {
    std::vector<std::uint8_t> tile(static_cast<std::size_t>(tileSize) * tileSize * 4, 0);
    if (!image.valid()) {
        return tile;
    }

    Image source = image;
    if (source.width == tileSize && source.height > tileSize && source.height % tileSize == 0) {
        source = cropTop(source, tileSize);
    }
    if (source.width != tileSize || source.height != tileSize) {
        source = resizeNearest(source, tileSize, tileSize);
    }
    if (!source.valid()) {
        return tile;
    }
    std::memcpy(tile.data(), source.rgba.data(), tile.size());
    return tile;
}

}

bool TextureArray::initialize(int tileSize, int maxLayers) {
    tileSize_ = tileSize;
    maxLayers_ = maxLayers;
    layers_.clear();
    names_.clear();
    uploaded_ = false;
    return true;
}

int TextureArray::add(const Image& image, std::string debugName) {
    if (static_cast<int>(layers_.size()) >= maxLayers_) {
        logWarn("texture array is full, skipping %s", debugName.c_str());
        return -1;
    }
    layers_.push_back(toTile(image, tileSize_));
    names_.push_back(std::move(debugName));
    return static_cast<int>(layers_.size()) - 1;
}

bool TextureArray::upload(std::string* error) {
    if (uploaded_) {
        return true;
    }
    if (layers_.empty()) {
        std::vector<std::uint8_t> empty(static_cast<std::size_t>(tileSize_) * tileSize_ * 4, 0);
        layers_.push_back(std::move(empty));
    }

    glGenTextures(1, &texture_);
    gl::ActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D_ARRAY, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl::TexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, tileSize_, tileSize_,
                   static_cast<GLsizei>(layers_.size()), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    for (std::size_t layer = 0; layer < layers_.size(); ++layer) {
        gl::TexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, static_cast<GLint>(layer), tileSize_,
                          tileSize_, 1, GL_RGBA, GL_UNSIGNED_BYTE, layers_[layer].data());
    }

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);

    const GLenum glError = glGetError();
    if (glError != GL_NO_ERROR) {
        if (error) {
            *error = "OpenGL error while uploading texture array: " + std::to_string(glError);
        }
        return false;
    }

    uploaded_ = true;
    logInfo("texture array uploaded: %d layers of %dx%d", layerCount(), tileSize_, tileSize_);
    return true;
}

void TextureArray::destroy() {
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    uploaded_ = false;
}

}
