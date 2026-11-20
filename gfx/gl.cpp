#include "gl.hpp"

#include <SDL.h>
#include <cstdio>
#include <cstring>
#include <string>

namespace gl {

#define PHD_GL_DEFINE(ret, name, args) ret(PHD_GL_API* name) args = nullptr;
PHD_GL_FUNCTIONS(PHD_GL_DEFINE)
#undef PHD_GL_DEFINE

namespace {
bool es = true;
}

const char* load() {
#define PHD_GL_LOAD(ret, name, args)                                                   \
    name = reinterpret_cast<ret(PHD_GL_API*) args>(SDL_GL_GetProcAddress("gl" #name)); \
    if (!name) {                                                                       \
        return "gl" #name;                                                             \
    }
    PHD_GL_FUNCTIONS(PHD_GL_LOAD)
#undef PHD_GL_LOAD
    const auto* version = reinterpret_cast<const char*>(GetString(VERSION));
    es = version && std::strstr(version, "OpenGL ES");
    return nullptr;
}

bool is_es() { return es; }

const char* shader_preamble() {
    // Full precision wherever the GPU has it: texture coordinates across the
    // terrain texture need more bits than half precision gives, and the
    // lookups would land up to a texel and a half off and shift with the camera.
    return es ? "#version 100\n#ifdef GL_FRAGMENT_PRECISION_HIGH\nprecision highp float;\n#else\n"
                "precision mediump float;\n#endif\n"
              : "#version 120\n";
}

namespace {

GLuint compile(GLenum type, const char* source, const char* directives = "") {
    const GLuint shader = CreateShader(type);
    std::string full = shader_preamble();
    full.insert(full.find('\n') + 1, directives); // extensions must precede precision declarations
    full += source;
    const char* text = full.c_str();
    ShaderSource(shader, 1, &text, nullptr);
    CompileShader(shader);
    GLint ok = 0;
    GetShaderiv(shader, COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::fprintf(stderr, "shader: %s\n", log);
        return 0;
    }
    return shader;
}

} // namespace

GLuint program(const char* vertex, const char* fragment, const char* const* attributes, int count, const char* fragment_directives) {
    const GLuint vs = compile(VERTEX_SHADER, vertex), fs = compile(FRAGMENT_SHADER, fragment, fragment_directives);
    if (!vs || !fs) {
        return 0;
    }
    const GLuint id = CreateProgram();
    AttachShader(id, vs);
    AttachShader(id, fs);
    for (int i = 0; i < count; ++i) {
        BindAttribLocation(id, GLuint(i), attributes[i]);
    }
    LinkProgram(id);
    DeleteShader(vs);
    DeleteShader(fs);
    GLint ok = 0;
    GetProgramiv(id, LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        GetProgramInfoLog(id, sizeof(log), nullptr, log);
        std::fprintf(stderr, "program: %s\n", log);
        return 0;
    }
    return id;
}

} // namespace gl
