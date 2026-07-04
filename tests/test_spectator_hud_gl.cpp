// GL render of the spectator HUD (scrubber bar + label) - issue #352.
//
// Resolves the scrubber HUD (SpectatorControls, #352) into positioned draw items and draws
// them into an offscreen FBO as filled quads: each Bar as a background track plus a filled
// portion (its progress), each Text/List as a panel. It asserts the HUD renders (the bar
// track and label panel are present) and that the fill grows as the replay timeline advances
// - i.e. the scrubber stays in sync with playback. A gl-labelled tool (hidden GLFW window +
// software GL under Xvfb); skips cleanly if no GL context.
//   built as test_spectator_hud_gl (links glad/glfw/glm)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "game/LevelFormat.h"
#include "game/SpectatorControls.h"

using namespace IKore;
using namespace IKore::game;

namespace {
constexpr int kW = 480, kH = 200;
const float kDt = 1.0f / 60.0f;

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}
std::string makeLevelJson() {
    LevelSpec s;
    s.walls.push_back(Wall{{{0, 0, 0}, {40, 0, 0}, {40, 0, 40}, {0, 0, 40}, {0, 0, 0}}});
    s.symbols.push_back(Symbol{"player", ecs::Vec3{5, 0, 5}, 0.0f});
    s.symbols.push_back(Symbol{"coin", ecs::Vec3{9, 0, 9}, 0.0f});
    s.symbols.push_back(Symbol{"exit", ecs::Vec3{14, 0, 14}, 0.0f});
    return toLevelJson(s);
}
RunTrace winTrace(const std::string& json) {
    LevelSpec spec; fromLevelJson(json, spec);
    DungeonGame g = loadGame(convert(spec));
    RunTrace t; t.dt = kDt;
    for (int i = 0; i < 200 && g.status == GameStatus::Playing; ++i) { GameInput in{1, 1}; g.update(in, kDt); t.inputs.push_back(in); }
    return t;
}
int countColor(const std::vector<unsigned char>& rgb, int r, int g, int b, int tol) {
    int n = 0;
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3)
        if (std::abs(int(rgb[i]) - r) <= tol && std::abs(int(rgb[i + 1]) - g) <= tol && std::abs(int(rgb[i + 2]) - b) <= tol) ++n;
    return n;
}
} // namespace

int main() {
    if (!glfwInit()) { std::printf("skip: GLFW init failed\n"); return 0; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "hud", nullptr, nullptr);
    if (!win) { std::printf("skip: no GL context\n"); glfwTerminate(); return 0; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::printf("skip: no GL\n"); glfwDestroyWindow(win); glfwTerminate(); return 0; }

    const std::string json = makeLevelJson();
    const Replay replay = makeRunReplay(json, winTrace(json));
    ReplayScrubber sc(replay);
    Hud hud = buildScrubberHud(sc);
    hud.setScreenSize(float(kW), float(kH));

    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { std::printf("skip: FBO\n"); return 0; }
    glViewport(0, 0, kW, kH);

    const char* vs = "#version 330 core\nlayout(location=0) in vec2 aPos;\nuniform mat4 uMVP;\n"
                     "void main(){ gl_Position = uMVP * vec4(aPos,0.0,1.0); }\n";
    const char* fs = "#version 330 core\nuniform vec3 uColor; out vec4 F; void main(){ F=vec4(uColor,1.0); }\n";
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog); glUseProgram(prog);
    const GLint uMVP = glGetUniformLocation(prog, "uMVP"), uColor = glGetUniformLocation(prog, "uColor");

    const float quad[12] = {0, 0, 1, 0, 1, 1, 0, 0, 1, 1, 0, 1}; // [0,1]^2, top-left origin
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    const glm::mat4 proj = glm::ortho(0.0f, float(kW), float(kH), 0.0f, -1.0f, 1.0f); // y down
    auto rect = [&](float x, float y, float w, float h, float r, float g, float b) {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, y, 0.0f));
        model = glm::scale(model, glm::vec3(w, h, 1.0f));
        const glm::mat4 mvp = proj * model;
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        glUniform3f(uColor, r, g, b);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    };
    auto drawHud = [&]() {
        glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        for (const HudDrawItem& it : resolveHud(hud)) {
            if (it.widget == HudWidget::Bar) {
                rect(it.x, it.y, it.w, it.h, 0.20f, 0.20f, 0.25f);           // track
                rect(it.x, it.y, it.w * it.bar, it.h, 0.20f, 0.80f, 0.35f);  // fill
            } else {
                rect(it.x, it.y, it.w, it.h, 0.15f, 0.15f, 0.22f);           // panel
            }
        }
        glFinish();
    };
    auto readback = [&]() {
        std::vector<unsigned char> rgba(std::size_t(kW) * kH * 4);
        glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        std::vector<unsigned char> rgb(std::size_t(kW) * kH * 3);
        for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) { rgb[i*3]=rgba[i*4]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2]; }
        return rgb;
    };

    int failures = 0;
    // At the start the fill is empty; the track and label panel still render.
    sc.seek(0);
    drawHud();
    std::vector<unsigned char> rgb = readback();
    const int track = countColor(rgb, 51, 51, 64, 8);   // 0.20,0.20,0.25
    const int panel = countColor(rgb, 38, 38, 56, 8);   // 0.15,0.15,0.22
    const int fill0 = countColor(rgb, 51, 204, 89, 10); // 0.20,0.80,0.35
    if (track <= 0) { std::fprintf(stderr, "FAIL: scrubber track not drawn\n"); ++failures; }
    if (panel <= 0) { std::fprintf(stderr, "FAIL: label panel not drawn\n"); ++failures; }

    // Advance the timeline to the end; the fill should grow.
    sc.seek(sc.length());
    drawHud();
    rgb = readback();
    const int fill1 = countColor(rgb, 51, 204, 89, 10);
    if (!(fill1 > fill0 + 100)) {
        std::fprintf(stderr, "FAIL: scrubber fill did not grow (start=%d end=%d)\n", fill0, fill1);
        ++failures;
    }

    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(win); glfwTerminate();

    if (failures == 0) { std::printf("test_spectator_hud_gl: all checks passed\n"); return 0; }
    std::printf("test_spectator_hud_gl: %d check(s) failed\n", failures);
    return 1;
}
