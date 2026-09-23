#include "render/MobileRenderer.h"

#include "doodle/Doodle.h"
#include "game/GameCamera.h"
#include "game/LaunchContent.h"
#include "game/TouchControls.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cmath>

namespace doodlebound {
namespace {

using IKore::ecs::Vec3;
using IKore::game::RenderColor;

struct Mat4 { float v[16]{}; };

Mat4 identity() { Mat4 m; m.v[0]=m.v[5]=m.v[10]=m.v[15]=1.0f; return m; }
Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int col=0; col<4; ++col)
        for (int row=0; row<4; ++row)
            for (int k=0; k<4; ++k) r.v[col*4+row] += a.v[k*4+row]*b.v[col*4+k];
    return r;
}
Vec3 sub(Vec3 a, Vec3 b) { return {a.x-b.x,a.y-b.y,a.z-b.z}; }
Vec3 cross(Vec3 a, Vec3 b) {
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
}
float dot(Vec3 a, Vec3 b) { return a.x*b.x+a.y*b.y+a.z*b.z; }
Vec3 unit(Vec3 a) {
    const float n=std::sqrt(dot(a,a));
    return n>0.00001f ? Vec3{a.x/n,a.y/n,a.z/n} : Vec3{0,1,0};
}
Mat4 perspective(float fov, float aspect, float nearZ, float farZ) {
    Mat4 m; const float f=1.0f/std::tan(fov*0.5f);
    m.v[0]=f/aspect; m.v[5]=f;
    m.v[10]=(farZ+nearZ)/(nearZ-farZ); m.v[11]=-1;
    m.v[14]=(2*farZ*nearZ)/(nearZ-farZ);
    return m;
}
Mat4 lookAt(Vec3 eye, Vec3 target, Vec3 up) {
    const Vec3 f=unit(sub(target,eye));
    const Vec3 s=unit(cross(f,up));
    const Vec3 u=cross(s,f);
    Mat4 m=identity();
    m.v[0]=s.x; m.v[4]=s.y; m.v[8]=s.z;
    m.v[1]=u.x; m.v[5]=u.y; m.v[9]=u.z;
    m.v[2]=-f.x; m.v[6]=-f.y; m.v[10]=-f.z;
    m.v[12]=-dot(s,eye); m.v[13]=-dot(u,eye); m.v[14]=dot(f,eye);
    return m;
}
Mat4 model(Vec3 center, Vec3 scale, float yaw=0.0f) {
    Mat4 m=identity(); const float c=std::cos(yaw), s=std::sin(yaw);
    m.v[0]=c*scale.x; m.v[2]=s*scale.x;
    m.v[5]=scale.y;
    m.v[8]=-s*scale.z; m.v[10]=c*scale.z;
    m.v[12]=center.x; m.v[13]=center.y; m.v[14]=center.z;
    return m;
}

GLuint compile(GLenum kind, const char* source) {
    const GLuint shader=glCreateShader(kind);
    glShaderSource(shader,1,&source,nullptr);
    glCompileShader(shader);
    GLint ok=GL_FALSE; glGetShaderiv(shader,GL_COMPILE_STATUS,&ok);
    if (!ok) { glDeleteShader(shader); return 0; }
    return shader;
}

const char* kVertex = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
uniform mat4 uViewProjection;
uniform mat4 uModel;
out vec3 vNormal;
void main() {
    gl_Position = uViewProjection * uModel * vec4(aPosition, 1.0);
    vNormal = normalize(mat3(uModel) * aNormal);
}
)GLSL";
const char* kFragment = R"GLSL(#version 300 es
precision mediump float;
in vec3 vNormal;
uniform vec3 uColor;
uniform vec3 uLight;
out vec4 fragColor;
void main() {
    float diffuse = max(dot(normalize(vNormal), normalize(uLight)), 0.0);
    fragColor = vec4(uColor * (0.52 + 0.48 * diffuse), 1.0);
}
)GLSL";

std::vector<float> cubeVertices() {
    // Counterclockwise unit cube, 36 vertices, interleaved position and normal.
    std::vector<float> out; out.reserve(36*6);
    struct Face { Vec3 n; Vec3 p[4]; };
    const Face faces[] = {
        {{ 1,0,0},{{.5f,-.5f,-.5f},{.5f,.5f,-.5f},{.5f,.5f,.5f},{.5f,-.5f,.5f}}},
        {{-1,0,0},{{-.5f,-.5f,.5f},{-.5f,.5f,.5f},{-.5f,.5f,-.5f},{-.5f,-.5f,-.5f}}},
        {{0,1,0},{{-.5f,.5f,-.5f},{-.5f,.5f,.5f},{.5f,.5f,.5f},{.5f,.5f,-.5f}}},
        {{0,-1,0},{{-.5f,-.5f,.5f},{-.5f,-.5f,-.5f},{.5f,-.5f,-.5f},{.5f,-.5f,.5f}}},
        {{0,0,1},{{.5f,-.5f,.5f},{.5f,.5f,.5f},{-.5f,.5f,.5f},{-.5f,-.5f,.5f}}},
        {{0,0,-1},{{-.5f,-.5f,-.5f},{-.5f,.5f,-.5f},{.5f,.5f,-.5f},{.5f,-.5f,-.5f}}},
    };
    for (const Face& face: faces) {
        for (int idx : {0,1,2,0,2,3}) {
            const Vec3 p=face.p[idx];
            out.insert(out.end(),{p.x,p.y,p.z,face.n.x,face.n.y,face.n.z});
        }
    }
    return out;
}

bool validLevel(const IKore::game::LevelSpec& spec) {
    if (spec.walls.size()>512 || spec.symbols.size()>512 ||
        !std::isfinite(spec.wallHeight) || !std::isfinite(spec.wallThickness) ||
        spec.wallHeight<=0 || spec.wallHeight>12 ||
        spec.wallThickness<=0 || spec.wallThickness>4) return false;
    std::size_t points=0; int players=0, exits=0;
    auto validPoint=[](Vec3 p) {
        return std::isfinite(p.x) && std::isfinite(p.z) &&
               std::fabs(p.x)<=256 && std::fabs(p.z)<=256;
    };
    for (const auto& wall: spec.walls) {
        points+=wall.polyline.size(); if (points>4096) return false;
        for (Vec3 p: wall.polyline) if (!validPoint(p)) return false;
    }
    for (const auto& s: spec.symbols) {
        if (!validPoint(s.position) || !std::isfinite(s.yaw)) return false;
        if (s.type=="player") ++players;
        if (s.type=="exit" || s.type=="door") ++exits;
    }
    return players==1 && exits==1;
}

} // namespace

MobileRenderer::MobileRenderer() { startLevel(0); }
MobileRenderer::~MobileRenderer() = default; // GL resources are released on the GL thread.

void MobileRenderer::loadSceneLocked(const IKore::game::SceneDescription& scene) {
    scene_=scene; game_=IKore::game::loadGame(scene_);
    tour_=IKore::game::TourController{};
    pointers_.clear(); movementPointer_=lookPointer_=-1;
    hasLevel_=true; paused_=false; elapsed_=accumulator_=effectTime_=0.0f;
    cameraX_=game_.playerPosition.x; cameraZ_=game_.playerPosition.z;
    lastCoins_=0; lastStatus_=IKore::game::GameStatus::Playing;
}

bool MobileRenderer::startLevel(int index) {
    const auto manifest=IKore::game::launchManifest();
    if (index<0 || index>=static_cast<int>(manifest.size())) return false;
    const auto scene=manifest[static_cast<std::size_t>(index)].build();
    std::lock_guard<std::mutex> lock(mutex_);
    levelIndex_=index; loadSceneLocked(scene); return true;
}
void MobileRenderer::restart() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (hasLevel_) loadSceneLocked(scene_);
}
bool MobileRenderer::loadLevelJson(const std::string& json) {
    if (json.size()>1024*1024) return false;
    IKore::game::detail::Json root;
    if (!IKore::game::detail::parse(json,root) || !root.isObj() ||
        root.at("format").asStr()!="doodle-level" || root.at("version").asNum()!=1.0)
        return false;
    IKore::game::LevelSpec spec;
    if (!IKore::game::fromLevelJson(json,spec) || !validLevel(spec)) return false;
    const auto scene=IKore::game::convert(spec);
    std::lock_guard<std::mutex> lock(mutex_);
    levelIndex_=-1; loadSceneLocked(scene); return true;
}

std::string MobileRenderer::convertPhoto(const std::vector<std::uint32_t>& argb,int width,int height) const {
    if (width<32 || height<32 || width>1024 || height>1024 ||
        argb.size()!=static_cast<std::size_t>(width)*static_cast<std::size_t>(height)) return {};
    IKore::cv::Image image(width,height,3);
    for (std::size_t i=0; i<argb.size(); ++i) {
        image.data[i*3+0]=static_cast<std::uint8_t>((argb[i]>>16)&255);
        image.data[i*3+1]=static_cast<std::uint8_t>((argb[i]>>8)&255);
        image.data[i*3+2]=static_cast<std::uint8_t>(argb[i]&255);
    }
    IKore::doodle::Options options;
    options.worldScale=0.05f;
    const auto review=IKore::doodle::interpretPhoto(image,options);
    auto spec=review.toLevelSpec();
    // CV data is a draft until reviewed. Preserve its detected geometry and
    // return JSON for Kotlin's review screen; do not silently mark it playable.
    if (spec.walls.size()>512 || spec.symbols.size()>512) return {};
    return IKore::game::toLevelJson(spec);
}

void MobileRenderer::touch(int action,int pointerId,float x,float y) {
    if (!std::isfinite(x) || !std::isfinite(y)) return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (action==3) { pointers_.clear(); movementPointer_=lookPointer_=-1; return; }
    if (action==0 || action==5) {
        if (pointers_.count(pointerId)) return;
        const bool movement=leftHanded_ ? x>=width_*0.5f : x<width_*0.5f;
        pointers_[pointerId]={x,y,x,y,movement};
        if (movement && movementPointer_<0) movementPointer_=pointerId;
        if (!movement && lookPointer_<0) lookPointer_=pointerId;
    } else if (action==2) {
        auto it=pointers_.find(pointerId);
        if (it==pointers_.end()) return;
        if (pointerId==lookPointer_ && tour_.isFirstPerson())
            tour_.camera.look(x-it->second.x,y-it->second.y);
        it->second.x=x; it->second.y=y;
    } else if (action==1 || action==6) {
        pointers_.erase(pointerId);
        if (pointerId==movementPointer_) movementPointer_=-1;
        if (pointerId==lookPointer_) lookPointer_=-1;
        for (const auto& [id,p]:pointers_) {
            if (p.movement && movementPointer_<0) movementPointer_=id;
            if (!p.movement && lookPointer_<0) lookPointer_=id;
        }
    }
}
void MobileRenderer::pause() {
    std::lock_guard<std::mutex> lock(mutex_);
    paused_=true; accumulator_=0; pointers_.clear(); movementPointer_=lookPointer_=-1;
}
void MobileRenderer::resume() { std::lock_guard<std::mutex> lock(mutex_); paused_=false; accumulator_=0; }
void MobileRenderer::setTour(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasLevel_) return;
    if (enabled && !tour_.isFirstPerson()) tour_.enterFirstPerson(game_);
    else if (!enabled) tour_.exitToTopDown();
    pointers_.clear(); movementPointer_=lookPointer_=-1;
}
void MobileRenderer::setLeftHanded(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    leftHanded_=enabled; pointers_.clear(); movementPointer_=lookPointer_=-1;
}
void MobileRenderer::setReducedMotion(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_); reducedMotion_=enabled;
}
int MobileRenderer::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!hasLevel_) return 3;
    return game_.won()?1:(game_.lost()?2:0);
}
int MobileRenderer::coinsCollected() const {
    std::lock_guard<std::mutex> lock(mutex_); return game_.coinsCollected;
}
int MobileRenderer::totalCoins() const {
    std::lock_guard<std::mutex> lock(mutex_); return game_.totalCoins;
}

void MobileRenderer::tickLocked(float dt) {
    if (!hasLevel_ || paused_) return;
    IKore::game::GameInput input;
    auto it=pointers_.find(movementPointer_);
    if (it!=pointers_.end()) {
        const auto& p=it->second;
        const float radius=std::max(50.0f,std::min(width_,height_)*0.18f);
        input=IKore::platform::joystickInput(p.originX,p.originY,p.x,p.y,radius);
    }
    if (tour_.isFirstPerson()) {
        tour_.walk(game_,-input.moveZ,input.moveX,dt);
    } else {
        game_.update(input,dt);
        const float smoothing=std::min(1.0f,dt*8.0f);
        cameraX_+=(game_.playerPosition.x-cameraX_)*smoothing;
        cameraZ_+=(game_.playerPosition.z-cameraZ_)*smoothing;
    }
    if (!reducedMotion_ && (game_.coinsCollected!=lastCoins_ || game_.status!=lastStatus_)) effectTime_=0.5f;
    lastCoins_=game_.coinsCollected; lastStatus_=game_.status;
    effectTime_=std::max(0.0f,effectTime_-dt); elapsed_+=dt;
}

void MobileRenderer::surfaceCreated() {
    std::lock_guard<std::mutex> lock(mutex_);
    // After EGL context loss the GLuint numbers are stale; do not delete them in
    // the new context. Create fresh resources in the current context.
    program_=vertexArray_=vertexBuffer_=0; glReady_=initGlLocked();
}
void MobileRenderer::surfaceDestroyed() {
    std::lock_guard<std::mutex> lock(mutex_); releaseGlLocked();
}
void MobileRenderer::resize(int width,int height) {
    std::lock_guard<std::mutex> lock(mutex_);
    width_=std::max(1,width); height_=std::max(1,height);
    glViewport(0,0,width_,height_);
}
void MobileRenderer::drawFrame(float deltaSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!glReady_) return;
    if (std::isfinite(deltaSeconds) && deltaSeconds>0 && !paused_) {
        accumulator_+=std::min(deltaSeconds,0.1f);
        constexpr float step=1.0f/60.0f;
        for (int i=0; accumulator_>=step && i<6; ++i) {
            tickLocked(step); accumulator_-=step;
        }
    }
    renderLocked();
}

bool MobileRenderer::initGlLocked() {
    const GLuint vs=compile(GL_VERTEX_SHADER,kVertex);
    const GLuint fs=compile(GL_FRAGMENT_SHADER,kFragment);
    if (!vs || !fs) { if(vs)glDeleteShader(vs); if(fs)glDeleteShader(fs); return false; }
    program_=glCreateProgram(); glAttachShader(program_,vs); glAttachShader(program_,fs);
    glLinkProgram(program_); glDeleteShader(vs); glDeleteShader(fs);
    GLint ok=GL_FALSE; glGetProgramiv(program_,GL_LINK_STATUS,&ok);
    if (!ok) { releaseGlLocked(); return false; }
    mvpLocation_=glGetUniformLocation(program_,"uViewProjection");
    modelLocation_=glGetUniformLocation(program_,"uModel");
    colorLocation_=glGetUniformLocation(program_,"uColor");
    lightLocation_=glGetUniformLocation(program_,"uLight");
    const auto cube=cubeVertices();
    glGenVertexArrays(1,&vertexArray_); glBindVertexArray(vertexArray_);
    glGenBuffers(1,&vertexBuffer_); glBindBuffer(GL_ARRAY_BUFFER,vertexBuffer_);
    glBufferData(GL_ARRAY_BUFFER,static_cast<GLsizeiptr>(cube.size()*sizeof(float)),cube.data(),GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,6*sizeof(float),nullptr); glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,6*sizeof(float),reinterpret_cast<void*>(3*sizeof(float)));
    glEnableVertexAttribArray(1); glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LEQUAL); glDisable(GL_CULL_FACE);
    return glGetError()==GL_NO_ERROR;
}
void MobileRenderer::releaseGlLocked() {
    if (vertexBuffer_) glDeleteBuffers(1,&vertexBuffer_);
    if (vertexArray_) glDeleteVertexArrays(1,&vertexArray_);
    if (program_) glDeleteProgram(program_);
    program_=vertexArray_=vertexBuffer_=0; glReady_=false;
}

void MobileRenderer::renderLocked() {
    const auto theme=IKore::game::dungeonTheme();
    glViewport(0,0,width_,height_);
    glClearColor(theme.background.r,theme.background.g,theme.background.b,1);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    if (!hasLevel_) return;
    Vec3 eye{},target{},up{};
    if (tour_.isFirstPerson()) {
        eye=tour_.camera.position; target=Vec3{eye.x+tour_.camera.forward().x,
            eye.y+tour_.camera.forward().y,eye.z+tour_.camera.forward().z};
        up={0,1,0};
    } else {
        target={cameraX_,0,cameraZ_};
        eye={cameraX_,14.0f,cameraZ_+3.0f}; up={0,0,-1};
    }
    const float aspect=static_cast<float>(width_)/height_;
    const float fov=tour_.isFirstPerson()?1.08f:(aspect<1.0f?0.90f:0.78f);
    const Mat4 vp=multiply(perspective(fov,aspect,0.05f,600.0f),lookAt(eye,target,up));
    glUseProgram(program_); glBindVertexArray(vertexArray_);
    glUniformMatrix4fv(mvpLocation_,1,GL_FALSE,vp.v);
    glUniform3f(lightLocation_,-0.45f,1.0f,0.45f);
    auto box=[&](Vec3 center,Vec3 size,RenderColor color,float yaw=0.0f) {
        const Mat4 transform=model(center,size,yaw);
        glUniformMatrix4fv(modelLocation_,1,GL_FALSE,transform.v);
        glUniform3f(colorLocation_,color.r,color.g,color.b);
        glDrawArrays(GL_TRIANGLES,0,36);
    };
    // Each scene gets a solid ground slab; its size includes every wall and actor.
    float minX=game_.playerPosition.x,maxX=minX,minZ=game_.playerPosition.z,maxZ=minZ;
    auto extend=[&](Vec3 p) { minX=std::min(minX,p.x); maxX=std::max(maxX,p.x);
                              minZ=std::min(minZ,p.z); maxZ=std::max(maxZ,p.z); };
    for (const auto& w:game_.walls) {
        extend({w.center.x-std::max(w.size.x,w.size.z),0,w.center.z-std::max(w.size.x,w.size.z)});
        extend({w.center.x+std::max(w.size.x,w.size.z),0,w.center.z+std::max(w.size.x,w.size.z)});
    }
    for (const auto& c:game_.coins) extend(c.position);
    for (const auto& e:game_.enemies) extend(e.position);
    if (game_.hasExit) extend(game_.exitPosition);
    const float floorX=(minX+maxX)*0.5f,floorZ=(minZ+maxZ)*0.5f;
    box({floorX,-0.14f,floorZ},{std::max(12.0f,maxX-minX+5),0.25f,
         std::max(12.0f,maxZ-minZ+5)},{0.17f,0.20f,0.25f});
    for (const auto& w:game_.walls) {
        Vec3 p=w.center; if (p.y<w.size.y*0.25f) p.y=w.size.y*0.5f;
        box(p,w.size,theme.wall,w.yaw);
    }
    for (const auto& d:game_.lockedDoors) if (!d.open)
        box(d.box.center,d.box.size,{0.65f,0.44f,0.20f},d.box.yaw);
    for (const auto& w:game_.toggleWalls) if (w.solid)
        box(w.box.center,w.box.size,{0.58f,0.58f,0.69f},w.box.yaw);
    for (const auto& b:game_.blocks)
        box({b.position.x,0.5f,b.position.z},{0.95f,0.95f,0.95f},{0.52f,0.36f,0.20f});
    for (const auto& h:game_.hazards)
        box({h.position.x,0.11f,h.position.z},{0.80f,0.22f,0.80f},{0.92f,0.20f,0.09f});
    for (const auto& s:game_.switches)
        box({s.position.x,0.12f,s.position.z},{0.65f,0.24f,0.65f},{0.20f,0.86f,0.90f});
    for (const auto& k:game_.keys) if (!k.collected)
        box({k.position.x,0.45f,k.position.z},{0.45f,0.35f,0.20f},{0.98f,0.80f,0.18f});
    if (game_.hasExit) {
        const bool open=game_.coinsRemaining()==0;
        const RenderColor c=open?theme.exit:RenderColor{0.17f,0.27f,0.49f};
        box({game_.exitPosition.x,0.10f,game_.exitPosition.z},{1.0f,0.20f,1.0f},c);
        box({game_.exitPosition.x,0.85f,game_.exitPosition.z},{0.16f,1.50f,0.16f},c);
    }
    for (const auto& c:game_.coins) if (!c.collected) {
        const float bob=reducedMotion_?0.0f:0.08f*std::sin(elapsed_*4.0f+c.position.x);
        box({c.position.x,0.48f+bob,c.position.z},{0.47f,0.47f,0.18f},theme.coin,
            reducedMotion_?0.0f:elapsed_*1.5f);
    }
    for (const auto& e:game_.enemies) {
        box({e.position.x,0.48f,e.position.z},{0.75f,0.95f,0.75f},theme.enemy);
        box({e.position.x,0.98f,e.position.z-0.35f},{0.34f,0.13f,0.08f},{0.98f,0.97f,0.90f});
    }
    for (const auto& p:game_.projectiles)
        box({p.position.x,0.42f,p.position.z},{0.22f,0.22f,0.22f},{1.0f,0.44f,0.13f});
    const float playerScale=game_.won() && effectTime_>0 ? 1.0f+0.3f*effectTime_ : 1.0f;
    box({game_.playerPosition.x,0.47f,game_.playerPosition.z},
        {0.72f*playerScale,0.94f*playerScale,0.72f*playerScale},theme.player);
    box({game_.playerPosition.x,1.0f,game_.playerPosition.z-0.33f},
        {0.32f,0.12f,0.08f},{0.99f,0.99f,0.94f});
    glBindVertexArray(0);
}

} // namespace doodlebound
