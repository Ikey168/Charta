package com.ikore.doodlebound

import android.accessibilityservice.AccessibilityService
import android.os.Build
import android.os.SystemClock
import android.view.KeyEvent
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class SystemBackRegressionTest {
    @Test fun systemBackFromSettingsReturnsToHome() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        instrumentation.targetContext.getSharedPreferences("settings", 0).edit()
            .putBoolean("introSeen", true)
            .commit()

        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val settings = findButton(activity.rootView, "Settings")
                assertNotNull(settings)
                assertTrue(settings!!.performClick())
                assertNotNull(findButtonStartingWith(activity.rootView, "Haptics:"))
            }
            instrumentation.waitForIdleSync()
            val focusDeadline = SystemClock.elapsedRealtime() + 5_000
            var windowFocused = false
            while (!windowFocused && SystemClock.elapsedRealtime() < focusDeadline) {
                scenario.onActivity { activity -> windowFocused = activity.hasWindowFocus() }
                if (!windowFocused) SystemClock.sleep(50)
            }
            assertTrue("MainActivity did not regain window focus before Back", windowFocused)

            if (Build.VERSION.SDK_INT >= 33) {
                assertTrue(instrumentation.uiAutomation.performGlobalAction(
                    AccessibilityService.GLOBAL_ACTION_BACK))
            } else {
                instrumentation.sendKeyDownUpSync(KeyEvent.KEYCODE_BACK)
            }
            instrumentation.waitForIdleSync()

            val homeDeadline = SystemClock.elapsedRealtime() + 5_000
            var homeButtonVisible = false
            var currentButtons = emptyList<String>()
            while (!homeButtonVisible && SystemClock.elapsedRealtime() < homeDeadline) {
                scenario.onActivity { activity ->
                    currentButtons = buttonLabels(activity.rootView)
                    homeButtonVisible = findButton(activity.rootView, "Play levels") != null
                }
                if (!homeButtonVisible) SystemClock.sleep(50)
            }
            assertTrue("Back did not return to Home; visible buttons: $currentButtons", homeButtonVisible)
        }
    }

    private fun findButton(root: View, label: String): Button? {
        if (root is Button && root.text.toString() == label) return root
        if (root is ViewGroup) {
            for (index in 0 until root.childCount) {
                findButton(root.getChildAt(index), label)?.let { return it }
            }
        }
        return null
    }

    private fun findButtonStartingWith(root: View, prefix: String): Button? {
        if (root is Button && root.text.toString().startsWith(prefix)) return root
        if (root is ViewGroup) {
            for (index in 0 until root.childCount) {
                findButtonStartingWith(root.getChildAt(index), prefix)?.let { return it }
            }
        }
        return null
    }

    private fun buttonLabels(root: View): List<String> {
        val labels = mutableListOf<String>()
        fun collect(view: View) {
            if (view is Button) labels += view.text.toString()
            if (view is ViewGroup) for (index in 0 until view.childCount) collect(view.getChildAt(index))
        }
        collect(root)
        return labels
    }
}
