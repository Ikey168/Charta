package com.ikore.doodlebound

import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class ProductFlowTest {
    @Test fun levelFormatAndShareRejectTampering() {
        assertEquals("DDL1:DD-43ZN6STG8X615:eyJmb3JtYXQiOiJkb29kbGUtbGV2ZWwiLCJ2ZXJzaW9uIjoxfQ",
            LevelShareCodec.encode("{\"format\":\"doodle-level\",\"version\":1}"))
        val draft = LevelDraft()
        draft.walls.add(mutableListOf(Point2(0f, 0f), Point2(8f, 0f)))
        draft.marks.add(Mark("player", 1f, 1f))
        draft.marks.add(Mark("exit", 7f, 1f))
        assertTrue(draft.issues().isEmpty())
        val json = draft.toJson()
        assertEquals(2, LevelDraft.fromJson(json).marks.size)
        val share = LevelShareCodec.encode(json)
        assertTrue(share.startsWith("DDL1:DD-"))
        assertEquals(json, LevelShareCodec.decode(share))
        val damaged = share.replaceRange(5, 6, if (share[5] == 'A') "B" else "A")
        assertThrows(IllegalArgumentException::class.java) { LevelShareCodec.decode(damaged) }
        assertThrows(IllegalArgumentException::class.java) { LevelDraft.fromJson(json.replace("\"version\":1", "\"version\":99")) }
    }

    @Test fun libraryPreservesCommittedLevelWhenDraftIsCorrupt() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val library = LevelLibrary(context)
        val level = LevelDraft(name = "Instrumentation ${System.nanoTime()}")
        level.walls.add(mutableListOf(Point2(0f, 0f), Point2(3f, 0f)))
        level.marks.add(Mark("player", 1f, 1f))
        level.marks.add(Mark("exit", 2f, 1f))
        try {
            library.save(level)
            library.saveDraft(level)
            val previousName = level.name
            level.name = "New draft name"
            library.saveDraft(level)
            File(context.filesDir, "draft.json").writeText("{truncated")
            assertEquals(previousName, library.loadDraft()?.name)
            assertEquals(level.toJson(), library.list().first { it.id == level.id }.toJson())
        } finally {
            library.delete(level)
            library.clearDraft()
        }
    }

    @Test fun homeDrawReviewAndBackAreReachableWithoutCamera() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val draw = findButton(activity.rootView, "Draw a dungeon")
                assertNotNull(draw)
                draw!!.performClick()
                assertNotNull(findText(activity.rootView, "Draw your dungeon"))
                findButton(activity.rootView, "Review")!!.performClick()
                assertNotNull(findText(activity.rootView, "Review your dungeon"))
                findButton(activity.rootView, "Back")!!.performClick()
                assertNotNull(findText(activity.rootView, "Draw your dungeon"))
            }
        }
    }

    @Test fun interruptedSampleCanRestartAfterActivityRecreation() {
        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                activity.getSharedPreferences("settings", 0).edit().remove("cleared_0").commit()
                findButton(activity.rootView, "Play levels")!!.performClick()
                findButton(activity.rootView, "1. First steps")!!.performClick()
                assertEquals("sample:0", activity.getSharedPreferences("settings", 0).getString("runCheckpoint", null))
            }
            scenario.recreate()
            scenario.onActivity { activity ->
                assertNotNull(findButton(activity.rootView, "Resume interrupted level (restart)"))
                findButton(activity.rootView, "Resume interrupted level (restart)")!!.performClick()
                assertNotNull(findButton(activity.rootView, "Pause"))
                activity.getSharedPreferences("settings", 0).edit().remove("runCheckpoint").commit()
            }
        }
    }

    private fun findButton(root: android.view.View, label: String): Button? {
        if (root is Button && root.text.toString() == label) return root
        if (root is android.view.ViewGroup) for (i in 0 until root.childCount) findButton(root.getChildAt(i), label)?.let { return it }
        return null
    }
    private fun findText(root: android.view.View, label: String): TextView? {
        if (root is TextView && root.text.toString() == label) return root
        if (root is android.view.ViewGroup) for (i in 0 until root.childCount) findText(root.getChildAt(i), label)?.let { return it }
        return null
    }
}
