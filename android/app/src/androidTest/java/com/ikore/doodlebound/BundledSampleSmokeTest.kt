package com.ikore.doodlebound

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.TextView
import androidx.test.core.app.ActivityScenario
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import java.io.File

@RunWith(AndroidJUnit4::class)
class BundledSampleSmokeTest {
    @Test fun bundledDrawingConvertsAndStartsPlaying() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        instrumentation.targetContext.getSharedPreferences("settings", Context.MODE_PRIVATE)
            .edit().putBoolean("introSeen", true).commit()

        ActivityScenario.launch(MainActivity::class.java).use { scenario ->
            scenario.onActivity { activity ->
                val sample = findButton(activity.rootView, "Try sample drawing")
                requireNotNull(sample) { "Sample entry point is missing" }
                sample.performClick()
            }

            var reviewVisible = false
            val deadline = System.currentTimeMillis() + 20_000
            while (System.currentTimeMillis() < deadline && !reviewVisible) {
                instrumentation.waitForIdleSync()
                scenario.onActivity { activity ->
                    reviewVisible = findText(activity.rootView, "Review your dungeon") != null
                }
                if (!reviewVisible) Thread.sleep(200)
            }
            assertTrue("Bundled sample did not reach review", reviewVisible)

            scenario.onActivity { activity ->
                val play = findButton(activity.rootView, "Play")
                requireNotNull(play) { "Review has no Play action" }
                play.performClick()
                assertEquals(View.VISIBLE, activity.gameView.visibility)
                assertTrue("Native session did not start", NativeBridge.status(activity.nativeHandle) in 0..2)
                assertTrue("Game HUD is missing", findButton(activity.rootView, "Pause") != null)
            }
        }
    }

    @Test fun importedFileCanBeSavedAndBadImportKeepsIt() {
        val instrumentation = InstrumentationRegistry.getInstrumentation()
        val context = instrumentation.targetContext
        context.getSharedPreferences("settings", Context.MODE_PRIVATE)
            .edit().putBoolean("introSeen", true).commit()
        val library = LevelLibrary(context)
        val beforeIds = library.list().map { it.id }.toSet()
        val level = LevelDraft(name = "Imported QA dungeon")
        level.walls.add(mutableListOf(Point2(0f, 0f), Point2(8f, 0f)))
        level.walls.add(mutableListOf(Point2(8f, 0f), Point2(8f, 8f)))
        level.walls.add(mutableListOf(Point2(8f, 8f), Point2(0f, 8f)))
        level.walls.add(mutableListOf(Point2(0f, 8f), Point2(0f, 0f)))
        level.marks.add(Mark("player", 1f, 1f))
        level.marks.add(Mark("exit", 7f, 7f))
        val exportDir = File(context.cacheDir, "exports").apply { mkdirs() }
        val valid = File(exportDir, "qa-valid.ddl").apply { writeText(level.toJson()) }
        val invalid = File(exportDir, "qa-invalid.ddl").apply { writeText("{truncated") }
        fun fileUri(file: File) = Uri.Builder().scheme("content")
            .authority("${context.packageName}.exports").appendPath(file.name).build()
        var importedId: String? = null
        try {
            ActivityScenario.launch(MainActivity::class.java).use { scenario ->
                scenario.onActivity { activity ->
                    deliverImportResult(activity, fileUri(valid))
                    assertTrue("Valid import did not reach review", findText(activity.rootView, "Review your dungeon") != null)
                    findButton(activity.rootView, "Play")!!.performClick()
                    assertTrue("Imported level did not start", findButton(activity.rootView, "Pause") != null)
                    findButton(activity.rootView, "Pause")!!.performClick()
                    findButton(activity.rootView, "Home")!!.performClick()
                }
                instrumentation.waitForIdleSync()
                val imported = library.list().singleOrNull { it.id !in beforeIds && it.toJson() == level.toJson() }
                assertTrue("Imported level was not saved", imported != null)
                importedId = imported?.id

                scenario.onActivity { activity ->
                    deliverImportResult(activity, fileUri(invalid))
                    assertTrue("Bad import should return home", findText(activity.rootView, "Doodlebound") != null)
                }
                assertTrue("Bad import replaced a committed level", library.list().any { it.id == importedId })
            }
        } finally {
            library.list().filter { it.id == importedId }.forEach(library::delete)
            valid.delete(); invalid.delete()
        }
    }

    private fun deliverImportResult(activity: MainActivity, uri: Uri) {
        DemoUi.onActivityResult(activity, 803, Activity.RESULT_OK, Intent().setData(uri))
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
