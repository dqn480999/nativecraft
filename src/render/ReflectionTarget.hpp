#pragma once

#include <string>

#include "render/GL.hpp"

namespace nc {

class ReflectionTarget {
public:
    bool resize(int width, int height, std::string* error = nullptr);
    void destroy();

    void bind() const;
    void unbind() const;

    bool valid() const { return fbo_ != 0; }
    GLuint colorTexture() const { return color_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    GLuint fbo_ = 0;
    GLuint color_ = 0;
    GLuint depth_ = 0;
    int width_ = 0;
    int height_ = 0;
};

}
