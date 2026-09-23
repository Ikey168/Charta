package com.ikore.doodlebound

import android.content.Intent
import android.app.ActivityManager
import android.content.Context
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
import com.google.androidgamesdk.GameActivity
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

/**
 * Android owns windows, permissions and navigation. The native session owns game state.
 * GLSurfaceView alone owns the EGL context and serializes all GL calls on its render thread.
 */
class MainActivity : GameActivity() {
    lateinit var rootView: FrameLayout
        private set
    lateinit var gameView: GLSurfaceView
        private set
    var nativeHandle: Long = 0L
        private set

    // GameActivity normally supplies its own SurfaceView. Our GLSurfaceView is the one
    // graphics owner, so the GameActivity view is intentionally suppressed.
    override fun onCreateSurfaceView() = Unit

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val graphics = (getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager)
            .deviceConfigurationInfo
        if (graphics.reqGlEsVersion < 0x30000) {
            setContentView(TextView(this).apply {
                text = "Doodlebound needs an Android phone with OpenGL ES 3.0."
                textSize = 22f
                setPadding(32, 32, 32, 32)
            })
            return
        }
        nativeHandle = NativeBridge.createSession()
        rootView = FrameLayout(this)
        gameView = object : GLSurfaceView(this) {
            override fun onTouchEvent(event: MotionEvent): Boolean {
                val handle = nativeHandle
                if (handle == 0L) return false
                val masked = event.actionMasked
                when (masked) {
                    MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN,
                    MotionEvent.ACTION_UP, MotionEvent.ACTION_POINTER_UP -> {
                        val i = event.actionIndex
                        NativeBridge.touch(handle, masked, event.getPointerId(i), event.getX(i), event.getY(i))
                    }
                    MotionEvent.ACTION_MOVE -> {
                        for (i in 0 until event.pointerCount) {
                            NativeBridge.touch(handle, masked, event.getPointerId(i), event.getX(i), event.getY(i))
                        }
                    }
                    MotionEvent.ACTION_CANCEL -> NativeBridge.touch(handle, masked, -1, 0f, 0f)
                }
                return true
            }
        }.apply {
            setEGLContextClientVersion(3)
            preserveEGLContextOnPause = false
            setRenderer(object : GLSurfaceView.Renderer {
                private var lastFrame = 0L
                override fun onSurfaceCreated(gl: GL10?, config: EGLConfig?) {
                    NativeBridge.surfaceCreated(nativeHandle)
                    lastFrame = System.nanoTime()
                }
                override fun onSurfaceChanged(gl: GL10?, width: Int, height: Int) {
                    NativeBridge.surfaceChanged(nativeHandle, width, height)
                }
                override fun onDrawFrame(gl: GL10?) {
                    val now = System.nanoTime()
                    val dt = ((now - lastFrame) * 1e-9).toFloat().coerceIn(0f, 0.1f)
                    lastFrame = now
                    NativeBridge.drawFrame(nativeHandle, dt)
                }
            })
            renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        }
        rootView.addView(gameView, FrameLayout.LayoutParams(-1, -1))
        setContentView(rootView)
        DemoUi.install(this, rootView)
    }

    fun showGame() {
        gameView.visibility = View.VISIBLE
        DemoUi.showGame()
    }

    fun showMenu() {
        DemoUi.showMenu()
    }

    override fun onResume() {
        super.onResume()
        if (::gameView.isInitialized) {
            gameView.onResume()
            if (nativeHandle != 0L) NativeBridge.resume(nativeHandle)
            DemoUi.onHostResume()
        }
    }

    override fun onPause() {
        if (::rootView.isInitialized) DemoUi.onHostPause()
        if (nativeHandle != 0L) NativeBridge.pause(nativeHandle)
        if (::gameView.isInitialized) {
            val handle = nativeHandle
            if (handle != 0L) gameView.queueEvent { NativeBridge.surfaceDestroyed(handle) }
            gameView.onPause()
        }
        super.onPause()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (nativeHandle == 0L) return
        if (hasFocus) NativeBridge.resume(nativeHandle) else NativeBridge.pause(nativeHandle)
    }

    override fun onDestroy() {
        if (::rootView.isInitialized) DemoUi.dispose()
        if (nativeHandle != 0L) {
            NativeBridge.pause(nativeHandle)
            if (::gameView.isInitialized) gameView.onPause()
            NativeBridge.destroySession(nativeHandle)
            nativeHandle = 0L
        }
        super.onDestroy()
    }

    @Deprecated("Legacy result bridge for camera and picker flows")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (::rootView.isInitialized) DemoUi.onActivityResult(this, requestCode, resultCode, data)
    }

    @Deprecated("Legacy permission bridge for the Camera2 preview")
    override fun onRequestPermissionsResult(requestCode: Int, permissions: Array<out String>,
                                            grantResults: IntArray) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (::rootView.isInitialized) DemoUi.onRequestPermissionsResult(
            this, requestCode, permissions, grantResults)
    }

    @Deprecated("Legacy back bridge for the demo UI")
    override fun onBackPressed() {
        if (!::rootView.isInitialized || !DemoUi.onBackPressed(this)) super.onBackPressed()
    }
}
