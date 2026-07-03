// Test for skeletal animation blending + crossfade transitions - issue #321.
//
// #287 (plus the animation-loading follow-up) drove the skinned path from a single clip,
// but AnimationComponent's blendToAnimation / AnimationTransition / blendBoneTransforms
// crossfade scaffolding was never exercised. This drives a weighted crossfade between two
// clips and asserts the blended bone palette equals the weighted mix of the two source
// poses at t = 0 / mid / end, for both translation and rotation. It is pure palette math
// (no GL context), but it links AnimationComponent.cpp + assimp, so it runs under the
// gl-labelled job:
//   g++ -std=c++17 -I . tests/test_animation_blend.cpp \
//       src/core/components/AnimationComponent.cpp src/core/Logger.cpp src/core/Component.cpp \
//       -lassimp -o test_animation_blend

#include "src/core/components/AnimationComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>

using namespace IKore;

static int g_failures = 0;

#define CHECK(cond)                                                \
    do {                                                           \
        if (!(cond)) {                                             \
            std::printf("FAIL (line %d): %s\n", __LINE__, #cond);  \
            ++g_failures;                                          \
        }                                                          \
    } while (0)

static bool approx(float a, float b, float eps = 0.02f) { return std::fabs(a - b) <= eps; }

// A one-bone clip with a single static keyframe (so the pose is purely (pos, rot) and does
// not vary with time; the crossfade is therefore driven only by the blend weight).
static std::unique_ptr<Animation> makeClip(const std::string& name, const glm::vec3& pos,
                                           const glm::quat& rot) {
    auto a = std::make_unique<Animation>();
    a->name = name;
    a->duration = 1.0f;
    a->ticksPerSecond = 1.0f;
    Bone b;
    b.name = "b0";
    b.id = 0;
    b.positions.push_back(VectorKey{pos, 0.0f});
    b.rotations.push_back(QuatKey{rot, 0.0f});
    b.scales.push_back(VectorKey{glm::vec3(1.0f), 0.0f});
    a->bones.push_back(b);
    return a;
}

static void setup(AnimationComponent& c) {
    std::map<std::string, BoneInfo> map;
    map["b0"] = BoneInfo{0, glm::mat4(1.0f)};
    c.setBoneInfoMap(map);
}

static float translationX(const AnimationComponent& c) { return c.getBoneTransform(0)[3].x; }
static glm::vec3 rotate100(const AnimationComponent& c) {
    return glm::vec3(c.getBoneTransform(0) * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f));
}

int main() {
    const glm::quat qId(1.0f, 0.0f, 0.0f, 0.0f);
    const glm::quat q90 = glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 0.0f, 1.0f));

    // --- Single-clip playback is unchanged (each clip resolves to its own pose) ---
    {
        AnimationComponent c; setup(c);
        c.addAnimation("A", makeClip("A", glm::vec3(0.0f), qId));
        c.addAnimation("B", makeClip("B", glm::vec3(2.0f, 0.0f, 0.0f), qId));
        c.playAnimation("A");
        c.update(0.01f);
        CHECK(approx(translationX(c), 0.0f)); // clip A
        c.playAnimation("B");
        c.update(0.01f);
        CHECK(approx(translationX(c), 2.0f)); // clip B
    }

    // --- Translation crossfade: palette == weighted mix at t = 0 / mid / end ------
    {
        AnimationComponent c; setup(c);
        c.addAnimation("A", makeClip("A", glm::vec3(0.0f), qId));
        c.addAnimation("B", makeClip("B", glm::vec3(2.0f, 0.0f, 0.0f), qId));
        c.playAnimation("A");
        c.update(0.01f);
        c.blendToAnimation("B", 1.0f);

        c.update(0.001f);               // weight ~ 0 -> clip A pose
        CHECK(approx(translationX(c), 0.0f, 0.05f));
        c.update(0.25f);                // weight ~ 0.25 -> mix = 0.5
        CHECK(approx(translationX(c), 0.5f, 0.05f));
        c.update(0.25f);                // weight ~ 0.5 -> mix = 1.0
        CHECK(approx(translationX(c), 1.0f, 0.05f));
        c.update(0.25f);                // weight ~ 0.75 -> mix = 1.5
        CHECK(approx(translationX(c), 1.5f, 0.05f));
        c.update(0.5f);                 // weight >= 1 -> transition completes, switches to B
        c.update(0.01f);                // now pure clip B
        CHECK(approx(translationX(c), 2.0f, 0.05f));
    }

    // --- Rotation crossfade: slerp by weight at t = 0 / mid / end -----------------
    {
        AnimationComponent c; setup(c);
        c.addAnimation("A", makeClip("A", glm::vec3(0.0f), qId));
        c.addAnimation("B", makeClip("B", glm::vec3(0.0f), q90));
        c.playAnimation("A");
        c.update(0.01f);
        glm::vec3 v0 = rotate100(c);
        CHECK(approx(v0.x, 1.0f) && approx(v0.y, 0.0f)); // 0 deg

        c.blendToAnimation("B", 1.0f);
        c.update(0.5f);                                  // weight ~ 0.5 -> slerp to 45 deg
        glm::vec3 vm = rotate100(c);
        CHECK(approx(vm.x, 0.7071f, 0.03f) && approx(vm.y, 0.7071f, 0.03f));

        c.update(0.5f);                                  // completes
        c.update(0.01f);                                 // pure clip B -> 90 deg
        glm::vec3 v1 = rotate100(c);
        CHECK(approx(v1.x, 0.0f, 0.03f) && approx(v1.y, 1.0f, 0.03f));
    }

    if (g_failures == 0) {
        std::printf("test_animation_blend: all checks passed\n");
        return 0;
    }
    std::printf("test_animation_blend: %d check(s) failed\n", g_failures);
    return 1;
}
