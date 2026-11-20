#pragma once

// A thin OpenGL ES 2 layer. Functions are loaded through SDL at startup, so
// the same code runs on a GLES 2 context (the Pi, Mesa) or a desktop GL 2.1
// context (Windows), with no GL headers or libraries at build time.

#include <cstddef>
#include <cstdint>

namespace gl {

using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLboolean = unsigned char;
using GLbitfield = unsigned int;
using GLfloat = float;
using GLchar = char;
using GLsizeiptr = std::ptrdiff_t;

constexpr GLenum TRIANGLES = 0x0004;
constexpr GLenum LESS = 0x0201, LEQUAL = 0x0203;
constexpr GLenum DEPTH_TEST = 0x0B71;
constexpr GLbitfield DEPTH_BUFFER_BIT = 0x0100;
constexpr GLenum SRC_ALPHA = 0x0302, ONE_MINUS_SRC_ALPHA = 0x0303;
constexpr GLenum BLEND = 0x0BE2;
constexpr GLenum UNPACK_ALIGNMENT = 0x0CF5, PACK_ALIGNMENT = 0x0D05;
constexpr GLenum TEXTURE_2D = 0x0DE1;
constexpr GLenum UNSIGNED_BYTE = 0x1401, UNSIGNED_SHORT = 0x1403, FLOAT = 0x1406;
constexpr GLenum RGB = 0x1907, RGBA = 0x1908;
constexpr GLenum VERSION = 0x1F02, EXTENSIONS = 0x1F03;
constexpr GLenum NEAREST = 0x2600, LINEAR = 0x2601;
constexpr GLenum TEXTURE_MAG_FILTER = 0x2800, TEXTURE_MIN_FILTER = 0x2801;
constexpr GLenum TEXTURE_WRAP_S = 0x2802, TEXTURE_WRAP_T = 0x2803;
constexpr GLenum CLAMP_TO_EDGE = 0x812F, REPEAT = 0x2901;
constexpr GLenum TEXTURE0 = 0x84C0;
constexpr GLenum ARRAY_BUFFER = 0x8892, ELEMENT_ARRAY_BUFFER = 0x8893, STREAM_DRAW = 0x88E0, STATIC_DRAW = 0x88E4,
                 DYNAMIC_DRAW = 0x88E8;
constexpr GLenum FRAGMENT_SHADER = 0x8B30, VERTEX_SHADER = 0x8B31;
constexpr GLenum COMPILE_STATUS = 0x8B81, LINK_STATUS = 0x8B82;
constexpr GLbitfield COLOR_BUFFER_BIT = 0x4000;
constexpr GLenum FRAMEBUFFER = 0x8D40, RENDERBUFFER = 0x8D41, COLOR_ATTACHMENT0 = 0x8CE0, DEPTH_ATTACHMENT = 0x8D00;
constexpr GLenum DEPTH_COMPONENT16 = 0x81A5, DEPTH_COMPONENT24 = 0x81A6, FRAMEBUFFER_COMPLETE = 0x8CD5;

#define PHD_GL_FUNCTIONS(X)                                                                  \
    X(void, ActiveTexture, (GLenum))                                                         \
    X(void, AttachShader, (GLuint, GLuint))                                                  \
    X(void, BindAttribLocation, (GLuint, GLuint, const GLchar*))                             \
    X(void, BindBuffer, (GLenum, GLuint))                                                    \
    X(void, BindFramebuffer, (GLenum, GLuint))                                               \
    X(void, BindRenderbuffer, (GLenum, GLuint))                                              \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                              \
    X(void, FramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint))                       \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                   \
    X(void, GenFramebuffers, (GLsizei, GLuint*))                                             \
    X(void, GenRenderbuffers, (GLsizei, GLuint*))                                            \
    X(void, RenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei))                         \
    X(void, BindTexture, (GLenum, GLuint))                                                   \
    X(void, BlendFunc, (GLenum, GLenum))                                                     \
    X(void, BufferData, (GLenum, GLsizeiptr, const void*, GLenum))                           \
    X(void, Clear, (GLbitfield))                                                             \
    X(void, ClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))                                \
    X(void, CompileShader, (GLuint))                                                         \
    X(GLuint, CreateProgram, ())                                                             \
    X(GLuint, CreateShader, (GLenum))                                                        \
    X(void, DeleteShader, (GLuint))                                                          \
    X(void, DepthFunc, (GLenum))                                                             \
    X(void, DepthMask, (GLboolean))                                                          \
    X(void, Disable, (GLenum))                                                               \
    X(void, DrawElements, (GLenum, GLsizei, GLenum, const void*))                            \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                            \
    X(void, Enable, (GLenum))                                                                \
    X(void, EnableVertexAttribArray, (GLuint))                                               \
    X(void, GenBuffers, (GLsizei, GLuint*))                                                  \
    X(void, GenTextures, (GLsizei, GLuint*))                                                 \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                         \
    X(void, GetProgramiv, (GLuint, GLenum, GLint*))                                          \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*))                          \
    X(void, GetShaderiv, (GLuint, GLenum, GLint*))                                           \
    X(const unsigned char*, GetString, (GLenum))                                             \
    X(GLint, GetUniformLocation, (GLuint, const GLchar*))                                    \
    X(void, LinkProgram, (GLuint))                                                           \
    X(void, PixelStorei, (GLenum, GLint))                                                    \
    X(void, ReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*))             \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*))             \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                          \
    X(void, TexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
    X(void, Uniform1i, (GLint, GLint))                                                       \
    X(void, Uniform1f, (GLint, GLfloat))                                                     \
    X(void, Uniform2f, (GLint, GLfloat, GLfloat))                                            \
    X(void, Uniform3f, (GLint, GLfloat, GLfloat, GLfloat))                                   \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                          \
    X(void, UseProgram, (GLuint))                                                            \
    X(void, VertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*))   \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

#ifdef _WIN32
#define PHD_GL_API __stdcall
#else
#define PHD_GL_API
#endif

#define PHD_GL_DECLARE(ret, name, args) extern ret(PHD_GL_API* name) args;
PHD_GL_FUNCTIONS(PHD_GL_DECLARE)
#undef PHD_GL_DECLARE

// Loads every function; returns the name of the first one missing, or null.
const char* load();

// True on a GLES context, false on desktop GL; sets the shader preamble.
bool is_es();
const char* shader_preamble();

// Compiles and links a program, binding attributes in order. Returns 0 and
// prints the log on failure.
GLuint program(const char* vertex, const char* fragment, const char* const* attributes, int count);

} // namespace gl
