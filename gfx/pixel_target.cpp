#include "pixel_target.hpp"

#include <cstdio>
#include <cstring>

namespace gfx {

bool PixelTarget::init(int width, int height) {
    texture_ = make_texture(width, height, gl::RGB, nullptr);
    gl::GenFramebuffers(1, &framebuffer_);
    gl::GenRenderbuffers(1, &depth_);
    const auto* extensions = reinterpret_cast<const char*>(gl::GetString(gl::EXTENSIONS));
    const bool deep = !gl::is_es() || (extensions && std::strstr(extensions, "GL_OES_depth24"));
    gl::BindRenderbuffer(gl::RENDERBUFFER, depth_);
    gl::RenderbufferStorage(gl::RENDERBUFFER, deep ? gl::DEPTH_COMPONENT24 : gl::DEPTH_COMPONENT16, width, height);
    depth_bits_ = deep ? 24 : 16;
    gl::BindFramebuffer(gl::FRAMEBUFFER, framebuffer_);
    gl::FramebufferTexture2D(gl::FRAMEBUFFER, gl::COLOR_ATTACHMENT0, gl::TEXTURE_2D, texture_.id, 0);
    gl::FramebufferRenderbuffer(gl::FRAMEBUFFER, gl::DEPTH_ATTACHMENT, gl::RENDERBUFFER, depth_);
    const bool ok = gl::CheckFramebufferStatus(gl::FRAMEBUFFER) == gl::FRAMEBUFFER_COMPLETE;
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    if (!ok) {
        std::fprintf(stderr, "the pixel target's framebuffer is incomplete\n");
    }
    return ok;
}

void PixelTarget::begin() {
    gl::BindFramebuffer(gl::FRAMEBUFFER, framebuffer_);
    gl::Viewport(0, 0, texture_.width, texture_.height);
    gl::ClearColor(0.04f, 0.05f, 0.05f, 1);
    gl::Clear(gl::COLOR_BUFFER_BIT | gl::DEPTH_BUFFER_BIT);
}

void PixelTarget::end(int drawable_w, int drawable_h) {
    gl::BindFramebuffer(gl::FRAMEBUFFER, 0);
    gl::Viewport(0, 0, drawable_w, drawable_h);
}

} // namespace gfx
