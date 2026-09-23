package com.ikore.doodlebound

/** The only JNI surface. The native handle is owned by MainActivity. */
object NativeBridge {
    init { System.loadLibrary("doodlebound") }

    external fun createSession(): Long
    external fun destroySession(handle: Long)
    external fun activeSessions(): Int
    external fun pause(handle: Long)
    external fun resume(handle: Long)
    external fun isPaused(handle: Long): Boolean
    external fun surfaceCreated(handle: Long)
    external fun surfaceDestroyed(handle: Long)
    external fun surfaceChanged(handle: Long, width: Int, height: Int)
    external fun drawFrame(handle: Long, deltaSeconds: Float)
    external fun touch(handle: Long, action: Int, pointerId: Int, x: Float, y: Float)

    external fun startLevel(handle: Long, index: Int): Boolean
    external fun restart(handle: Long)
    external fun setTour(handle: Long, enabled: Boolean)
    external fun setLeftHanded(handle: Long, enabled: Boolean)
    external fun setReducedMotion(handle: Long, enabled: Boolean)
    external fun status(handle: Long): Int
    external fun coinsCollected(handle: Long): Int
    external fun totalCoins(handle: Long): Int
    external fun loadLevelJson(handle: Long, json: String): Boolean
    external fun convertPhoto(handle: Long, argb: IntArray, width: Int, height: Int): String?
    /** Call only on the GLSurfaceView render thread. [width, height, ARGB pixels...]. */
    external fun captureFrame(handle: Long): IntArray?
    /** Pure validation result JSON; call on a worker thread. */
    external fun reviewLevelJson(handle: Long, json: String): String?
    /** Pure proposed repair result JSON; call on a worker thread. */
    external fun suggestRepair(handle: Long, json: String): String?
}
