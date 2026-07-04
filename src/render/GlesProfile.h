#pragma once

#include <string>

/**
 * @file GlesProfile.h
 * @brief Select a desktop-GL vs GLES render path and its shader dialect (issue #367).
 *
 * The desktop build targets OpenGL 3.3 core; a phone targets OpenGL ES. Rather than fork the
 * renderer, this picks a small capability set (which GLSL dialect to emit, whether instancing
 * and float textures are available) from the runtime/build target, so the shared render code
 * can branch on capabilities instead of on the platform. The actual context creation
 * (EGL/GLFW/NDK) is platform glue; this selection logic is pure and unit-testable.
 * Header-only, std only.
 */
namespace IKore {
namespace render {

enum class GlProfile { DesktopGL, GLES3, GLES2 };

/// The render capabilities a chosen profile exposes.
struct RenderCaps {
    GlProfile profile{GlProfile::DesktopGL};
    int glslVersion{330}; ///< GLSL version number (330 desktop, 300/100 ES).
    bool es{false};       ///< true for an OpenGL ES profile.
    bool instancing{true};
    bool floatTextures{true};
};

/// Choose the render path: desktop GL off-mobile, else GLES3 when available, GLES2 otherwise
/// (GLES2 drops instancing and float textures, so the renderer falls back for those).
inline RenderCaps selectRenderCaps(bool mobile, int glesMajor = 3) {
    RenderCaps c;
    if (!mobile) {
        c.profile = GlProfile::DesktopGL;
        c.glslVersion = 330;
        c.es = false;
        c.instancing = true;
        c.floatTextures = true;
        return c;
    }
    if (glesMajor >= 3) {
        c.profile = GlProfile::GLES3;
        c.glslVersion = 300;
        c.es = true;
        c.instancing = true;
        c.floatTextures = true;
    } else {
        c.profile = GlProfile::GLES2;
        c.glslVersion = 100;
        c.es = true;
        c.instancing = false;
        c.floatTextures = false;
    }
    return c;
}

/// The `#version` directive a shader should be compiled with for these caps.
inline std::string shaderVersionDirective(const RenderCaps& caps) {
    if (!caps.es) return "#version 330 core";
    if (caps.glslVersion >= 300) return "#version 300 es";
    return "#version 100";
}

/// Human-readable profile name (logging / diagnostics).
inline const char* profileName(GlProfile p) {
    switch (p) {
        case GlProfile::DesktopGL: return "DesktopGL";
        case GlProfile::GLES3: return "GLES3";
        case GlProfile::GLES2: return "GLES2";
    }
    return "Unknown";
}

} // namespace render
} // namespace IKore
