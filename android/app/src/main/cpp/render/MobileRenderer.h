#pragma once

#include "game/DungeonGame.h"
#include "game/TourCamera.h"

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

namespace doodlebound {

// All public methods are safe to call from the Android UI or GL thread. GLES calls
// are confined to surfaceCreated, surfaceDestroyed, resize and drawFrame, which
// the host must call on the GL thread. Gameplay and input share one mutex.
class MobileRenderer {
public:
    MobileRenderer();
    ~MobileRenderer();

    void surfaceCreated();
    void surfaceDestroyed();
    void resize(int width, int height);
    void drawFrame(float deltaSeconds);

    // Android MotionEvent action values: DOWN=0, UP=1, MOVE=2, CANCEL=3,
    // POINTER_DOWN=5, POINTER_UP=6. MOVE is delivered for each active pointer.
    void touch(int action, int pointerId, float x, float y);
    void pause();
    void resume();
    bool startLevel(int index);
    void restart();
    void setTour(bool enabled);
    void setLeftHanded(bool enabled);
    void setReducedMotion(bool enabled);
    int status() const; // 0 playing, 1 won, 2 lost, 3 invalid/no level
    int coinsCollected() const;
    int totalCoins() const;
    bool loadLevelJson(const std::string& json);
    // Pure, bounded review operations. JSON response schemas are documented in
    // the Android bridge; neither call changes the active game.
    std::string reviewLevelJson(const std::string& json) const;
    std::string suggestRepair(const std::string& json) const;
    // ARGB packed pixels, as returned by Android Bitmap.getPixels(). Empty on
    // invalid input or an image that cannot form a playable level.
    std::string convertPhoto(const std::vector<std::uint32_t>& argb, int width, int height) const;
    // Call on the GL thread after drawFrame. [width, height, top-left ARGB...].
    // Returns empty if the surface is unavailable or exceeds four million pixels.
    std::vector<std::uint32_t> captureFrameArgb();

private:
    struct Pointer { float originX{}, originY{}, x{}, y{}; bool movement{}; };
    void loadSceneLocked(const IKore::game::SceneDescription& scene);
    void tickLocked(float dt);
    void releaseGlLocked();
    bool initGlLocked();
    void renderLocked();

    mutable std::mutex mutex_;
    IKore::game::SceneDescription scene_;
    IKore::game::DungeonGame game_;
    IKore::game::TourController tour_;
    std::map<int, Pointer> pointers_;
    int movementPointer_{-1};
    int lookPointer_{-1};
    int width_{1}, height_{1};
    int levelIndex_{0};
    bool hasLevel_{false};
    bool paused_{false};
    bool leftHanded_{false};
    bool reducedMotion_{false};
    float elapsed_{0.0f};
    float accumulator_{0.0f};
    float cameraX_{0.0f}, cameraZ_{0.0f};
    float effectTime_{0.0f};
    int lastCoins_{0};
    IKore::game::GameStatus lastStatus_{IKore::game::GameStatus::Playing};

    // GLuint/GLint values are kept as primitive types so this header can be
    // included by JNI and host-side gameplay tests without GLES headers.
    unsigned int program_{0}, vertexArray_{0}, vertexBuffer_{0};
    int mvpLocation_{-1}, modelLocation_{-1}, colorLocation_{-1}, lightLocation_{-1};
    bool glReady_{false};
};

} // namespace doodlebound
