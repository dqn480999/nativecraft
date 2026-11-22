#pragma once

#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <GL/glext.h>

#include <string>

namespace nc::gl {

extern PFNGLGENVERTEXARRAYSPROC GenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC BindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC DeleteVertexArrays;
extern PFNGLGENBUFFERSPROC GenBuffers;
extern PFNGLBINDBUFFERPROC BindBuffer;
extern PFNGLBUFFERDATAPROC BufferData;
extern PFNGLDELETEBUFFERSPROC DeleteBuffers;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC EnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC VertexAttribPointer;
extern PFNGLCREATESHADERPROC CreateShader;
extern PFNGLSHADERSOURCEPROC ShaderSource;
extern PFNGLCOMPILESHADERPROC CompileShader;
extern PFNGLGETSHADERIVPROC GetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC GetShaderInfoLog;
extern PFNGLDELETESHADERPROC DeleteShader;
extern PFNGLCREATEPROGRAMPROC CreateProgram;
extern PFNGLATTACHSHADERPROC AttachShader;
extern PFNGLLINKPROGRAMPROC LinkProgram;
extern PFNGLGETPROGRAMIVPROC GetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC GetProgramInfoLog;
extern PFNGLDELETEPROGRAMPROC DeleteProgram;
extern PFNGLUSEPROGRAMPROC UseProgram;
extern PFNGLGETUNIFORMLOCATIONPROC GetUniformLocation;
extern PFNGLUNIFORMMATRIX4FVPROC UniformMatrix4fv;
extern PFNGLUNIFORM1IPROC Uniform1i;
extern PFNGLUNIFORM1FPROC Uniform1f;
extern PFNGLUNIFORM3FPROC Uniform3f;
extern PFNGLACTIVETEXTUREPROC ActiveTexture;
extern PFNGLTEXIMAGE3DPROC TexImage3D;
extern PFNGLTEXSUBIMAGE3DPROC TexSubImage3D;
extern PFNGLGENFRAMEBUFFERSPROC GenFramebuffers;
extern PFNGLBINDFRAMEBUFFERPROC BindFramebuffer;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC FramebufferTexture2D;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC CheckFramebufferStatus;
extern PFNGLDELETEFRAMEBUFFERSPROC DeleteFramebuffers;

bool loadFunctions(std::string* error = nullptr);

}
