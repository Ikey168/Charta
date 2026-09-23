package com.ikore.doodlebound

import androidx.lifecycle.Lifecycle
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class MainActivityLifecycleTest {
    @Test fun tenPauseAndRecreateCyclesKeepNativeSessionUsable() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            repeat(10) {
                scenario.moveToState(Lifecycle.State.STARTED)
                scenario.moveToState(Lifecycle.State.RESUMED)
                scenario.recreate()
                scenario.onActivity { activity ->
                    assertNotEquals(0L, activity.nativeHandle)
                    assertEquals(1, NativeBridge.activeSessions())
                    assertTrue(activity.gameView.isAttachedToWindow)
                    assertTrue(NativeBridge.status(activity.nativeHandle) in 0..3)
                }
            }
        }
        assertEquals(0, NativeBridge.activeSessions())
    }
}
