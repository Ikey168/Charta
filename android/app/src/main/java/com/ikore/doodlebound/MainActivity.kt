package com.ikore.doodlebound

import android.content.Intent
import android.app.ActivityManager
import android.content.Context
import android.graphics.Color
import android.opengl.GLSurfaceView
import android.os.Bundle
import android.view.MotionEvent
import android.view.View
import android.widget.FrameLayout
import android.widget.TextView
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import com.google.androidgamesdk.GameActivity
import androidx.activity.OnBackPressedCallback
import androidx.core.view.ViewCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import javax.microedition.khronos.egl.EGLConfig
import javax.microedition.khronos.opengles.GL10

internal class NativeSessionViewModel : ViewModel() {
    val handle: Long = NativeBridge.createSession()

    override fun onCleared() {
        NativeBridge.destroySession(handle)
    }
}

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
    private var gameplayVisible = false
    private var hostResumed = false
    private var windowFocused = false

    // GameActivity normally supplies its own SurfaceView. Our GLSurfaceView is the one
    // graphics owner, so the GameActivity view is intentionally suppressed.
    override fun onCreateSurfaceView() = Unit

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val systemBarBackground = Color.rgb(18, 24, 32)
        window.decorView.setBackgroundColor(systemBarBackground)
        window.statusBarColor = systemBarBackground
        window.navigationBarColor = systemBarBackground
        WindowInsetsControllerCompat(window, window.decorView).apply {
            isAppearanceLightStatusBars = false
            isAppearanceLightNavigationBars = false
        }
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
        nativeHandle = ViewModelProvider(this).get(NativeSessionViewModel::class.java).handle
        rootView = FrameLayout(this)
        rootView.setBackgroundColor(systemBarBackground)
        ViewCompat.setOnApplyWindowInsetsListener(rootView) { view, insets ->
            val safe = insets.getInsets(
                WindowInsetsCompat.Type.systemBars() or WindowInsetsCompat.Type.displayCutout())
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom)
            insets
        }
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
        ViewCompat.requestApplyInsets(rootView)
        DemoUi.install(this, rootView)
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() = handleBack()
        })
    }

    private fun handleBack() {
        if (!::rootView.isInitialized || !DemoUi.onBackPressed(this)) finish()
    }

    fun showGame() {
        gameplayVisible = true
        gameView.renderMode = GLSurfaceView.RENDERMODE_CONTINUOUSLY
        gameView.visibility = View.VISIBLE
        DemoUi.showGame()
        if (hostResumed && windowFocused) {
            if (nativeHandle != 0L) NativeBridge.resume(nativeHandle)
        } else {
            if (nativeHandle != 0L) NativeBridge.pause(nativeHandle)
            DemoUi.onHostPause()
        }
    }

    fun showMenu() {
        gameplayVisible = false
        gameView.renderMode = GLSurfaceView.RENDERMODE_WHEN_DIRTY
        if (nativeHandle != 0L) NativeBridge.pause(nativeHandle)
        DemoUi.showMenu()
    }

    override fun onResume() {
        super.onResume()
        hostResumed = true
        if (::gameView.isInitialized) {
            gameView.onResume()
            if (nativeHandle != 0L && gameplayVisible && windowFocused) NativeBridge.resume(nativeHandle)
            if (windowFocused) DemoUi.onHostResume() else DemoUi.onHostPause()
        }
    }

    override fun onPause() {
        hostResumed = false
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
        windowFocused = hasFocus
        if (nativeHandle == 0L) return
        if (hasFocus && hostResumed && gameplayVisible) NativeBridge.resume(nativeHandle)
        else NativeBridge.pause(nativeHandle)
        if (::rootView.isInitialized) {
            if (hasFocus && hostResumed) DemoUi.onHostResume() else DemoUi.onHostPause()
        }
    }

    override fun onDestroy() {
        if (::rootView.isInitialized) DemoUi.dispose()
        if (nativeHandle != 0L) {
            NativeBridge.pause(nativeHandle)
            if (::gameView.isInitialized) gameView.onPause()
        }
        super.onDestroy()
        nativeHandle = 0L
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
        handleBack()
    }
}
