#include "render/Shader.hpp"

#include <vector>

namespace nc {
namespace {

GLuint compileStage(GLenum type, const char* source, std::string* error) {
    const GLuint shader = gl::CreateShader(type);
    gl::ShaderSource(shader, 1, &source, nullptr);
    gl::CompileShader(shader);

    GLint status = GL_FALSE;
    gl::GetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        gl::GetShaderiv(shader, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(logLength > 0 ? logLength : 1));
        gl::GetShaderInfoLog(shader, logLength, nullptr, log.data());
        if (error) {
            *error = std::string(type == GL_VERTEX_SHADER ? "vertex" : "fragment")
                   + " shader compile failed: " + log.data();
        }
        gl::DeleteShader(shader);
        return 0;
    }
    return shader;
}

}

Shader::~Shader() {
    destroy();
}

bool Shader::compile(const char* vertexSource, const char* fragmentSource, std::string* error) {
    destroy();

    const GLuint vertex = compileStage(GL_VERTEX_SHADER, vertexSource, error);
    if (!vertex) {
        return false;
    }
    const GLuint fragment = compileStage(GL_FRAGMENT_SHADER, fragmentSource, error);
    if (!fragment) {
        gl::DeleteShader(vertex);
        return false;
    }

    program_ = gl::CreateProgram();
    gl::AttachShader(program_, vertex);
    gl::AttachShader(program_, fragment);
    gl::LinkProgram(program_);

    gl::DeleteShader(vertex);
    gl::DeleteShader(fragment);

    GLint status = GL_FALSE;
    gl::GetProgramiv(program_, GL_LINK_STATUS, &status);
    if (status != GL_TRUE) {
        GLint logLength = 0;
        gl::GetProgramiv(program_, GL_INFO_LOG_LENGTH, &logLength);
        std::vector<char> log(static_cast<std::size_t>(logLength > 0 ? logLength : 1));
        gl::GetProgramInfoLog(program_, logLength, nullptr, log.data());
        if (error) {
            *error = std::string("program link failed: ") + log.data();
        }
        destroy();
        return false;
    }

    uniforms_.clear();
    return true;
}

void Shader::destroy() {
    if (program_) {
        gl::DeleteProgram(program_);
        program_ = 0;
    }
    uniforms_.clear();
}

void Shader::use() const {
    gl::UseProgram(program_);
}

int Shader::uniform(const char* name) const {
    const auto it = uniforms_.find(name);
    if (it != uniforms_.end()) {
        return it->second;
    }
    const int location = gl::GetUniformLocation(program_, name);
    uniforms_.emplace(name, location);
    return location;
}

void Shader::setMat4(const char* name, const Mat4& matrix) const {
    gl::UniformMatrix4fv(uniform(name), 1, GL_FALSE, matrix.data());
}

void Shader::setInt(const char* name, int value) const {
    gl::Uniform1i(uniform(name), value);
}

void Shader::setFloat(const char* name, float value) const {
    gl::Uniform1f(uniform(name), value);
}

void Shader::setVec3(const char* name, const Vec3& value) const {
    gl::Uniform3f(uniform(name), value.x, value.y, value.z);
}

}
