#pragma once

#include <string>
#include <unordered_map>

#include "core/Math.hpp"
#include "render/GL.hpp"

namespace nc {

class Shader {
public:
    Shader() = default;
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    bool compile(const char* vertexSource, const char* fragmentSource,
                 std::string* error = nullptr);
    void destroy();

    void use() const;
    GLuint id() const { return program_; }

    int uniform(const char* name) const;

    void setMat4(const char* name, const Mat4& matrix) const;
    void setInt(const char* name, int value) const;
    void setFloat(const char* name, float value) const;
    void setVec3(const char* name, const Vec3& value) const;

private:
    GLuint program_ = 0;
    mutable std::unordered_map<std::string, int> uniforms_;
};

}
