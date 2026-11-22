#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Math.hpp"
#include "render/GL.hpp"
#include "util/Image.hpp"

namespace nc {

class TextureArray {
public:
    bool initialize(int tileSize = 16, int maxLayers = 512);
    int add(const Image& image, std::string debugName);
    bool upload(std::string* error = nullptr);
    void destroy();

    GLuint id() const { return texture_; }
    int layerCount() const { return static_cast<int>(layers_.size()); }
    int tileSize() const { return tileSize_; }
    bool hasLayer(int layer) const { return layer >= 0 && layer < layerCount(); }
    Vec3 averageColor(int layer) const;
    Vec3 sampleNearest(int layer, float u, float v) const;

private:
    int tileSize_ = 16;
    int maxLayers_ = 512;
    std::vector<std::vector<std::uint8_t>> layers_;
    std::vector<Vec3> layerColors_;
    std::vector<std::string> names_;
    GLuint texture_ = 0;
    bool uploaded_ = false;
};

}
