package com.ikore.doodlebound

import android.accessibilityservice.AccessibilityService
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
                assertNotNull(findButton(activity.rootView, "Settings"))
                findButton(activity.rootView, "Settings")!!.performClick()
                assertNotNull(findViewWithText(activity.rootView, "Settings"))
            }

            assertTrue(instrumentation.uiAutomation.performGlobalAction(AccessibilityService.GLOBAL_ACTION_BACK))
            instrumentation.waitForIdleSync()

            scenario.onActivity { activity ->
                // If Back fell through to Activity.finish(), ActivityScenario cannot
                // reach this callback. "Play levels" is unique to the Home screen.
                assertNotNull(findButton(activity.rootView, "Play levels"))
            }
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

    private fun findViewWithText(root: View, label: String): View? {
        if (root is android.widget.TextView && root.text.toString() == label) return root
        if (root is ViewGroup) {
            for (index in 0 until root.childCount) {
                findViewWithText(root.getChildAt(index), label)?.let { return it }
            }
        }
        return null
    }
}
