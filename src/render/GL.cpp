#include "render/GL.hpp"

#include <vector>

namespace nc::gl {

PFNGLGENVERTEXARRAYSPROC GenVertexArrays = nullptr;
PFNGLBINDVERTEXARRAYPROC BindVertexArray = nullptr;
PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays = nullptr;
PFNGLGENBUFFERSPROC GenBuffers = nullptr;
PFNGLBINDBUFFERPROC BindBuffer = nullptr;
PFNGLBUFFERDATAPROC BufferData = nullptr;
PFNGLDELETEBUFFERSPROC DeleteBuffers = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer = nullptr;
PFNGLCREATESHADERPROC CreateShader = nullptr;
PFNGLSHADERSOURCEPROC ShaderSource = nullptr;
PFNGLCOMPILESHADERPROC CompileShader = nullptr;
PFNGLGETSHADERIVPROC GetShaderiv = nullptr;
PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog = nullptr;
PFNGLDELETESHADERPROC DeleteShader = nullptr;
PFNGLCREATEPROGRAMPROC CreateProgram = nullptr;
PFNGLATTACHSHADERPROC AttachShader = nullptr;
PFNGLLINKPROGRAMPROC LinkProgram = nullptr;
PFNGLGETPROGRAMIVPROC GetProgramiv = nullptr;
PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog = nullptr;
PFNGLDELETEPROGRAMPROC DeleteProgram = nullptr;
PFNGLUSEPROGRAMPROC UseProgram = nullptr;
PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation = nullptr;
PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv = nullptr;
PFNGLUNIFORM1IPROC Uniform1i = nullptr;
PFNGLUNIFORM1FPROC Uniform1f = nullptr;
PFNGLUNIFORM3FPROC Uniform3f = nullptr;
PFNGLACTIVETEXTUREPROC ActiveTexture = nullptr;
PFNGLTEXIMAGE3DPROC TexImage3D = nullptr;
PFNGLTEXSUBIMAGE3DPROC TexSubImage3D = nullptr;
PFNGLGENFRAMEBUFFERSPROC GenFramebuffers = nullptr;
PFNGLBINDFRAMEBUFFERPROC BindFramebuffer = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers = nullptr;

bool loadFunctions(std::string* error) {
    std::vector<std::string> missing;

#define NC_LOAD(name)                                                                              \
    do {                                                                                           \
        name = reinterpret_cast<decltype(name)>(SDL_GL_GetProcAddress("gl" #name));                \
        if (!name) {                                                                               \
            missing.emplace_back("gl" #name);                                                      \
        }                                                                                          \
    } while (false)

    NC_LOAD(GenVertexArrays);
    NC_LOAD(BindVertexArray);
    NC_LOAD(DeleteVertexArrays);
    NC_LOAD(GenBuffers);
    NC_LOAD(BindBuffer);
    NC_LOAD(BufferData);
    NC_LOAD(DeleteBuffers);
    NC_LOAD(EnableVertexAttribArray);
    NC_LOAD(VertexAttribPointer);
    NC_LOAD(CreateShader);
    NC_LOAD(ShaderSource);
    NC_LOAD(CompileShader);
    NC_LOAD(GetShaderiv);
    NC_LOAD(GetShaderInfoLog);
    NC_LOAD(DeleteShader);
    NC_LOAD(CreateProgram);
    NC_LOAD(AttachShader);
    NC_LOAD(LinkProgram);
    NC_LOAD(GetProgramiv);
    NC_LOAD(GetProgramInfoLog);
    NC_LOAD(DeleteProgram);
    NC_LOAD(UseProgram);
    NC_LOAD(GetUniformLocation);
    NC_LOAD(UniformMatrix4fv);
    NC_LOAD(Uniform1i);
    NC_LOAD(Uniform1f);
    NC_LOAD(Uniform3f);
    NC_LOAD(ActiveTexture);
    NC_LOAD(TexImage3D);
    NC_LOAD(TexSubImage3D);
    NC_LOAD(GenFramebuffers);
    NC_LOAD(BindFramebuffer);
    NC_LOAD(FramebufferTexture2D);
    NC_LOAD(CheckFramebufferStatus);
    NC_LOAD(DeleteFramebuffers);

#undef NC_LOAD

    if (!missing.empty()) {
        if (error) {
            *error = "missing OpenGL functions:";
            for (const auto& name : missing) {
                *error += " " + name;
            }
        }
        return false;
    }
    return true;
}

}
