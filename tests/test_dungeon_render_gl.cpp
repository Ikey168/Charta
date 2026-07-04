// GL render pass for a live DungeonRuntime - issue #333.
//
// Proves the render list (test_dungeon_render_data, headless) actually draws: it loads a
// level, spawns it via DungeonRuntime, builds the draw list, and renders one colored quad
// per instance (top-down ortho) into an offscreen FBO, then reads the pixels back and
// asserts each type's color is present. A gl-labelled test (hidden GLFW window + software
// GL under Xvfb); it skips cleanly if no GL context is available.
//   built as test_dungeon_render_gl (links glad/glfw/glm)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/DungeonRenderData.h"
#include "game/DungeonRuntime.h"

using namespace IKore;

namespace {

constexpr int kW = 200, kH = 200;

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::fprintf(stderr, "shader compile failed: %s\n", log);
    }
    return s;
}

// Presence of a color (bytes) in the RGB readback, within a per-channel tolerance.
int countColor(const std::vector<unsigned char>& rgb, int r, int g, int b, int tol) {
    int n = 0;
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3) {
        if (std::abs(int(rgb[i]) - r) <= tol && std::abs(int(rgb[i + 1]) - g) <= tol &&
            std::abs(int(rgb[i + 2]) - b) <= tol)
            ++n;
    }
    return n;
}

} // namespace

int main() {
    if (!glfwInit()) { std::printf("skip: GLFW init failed (no display?)\n"); return 0; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "dungeon-render", nullptr, nullptr);
    if (!win) { std::printf("skip: no GL context\n"); glfwTerminate(); return 0; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::printf("skip: could not load GL\n");
        glfwDestroyWindow(win);
        glfwTerminate();
        return 0;
    }

    // Build a small level and its live draw list.
    game::SceneDescription scene;
    scene.spawns = {game::EntitySpawn{"player", ecs::Vec3{2, 0, 2}, 0.0f},
                    game::EntitySpawn{"coin", ecs::Vec3{5, 0, 5}, 0.0f},
                    game::EntitySpawn{"exit", ecs::Vec3{8, 0, 8}, 0.0f}};
    game::DungeonRuntime rt(game::loadGame(scene));
    ecs::Registry reg;
    rt.spawnInto(reg);
    const std::vector<game::RenderInstance> list = buildRenderList(reg, rt.game());

    // Ortho bounds around the instances.
    float minX = 1e9f, maxX = -1e9f, minZ = 1e9f, maxZ = -1e9f;
    for (const game::RenderInstance& i : list) {
        minX = std::fmin(minX, i.position.x - i.size.x);
        maxX = std::fmax(maxX, i.position.x + i.size.x);
        minZ = std::fmin(minZ, i.position.z - i.size.z);
        maxZ = std::fmax(maxZ, i.position.z + i.size.z);
    }
    const glm::mat4 mvp = glm::ortho(minX, maxX, minZ, maxZ, -1.0f, 1.0f);

    // Offscreen target.
    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::printf("skip: FBO incomplete\n");
        return 0;
    }
    glViewport(0, 0, kW, kH);
    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    const char* vs = "#version 330 core\n"
                     "layout(location=0) in vec2 aPos;\n"
                     "uniform mat4 uMVP; uniform vec2 uCenter; uniform vec2 uHalf;\n"
                     "void main(){ vec2 w = uCenter + aPos * uHalf * 2.0;\n"
                     "  gl_Position = uMVP * vec4(w, 0.0, 1.0); }\n";
    const char* fs = "#version 330 core\n"
                     "uniform vec3 uColor; out vec4 F; void main(){ F = vec4(uColor, 1.0); }\n";
    GLuint prog = glCreateProgram();
    GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
    glAttachShader(prog, v);
    glAttachShader(prog, f);
    glLinkProgram(prog);
    glUseProgram(prog);

    const float quad[12] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f,
                            -0.5f, -0.5f, 0.5f, 0.5f,  -0.5f, 0.5f};
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glUniformMatrix4fv(glGetUniformLocation(prog, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));

    // Draw walls first, then actors (so a marker is not hidden behind a wall).
    auto draw = [&](const game::RenderInstance& i) {
        glUniform2f(glGetUniformLocation(prog, "uCenter"), i.position.x, i.position.z);
        glUniform2f(glGetUniformLocation(prog, "uHalf"), i.size.x * 0.5f, i.size.z * 0.5f);
        glUniform3f(glGetUniformLocation(prog, "uColor"), i.color.r, i.color.g, i.color.b);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };
    for (const game::RenderInstance& i : list) if (i.type == "wall") draw(i);
    for (const game::RenderInstance& i : list) if (i.type != "wall") draw(i);
    glFinish();

    std::vector<unsigned char> rgba(static_cast<std::size_t>(kW) * kH * 4);
    glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<unsigned char> rgb(static_cast<std::size_t>(kW) * kH * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kW) * kH; ++i) {
        rgb[i * 3 + 0] = rgba[i * 4 + 0];
        rgb[i * 3 + 1] = rgba[i * 4 + 1];
        rgb[i * 3 + 2] = rgba[i * 4 + 2];
    }

    int failures = 0;
    auto expectColor = [&](const char* type, const char* label) {
        const game::RenderColor c = game::renderColorForType(type);
        const int r = int(c.r * 255.0f + 0.5f), g = int(c.g * 255.0f + 0.5f), b = int(c.b * 255.0f + 0.5f);
        const int n = countColor(rgb, r, g, b, 12);
        if (n <= 0) {
            std::fprintf(stderr, "FAIL: %s color not present in the render\n", label);
            ++failures;
        }
    };
    expectColor("player", "player");
    expectColor("coin", "coin");
    expectColor("exit", "exit");

    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(win);
    glfwTerminate();

    if (failures == 0) {
        std::printf("test_dungeon_render_gl: all checks passed\n");
        return 0;
    }
    std::printf("test_dungeon_render_gl: %d check(s) failed\n", failures);
    return 1;
}
