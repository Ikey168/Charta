// GL render from the game camera with a theme - issue #354.
//
// Renders a small level from the top-down game camera (GameCamera, #354) with a theme
// palette into an offscreen FBO, using a perspective lookAt from the camera's eye/target/up.
// It asserts the camera frames the player (the player's themed color lands near the image
// center) and the theme applies (its background and wall colors are present), then re-renders
// with a second theme and confirms that theme's distinct player color appears. A gl-labelled
// tool (hidden GLFW window + software GL under Xvfb); skips if no GL context.
//   built as test_game_camera_gl (links glad/glfw/glm)

#include <glad/glad.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

#include "game/GameCamera.h"

using namespace IKore;
using namespace IKore::game;

namespace {
constexpr int kW = 200, kH = 200;

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    return s;
}
int toByte(float f) { return int(f * 255.0f + 0.5f); }
int countColor(const std::vector<unsigned char>& rgb, int r, int g, int b, int tol) {
    int n = 0;
    for (std::size_t i = 0; i + 2 < rgb.size(); i += 3)
        if (std::abs(int(rgb[i]) - r) <= tol && std::abs(int(rgb[i + 1]) - g) <= tol && std::abs(int(rgb[i + 2]) - b) <= tol) ++n;
    return n;
}
void centroid(const std::vector<unsigned char>& rgb, int w, int h, int r, int g, int b, int tol, float& cx, float& cy, int& n) {
    double sx = 0, sy = 0; n = 0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const std::size_t i = (std::size_t(y) * w + x) * 3;
            if (std::abs(int(rgb[i]) - r) <= tol && std::abs(int(rgb[i+1]) - g) <= tol && std::abs(int(rgb[i+2]) - b) <= tol) { sx += x; sy += y; ++n; }
        }
    cx = n ? float(sx / n) : -1; cy = n ? float(sy / n) : -1;
}
// A unit cube (triangle soup, positions only).
std::vector<float> cube() {
    const float v[8][3] = {{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                           {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}};
    const int f[12][3] = {{0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,4,5},{0,5,1},{3,2,6},{3,6,7},{1,5,6},{1,6,2},{0,3,7},{0,7,4}};
    std::vector<float> out;
    for (auto& tri : f) for (int k = 0; k < 3; ++k) { out.push_back(v[tri[k]][0]); out.push_back(v[tri[k]][1]); out.push_back(v[tri[k]][2]); }
    return out;
}
} // namespace

int main() {
    if (!glfwInit()) { std::printf("skip: GLFW init failed\n"); return 0; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* win = glfwCreateWindow(64, 64, "cam", nullptr, nullptr);
    if (!win) { std::printf("skip: no GL context\n"); glfwTerminate(); return 0; }
    glfwMakeContextCurrent(win);
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { std::printf("skip: no GL\n"); glfwDestroyWindow(win); glfwTerminate(); return 0; }

    GLuint fbo = 0, tex = 0, depth = 0;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kW, kH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kW, kH);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { std::printf("skip: FBO\n"); return 0; }
    glViewport(0, 0, kW, kH);
    glEnable(GL_DEPTH_TEST);

    const char* vs = "#version 330 core\nlayout(location=0) in vec3 aPos;\nuniform mat4 uMVP;\n"
                     "void main(){ gl_Position = uMVP * vec4(aPos,1.0); }\n";
    const char* fs = "#version 330 core\nuniform vec3 uColor; out vec4 F; void main(){ F=vec4(uColor,1.0); }\n";
    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, vs));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, fs));
    glLinkProgram(prog); glUseProgram(prog);
    const GLint uMVP = glGetUniformLocation(prog, "uMVP"), uColor = glGetUniformLocation(prog, "uColor");

    const std::vector<float> cubeMesh = cube();
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, cubeMesh.size() * sizeof(float), cubeMesh.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    // A small level: player centered, walls/coin/exit around it.
    struct Obj { ecs::Vec3 pos; std::string type; float s; };
    const std::vector<Obj> objs = {
        {{5, 0, 5}, "player", 1.2f}, {{5, 0, 1}, "wall", 1.0f}, {{1, 0, 5}, "wall", 1.0f},
        {{8, 0, 8}, "coin", 1.0f},  {{2, 0, 8}, "exit", 1.0f}};

    GameCamera cam;
    cam.mode = GameCameraMode::TopDown;
    cam.height = 12.0f;
    cam.update(ecs::Vec3{5, 0, 5});

    const glm::mat4 proj = glm::perspective(glm::radians(50.0f), float(kW) / kH, 0.1f, 100.0f);
    auto renderWith = [&](const Theme& theme) {
        const glm::mat4 view = glm::lookAt(glm::vec3(cam.eye.x, cam.eye.y, cam.eye.z),
                                           glm::vec3(cam.target.x, cam.target.y, cam.target.z),
                                           glm::vec3(cam.up.x, cam.up.y, cam.up.z));
        glClearColor(theme.background.r, theme.background.g, theme.background.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        for (const Obj& o : objs) {
            const RenderColor c = themedColor(o.type, theme);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(o.pos.x, o.pos.y, o.pos.z));
            model = glm::scale(model, glm::vec3(o.s, o.s, o.s));
            const glm::mat4 mvp = proj * view * model;
            glUniformMatrix4fv(uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
            glUniform3f(uColor, c.r, c.g, c.b);
            glDrawArrays(GL_TRIANGLES, 0, GLsizei(cubeMesh.size() / 3));
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
    // Dungeon theme: player framed near center; theme background + wall present.
    const Theme dun = dungeonTheme();
    renderWith(dun);
    std::vector<unsigned char> rgb = readback();
    float pcx, pcy; int pn;
    centroid(rgb, kW, kH, toByte(dun.player.r), toByte(dun.player.g), toByte(dun.player.b), 12, pcx, pcy, pn);
    if (pn <= 0) { std::fprintf(stderr, "FAIL: player not rendered\n"); ++failures; }
    else if (std::fabs(pcx - kW / 2) > 40 || std::fabs(pcy - kH / 2) > 40) {
        std::fprintf(stderr, "FAIL: player not framed near center (%.0f,%.0f)\n", pcx, pcy); ++failures;
    }
    if (countColor(rgb, toByte(dun.background.r), toByte(dun.background.g), toByte(dun.background.b), 10) <= 0) {
        std::fprintf(stderr, "FAIL: dungeon background missing\n"); ++failures;
    }
    if (countColor(rgb, toByte(dun.wall.r), toByte(dun.wall.g), toByte(dun.wall.b), 12) <= 0) {
        std::fprintf(stderr, "FAIL: dungeon wall missing\n"); ++failures;
    }

    // Grass theme applies its own distinct player color.
    const Theme grs = grassTheme();
    renderWith(grs);
    rgb = readback();
    if (countColor(rgb, toByte(grs.player.r), toByte(grs.player.g), toByte(grs.player.b), 12) <= 0) {
        std::fprintf(stderr, "FAIL: grass theme player color missing\n"); ++failures;
    }

    glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &tex); glDeleteRenderbuffers(1, &depth);
    glDeleteBuffers(1, &vbo); glDeleteVertexArrays(1, &vao);
    glfwDestroyWindow(win); glfwTerminate();

    if (failures == 0) { std::printf("test_game_camera_gl: all checks passed\n"); return 0; }
    std::printf("test_game_camera_gl: %d check(s) failed\n", failures);
    return 1;
}
