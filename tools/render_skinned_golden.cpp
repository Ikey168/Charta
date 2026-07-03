// Skinned-render golden-image regression test - issue #287 / #290 follow-up.
//
// #290 gave the engine a headless offscreen golden harness, but it renders a self-contained
// raymarch - it does not exercise the engine's real skinned pipeline. This does: it loads the
// rigged animated model (#287), drives it to a fixed animation pose, and renders the mesh
// through the actual skinned vertex program (skinned_phong.vert + finalBonesMatrices) to an
// FBO, then compares the readback to a committed golden with a mean-absolute-error tolerance.
// So a regression in the GPU skinning path (wrong bone palette, broken deform) is caught by
// the pixels, not just by the CPU-side palette check (test_skinned_animation).
//
// It needs a GL context (GLFW hidden window) + the engine, so it is a gl-labelled test run
// under Xvfb + a software GL. The pose is fixed (a keyframe) so the output is deterministic.
//
// Usage:
//   render_skinned_golden --emit  <path.ppm>            render and write the golden
//   render_skinned_golden --check <path.ppm> [--mae M]  render and fail (exit 1) if the mean
//                                                        absolute error exceeds M (default 8)
//
// Regenerate the golden intentionally after a legitimate change:
//   ./render_skinned_golden --emit tools/golden/skinned.ppm    (from the repo root)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "src/Model.h"
#include "src/Shader.h"
#include "src/core/components/AnimationComponent.h"

namespace {

constexpr int kWidth = 200;
constexpr int kHeight = 200;

bool writePpm(const std::string& path, const std::vector<unsigned char>& rgb) {
    std::ofstream f(path, std::ios::binary);
    if (!f.good()) return false;
    f << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return f.good();
}

bool readPpm(const std::string& path, std::vector<unsigned char>& rgb, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;
    std::string magic;
    int maxval = 0;
    f >> magic >> w >> h >> maxval;
    if (magic != "P6" || maxval != 255) return false;
    f.get();
    rgb.resize(static_cast<std::size_t>(w) * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return static_cast<bool>(f);
}

// Render the skinned model at a fixed pose into `out` (RGB, kWidth*kHeight*3).
bool renderSkinned(std::vector<unsigned char>& out) {
    IKore::Model model;
    if (!model.loadFromFile("assets/models/animated_bone.gltf") || !model.hasBones() ||
        model.getAnimations().empty()) {
        std::fprintf(stderr, "render_skinned_golden: failed to load the animated model\n");
        return false;
    }

    // Drive the animation to a fixed keyframe pose (t = 0.5 s, the joint rotated ~45 deg).
    IKore::AnimationComponent anim;
    anim.setBoneInfoMap(model.getBoneInfoMap());
    for (const auto& clip : model.getAnimations()) {
        anim.addAnimation(clip.name, std::make_unique<IKore::Animation>(clip));
    }
    anim.playAnimation(model.getAnimations().front().name, /*loop=*/true, /*speed=*/1.0f);
    anim.update(0.5f);
    const std::vector<glm::mat4>& palette = anim.getBoneTransforms();

    std::string shaderError;
    auto shader = IKore::Shader::loadFromFilesCached("src/shaders/skinned_phong.vert",
                                                     "tools/skinned_golden.frag", shaderError);
    if (!shader) {
        std::fprintf(stderr, "render_skinned_golden: shader load failed: %s\n", shaderError.c_str());
        return false;
    }

    GLuint fbo = 0, tex = 0, depth = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kWidth, kHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "render_skinned_golden: FBO incomplete\n");
        return false;
    }

    glViewport(0, 0, kWidth, kHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.08f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Fixed camera framing the unit quad (which rotates about the origin with the bone).
    const glm::mat4 modelMat(1.0f);
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 0.5f, 3.0f), glm::vec3(0.0f, 0.5f, 0.0f),
                                       glm::vec3(0.0f, 1.0f, 0.0f));
    const glm::mat4 proj = glm::perspective(glm::radians(45.0f),
                                            static_cast<float>(kWidth) / kHeight, 0.1f, 10.0f);
    const glm::mat3 normalMat(glm::transpose(glm::inverse(modelMat)));

    shader->use();
    shader->setMat4("model", glm::value_ptr(modelMat));
    shader->setMat4("view", glm::value_ptr(view));
    shader->setMat4("projection", glm::value_ptr(proj));
    shader->setMat3("normalMatrix", glm::value_ptr(normalMat));
    shader->setMat4("lightSpaceMatrix", glm::value_ptr(glm::mat4(1.0f)));
    shader->setVec3("lightDir", 0.3f, 0.6f, 0.8f);
    const int count = static_cast<int>(palette.size() < 100 ? palette.size() : 100);
    for (int b = 0; b < count; ++b) {
        shader->setMat4(("finalBonesMatrices[" + std::to_string(b) + "]").c_str(),
                        glm::value_ptr(palette[static_cast<std::size_t>(b)]));
    }

    model.render(shader);
    glFinish();

    std::vector<unsigned char> rgba(static_cast<std::size_t>(kWidth) * kHeight * 4);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    out.resize(static_cast<std::size_t>(kWidth) * kHeight * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kWidth) * kHeight; ++i) {
        out[i * 3 + 0] = rgba[i * 4 + 0];
        out[i * 3 + 1] = rgba[i * 4 + 1];
        out[i * 3 + 2] = rgba[i * 4 + 2];
    }
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDeleteRenderbuffers(1, &depth);
    return true;
}

} // namespace

int main(int argc, char** argv) {
    std::string mode, path;
    double maeThreshold = 8.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--emit" || a == "--check") && i + 1 < argc) {
            mode = a;
            path = argv[++i];
        } else if (a == "--mae" && i + 1 < argc) {
            maeThreshold = std::stod(argv[++i]);
        }
    }
    if (mode.empty()) {
        std::fprintf(stderr, "usage: render_skinned_golden --emit|--check <path.ppm> [--mae M]\n");
        return 2;
    }

    // Headless GL context. Skip cleanly if none is available so the job stays reliable.
    if (!glfwInit()) { std::printf("skip: GLFW init failed (no display?)\n"); return 0; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "skinned-golden", nullptr, nullptr);
    if (!win) { std::printf("skip: no GL context\n"); glfwTerminate(); return 0; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::printf("skip: could not load GL\n");
        glfwDestroyWindow(win);
        glfwTerminate();
        return 0;
    }

    int rc = 0;
    std::vector<unsigned char> img;
    if (!renderSkinned(img)) {
        rc = 3;
    } else if (mode == "--emit") {
        if (!writePpm(path, img)) {
            std::fprintf(stderr, "render_skinned_golden: cannot write '%s'\n", path.c_str());
            rc = 5;
        } else {
            std::printf("render_skinned_golden: wrote %s (%dx%d)\n", path.c_str(), kWidth, kHeight);
        }
    } else { // --check
        std::vector<unsigned char> golden;
        int gw = 0, gh = 0;
        if (!readPpm(path, golden, gw, gh)) {
            std::fprintf(stderr, "render_skinned_golden: cannot read golden '%s'\n", path.c_str());
            rc = 5;
        } else if (gw != kWidth || gh != kHeight || golden.size() != img.size()) {
            std::fprintf(stderr, "render_skinned_golden: golden size mismatch\n");
            rc = 1;
        } else {
            double sumAbs = 0.0;
            int maxDiff = 0;
            for (std::size_t i = 0; i < img.size(); ++i) {
                const int d = std::abs(static_cast<int>(img[i]) - static_cast<int>(golden[i]));
                sumAbs += d;
                if (d > maxDiff) maxDiff = d;
            }
            const double mae = sumAbs / static_cast<double>(img.size());
            std::printf("render_skinned_golden: MAE=%.3f maxDiff=%d (threshold MAE<=%.2f)\n",
                        mae, maxDiff, maeThreshold);
            if (mae > maeThreshold) {
                std::fprintf(stderr, "render_skinned_golden: FAIL - skinned render regressed\n");
                rc = 1;
            } else {
                std::printf("render_skinned_golden: PASS\n");
            }
        }
    }

    glfwDestroyWindow(win);
    glfwTerminate();
    return rc;
}
