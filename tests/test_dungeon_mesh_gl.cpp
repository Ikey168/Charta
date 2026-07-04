// GL render pass drawing real per-type meshes for a live DungeonRuntime - issue #350.
//
// Where test_dungeon_render_gl (#333) drew one flat quad per instance, this draws a distinct
// primitive mesh per spawn type chosen by meshDecorator/RenderKind (#350): a circle for the
// player, a triangle for enemies, a small disc for coins, a hexagon for the exit, and cubes
// for walls - each in its material color. It renders the live scene top-down into an
// offscreen FBO, reads the pixels back, and asserts every type's color is present; then it
// steps the deterministic sim and re-renders, asserting the player's mesh moved with it.
// A gl-labelled test (hidden GLFW window + software GL under Xvfb); skips if no GL context.
//   built as test_dungeon_mesh_gl (links glad/glfw/glm)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <map>
#include <vector>

#include "game/DungeonDecor.h"
#include "game/DungeonRuntime.h"

using namespace IKore;

namespace {

constexpr int kW = 220, kH = 220;

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; glGetShaderInfoLog(s, 512, nullptr, log); std::fprintf(stderr, "shader: %s\n", log); }
    return s;
}

// A filled n-gon footprint (triangle fan as soup) in the XZ plane, radius 0.5, at angle a0.
std::vector<float> ngon(int n, float a0) {
    std::vector<float> v;
    const float r = 0.5f;
    for (int i = 0; i < n; ++i) {
        const float t0 = a0 + 6.2831853f * i / n, t1 = a0 + 6.2831853f * (i + 1) / n;
        v.insert(v.end(), {0.0f, 0.0f, r * std::cos(t0), r * std::sin(t0), r * std::cos(t1), r * std::sin(t1)});
    }
    return v;
}
// An axis-aligned unit square footprint (two triangles), for cubes/walls.
std::vector<float> quad() {
    return {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
}

std::vector<float> meshFor(game::MeshKind k) {
    switch (k) {
        case game::MeshKind::Sphere: return ngon(16, 0.0f);   // circle
        case game::MeshKind::Pyramid: return ngon(3, 1.5708f); // triangle
        case game::MeshKind::Disc: return ngon(12, 0.0f);      // circle (coin)
        case game::MeshKind::Prism: return ngon(6, 0.0f);      // hexagon
        case game::MeshKind::Diamond: return ngon(4, 0.0f);    // diamond
        case game::MeshKind::Cube: default: return quad();     // square
    }
}

int countColor(const std::vector<unsigned char>& rgb, int r, int g, int b, int tol) {
    int n = 0;
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3)
        if (std::abs(int(rgb[i]) - r) <= tol && std::abs(int(rgb[i + 1]) - g) <= tol &&
            std::abs(int(rgb[i + 2]) - b) <= tol)
            ++n;
    return n;
}
// Mean X (column) of pixels matching a color, or -1 if none.
float centroidX(const std::vector<unsigned char>& rgb, int w, int h, int r, int g, int b, int tol) {
    double sum = 0; long n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (std::size_t(y) * w + x) * 3;
            if (std::abs(int(rgb[i]) - r) <= tol && std::abs(int(rgb[i + 1]) - g) <= tol &&
                std::abs(int(rgb[i + 2]) - b) <= tol) { sum += x; ++n; }
        }
    return n ? float(sum / n) : -1.0f;
}

} // namespace

int main() {
    if (!glfwInit()) { std::printf("skip: GLFW init failed (no display?)\n"); return 0; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "dungeon-mesh", nullptr, nullptr);
    if (!win) { std::printf("skip: no GL context\n"); glfwTerminate(); return 0; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::printf("skip: could not load GL\n"); glfwDestroyWindow(win); glfwTerminate(); return 0;
    }

    // Live scene: open field so the player can move; enemy parked far away.
    game::SceneDescription scene;
    world::Box wall; wall.center = ecs::Vec3{6, 0, 0}; wall.size = ecs::Vec3{1, 2, 1}; wall.yaw = 0;
    scene.wallBoxes = {wall};
    scene.spawns = {game::EntitySpawn{"player", ecs::Vec3{2, 0, 6}, 0.0f},
                    game::EntitySpawn{"enemy", ecs::Vec3{11, 0, 8}, 0.0f}, // in view, far from the player
                    game::EntitySpawn{"coin", ecs::Vec3{9, 0, 6}, 0.0f},
                    game::EntitySpawn{"exit", ecs::Vec3{11, 0, 2}, 0.0f}};
    game::DungeonRuntime rt(game::loadGame(scene));
    ecs::Registry reg;
    rt.spawnInto(reg, game::meshDecorator());

    // Ortho bounds around the content.
    float minX = -2, maxX = 13, minZ = -2, maxZ = 9;
    const glm::mat4 proj = glm::ortho(minX, maxX, minZ, maxZ, -1.0f, 1.0f);

    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { std::printf("skip: FBO\n"); return 0; }
    glViewport(0, 0, kW, kH);

    const char* vs = "#version 330 core\nlayout(location=0) in vec2 aPos;\nuniform mat4 uMVP;\n"
                     "void main(){ gl_Position = uMVP * vec4(aPos, 0.0, 1.0); }\n";
    const char* fs = "#version 330 core\nuniform vec3 uColor; out vec4 F; void main(){ F = vec4(uColor,1.0); }\n";
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog);
    glUseProgram(prog);
    const GLint uMVP = glGetUniformLocation(prog, "uMVP");
    const GLint uColor = glGetUniformLocation(prog, "uColor");

    // One VBO per mesh kind.
    GLuint vao = 0; glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    std::map<int, std::pair<GLuint, int>> meshes; // kind -> (vbo, vertexCount)
    for (int k = 0; k <= int(game::MeshKind::Diamond); ++k) {
        const std::vector<float> m = meshFor(game::MeshKind(k));
        GLuint vbo = 0; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, m.size() * sizeof(float), m.data(), GL_STATIC_DRAW);
        meshes[k] = {vbo, int(m.size() / 2)};
    }

    auto drawScene = [&]() {
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Walls (cubes) from the game geometry, in the wall color.
        const game::RenderColor wc = game::renderColorForType("wall");
        for (const world::Box& b : rt.game().walls) {
            glBindBuffer(GL_ARRAY_BUFFER, meshes[int(game::MeshKind::Cube)].first);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(b.center.x, b.center.z, 0.0f));
            model = glm::scale(model, glm::vec3(b.size.x, b.size.z, 1.0f));
            const glm::mat4 mvp = proj * model;
            glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(uColor, wc.r, wc.g, wc.b);
            glDrawArrays(GL_TRIANGLES, 0, meshes[int(game::MeshKind::Cube)].second);
        }
        // Actors: each entity's per-type mesh from its RenderKind + Transform.
        reg.view<ecs::Transform, game::RenderKind>().each(
            [&](ecs::Entity, ecs::Transform& t, game::RenderKind& rk) {
                auto& mesh = meshes[int(rk.kind)];
                glBindBuffer(GL_ARRAY_BUFFER, mesh.first);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(0);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(t.position.x, t.position.z, 0.0f));
                model = glm::scale(model, glm::vec3(rk.size, rk.size, 1.0f));
                const glm::mat4 mvp = proj * model;
                glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
                glUniform3f(uColor, rk.color.r, rk.color.g, rk.color.b);
                glDrawArrays(GL_TRIANGLES, 0, mesh.second);
            });
        glFinish();
    };
    auto readback = [&]() {
        std::vector<unsigned char> rgba(std::size_t(kW) * kH * 4);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        std::vector<unsigned char> rgb(std::size_t(kW) * kH * 3);
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) {
            rgb[i * 3 + 0] = rgba[i * 4 + 0]; rgb[i * 3 + 1] = rgba[i * 4 + 1]; rgb[i * 3 + 2] = rgba[i * 4 + 2];
        }
        return rgb;
    };

    drawScene();
    std::vector<unsigned char> rgb = readback();

    int failures = 0;
    auto expect = [&](const char* type, const char* label) {
        const game::RenderColor c = game::renderColorForType(type);
        const int r = int(c.r * 255 + 0.5f), g = int(c.g * 255 + 0.5f), b = int(c.b * 255 + 0.5f);
        if (countColor(rgb, r, g, b, 14) <= 0) { std::fprintf(stderr, "FAIL: %s not drawn\n", label); ++failures; }
    };
    expect("player", "player"); expect("enemy", "enemy"); expect("coin", "coin");
    expect("exit", "exit"); expect("wall", "wall");

    // Player mesh moves with the deterministic sim: step it east and confirm the green
    // (player) pixels' centroid shifts right.
    const game::RenderColor pc = game::renderColorForType("player");
    const int pr = int(pc.r * 255 + 0.5f), pg = int(pc.g * 255 + 0.5f), pb = int(pc.b * 255 + 0.5f);
    const float beforeX = centroidX(rgb, kW, kH, pr, pg, pb, 14);
    for (int i = 0; i < 40; ++i) rt.update(reg, game::GameInput{1.0f, 0.0f}, 1.0f / 60.0f);
    drawScene();
    rgb = readback();
    const float afterX = centroidX(rgb, kW, kH, pr, pg, pb, 14);
    if (!(beforeX >= 0 && afterX > beforeX + 2.0f)) {
        std::fprintf(stderr, "FAIL: player mesh did not move (before=%.1f after=%.1f)\n", beforeX, afterX);
        ++failures;
    }

    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
    glfwDestroyWindow(win); glfwTerminate();

    if (failures == 0) { std::printf("test_dungeon_mesh_gl: all checks passed\n"); return 0; }
    std::printf("test_dungeon_mesh_gl: %d check(s) failed\n", failures);
    return 1;
}
