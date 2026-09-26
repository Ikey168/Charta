package com.ikore.doodlebound

import android.Manifest
import android.app.Activity
import android.content.Context
import android.content.pm.PackageManager
import android.os.ParcelFileDescriptor
import android.os.Process
import android.os.SystemClock
import android.view.View
import android.view.ViewGroup
import android.view.accessibility.AccessibilityNodeInfo
import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Assume.assumeTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class AcceptanceRecoveryTest {
    @Test fun deniedCameraPermissionOffersNonCameraFallbacks() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            .edit().putBoolean("introSeen", true).commit()
        revokeCameraPermission(instrumentation, context)

        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                // Matches DemoUi's private CAMERA_PERMISSION request code.
                assertTrue(DemoUi.onRequestPermissionsResult(
                    activity,
                    804,
                    arrayOf(Manifest.permission.CAMERA),
                    intArrayOf(PackageManager.PERMISSION_DENIED)
                ))
            }

            assertTrue("Camera denial dialog was not shown", waitForActiveWindowText(instrumentation, "Camera access is off"))
            assertTrue("Camera denial did not offer drawing", activeWindowHasText(instrumentation, "Draw"))
            assertTrue("Camera denial did not offer photo selection", activeWindowHasText(instrumentation, "Choose photo"))
            assertTrue("Drawing fallback button could not be activated", clickActiveWindowText(instrumentation, "Draw"))

            scenario.onActivity { activity ->
                assertNotNull("Drawing fallback did not open the editor", findText(activity.rootView, "Draw your dungeon"))
                assertNotNull("Drawing fallback did not remain interactive", findButton(activity.rootView, "Review"))
            }
        }
    }

    @Test fun cancelledPhotoPickerReturnsToHome() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            .edit().putBoolean("introSeen", true).commit()

        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                assertNotNull(findButton(activity.rootView, "Choose a photo"))
                // Deliver RESULT_CANCELED at the picker-result boundary without depending on
                // the installed DocumentsUI app or its version-specific screen layout.
                assertTrue(DemoUi.onActivityResult(activity, 801, Activity.RESULT_CANCELED, null))
                assertNotNull("Picker cancel did not return to Home", findText(activity.rootView, "Doodlebound"))
                assertEquals(null, findText(activity.rootView, "Reading your drawing"))
            }
        }
    }

    @Test fun seedRunCheckpointForHostProcessDeath() {
        assumeTrue("Run this stage from the process-death CI harness", recoveryStage() == "seed")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
        prefs.edit().putBoolean("introSeen", true).remove("runCheckpoint").commit()

        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                findButton(activity.rootView, "Play levels")!!.performClick()
                findButton(activity.rootView, "1. First steps")!!.performClick()
                assertEquals("sample:0", prefs.getString("runCheckpoint", null))
                prefs.edit().putInt("acceptanceRecoverySeedPid", Process.myPid()).commit()
            }
        }
    }

    @Test fun restoreRunCheckpointAfterHostProcessDeath() {
        assumeTrue("Run this stage from the process-death CI harness", recoveryStage() == "restore")
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val prefs = context.getSharedPreferences("settings", Context.MODE_PRIVATE)
        val seedPid = prefs.getInt("acceptanceRecoverySeedPid", -1)
        assertTrue("Checkpoint seed stage did not run", seedPid > 0)
        assertNotEquals("Target process was not restarted by the host harness", seedPid, Process.myPid())
        assertEquals("sample:0", prefs.getString("runCheckpoint", null))

        try {
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    val resume = findButton(activity.rootView, "Resume interrupted level (restart)")
                    assertNotNull("Saved checkpoint was not offered after process death", resume)
                    resume!!.performClick()
                    assertNotNull("Saved checkpoint did not restart the level", findButton(activity.rootView, "Pause"))
                    assertEquals(View.VISIBLE, activity.gameView.visibility)
                    assertTrue("Restarted level has no usable native session", NativeBridge.status(activity.nativeHandle) in 0..2)
                }
            }
        } finally {
            prefs.edit().remove("runCheckpoint").remove("acceptanceRecoverySeedPid").commit()
        }
    }

    private fun revokeCameraPermission(instrumentation: android.app.Instrumentation, context: Context) {
        val command = instrumentation.uiAutomation.executeShellCommand(
            "pm revoke ${context.packageName} ${Manifest.permission.CAMERA}"
        )
        ParcelFileDescriptor.AutoCloseInputStream(command).bufferedReader().use { it.readText() }
        assertEquals(
            "Camera permission must be denied for this test",
            PackageManager.PERMISSION_DENIED,
            context.checkSelfPermission(Manifest.permission.CAMERA)
        )
    }

    private fun recoveryStage(): String? =
        InstrumentationRegistry.getArguments().getString("acceptanceRecoveryStage")

    private fun waitForActiveWindowText(instrumentation: android.app.Instrumentation, text: String): Boolean {
        val deadline = SystemClock.elapsedRealtime() + 5_000
        while (SystemClock.elapsedRealtime() < deadline) {
            if (activeWindowHasText(instrumentation, text)) return true
            SystemClock.sleep(100)
        }
        return activeWindowHasText(instrumentation, text)
    }

    private fun activeWindowHasText(instrumentation: android.app.Instrumentation, text: String): Boolean {
        val root = instrumentation.uiAutomation.rootInActiveWindow ?: return false
        return try {
            val matches = root.findAccessibilityNodeInfosByText(text)
            try { matches.isNotEmpty() } finally { matches.forEach(AccessibilityNodeInfo::recycle) }
        } finally {
            root.recycle()
        }
    }

    private fun clickActiveWindowText(instrumentation: android.app.Instrumentation, text: String): Boolean {
        val root = instrumentation.uiAutomation.rootInActiveWindow ?: return false
        return try {
            val matches = root.findAccessibilityNodeInfosByText(text)
            try {
                val target = matches.firstOrNull { it.isClickable } ?: matches.firstOrNull()
                target?.performAction(AccessibilityNodeInfo.ACTION_CLICK) == true
            } finally {
                matches.forEach(AccessibilityNodeInfo::recycle)
            }
        } finally {
            root.recycle()
        }
    }

    private fun findButton(view: View, label: String): Button? {
        if (view is Button && view.text.toString() == label) return view
        if (view is ViewGroup) for (i in 0 until view.childCount) {
            findButton(view.getChildAt(i), label)?.let { return it }
        }
        return null
    }

    private fun findText(view: View, label: String): TextView? {
        if (view is TextView && view.text.toString() == label) return view
        if (view is ViewGroup) for (i in 0 until view.childCount) {
            findText(view.getChildAt(i), label)?.let { return it }
        }
        return null
    }
}
