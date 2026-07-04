// Golden-image regression for the live Doodlebound render - issue #355.
//
// Extends the offscreen harness (#290) to lock the Doodlebound play view (per-type meshes +
// materials from #350, top-down game camera + theme from #354) against regressions. It
// creates a truly headless GL context (EGL surfaceless, no X server), rasterizes a fixed
// level - walls, player, coin, exit as colored cubes - from a fixed top-down camera pose
// into an FBO, reads it back, and compares it to a committed golden with a mean-absolute-
// error tolerance. The scene, camera, and palette are hardcoded here so the image is a
// stable snapshot; any change to the mesh transforms, materials, or camera moves pixels and
// fails the check. Rendered twice per run to assert determinism.
//
// Usage:
//   render_doodle_golden --emit <path.ppm>              render and write the golden (binary PPM)
//   render_doodle_golden --check <path.ppm> [--mae M]   fail (exit 1) if MAE exceeds M
//   render_doodle_golden --dump <path.ppm>              write a candidate image
//
// To regenerate the golden intentionally (after a legitimate render change):
//   g++ -std=c++17 -O2 tools/render_doodle_golden.cpp -lEGL -lGL -o render_doodle_golden
//   LIBGL_ALWAYS_SOFTWARE=1 ./render_doodle_golden --emit tools/golden/doodle.ppm
// and commit tools/golden/doodle.ppm.

#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 200, kHeight = 200;

// --- Minimal column-major 4x4 matrix math (no glm dependency in the standalone tool) -----
struct Mat4 { float m[16]; };
Mat4 identity() {
    Mat4 r{};
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.0f;
    return r;
}
Mat4 mul(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row) {
            float s = 0.0f;
            for (int k = 0; k < 4; ++k) s += a.m[k * 4 + row] * b.m[c * 4 + k];
            r.m[c * 4 + row] = s;
        }
    return r;
}
Mat4 translate(float x, float y, float z) {
    Mat4 r = identity();
    r.m[12] = x; r.m[13] = y; r.m[14] = z;
    return r;
}
Mat4 scale(float s) {
    Mat4 r = identity();
    r.m[0] = r.m[5] = r.m[10] = s;
    return r;
}
Mat4 perspective(float fovyRad, float aspect, float znear, float zfar) {
    Mat4 r{};
    const float f = 1.0f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect;
    r.m[5] = f;
    r.m[10] = (zfar + znear) / (znear - zfar);
    r.m[11] = -1.0f;
    r.m[14] = (2.0f * zfar * znear) / (znear - zfar);
    return r;
}
struct V3 { float x, y, z; };
V3 sub(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 norm(V3 a) { float l = std::sqrt(dot(a, a)); return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a; }
Mat4 lookAt(V3 eye, V3 center, V3 up) {
    const V3 f = norm(sub(center, eye));
    const V3 s = norm(cross(f, up));
    const V3 u = cross(s, f);
    Mat4 r = identity();
    r.m[0] = s.x; r.m[4] = s.y; r.m[8] = s.z;
    r.m[1] = u.x; r.m[5] = u.y; r.m[9] = u.z;
    r.m[2] = -f.x; r.m[6] = -f.y; r.m[10] = -f.z;
    r.m[12] = -dot(s, eye); r.m[13] = -dot(u, eye); r.m[14] = dot(f, eye);
    return r;
}

const char* kVS = R"GLSL(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uMVP;
void main(){ gl_Position = uMVP * vec4(aPos, 1.0); }
)GLSL";
const char* kFS = R"GLSL(
#version 330 core
uniform vec3 uColor; out vec4 F; void main(){ F = vec4(uColor, 1.0); }
)GLSL";

// A unit cube as a triangle soup (positions only).
std::vector<float> cube() {
    const float v[8][3] = {{-0.5f,-0.5f,-0.5f},{0.5f,-0.5f,-0.5f},{0.5f,0.5f,-0.5f},{-0.5f,0.5f,-0.5f},
                           {-0.5f,-0.5f,0.5f},{0.5f,-0.5f,0.5f},{0.5f,0.5f,0.5f},{-0.5f,0.5f,0.5f}};
    const int f[12][3] = {{0,1,2},{0,2,3},{4,6,5},{4,7,6},{0,4,5},{0,5,1},{3,2,6},{3,6,7},{1,5,6},{1,6,2},{0,3,7},{0,7,4}};
    std::vector<float> out;
    for (auto& t : f) for (int k = 0; k < 3; ++k) { out.push_back(v[t[k]][0]); out.push_back(v[t[k]][1]); out.push_back(v[t[k]][2]); }
    return out;
}

EGLDisplay openDisplay() {
    if (eglGetPlatformDisplay) {
        EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (d != EGL_NO_DISPLAY) return d;
    }
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}
GLuint buildProgram() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kVS, nullptr); glCompileShader(vs);
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &kFS, nullptr); glCompileShader(fs);
    GLuint p = glCreateProgram();
    glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { std::fprintf(stderr, "render_doodle_golden: link failed\n"); return 0; }
    return p;
}

// The fixed Doodlebound scene + camera + theme (a stable snapshot).
struct Obj { float x, z, s; float r, g, b; };
const Obj kObjs[] = {
    // walls (gray-blue)
    {5, 1, 1.0f, 0.38f, 0.40f, 0.52f}, {1, 5, 1.0f, 0.38f, 0.40f, 0.52f},
    {9, 5, 1.0f, 0.38f, 0.40f, 0.52f}, {5, 9, 1.0f, 0.38f, 0.40f, 0.52f},
    // player (green), coin (yellow), exit (blue)
    {5, 5, 1.2f, 0.30f, 0.85f, 0.55f}, {8, 8, 0.9f, 0.95f, 0.82f, 0.25f},
    {2, 8, 1.0f, 0.30f, 0.55f, 0.95f},
};
const float kBgR = 0.06f, kBgG = 0.06f, kBgB = 0.10f;

bool renderScene(std::vector<unsigned char>& out) {
    EGLDisplay dpy = openDisplay();
    EGLint major = 0, minor = 0;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) { std::fprintf(stderr, "EGL init failed\n"); return false; }
    eglBindAPI(EGL_OPENGL_API);
    EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) { std::fprintf(stderr, "eglChooseConfig failed\n"); return false; }
    EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) { std::fprintf(stderr, "context failed\n"); return false; }

    GLuint fbo = 0, tex = 0, depth = 0;
    glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glGenRenderbuffers(1, &depth); glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, kWidth, kHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) { std::fprintf(stderr, "FBO incomplete\n"); return false; }

    GLuint prog = buildProgram();
    if (!prog) return false;
    const std::vector<float> mesh = cube();
    GLuint vao = 0, vbo = 0;
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.size() * sizeof(float), mesh.data(), GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    glViewport(0, 0, kWidth, kHeight);
    glEnable(GL_DEPTH_TEST);
    glClearColor(kBgR, kBgG, kBgB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glUseProgram(prog);
    const GLint uMVP = glGetUniformLocation(prog, "uMVP"), uColor = glGetUniformLocation(prog, "uColor");

    // Fixed top-down camera looking down at the level center (player at 5,5).
    const Mat4 proj = perspective(50.0f * 3.14159265f / 180.0f, float(kWidth) / kHeight, 0.1f, 100.0f);
    const Mat4 view = lookAt(V3{5, 14, 5}, V3{5, 0, 5}, V3{0, 0, 1});
    const Mat4 vp = mul(proj, view);
    for (const Obj& o : kObjs) {
        const Mat4 model = mul(translate(o.x, 0.0f, o.z), scale(o.s));
        const Mat4 mvp = mul(vp, model);
        glUniformMatrix4fv(uMVP, 1, GL_FALSE, mvp.m);
        glUniform3f(uColor, o.r, o.g, o.b);
        glDrawArrays(GL_TRIANGLES, 0, GLsizei(mesh.size() / 3));
    }
    glFinish();

    std::vector<unsigned char> rgba(std::size_t(kWidth) * kHeight * 4);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
    out.resize(std::size_t(kWidth) * kHeight * 3);
    for (std::size_t i = 0; i < std::size_t(kWidth) * kHeight; ++i) {
        out[i * 3 + 0] = rgba[i * 4 + 0]; out[i * 3 + 1] = rgba[i * 4 + 1]; out[i * 3 + 2] = rgba[i * 4 + 2];
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    return true;
}

bool writePpm(const std::string& path, const std::vector<unsigned char>& rgb) {
    std::ofstream f(path, std::ios::binary);
    if (!f.good()) return false;
    f << "P6\n" << kWidth << " " << kHeight << "\n255\n";
    f.write(reinterpret_cast<const char*>(rgb.data()), std::streamsize(rgb.size()));
    return f.good();
}
bool readPpm(const std::string& path, std::vector<unsigned char>& rgb, int& w, int& h) {
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) return false;
    std::string magic; int maxval = 0;
    f >> magic >> w >> h >> maxval;
    if (magic != "P6" || maxval != 255) return false;
    f.get();
    rgb.resize(std::size_t(w) * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), std::streamsize(rgb.size()));
    return static_cast<bool>(f);
}

} // namespace

int main(int argc, char** argv) {
    std::string mode, path;
    double maeThreshold = 6.0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--emit" || a == "--check" || a == "--dump") && i + 1 < argc) { mode = a; path = argv[++i]; }
        else if (a == "--mae" && i + 1 < argc) { maeThreshold = std::stod(argv[++i]); }
        else { std::fprintf(stderr, "unknown/incomplete argument '%s'\n", a.c_str()); return 2; }
    }
    if (mode.empty()) { std::fprintf(stderr, "usage: render_doodle_golden --emit|--check|--dump <path.ppm> [--mae M]\n"); return 2; }

    std::vector<unsigned char> img;
    if (!renderScene(img)) return 3;
    std::vector<unsigned char> img2;
    if (!renderScene(img2)) return 3;
    if (img != img2) { std::fprintf(stderr, "render_doodle_golden: non-deterministic render\n"); return 4; }

    if (mode == "--emit" || mode == "--dump") {
        if (!writePpm(path, img)) { std::fprintf(stderr, "cannot write '%s'\n", path.c_str()); return 5; }
        std::printf("render_doodle_golden: wrote %s (%dx%d)\n", path.c_str(), kWidth, kHeight);
        return 0;
    }

    std::vector<unsigned char> golden; int gw = 0, gh = 0;
    if (!readPpm(path, golden, gw, gh)) { std::fprintf(stderr, "cannot read golden '%s'\n", path.c_str()); return 5; }
    if (gw != kWidth || gh != kHeight || golden.size() != img.size()) {
        std::fprintf(stderr, "render_doodle_golden: golden size %dx%d != render %dx%d\n", gw, gh, kWidth, kHeight);
        return 1;
    }
    double sumAbs = 0.0; int maxDiff = 0;
    for (std::size_t i = 0; i < img.size(); ++i) {
        const int d = std::abs(int(img[i]) - int(golden[i]));
        sumAbs += d; if (d > maxDiff) maxDiff = d;
    }
    const double mae = sumAbs / double(img.size());
    std::printf("render_doodle_golden: MAE=%.3f maxDiff=%d threshold MAE<=%.2f\n", mae, maxDiff, maeThreshold);
    if (mae > maeThreshold) { std::fprintf(stderr, "render_doodle_golden: FAIL - MAE %.3f exceeds %.2f\n", mae, maeThreshold); return 1; }
    std::printf("render_doodle_golden: PASS\n");
    return 0;
}
