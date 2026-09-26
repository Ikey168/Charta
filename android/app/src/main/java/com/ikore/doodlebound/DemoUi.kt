package com.ikore.doodlebound

import android.app.Activity
import android.app.AlertDialog
import android.Manifest
import android.content.pm.PackageManager
import android.content.Intent
import android.graphics.BitmapFactory
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Color
import android.net.Uri
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.provider.MediaStore
import android.provider.Settings
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.view.HapticFeedbackConstants
import android.widget.Button
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.ImageView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.SeekBar
import android.widget.TextView
import android.view.TextureView
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import android.widget.Toast
import android.util.Log
import java.io.File
import java.io.ByteArrayOutputStream
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.ExecutorService
import java.util.concurrent.atomic.AtomicInteger
import org.json.JSONObject
import kotlin.math.hypot

/** Native game HUD and Android product navigation. The Activity owns only lifecycle and the GL surface. */
object DemoUi {
    private const val TAG = "DemoUi"
    private const val PICK_PHOTO = 801
    private const val TAKE_PHOTO = 802
    private const val IMPORT_LEVEL = 803
    private const val CAMERA_PERMISSION = 804
    private lateinit var activity: MainActivity
    private lateinit var root: FrameLayout
    private lateinit var panel: FrameLayout
    private lateinit var hud: LinearLayout
    private lateinit var controlCue: TextView
    private lateinit var library: LevelLibrary
    private lateinit var photoInput: PhotoInput
    private lateinit var audio: AudioFeedback
    private val handler = Handler(Looper.getMainLooper())
    private lateinit var reviewWorker: ExecutorService
    private val reviewGeneration = AtomicInteger(0)
    private var currentDraft: LevelDraft? = null
    private var photoUri: Uri? = null
    private var cameraPreview: CameraPreview? = null
    private var pendingCameraFile: File? = null
    private var screen = "home"
    private var playingLevel = -1
    private var lastStatus = 0
    private var lastCoins = 0
    private var fullscreenGame = false
    private var tutorialStep = 0
    private var hudPoll: Runnable? = null
    private val prefs get() = activity.getSharedPreferences("settings", Activity.MODE_PRIVATE)

    @JvmStatic fun install(host: MainActivity, container: FrameLayout) {
        activity = host; root = container
        library = LevelLibrary(host)
        photoInput = PhotoInput(host) { host.nativeHandle }
        reviewWorker = Executors.newSingleThreadExecutor()
        audio = AudioFeedback(host)
        configureAudio()
        panel = FrameLayout(host).apply { setBackgroundColor(Color.rgb(18, 24, 32)) }
        hud = LinearLayout(host).apply {
            orientation = LinearLayout.HORIZONTAL
            gravity = Gravity.TOP or Gravity.CENTER_VERTICAL
            setBackgroundColor(0xAA101820.toInt())
            visibility = View.GONE
        }
        root.addView(panel, FrameLayout.LayoutParams(-1, -1))
        root.addView(hud, FrameLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.TOP))
        controlCue = TextView(host).apply {
            text = "Drag to move"
            setTextColor(Color.WHITE)
            setBackgroundColor(0x880D1820.toInt())
            textSize = 18f
            setPadding(dp(12), dp(8), dp(12), dp(8))
            isClickable = false
            visibility = View.GONE
        }
        val cueSide = if (prefs.getBoolean("leftHanded", false)) Gravity.RIGHT else Gravity.LEFT
        root.addView(controlCue, FrameLayout.LayoutParams(ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM or cueSide).apply { setMargins(dp(28), 0, dp(28), dp(24)) })
        File(host.cacheDir, "exports").listFiles()?.filter { System.currentTimeMillis() - it.lastModified() > 3600_000 }?.forEach { it.delete() }
        host.cacheDir.listFiles()?.filter { it.name.startsWith("camera-") && it.name.endsWith(".jpg") }?.forEach { it.delete() }
        NativeBridge.setLeftHanded(host.nativeHandle, prefs.getBoolean("leftHanded", false))
        NativeBridge.setReducedMotion(host.nativeHandle, prefs.getBoolean("reducedMotion", false))
        showHome()
        hudPoll = object : Runnable {
            override fun run() {
                if (fullscreenGame && host.nativeHandle != 0L) updateHud()
                handler.postDelayed(this, 250)
            }
        }
        handler.post(hudPoll!!)
    }

    @JvmStatic fun showGame() { panel.visibility = View.GONE; hud.visibility = View.VISIBLE; controlCue.visibility = View.VISIBLE; fullscreenGame = true; audio.resume() }
    @JvmStatic fun showMenu() { panel.visibility = View.VISIBLE; hud.visibility = View.GONE; controlCue.visibility = View.GONE; fullscreenGame = false; audio.pause() }
    @JvmStatic fun onHostPause() { if (::audio.isInitialized) audio.pause(); cameraPreview?.close(); cameraPreview = null }
    @JvmStatic fun onHostResume() { if (::audio.isInitialized && fullscreenGame) audio.resume(); if (screen == "camera" && cameraPreview == null) showCameraPreview() }
    @JvmStatic fun dispose() { handler.removeCallbacksAndMessages(null); reviewGeneration.incrementAndGet(); if (::reviewWorker.isInitialized) reviewWorker.shutdownNow(); cameraPreview?.close(); photoInput.close(); audio.close() }

    @JvmStatic fun onBackPressed(host: MainActivity): Boolean {
        when (screen) {
            "home" -> return false
            "play" -> showPause()
            "draw", "review" -> confirmDiscard { showHome() }
            "conversion" -> { photoInput.cancel(); cleanupCameraPhoto(); cleanupCameraFile(); showHome() }
            "camera" -> { cameraPreview?.close(); cameraPreview = null; showHome() }
            else -> showHome()
        }
        return true
    }

    @JvmStatic fun onActivityResult(host: MainActivity, requestCode: Int, resultCode: Int, data: Intent?): Boolean {
        if (requestCode !in listOf(PICK_PHOTO, TAKE_PHOTO, IMPORT_LEVEL)) return false
        if (resultCode != Activity.RESULT_OK) {
            if (requestCode == TAKE_PHOTO) cleanupCameraPhoto()
            showHome(); return true
        }
        if (requestCode == IMPORT_LEVEL) {
            val uri = data?.data ?: return true
            try {
                val bytes = host.contentResolver.openInputStream(uri)?.use { input ->
                    val output = ByteArrayOutputStream()
                    val chunk = ByteArray(8192)
                    while (true) {
                        val count = input.read(chunk)
                        if (count < 0) break
                        require(output.size() + count <= LevelDraft.MAX_JSON_BYTES) { "Level file is too large." }
                        output.write(chunk, 0, count)
                    }
                    output.toByteArray()
                } ?: error("Could not read the level file.")
                val text = bytes.toString(Charsets.UTF_8).trim()
                val json = if (text.startsWith("DDL1:")) LevelShareCodec.decode(text) else text
                currentDraft = LevelDraft.fromJson(json).apply { id = UUID.randomUUID().toString() }
                showReview(currentDraft!!)
            } catch (error: Exception) { message(error.message ?: "Could not import this level."); showHome() }
            return true
        }
        val uri = if (requestCode == TAKE_PHOTO) photoUri else data?.data
        if (uri == null) { message("No image was returned."); showHome(); return true }
        screen = "conversion"
        showSimple("Reading your drawing", "Finding colored symbols and walls…", "Cancel" to { photoInput.cancel(); cleanupCameraPhoto(); showHome() })
        photoInput.convert(uri) { result ->
            if (requestCode == TAKE_PHOTO) cleanupCameraPhoto()
            result.onSuccess { (draft, _) -> currentDraft = draft; library.saveDraft(draft); showReview(draft) }
                .onFailure { message(it.message ?: "Could not read this drawing."); showHome() }
        }
        return true
    }

    @JvmStatic fun onRequestPermissionsResult(host: MainActivity, requestCode: Int, permissions: Array<out String>, grantResults: IntArray): Boolean {
        if (requestCode != CAMERA_PERMISSION) return false
        if (grantResults.firstOrNull() == PackageManager.PERMISSION_GRANTED) showCameraPreview()
        else {
            val blocked = !ActivityCompat.shouldShowRequestPermissionRationale(host, Manifest.permission.CAMERA)
            AlertDialog.Builder(host).setTitle("Camera access is off")
                .setMessage("You can still choose a photo or draw a dungeon.${if (blocked) " To enable the camera, open app settings." else ""}")
                .setNegativeButton("Draw") { _, _ -> currentDraft = LevelDraft(); showDraw(currentDraft!!) }
                .setNeutralButton("Choose photo") { _, _ -> pickPhoto() }
                .setPositiveButton(if (blocked) "App settings" else "Back") { _, _ ->
                    if (blocked) host.startActivity(Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS, Uri.parse("package:${host.packageName}")))
                    showHome()
                }.show()
        }
        return true
    }

    private fun showHome() {
        screen = "home"; playingLevel = -1; tutorialStep = 0
        activity.showMenu()
        content("Doodlebound", "Make a dungeon. Play your drawing.") {
            if (prefs.contains("runCheckpoint")) action("Resume interrupted level (restart)") { resumeCheckpoint() }
            library.loadDraft()?.let { draft -> action("Resume draft") { currentDraft = draft; showDraw(draft) } }
            action("Play levels") { showLevels() }
            action("Guided lessons") { showLessons() }
            action("Draw a dungeon") { currentDraft = LevelDraft(); showDraw(currentDraft!!) }
            action("Photograph paper") { launchCamera() }
            action("Choose a photo") { pickPhoto() }
            action("Try sample drawing") { loadSampleDrawing() }
            action("My dungeons") { showLibrary() }
            action("Import level file") { importLevel() }
            action("Paste a level") { pasteLevel() }
            action("How to play") { showHelp() }
            action("Settings") { showSettings() }
        }
        if (!prefs.getBoolean("introSeen", false)) {
            prefs.edit().putBoolean("introSeen", true).apply()
            AlertDialog.Builder(activity).setTitle("Welcome to Doodlebound")
                .setMessage("Draw walls and place a green start, blue exit, yellow coins and red enemies. You can try a level first, or make your own. Camera access is only used when you choose to photograph paper.")
                .setPositiveButton("Got it", null).show()
        }
    }

    private fun showLevels() {
        screen = "levels"
        content("Choose a level", "Play offline with touch controls.") {
            listOf("First steps", "The corridor", "The chase", "Lock and key", "The gauntlet", "The push", "Crossroads", "Finale").forEachIndexed { index, title ->
                val cleared = if (prefs.getBoolean("cleared_$index", false)) " ✓" else ""
                action("${index + 1}. $title$cleared") { launchLevel(index) }
            }
            action("Back") { showHome() }
        }
    }

    private fun showLessons() {
        screen = "lessons"
        content("Guided lessons", "Each lesson can be replayed or skipped.") {
            action("1. Move, collect, exit") { launchTutorial(1) }
            action("2. Danger and retry") { launchTutorial(2) }
            action("3. Draw and review") { launchTutorial(3) }
            action("Back") { showHome() }
        }
    }

    private fun launchTutorial(step: Int) {
        tutorialStep = step
        when (step) {
            1 -> AlertDialog.Builder(activity).setTitle("Lesson 1: Move")
                .setMessage("Drag on the marked side to move. Collect the yellow coin, then reach the blue exit.")
                .setNegativeButton("Skip") { _, _ -> showLessons() }
                .setPositiveButton("Play") { _, _ -> launchLevel(0) }.show()
            2 -> AlertDialog.Builder(activity).setTitle("Lesson 2: Danger")
                .setMessage("The red enemy can catch you. Try the level, then use Retry if needed.")
                .setNegativeButton("Skip") { _, _ -> showLessons() }
                .setPositiveButton("Play") { _, _ -> launchLevel(2) }.show()
            else -> {
                val draft = LevelDraft(name = "My first drawing")
                currentDraft = draft
                AlertDialog.Builder(activity).setTitle("Lesson 3: Draw")
                    .setMessage("Drag with Wall, then tap Start and Exit. Open Review to fix or add symbols and test your dungeon.")
                    .setNegativeButton("Skip") { _, _ -> showLessons() }
                    .setNeutralButton("Try sample") { _, _ -> loadSampleDrawing() }
                    .setPositiveButton("Draw") { _, _ -> showDraw(draft) }.show()
            }
        }
    }

    private fun launchLevel(index: Int) {
        val ok = NativeBridge.startLevel(activity.nativeHandle, index)
        if (!ok) { message("This level could not start."); return }
        prefs.edit().putString("runCheckpoint", "sample:$index").commit()
        playingLevel = index; lastStatus = 0; lastCoins = 0; screen = "play"
        activity.showGame(); buildHud()
    }

    private fun launchDraft(draft: LevelDraft) {
        val issues = draft.issues()
        if (issues.isNotEmpty()) { message(issues.joinToString("\n")); return }
        if (!NativeBridge.loadLevelJson(activity.nativeHandle, draft.toJson())) {
            message("The game could not load this level. Check the walls and symbol positions."); return
        }
        try {
            library.save(draft); library.clearDraft()
            prefs.edit().putString("runCheckpoint", "level:${draft.id}").commit()
        } catch (error: Exception) { prefs.edit().remove("runCheckpoint").commit(); message("Played, but could not save: ${error.message}") }
        playingLevel = -1; currentDraft = draft; lastStatus = 0; lastCoins = 0; screen = "play"
        activity.showGame(); buildHud()
    }

    private fun buildHud() {
        hud.removeAllViews()
        hud.addView(TextView(activity).apply { text = "Coins: 0"; setTextColor(Color.WHITE); textSize = 18f; setPadding(dp(16), dp(10), dp(16), dp(10)); tag = "coins" })
        hud.addView(Button(activity).apply { text = "Pause"; setOnClickListener { showPause() } })
        hud.addView(Button(activity).apply { text = "Tour"; setOnClickListener { NativeBridge.setTour(activity.nativeHandle, true); controlCue.text = "Drag to look"; text = "Return"; setOnClickListener { NativeBridge.setTour(activity.nativeHandle, false); controlCue.text = "Drag to move"; buildHud() } } })
    }

    private fun updateHud() {
        val status = NativeBridge.status(activity.nativeHandle)
        val coins = NativeBridge.coinsCollected(activity.nativeHandle)
        hud.findViewWithTag<TextView>("coins")?.text = "Coins: $coins/${NativeBridge.totalCoins(activity.nativeHandle)}"
        if (coins > lastCoins) { audio.coin(); haptic(false) }
        lastCoins = coins
        if (status != lastStatus) {
            lastStatus = status
            if (status == 1) { audio.win(); haptic(true) }
            if (status == 2) { audio.loss(); haptic(true) }
            if (status == 1 || status == 2) showResults(status == 1)
            if (status == 3) { message("Game rendering failed."); showHome() }
        }
    }

    private fun showPause() {
        screen = "pause"; NativeBridge.pause(activity.nativeHandle); activity.showMenu()
        content("Paused", "Your dungeon is waiting.") {
            action("Resume") { screen = "play"; activity.showGame() }
            action("Retry") { NativeBridge.restart(activity.nativeHandle); lastStatus = 0; lastCoins = 0; screen = "play"; activity.showGame() }
            action("Home") { clearCheckpoint(); showHome() }
        }
    }

    private fun showResults(won: Boolean) {
        clearCheckpoint()
        if (won && playingLevel >= 0) prefs.edit().putBoolean("cleared_$playingLevel", true).apply()
        if (won && playingLevel < 0) currentDraft?.let { prefs.edit().putBoolean("cleared_${it.id}", true).apply() }
        screen = "results"; activity.showMenu()
        content(if (won) "Dungeon cleared!" else "Try again", if (won) "You reached the exit." else "Your adventurer was caught.") {
            action("Retry") {
                if (playingLevel >= 0) prefs.edit().putString("runCheckpoint", "sample:$playingLevel").commit()
                else currentDraft?.let { prefs.edit().putString("runCheckpoint", "level:${it.id}").commit() }
                NativeBridge.restart(activity.nativeHandle); lastStatus = 0; lastCoins = 0; screen = "play"; activity.showGame()
            }
            if (tutorialStep == 1 && won) action("Next lesson") { launchTutorial(2) }
            else if (tutorialStep == 2 && won) action("Next lesson") { launchTutorial(3) }
            else if (playingLevel in 0..6 && won) action("Next level") { launchLevel(playingLevel + 1) }
            action("Home") { showHome() }
        }
    }

    private fun resumeCheckpoint() {
        val checkpoint = prefs.getString("runCheckpoint", null) ?: return
        if (checkpoint.startsWith("sample:")) {
            val index = checkpoint.removePrefix("sample:").toIntOrNull()
            if (index in 0..7) { launchLevel(index!!); return }
        } else if (checkpoint.startsWith("level:")) {
            val id = checkpoint.removePrefix("level:")
            val level = library.list().firstOrNull { it.id == id }
            if (level != null) { currentDraft = level; launchDraft(level); return }
        }
        clearCheckpoint(); message("The interrupted level is no longer available."); showHome()
    }

    private fun clearCheckpoint() { prefs.edit().remove("runCheckpoint").commit() }

    private fun showDraw(draft: LevelDraft) { showEditor(draft, false) }
    private fun showReview(draft: LevelDraft) { showEditor(draft, true) }

    private fun showEditor(draft: LevelDraft, reviewing: Boolean) {
        screen = if (reviewing) "review" else "draw"
        reviewGeneration.incrementAndGet()
        activity.showMenu(); panel.removeAllViews()
        val body = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(12), dp(8), dp(12), dp(8)) }
        panel.addView(body, FrameLayout.LayoutParams(-1, -1))
        val title = TextView(activity).apply {
            text = if (reviewing) "Review your dungeon" else "Draw your dungeon"
            setTextColor(Color.WHITE); textSize = 23f; setPadding(0, 0, 0, dp(6))
        }
        body.addView(title)
        val toolRow = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        body.addView(toolRow)
        val verdict = TextView(activity).apply { setTextColor(Color.WHITE); textSize = 15f }
        lateinit var editor: DrawView
        editor = DrawView(activity, draft) {
            currentDraft = editor.draft
            try { library.saveDraft(editor.draft) } catch (error: Exception) { message("Draft save failed: ${error.message}") }
            if (reviewing) requestReview(editor.draft, verdict)
        }
        DrawView.Tool.entries.forEach { tool ->
            toolRow.addView(Button(activity).apply {
                text = tool.label
                contentDescription = "${tool.label} tool"
                setOnClickListener { editor.tool = tool; message("${tool.label} tool") }
            }, LinearLayout.LayoutParams(0, dp(48), 1f))
        }
        val workspace = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        body.addView(workspace, LinearLayout.LayoutParams(-1, 0, 1f))
        draft.sourcePhoto?.let { path ->
            BitmapFactory.decodeFile(path)?.let { bitmap ->
                workspace.addView(ImageView(activity).apply { setImageBitmap(bitmap); scaleType = ImageView.ScaleType.FIT_CENTER; contentDescription = "Original paper drawing" }, LinearLayout.LayoutParams(0, -1, 1f))
            }
        }
        workspace.addView(editor, LinearLayout.LayoutParams(0, -1, if (draft.sourcePhoto != null) 1f else 2f))
        val footer = LinearLayout(activity).apply { orientation = LinearLayout.HORIZONTAL }
        body.addView(footer)
        fun footerButton(label: String, action: () -> Unit) { footer.addView(Button(activity).apply { text = label; setOnClickListener { action() } }, LinearLayout.LayoutParams(0, dp(50), 1f)) }
        footerButton("Undo") { editor.undo() }
        footerButton("Redo") { editor.redo() }
        footerButton("Clear") { AlertDialog.Builder(activity).setMessage("Clear all walls and symbols?").setNegativeButton("Cancel", null).setPositiveButton("Clear") { _, _ -> editor.clear() }.show() }
        footerButton("Save") { saveNamed(editor.draft) }
        if (reviewing) footerButton("Suggest fix") { suggestRepair(editor, verdict) }
        footerButton(if (reviewing) "Play" else "Review") {
            if (reviewing) launchDraft(editor.draft) else showReview(editor.draft)
        }
        footerButton("Back") { if (reviewing) showDraw(editor.draft) else confirmDiscard { showHome() } }
        if (reviewing) {
            val issues = draft.issues()
            verdict.text = if (issues.isEmpty()) "Checking the static route…" else issues.joinToString(" ")
            body.addView(verdict, 1)
            if (issues.isEmpty()) requestReview(draft, verdict)
        } else {
            body.addView(TextView(activity).apply { text = "Green start · Blue exit · Yellow coin · Red enemy. Draw walls by dragging; pinch to zoom."; setTextColor(Color.WHITE); textSize = 15f }, 1)
        }
    }

    private fun requestReview(draft: LevelDraft, verdict: TextView) {
        val ticket = reviewGeneration.incrementAndGet()
        val localIssues = draft.issues()
        if (localIssues.isNotEmpty()) { verdict.text = localIssues.joinToString(" "); return }
        verdict.text = "Checking the static route…"
        val json = draft.toJson()
        reviewWorker.execute {
            val result = runCatching { NativeBridge.reviewLevelJson(activity.nativeHandle, json)?.let { JSONObject(it) } }.getOrNull()
            activity.runOnUiThread {
                if (reviewGeneration.get() != ticket || screen != "review") return@runOnUiThread
                verdict.text = when (result?.optString("state")) {
                    "solvable" -> "Static route found · par ${result.optInt("par", -1)} steps. Dynamic enemies may still catch you."
                    "unsolvable" -> "Static route blocked: ${result.optString("message", "an exit or coin is unreachable")}. Edit it or preview a suggested fix."
                    "invalid" -> result.optString("message", "The level needs a start, exit and walls.")
                    "unsupported" -> "Route unknown: ${result.optString("message", "this mechanic or level size exceeds the checker")}. You can still test play."
                    else -> "Route check unavailable. You can still test play."
                }
            }
        }
    }

    private fun suggestRepair(editor: DrawView, verdict: TextView) {
        if (editor.draft.issues().isNotEmpty()) { message("Place a start, exit and walls before asking for a fix."); return }
        val ticket = reviewGeneration.incrementAndGet()
        val snapshot = editor.draft.copyDraft()
        reviewWorker.execute {
            val result = runCatching { NativeBridge.suggestRepair(activity.nativeHandle, snapshot.toJson())?.let { JSONObject(it) } }.getOrNull()
            activity.runOnUiThread {
                if (reviewGeneration.get() != ticket || screen != "review") return@runOnUiThread
                if (result?.optString("state") != "suggested") {
                    message(result?.optString("message")?.ifBlank { "No safe fix is available." } ?: "No safe fix is available.")
                    requestReview(editor.draft, verdict)
                    return@runOnUiThread
                }
                val edits = result.optJSONArray("edits")
                if (edits == null || edits.length() == 0) { message("No move is suggested."); requestReview(editor.draft, verdict); return@runOnUiThread }
                val proposal = snapshot.copyDraft()
                val descriptions = mutableListOf<String>()
                var applicable = true
                for (index in 0 until edits.length()) {
                    val item = edits.getJSONObject(index)
                    val type = item.getString("type")
                    val fromX = item.getDouble("fromX").toFloat(); val fromZ = item.getDouble("fromZ").toFloat()
                    val toX = item.getDouble("toX").toFloat(); val toZ = item.getDouble("toZ").toFloat()
                    val markIndex = proposal.marks.indexOfFirst { it.type == type && hypot(it.x - fromX, it.z - fromZ) < 0.75f }
                    if (markIndex < 0) { applicable = false; break }
                    proposal.marks[markIndex] = Mark(type, toX, toZ)
                    descriptions.add("Move $type from (${fromX.toInt()}, ${fromZ.toInt()}) to (${toX.toInt()}, ${toZ.toInt()})")
                }
                if (!applicable) { message("Suggested fix no longer matches this drawing."); requestReview(editor.draft, verdict); return@runOnUiThread }
                AlertDialog.Builder(activity).setTitle("Preview suggested fix")
                    .setMessage(descriptions.joinToString("\n") + "\n\nThis checks a static route only. Apply these moves?")
                    .setNegativeButton("Keep original") { _, _ -> requestReview(editor.draft, verdict) }
                    .setPositiveButton("Apply moves") { _, _ -> editor.replace(proposal) }.show()
            }
        }
    }

    private fun saveNamed(draft: LevelDraft) {
        val input = EditText(activity).apply { setText(draft.name); selectAll(); filters = arrayOf(android.text.InputFilter.LengthFilter(80)) }
        AlertDialog.Builder(activity).setTitle("Save dungeon").setView(input).setNegativeButton("Cancel", null)
            .setPositiveButton("Save") { _, _ ->
                draft.name = input.text.toString().trim().ifBlank { "My dungeon" }
                try { library.save(draft); library.clearDraft(); message("Saved to My dungeons") }
                catch (error: Exception) { message("Could not save: ${error.message}") }
            }.show()
    }

    private fun showLibrary() {
        screen = "library"
        content("My dungeons", "Saved levels stay on this phone.") {
            val levels = library.list()
            if (levels.isEmpty()) note("No saved dungeons yet.")
            levels.forEach { level ->
                addView(ImageView(activity).apply {
                    setImageBitmap(levelThumbnail(level))
                    contentDescription = "Preview of ${level.name}"
                    scaleType = ImageView.ScaleType.FIT_START
                }, LinearLayout.LayoutParams(-1, dp(88)))
                val cleared = if (prefs.getBoolean("cleared_${level.id}", false)) " ✓" else ""
                action(level.name + cleared) {
                    AlertDialog.Builder(activity).setTitle(level.name)
                        .setItems(arrayOf("Play", "Edit", "Duplicate", "Share level", "Compare paper and game", "Delete")) { _, choice ->
                            when (choice) {
                                0 -> launchDraft(level)
                                1 -> { currentDraft = level; showReview(level) }
                                2 -> { val duplicate = level.copyDraft().apply { id = java.util.UUID.randomUUID().toString(); name += " copy" }; library.save(duplicate); showLibrary() }
                                3 -> shareLevel(level)
                                4 -> shareComparison(level)
                                5 -> AlertDialog.Builder(activity).setMessage("Delete ${level.name} and its photo from this phone?")
                                    .setNegativeButton("Cancel", null).setPositiveButton("Delete") { _, _ -> library.delete(level); showLibrary() }.show()
                            }
                        }.show()
                }
            }
            action("Back") { showHome() }
        }
    }

    private fun levelThumbnail(level: LevelDraft): Bitmap {
        val image = Bitmap.createBitmap(400, 120, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(image)
        canvas.drawColor(Color.rgb(244, 240, 225))
        val points = level.walls.flatten()
        val xs = points.map { it.x } + level.marks.map { it.x }
        val zs = points.map { it.z } + level.marks.map { it.z }
        val minX = xs.minOrNull() ?: 0f; val maxX = xs.maxOrNull() ?: 24f
        val minZ = zs.minOrNull() ?: 0f; val maxZ = zs.maxOrNull() ?: 16f
        val scale = minOf(376f / (maxX - minX + 2f), 98f / (maxZ - minZ + 2f))
        fun x(value: Float) = 12f + (value - minX + 1f) * scale
        fun y(value: Float) = 11f + (value - minZ + 1f) * scale
        val paint = Paint(Paint.ANTI_ALIAS_FLAG).apply { color = Color.DKGRAY; strokeWidth = 4f; strokeCap = Paint.Cap.ROUND }
        level.walls.forEach { wall -> for (i in 1 until wall.size) canvas.drawLine(x(wall[i - 1].x), y(wall[i - 1].z), x(wall[i].x), y(wall[i].z), paint) }
        level.marks.forEach { mark ->
            paint.color = when (mark.type) { "player" -> Color.rgb(30, 180, 30); "exit" -> Color.rgb(40, 80, 220); "coin" -> Color.rgb(220, 180, 30); else -> Color.rgb(220, 40, 40) }
            canvas.drawCircle(x(mark.x), y(mark.z), 7f, paint)
        }
        return image
    }

    private fun shareLevel(level: LevelDraft) {
        val code = LevelShareCodec.encode(level.toJson())
        if (code.length <= 32_000) {
            val intent = Intent(Intent.ACTION_SEND).apply { type = "text/plain"; putExtra(Intent.EXTRA_TEXT, code) }
            activity.startActivity(Intent.createChooser(intent, "Share Doodlebound level"))
        } else {
            val file = exportFile("ddl")
            file.writeText(code)
            shareUri(file, "application/vnd.doodlebound.level", "Share Doodlebound level")
        }
    }

    private fun shareComparison(level: LevelDraft) {
        if (level.sourcePhoto == null || !File(level.sourcePhoto!!).exists()) { message("This level has no original paper photo."); return }
        AlertDialog.Builder(activity).setTitle("Include original drawing?")
            .setMessage("The exported comparison image includes your original paper photo and the 3D level. Only share it with people you choose.")
            .setNegativeButton("Cancel", null).setPositiveButton("Create image") { _, _ -> createComparison(level) }.show()
    }

    private fun createComparison(level: LevelDraft) {
        if (!NativeBridge.loadLevelJson(activity.nativeHandle, level.toJson())) { message("This level cannot be rendered."); return }
        activity.showGame()
        handler.postDelayed({
            activity.gameView.queueEvent {
                NativeBridge.drawFrame(activity.nativeHandle, 0f)
                val frame = NativeBridge.captureFrame(activity.nativeHandle)
                activity.runOnUiThread {
                    if (frame == null) { message("Could not capture the 3D view."); showLibrary() }
                    else Thread {
                        val result = runCatching { writeComparison(level, frame) }
                        activity.runOnUiThread {
                            showLibrary()
                            result.onSuccess { shareUri(it, "image/png", "Share paper and 3D comparison") }
                                .onFailure { message(it.message ?: "Could not create comparison image.") }
                        }
                    }.start()
                }
            }
        }, 250)
    }

    private fun writeComparison(level: LevelDraft, frame: IntArray): File {
        require(frame.size >= 3)
        val width = frame[0]; val height = frame[1]
        require(width in 1..4096 && height in 1..4096 && width.toLong() * height <= 4_000_000 && frame.size == width * height + 2)
        val source = BitmapFactory.decodeFile(level.sourcePhoto) ?: error("Original photo is missing.")
        val game = Bitmap.createBitmap(frame.copyOfRange(2, frame.size), width, height, Bitmap.Config.ARGB_8888)
        val output = Bitmap.createBitmap(1600, 840, Bitmap.Config.ARGB_8888)
        val canvas = Canvas(output)
        canvas.drawColor(Color.rgb(244, 240, 225))
        val paint = Paint(Paint.ANTI_ALIAS_FLAG or Paint.FILTER_BITMAP_FLAG)
        paint.color = Color.BLACK; paint.textSize = 28f
        canvas.drawText("Original drawing", 24f, 35f, paint)
        canvas.drawText("Doodlebound 3D", 820f, 35f, paint)
        canvas.drawBitmap(source, null, android.graphics.Rect(20, 55, 780, 820), paint)
        canvas.drawBitmap(game, null, android.graphics.Rect(820, 55, 1580, 820), paint)
        val file = exportFile("png")
        file.outputStream().use { require(output.compress(Bitmap.CompressFormat.PNG, 100, it)) }
        source.recycle(); game.recycle(); output.recycle()
        return file
    }

    private fun exportFile(extension: String): File = File(activity.cacheDir, "exports").apply { mkdirs() }
        .resolve("${UUID.randomUUID()}.$extension")

    private fun shareUri(file: File, mime: String, title: String) {
        val uri = Uri.Builder().scheme("content").authority("${activity.packageName}.exports").appendPath(file.name).build()
        val intent = Intent(Intent.ACTION_SEND).apply {
            type = mime
            putExtra(Intent.EXTRA_STREAM, uri)
            addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        }
        activity.startActivity(Intent.createChooser(intent, title))
    }

    private fun showSettings() {
        screen = "settings"
        content("Settings", "Changes apply now and are saved on this phone.") {
            fun toggle(key: String, title: String, default: Boolean = false, onChange: (Boolean) -> Unit = {}) {
                action("$title: ${if (prefs.getBoolean(key, default)) "On" else "Off"}") {
                    val next = !prefs.getBoolean(key, default)
                    prefs.edit().putBoolean(key, next).apply(); onChange(next); showSettings()
                }
            }
            toggle("leftHanded", "Left-handed controls") {
                NativeBridge.setLeftHanded(activity.nativeHandle, it)
                val params = controlCue.layoutParams as FrameLayout.LayoutParams
                params.gravity = Gravity.BOTTOM or if (it) Gravity.RIGHT else Gravity.LEFT
                controlCue.layoutParams = params
            }
            toggle("haptics", "Haptics", true)
            toggle("reducedMotion", "Reduced motion") { NativeBridge.setReducedMotion(activity.nativeHandle, it) }
            toggle("sound", "Sound", true) { configureAudio() }
            toggle("musicOn", "Ambient music", true) { configureAudio() }
            volumeControl("Master volume", "master", 60)
            volumeControl("Effects volume", "effects", 80)
            volumeControl("Music volume", "music", 35)
            action("Help") { showHelp() }
            action("Back") { showHome() }
        }
    }

    private fun showHelp() {
        screen = "help"
        content("How to play", "Move with the on-screen controls. Collect coins, avoid red enemies, and reach the blue exit.") {
            note("Draw walls as dark lines. Place solid GREEN start, BLUE exit, YELLOW coin, and RED enemy marks. Colors are primary; labels help identify them without color. Use a bright, even photo of just the square drawing field.")
            note("Review lets you move or erase symbols, repair walls, and test the level. A start and exit only mean it is ready to try; they do not prove it can be solved.")
            action("Practice: first steps") { launchLevel(0) }
            action("Try sample drawing") { loadSampleDrawing() }
            action("Credits and notices") { showCredits() }
            action("Back") { showHome() }
        }
    }

    private fun showCredits() {
        screen = "credits"
        val notices = runCatching { activity.assets.open("NOTICE.txt").bufferedReader().use { it.readText() } }
            .getOrDefault("Doodlebound demo · IKore / Charta")
        content("Credits and notices", "Licenses for the app and its Android dependencies.") {
            note(notices)
            action("Back") { showHelp() }
        }
    }

    private fun LinearLayout.volumeControl(label: String, key: String, default: Int) {
        note("$label: ${prefs.getInt(key, default)}%")
        addView(SeekBar(activity).apply {
            max = 100; progress = prefs.getInt(key, default)
            contentDescription = label
            setOnSeekBarChangeListener(object : SeekBar.OnSeekBarChangeListener {
                override fun onProgressChanged(bar: SeekBar?, value: Int, fromUser: Boolean) {
                    if (fromUser) { prefs.edit().putInt(key, value).apply(); configureAudio() }
                }
                override fun onStartTrackingTouch(bar: SeekBar?) = Unit
                override fun onStopTrackingTouch(bar: SeekBar?) = Unit
            })
        })
    }

    private fun configureAudio() {
        if (::audio.isInitialized) audio.configure(
            prefs.getBoolean("sound", true), prefs.getBoolean("musicOn", true),
            prefs.getInt("master", 60), prefs.getInt("effects", 80), prefs.getInt("music", 35))
    }

    private fun haptic(strong: Boolean) {
        if (prefs.getBoolean("haptics", true)) root.performHapticFeedback(if (strong) HapticFeedbackConstants.LONG_PRESS else HapticFeedbackConstants.VIRTUAL_KEY)
    }

    private fun launchCamera() {
        if (ContextCompat.checkSelfPermission(activity, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED) showCameraPreview()
        else ActivityCompat.requestPermissions(activity, arrayOf(Manifest.permission.CAMERA), CAMERA_PERMISSION)
    }

    private fun showCameraPreview() {
        screen = "camera"
        activity.showMenu(); panel.removeAllViews()
        val texture = TextureView(activity)
        panel.addView(texture, FrameLayout.LayoutParams(-1, -1))
        val controls = LinearLayout(activity).apply { gravity = Gravity.BOTTOM or Gravity.CENTER_HORIZONTAL; setPadding(dp(12), dp(8), dp(12), dp(8)); setBackgroundColor(0xAA101820.toInt()) }
        controls.addView(Button(activity).apply { text = "Cancel"; setOnClickListener { cameraPreview?.close(); cameraPreview = null; showHome() } })
        controls.addView(Button(activity).apply { text = "Capture"; setOnClickListener { cameraPreview?.capture() } })
        panel.addView(controls, FrameLayout.LayoutParams(-1, ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.BOTTOM))
        cameraPreview?.close()
        cameraPreview = CameraPreview(activity, texture,
            onCaptured = { uri ->
                cameraPreview?.close(); cameraPreview = null
                pendingCameraFile = uri.path?.let { File(it) }
                screen = "conversion"
                showSimple("Reading your drawing", "Finding colored symbols and walls…", "Cancel" to { photoInput.cancel(); cleanupCameraFile(); showHome() })
                photoInput.convert(uri) { result ->
                    cleanupCameraFile()
                    result.onSuccess { (draft, _) -> currentDraft = draft; library.saveDraft(draft); showReview(draft) }
                        .onFailure { message(it.message ?: "Could not read this drawing."); showHome() }
                }
            },
            onError = { error ->
                cameraPreview?.close(); cameraPreview = null
                AlertDialog.Builder(activity).setTitle("Camera unavailable").setMessage(error)
                    .setNegativeButton("Back") { _, _ -> showHome() }
                    .setNeutralButton("Choose photo") { _, _ -> pickPhoto() }
                    .setPositiveButton("System camera") { _, _ -> launchSystemCamera() }.show()
            })
        cameraPreview?.start()
    }

    private fun launchSystemCamera() {
        val intent = Intent(MediaStore.ACTION_IMAGE_CAPTURE)
        if (intent.resolveActivity(activity.packageManager) == null) { message("No camera app is available. You can draw or choose a photo."); return }
        val values = android.content.ContentValues().apply { put(MediaStore.Images.Media.DISPLAY_NAME, "doodlebound-${System.currentTimeMillis()}.jpg"); put(MediaStore.Images.Media.MIME_TYPE, "image/jpeg") }
        photoUri = activity.contentResolver.insert(MediaStore.Images.Media.EXTERNAL_CONTENT_URI, values)
        if (photoUri == null) { message("Could not prepare camera storage."); return }
        intent.putExtra(MediaStore.EXTRA_OUTPUT, photoUri)
        intent.addFlags(Intent.FLAG_GRANT_WRITE_URI_PERMISSION or Intent.FLAG_GRANT_READ_URI_PERMISSION)
        activity.startActivityForResult(intent, TAKE_PHOTO)
    }

    private fun cleanupCameraPhoto() {
        photoUri?.let { runCatching { activity.contentResolver.delete(it, null, null) } }
        photoUri = null
    }

    private fun cleanupCameraFile() { pendingCameraFile?.delete(); pendingCameraFile = null }

    private fun pickPhoto() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply { type = "image/*"; addCategory(Intent.CATEGORY_OPENABLE) }
        activity.startActivityForResult(intent, PICK_PHOTO)
    }

    private fun importLevel() {
        val intent = Intent(Intent.ACTION_OPEN_DOCUMENT).apply { type = "*/*"; addCategory(Intent.CATEGORY_OPENABLE) }
        activity.startActivityForResult(intent, IMPORT_LEVEL)
    }

    private fun loadSampleDrawing() {
        val startedAt = SystemClock.elapsedRealtime()
        logInfo("bundled_sample start")
        val file = File(activity.cacheDir, "doodlebound-three-room.png")
        val assetStartedAt = SystemClock.elapsedRealtime()
        try {
            activity.assets.open("doodlebound-three-room.png").use { source -> file.outputStream().use { source.copyTo(it) } }
            logInfo("bundled_sample phase=asset_copy result=ok duration_ms=${SystemClock.elapsedRealtime() - assetStartedAt} bytes=${file.length()}")
        } catch (error: Exception) {
            logError("bundled_sample phase=asset_copy result=failed error=${error.javaClass.simpleName}")
            message("Sample drawing is unavailable: ${error.message}")
            return
        }
        screen = "conversion"
        showSimple("Reading sample drawing", "This uses the same photo conversion as paper capture.", "Cancel" to { photoInput.cancel(); showHome() })
        photoInput.convert(Uri.fromFile(file)) { result ->
            logInfo("bundled_sample callback result=${if (result.isSuccess) "ok" else "failed"} elapsed_ms=${SystemClock.elapsedRealtime() - startedAt}")
            result.onSuccess { (draft, _) ->
                currentDraft = draft
                library.saveDraft(draft)
                showReview(draft)
                logInfo("bundled_sample review=shown elapsed_ms=${SystemClock.elapsedRealtime() - startedAt}")
            }.onFailure { error ->
                logWarning("bundled_sample review=failed elapsed_ms=${SystemClock.elapsedRealtime() - startedAt} error=${error.javaClass.simpleName}")
                message(error.message ?: "Could not convert the sample.")
                showHome()
            }
        }
    }

    private fun pasteLevel() {
        val input = EditText(activity).apply {
            hint = "Paste the complete doodle-level JSON here"
            minLines = 4
            maxLines = 8
            inputType = android.text.InputType.TYPE_CLASS_TEXT or android.text.InputType.TYPE_TEXT_FLAG_MULTI_LINE
        }
        AlertDialog.Builder(activity).setTitle("Paste a level").setView(input).setNegativeButton("Cancel", null)
            .setPositiveButton("Import") { _, _ ->
                try {
                    val level = LevelDraft.fromJson(input.text.toString()).apply { id = java.util.UUID.randomUUID().toString() }
                    currentDraft = level; showReview(level)
                } catch (error: Exception) { message(error.message ?: "This level code is invalid.") }
            }.show()
    }

    private fun confirmDiscard(onDiscard: () -> Unit) {
        AlertDialog.Builder(activity).setTitle("Leave your drawing?").setMessage("The current draft is saved and can be resumed from Home.")
            .setNegativeButton("Keep editing", null).setPositiveButton("Leave") { _, _ -> onDiscard() }.show()
    }

    private fun logInfo(message: String) {
        if (BuildConfig.DEBUG) Log.i(TAG, message)
    }

    private fun logWarning(message: String) {
        if (BuildConfig.DEBUG) Log.w(TAG, message)
    }

    private fun logError(message: String) {
        if (BuildConfig.DEBUG) Log.e(TAG, message)
    }

    private fun content(title: String, subtitle: String, build: LinearLayout.() -> Unit) {
        activity.showMenu(); panel.removeAllViews()
        val scroller = ScrollView(activity)
        val column = LinearLayout(activity).apply { orientation = LinearLayout.VERTICAL; setPadding(dp(28), dp(18), dp(28), dp(24)) }
        scroller.addView(column)
        panel.addView(scroller, FrameLayout.LayoutParams(-1, -1))
        column.addView(TextView(activity).apply { text = title; setTextColor(Color.WHITE); textSize = 30f; contentDescription = title })
        column.addView(TextView(activity).apply { text = subtitle; setTextColor(0xFFD6DDE2.toInt()); textSize = 16f; setPadding(0, dp(3), 0, dp(15)) })
        column.build()
    }

    private fun LinearLayout.action(label: String, click: () -> Unit) {
        addView(Button(activity).apply { text = label; textSize = 17f; setOnClickListener { click() } }, LinearLayout.LayoutParams(-1, dp(50)).apply { bottomMargin = dp(6) })
    }
    private fun LinearLayout.note(textValue: String) {
        addView(TextView(activity).apply { text = textValue; textSize = 17f; setTextColor(Color.WHITE); setPadding(0, dp(8), 0, dp(12)) })
    }
    private fun showSimple(title: String, subtitle: String, button: Pair<String, () -> Unit>) {
        content(title, subtitle) { action(button.first, button.second) }
    }
    private fun message(text: String) { Toast.makeText(activity, text, Toast.LENGTH_LONG).show() }
    private fun dp(value: Int) = (value * activity.resources.displayMetrics.density).toInt()
}
