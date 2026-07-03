// Test for the runtime skeletal-animation loading pipeline - issue #287 follow-up.
//
// #287 wired the skinned forward + shadow passes but the engine never loaded an animation
// clip into an AnimationComponent, so the skinned path was inert. This verifies the new
// pipeline end to end on the committed animated asset (assets/models/animated_bone.gltf, a
// single bone rotating 0..90 degrees over one second):
//   - Model::loadFromFile reports bones + animations and extracts the clip,
//   - an AnimationComponent driven by that clip produces a bone palette that actually
//     animates frame to frame (not the bind pose).
//
// Needs a GL context (Model creates VBOs on load); runs headlessly on Xvfb + a software GL.
#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <cmath>
#include <iostream>
#include <memory>

#include "src/Model.h"
#include "src/core/components/AnimationComponent.h"

using namespace IKore;

static int g_failures = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL (line " << __LINE__ << "): " #cond << "\n"; \
            ++g_failures;                                                  \
        }                                                                  \
    } while (0)

static bool mat4Differs(const glm::mat4& a, const glm::mat4& b, float eps = 1e-4f) {
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            if (std::fabs(a[i][j] - b[i][j]) > eps) return true;
    return false;
}

int main() {
    // Headless GL context (Model::loadFromFile creates GL buffers). Skip cleanly if no
    // display/context is available so the job stays reliable.
    if (!glfwInit()) { std::cout << "skip: GLFW init failed (no display?)\n"; return 0; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "anim-test", nullptr, nullptr);
    if (!win) { std::cout << "skip: no GL context\n"; glfwTerminate(); return 0; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "skip: could not load GL\n";
        glfwDestroyWindow(win);
        glfwTerminate();
        return 0;
    }

    {
        Model model;
        const bool loaded = model.loadFromFile("assets/models/animated_bone.gltf");
        CHECK(loaded);
        CHECK(model.hasBones());
        CHECK(model.hasAnimations());
        CHECK(model.getBoneCount() >= 1);
        CHECK(!model.getAnimations().empty());

        if (loaded && !model.getAnimations().empty() && !model.getBoneInfoMap().empty()) {
            // The extracted clip should carry the rotation keyframes.
            const Animation& clip = model.getAnimations().front();
            CHECK(clip.duration > 0.0f);
            CHECK(!clip.bones.empty());
            CHECK(!clip.bones.front().rotations.empty());

            // Drive an AnimationComponent with the model's clip + bone map.
            AnimationComponent anim;
            anim.setBoneInfoMap(model.getBoneInfoMap());
            for (const auto& a : model.getAnimations()) {
                anim.addAnimation(a.name, std::make_unique<Animation>(a));
            }
            anim.playAnimation(clip.name, /*loop=*/true, /*speed=*/1.0f);

            const int boneId = model.getBoneInfoMap().begin()->second.id;
            CHECK(boneId >= 0);

            // Sample the palette near t=0 (bind pose) and near t=0.5s (rotated ~45 deg).
            anim.update(0.0001f);
            const glm::mat4 atStart = anim.getBoneTransforms()[static_cast<std::size_t>(boneId)];
            for (int i = 0; i < 50; ++i) anim.update(0.01f); // advance ~0.5 s
            const glm::mat4 atMid = anim.getBoneTransforms()[static_cast<std::size_t>(boneId)];

            // The bone palette must actually change over time (the whole point of #287).
            CHECK(mat4Differs(atStart, atMid));
            // And at ~0.5 s the bone is posed away from the identity bind pose.
            CHECK(mat4Differs(atMid, glm::mat4(1.0f)));
        }
    }

    glfwDestroyWindow(win);
    glfwTerminate();

    if (g_failures == 0) {
        std::cout << "All skinned animation tests passed.\n";
        return 0;
    }
    std::cout << g_failures << " skinned animation test(s) failed.\n";
    return 1;
}
