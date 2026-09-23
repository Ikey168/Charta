#include "render/MobileRenderer.h"

#include <game-activity/GameActivity.h>
#include <jni.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// GameActivity still initializes its native lifecycle, while Kotlin owns the
// app's view and game session. Its default callbacks are intentionally unused.
extern "C" void GameActivity_onCreate(GameActivity*, void*, size_t) {}

namespace {

std::atomic<int> activeSessions{0};
std::atomic<jlong> nextHandle{1};
std::mutex sessionsMutex;
std::unordered_map<jlong, std::shared_ptr<doodlebound::MobileRenderer>> sessions;

std::shared_ptr<doodlebound::MobileRenderer> session(jlong handle) {
    std::lock_guard<std::mutex> lock(sessionsMutex);
    const auto found = sessions.find(handle);
    return found == sessions.end() ? nullptr : found->second;
}

} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_ikore_doodlebound_NativeBridge_createSession(JNIEnv*, jobject) {
    auto created = std::shared_ptr<doodlebound::MobileRenderer>(
        new doodlebound::MobileRenderer(), [](doodlebound::MobileRenderer* s) {
            delete s;
            --activeSessions;
        });
    ++activeSessions;
    const jlong handle = nextHandle.fetch_add(1);
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        sessions.emplace(handle, std::move(created));
    }
    return handle;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_destroySession(JNIEnv*, jobject, jlong handle) {
    std::shared_ptr<doodlebound::MobileRenderer> old;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex);
        auto found = sessions.find(handle);
        if (found != sessions.end()) {
            old = std::move(found->second);
            sessions.erase(found);
        }
    }
    old.reset(); // In-flight JNI calls retain their own shared owner until completion.
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ikore_doodlebound_NativeBridge_activeSessions(JNIEnv*, jobject) {
    return activeSessions.load();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_pause(JNIEnv*, jobject, jlong handle) {
    if (auto s = session(handle)) s->pause();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_resume(JNIEnv*, jobject, jlong handle) {
    if (auto s = session(handle)) s->resume();
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ikore_doodlebound_NativeBridge_isPaused(JNIEnv*, jobject, jlong handle) {
    auto s = session(handle);
    return !s || s->isPaused() ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_surfaceCreated(JNIEnv*, jobject, jlong handle) {
    if (auto s = session(handle)) s->surfaceCreated();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_surfaceDestroyed(JNIEnv*, jobject, jlong handle) {
    if (auto s = session(handle)) s->surfaceDestroyed();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_surfaceChanged(JNIEnv*, jobject, jlong handle,
                                                        jint width, jint height) {
    if (auto s = session(handle)) s->resize(width, height);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_drawFrame(JNIEnv*, jobject, jlong handle, jfloat dt) {
    if (auto s = session(handle)) s->drawFrame(dt);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_touch(JNIEnv*, jobject, jlong handle,
                                               jint action, jint pointerId, jfloat x, jfloat y) {
    if (auto s = session(handle)) s->touch(action, pointerId, x, y);
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ikore_doodlebound_NativeBridge_startLevel(JNIEnv*, jobject, jlong handle, jint index) {
    auto s = session(handle);
    return s && s->startLevel(index) ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_restart(JNIEnv*, jobject, jlong handle) {
    if (auto s = session(handle)) s->restart();
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_setTour(JNIEnv*, jobject, jlong handle, jboolean enabled) {
    if (auto s = session(handle)) s->setTour(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_setLeftHanded(JNIEnv*, jobject, jlong handle,
                                                        jboolean enabled) {
    if (auto s = session(handle)) s->setLeftHanded(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT void JNICALL
Java_com_ikore_doodlebound_NativeBridge_setReducedMotion(JNIEnv*, jobject, jlong handle,
                                                          jboolean enabled) {
    if (auto s = session(handle)) s->setReducedMotion(enabled == JNI_TRUE);
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ikore_doodlebound_NativeBridge_status(JNIEnv*, jobject, jlong handle) {
    auto s = session(handle);
    return s ? s->status() : 3;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ikore_doodlebound_NativeBridge_coinsCollected(JNIEnv*, jobject, jlong handle) {
    auto s = session(handle);
    return s ? s->coinsCollected() : 0;
}

extern "C" JNIEXPORT jint JNICALL
Java_com_ikore_doodlebound_NativeBridge_totalCoins(JNIEnv*, jobject, jlong handle) {
    auto s = session(handle);
    return s ? s->totalCoins() : 0;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_ikore_doodlebound_NativeBridge_loadLevelJson(JNIEnv* env, jobject, jlong handle,
                                                        jstring json) {
    auto s = session(handle);
    if (!s || !json || env->GetStringUTFLength(json) > 1024 * 1024) return JNI_FALSE;
    const char* chars = env->GetStringUTFChars(json, nullptr);
    if (!chars) return JNI_FALSE;
    const bool ok = s->loadLevelJson(chars);
    env->ReleaseStringUTFChars(json, chars);
    return ok ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ikore_doodlebound_NativeBridge_convertPhoto(JNIEnv* env, jobject, jlong handle,
                                                       jintArray pixels, jint width, jint height) {
    auto s = session(handle);
    if (!s || !pixels || width < 1 || height < 1 || width > 1024 || height > 1024 ||
        env->GetArrayLength(pixels) != static_cast<jsize>(width * height)) return nullptr;

    std::vector<jint> raw(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    env->GetIntArrayRegion(pixels, 0, static_cast<jsize>(raw.size()), raw.data());
    if (env->ExceptionCheck()) return nullptr;
    std::vector<std::uint32_t> argb(raw.begin(), raw.end());
    const std::string result = s->convertPhoto(argb, width, height);
    return result.empty() ? nullptr : env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_ikore_doodlebound_NativeBridge_captureFrame(JNIEnv* env, jobject, jlong handle) {
    auto s = session(handle);
    if (!s) return nullptr;
    const std::vector<std::uint32_t> argb = s->captureFrameArgb();
    if (argb.size() < 3 || argb.size() > 4'000'002 ||
        argb[0] == 0 || argb[1] == 0 ||
        static_cast<std::uint64_t>(argb[0]) * argb[1] != argb.size() - 2) return nullptr;
    jintArray result = env->NewIntArray(static_cast<jsize>(argb.size()));
    if (result) env->SetIntArrayRegion(result, 0, static_cast<jsize>(argb.size()),
                                       reinterpret_cast<const jint*>(argb.data()));
    return result;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ikore_doodlebound_NativeBridge_reviewLevelJson(JNIEnv* env, jobject, jlong handle,
                                                          jstring json) {
    auto s = session(handle);
    if (!s || !json || env->GetStringUTFLength(json) > 1024 * 1024) return nullptr;
    const char* chars = env->GetStringUTFChars(json, nullptr);
    if (!chars) return nullptr;
    const std::string result = s->reviewLevelJson(chars);
    env->ReleaseStringUTFChars(json, chars);
    return env->NewStringUTF(result.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_ikore_doodlebound_NativeBridge_suggestRepair(JNIEnv* env, jobject, jlong handle,
                                                        jstring json) {
    auto s = session(handle);
    if (!s || !json || env->GetStringUTFLength(json) > 1024 * 1024) return nullptr;
    const char* chars = env->GetStringUTFChars(json, nullptr);
    if (!chars) return nullptr;
    const std::string result = s->suggestRepair(chars);
    env->ReleaseStringUTFChars(json, chars);
    return env->NewStringUTF(result.c_str());
}
