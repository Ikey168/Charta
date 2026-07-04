// GL render of gameplay-event particle bursts - issue #353.
//
// Turns game events (GameEvents, #353) into deterministic particle bursts and draws them
// into an offscreen FBO: a win burst (green) and a loss burst (red), each a ring of small
// quads sized by the event's EffectSpec, at the event position. It reads the pixels back and
// asserts each effect's color is present - i.e. the event -> particle path renders under the
// GL/audio job. A gl-labelled tool (hidden GLFW window + software GL under Xvfb); skips if no
// GL context.
//   built as test_game_fx_gl (links glad/glfw/glm)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "game/GameEvents.h"

using namespace IKore;
using namespace IKore::game;

namespace {
constexpr int kW = 240, kH = 120;

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
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
    GLFWwindow* win = glfwCreateWindow(64, 64, "fx", nullptr, nullptr);
    if (!win) { std::printf("skip: no GL context\n"); glfwTerminate(); return 0; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::printf("skip: no GL\n"); glfwDestroyWindow(win); glfwTerminate(); return 0; }

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

    const float quad[12] = {-0.5f, -0.5f, 0.5f, -0.5f, 0.5f, 0.5f, -0.5f, -0.5f, 0.5f, 0.5f, -0.5f, 0.5f};
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    const glm::mat4 proj = glm::ortho(0.0f, 10.0f, 0.0f, 5.0f, -1.0f, 1.0f);
    // Draw a burst: `count` quads on a ring around (cx,cz), colored by the EffectSpec.
    auto burst = [&](const EffectSpec& fx, float cx, float cz) {
        for (int i = 0; i < fx.particles; ++i) {
            const float a = 6.2831853f * i / fx.particles;
            const float rad = 0.4f + 0.5f * (float(i % 3) / 3.0f);
            const float x = cx + rad * std::cos(a), z = cz + rad * std::sin(a);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(x, z, 0.0f));
            model = glm::scale(model, glm::vec3(0.18f, 0.18f, 1.0f));
            const glm::mat4 mvp = proj * model;
            glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(uColor, fx.r, fx.g, fx.b);
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }
    };

    glClearColor(0.02f, 0.02f, 0.03f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    burst(effectFor(GameEventType::Win), 2.5f, 2.5f);   // green
    burst(effectFor(GameEventType::Lose), 7.5f, 2.5f);  // red
    glFinish();

    std::vector<unsigned char> rgba(std::size_t(kW) * kH * 4);
    glReadPixels(0, 0, kW, kH, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    std::vector<unsigned char> rgb(std::size_t(kW) * kH * 3);
    for (std::size_t i = 0; i < std::size_t(kW) * kH; ++i) { rgb[i*3]=rgba[i*4]; rgb[i*3+1]=rgba[i*4+1]; rgb[i*3+2]=rgba[i*4+2]; }

    int failures = 0;
    const EffectSpec winFx = effectFor(GameEventType::Win), loseFx = effectFor(GameEventType::Lose);
    const int winN = countColor(rgb, int(winFx.r*255+0.5f), int(winFx.g*255+0.5f), int(winFx.b*255+0.5f), 12);
    const int loseN = countColor(rgb, int(loseFx.r*255+0.5f), int(loseFx.g*255+0.5f), int(loseFx.b*255+0.5f), 12);
    if (winN <= 0) { std::fprintf(stderr, "FAIL: win burst not drawn\n"); ++failures; }
    if (loseN <= 0) { std::fprintf(stderr, "FAIL: lose burst not drawn\n"); ++failures; }

    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(win); glfwTerminate();

    if (failures == 0) { std::printf("test_game_fx_gl: all checks passed\n"); return 0; }
    std::printf("test_game_fx_gl: %d check(s) failed\n", failures);
    return 1;
}
