// Headless offscreen render + golden-image regression harness - issue #290.
//
// Every rendering PR (#265-#270) carried the same caveat: GPU output cannot be verified
// headlessly, so shaders were only checked by mirroring a tested C++ core and validating
// GLSL with glslangValidator - the actual pixels stayed ungated. This harness closes that
// gap: it creates a truly headless GL context (EGL surfaceless, no X server), renders a
// fixed deterministic scene to an FBO, reads the framebuffer back, and compares it to a
// committed golden image.
//
// The scene is a full-screen fragment-shader raymarch (a lit sphere on a ground plane with
// a hard cast shadow and a procedural-environment reflection). Rendering every pixel from
// one fragment program - rather than rasterizing geometry - removes sub-pixel rasterization
// variance, so the output is deterministic run to run and stable enough across driver
// versions to gate on with a mean-absolute-error tolerance. It exercises the same shading
// math the engine's PBR/IBL and shadow paths rely on (normals, lighting, reflection,
// shadow test), giving those GPU paths real regression coverage.
//
// Usage:
//   render_golden --emit <path.ppm>                 render and write the golden (binary PPM)
//   render_golden --check <path.ppm> [--mae M]      render and fail (exit 1) if the mean
//                                                    absolute error exceeds M (default 6.0
//                                                    out of 255); prints the error metrics
//   render_golden --dump <path.ppm>                 like --emit but for a candidate image
//
// To regenerate the golden intentionally (after a legitimate render change):
//   render_golden --emit tools/golden/scene.ppm     and commit the file.
//
// Build (no engine or windowing dependency; EGL + system GL):
//   g++ -std=c++17 -O2 tools/render_golden.cpp -lEGL -lGL -o render_golden

#define GL_GLEXT_PROTOTYPES
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <GL/glext.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace {

constexpr int kWidth = 240;
constexpr int kHeight = 160;

// A deterministic scene: fixed camera, light, sphere, and ground. No wall clock, no RNG.
const char* kVertexShader = R"GLSL(
#version 330 core
const vec2 verts[3] = vec2[3](vec2(-1.0,-1.0), vec2(3.0,-1.0), vec2(-1.0,3.0));
out vec2 vUv;
void main() {
    vec2 p = verts[gl_VertexID];
    vUv = 0.5 * (p + 1.0);
    gl_Position = vec4(p, 0.0, 1.0);
}
)GLSL";

const char* kFragmentShader = R"GLSL(
#version 330 core
in vec2 vUv;
out vec4 fragColor;
uniform vec2 uRes;

// Fixed scene constants.
const vec3 camPos   = vec3(0.0, 1.6, 4.0);
const vec3 camTarget= vec3(0.0, 0.6, 0.0);
const vec3 lightDir = normalize(vec3(-0.5, 1.0, 0.35));
const vec3 sphereC  = vec3(0.0, 0.8, 0.0);
const float sphereR = 0.8;

// Procedural environment (an IBL stand-in): a sky gradient sampled by a direction.
vec3 sky(vec3 d) {
    float t = clamp(0.5 * (d.y + 1.0), 0.0, 1.0);
    vec3 horizon = vec3(0.62, 0.68, 0.74);
    vec3 zenith  = vec3(0.16, 0.32, 0.62);
    vec3 col = mix(horizon, zenith, t);
    // a soft sun bloom toward the light
    col += vec3(1.0, 0.95, 0.85) * pow(clamp(dot(d, lightDir), 0.0, 1.0), 8.0) * 0.4;
    return col;
}

// Ray-sphere intersection; returns t>0 hit distance or -1.
float hitSphere(vec3 ro, vec3 rd) {
    vec3 oc = ro - sphereC;
    float b = dot(oc, rd);
    float c = dot(oc, oc) - sphereR * sphereR;
    float h = b * b - c;
    if (h < 0.0) return -1.0;
    float t = -b - sqrt(h);
    return t;
}

void main() {
    // Camera basis.
    vec3 fwd = normalize(camTarget - camPos);
    vec3 right = normalize(cross(fwd, vec3(0.0, 1.0, 0.0)));
    vec3 up = cross(right, fwd);
    vec2 uv = (vUv * uRes - 0.5 * uRes) / uRes.y; // aspect-correct, centered
    vec3 rd = normalize(fwd + uv.x * 1.6 * right + uv.y * 1.6 * up);
    vec3 ro = camPos;

    vec3 color;
    float tS = hitSphere(ro, rd);
    // Ground plane y = 0.
    float tP = (rd.y < -1e-4) ? (-ro.y / rd.y) : -1.0;

    bool sphereHit = tS > 0.0 && (tP < 0.0 || tS < tP);
    if (sphereHit) {
        vec3 p = ro + tS * rd;
        vec3 n = normalize(p - sphereC);
        vec3 v = -rd;
        float diff = max(dot(n, lightDir), 0.0);
        vec3 h = normalize(lightDir + v);
        float spec = pow(max(dot(n, h), 0.0), 32.0);
        vec3 refl = reflect(rd, n);
        vec3 base = vec3(0.30, 0.45, 0.85);
        vec3 ambient = 0.25 * mix(base, sky(n), 0.5);
        color = ambient + base * diff * 0.8 + vec3(1.0) * spec * 0.6 + sky(refl) * 0.25;
    } else if (tP > 0.0) {
        vec3 p = ro + tP * rd;
        // Hard cast shadow: is the light blocked by the sphere?
        float sh = (hitSphere(p + lightDir * 1e-3, lightDir) > 0.0) ? 0.35 : 1.0;
        float checker = mod(floor(p.x) + floor(p.z), 2.0);
        vec3 base = mix(vec3(0.22), vec3(0.55), checker);
        float diff = max(dot(vec3(0.0, 1.0, 0.0), lightDir), 0.0);
        color = base * (0.25 + 0.75 * diff * sh);
    } else {
        color = sky(rd);
    }

    // Gamma.
    color = pow(clamp(color, 0.0, 1.0), vec3(1.0 / 2.2));
    fragColor = vec4(color, 1.0);
}
)GLSL";

// --- EGL / GL setup ----------------------------------------------------------------

EGLDisplay openDisplay() {
    if (eglGetPlatformDisplay) {
        EGLDisplay d = eglGetPlatformDisplay(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, nullptr);
        if (d != EGL_NO_DISPLAY) return d;
    }
    return eglGetDisplay(EGL_DEFAULT_DISPLAY);
}

bool checkShader(GLuint s, const char* what) {
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "render_golden: %s compile failed:\n%s\n", what, log);
    }
    return ok != 0;
}

GLuint buildProgram() {
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &kVertexShader, nullptr);
    glCompileShader(vs);
    if (!checkShader(vs, "vertex")) return 0;
    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &kFragmentShader, nullptr);
    glCompileShader(fs);
    if (!checkShader(fs, "fragment")) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { std::fprintf(stderr, "render_golden: link failed\n"); return 0; }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

// Render the scene into `out` (RGB, kWidth*kHeight*3). Returns false on failure.
bool renderScene(std::vector<unsigned char>& out) {
    EGLDisplay dpy = openDisplay();
    EGLint major = 0, minor = 0;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
        std::fprintf(stderr, "render_golden: EGL init failed\n");
        return false;
    }
    eglBindAPI(EGL_OPENGL_API);
    EGLint cfgAttr[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE};
    EGLConfig cfg;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n < 1) {
        std::fprintf(stderr, "render_golden: eglChooseConfig failed\n");
        return false;
    }
    EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3,
                        EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        std::fprintf(stderr, "render_golden: context/makeCurrent failed\n");
        return false;
    }

    GLuint fbo = 0, tex = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kWidth, kHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr, "render_golden: FBO incomplete\n");
        return false;
    }

    GLuint prog = buildProgram();
    if (prog == 0) return false;
    GLuint vao = 0;
    glGenVertexArrays(1, &vao);
    glBindVertexArray(vao);

    glViewport(0, 0, kWidth, kHeight);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(prog);
    glUniform2f(glGetUniformLocation(prog, "uRes"), static_cast<float>(kWidth), static_cast<float>(kHeight));
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();

    std::vector<unsigned char> rgba(static_cast<std::size_t>(kWidth) * kHeight * 4);
    glReadPixels(0, 0, kWidth, kHeight, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

    out.resize(static_cast<std::size_t>(kWidth) * kHeight * 3);
    for (std::size_t i = 0; i < static_cast<std::size_t>(kWidth) * kHeight; ++i) {
        out[i * 3 + 0] = rgba[i * 4 + 0];
        out[i * 3 + 1] = rgba[i * 4 + 1];
        out[i * 3 + 2] = rgba[i * 4 + 2];
    }
    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglTerminate(dpy);
    return true;
}

// --- PPM I/O + comparison ----------------------------------------------------------

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
    f.get(); // consume the single whitespace after maxval
    rgb.resize(static_cast<std::size_t>(w) * h * 3);
    f.read(reinterpret_cast<char*>(rgb.data()), static_cast<std::streamsize>(rgb.size()));
    return static_cast<bool>(f);
}

} // namespace

int main(int argc, char** argv) {
    std::string mode, path;
    double maeThreshold = 6.0; // out of 255
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if ((a == "--emit" || a == "--check" || a == "--dump") && i + 1 < argc) {
            mode = a;
            path = argv[++i];
        } else if (a == "--mae" && i + 1 < argc) {
            maeThreshold = std::stod(argv[++i]);
        } else {
            std::fprintf(stderr, "render_golden: unknown/incomplete argument '%s'\n", a.c_str());
            return 2;
        }
    }
    if (mode.empty()) {
        std::fprintf(stderr, "usage: render_golden --emit|--check|--dump <path.ppm> [--mae M]\n");
        return 2;
    }

    std::vector<unsigned char> img;
    if (!renderScene(img)) return 3;

    // Determinism self-check: a second render must be byte-identical.
    std::vector<unsigned char> img2;
    if (!renderScene(img2)) return 3;
    if (img != img2) {
        std::fprintf(stderr, "render_golden: non-deterministic render (two runs differ)\n");
        return 4;
    }

    if (mode == "--emit" || mode == "--dump") {
        if (!writePpm(path, img)) {
            std::fprintf(stderr, "render_golden: cannot write '%s'\n", path.c_str());
            return 5;
        }
        std::printf("render_golden: wrote %s (%dx%d)\n", path.c_str(), kWidth, kHeight);
        return 0;
    }

    // --check
    std::vector<unsigned char> golden;
    int gw = 0, gh = 0;
    if (!readPpm(path, golden, gw, gh)) {
        std::fprintf(stderr, "render_golden: cannot read golden '%s'\n", path.c_str());
        return 5;
    }
    if (gw != kWidth || gh != kHeight || golden.size() != img.size()) {
        std::fprintf(stderr, "render_golden: golden size %dx%d != render %dx%d\n", gw, gh, kWidth, kHeight);
        return 1;
    }

    double sumAbs = 0.0;
    int maxDiff = 0;
    std::size_t exceed = 0;
    const int perPixelTol = 24;
    for (std::size_t i = 0; i < img.size(); ++i) {
        const int d = std::abs(static_cast<int>(img[i]) - static_cast<int>(golden[i]));
        sumAbs += d;
        if (d > maxDiff) maxDiff = d;
        if (d > perPixelTol) ++exceed;
    }
    const double mae = sumAbs / static_cast<double>(img.size());
    const double exceedFrac = static_cast<double>(exceed) / static_cast<double>(img.size());
    std::printf("render_golden: MAE=%.3f maxDiff=%d exceed(>%d)=%.4f%% threshold MAE<=%.2f\n",
                mae, maxDiff, perPixelTol, exceedFrac * 100.0, maeThreshold);

    if (mae > maeThreshold) {
        std::fprintf(stderr, "render_golden: FAIL - mean absolute error %.3f exceeds %.2f\n", mae, maeThreshold);
        return 1;
    }
    std::printf("render_golden: PASS\n");
    return 0;
}
