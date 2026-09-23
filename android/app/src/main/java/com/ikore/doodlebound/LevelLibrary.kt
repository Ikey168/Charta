package com.ikore.doodlebound

import android.content.Context
import android.util.AtomicFile
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.io.ByteArrayOutputStream

/** App-private level and draft store. AtomicFile retains a previous complete copy on failed writes. */
internal class LevelLibrary(private val context: Context) {
    private val directory = File(context.filesDir, "levels").apply { mkdirs() }
    private val draftFile = AtomicFile(File(context.filesDir, "draft.json"))

    fun save(level: LevelDraft) {
        val file = AtomicFile(File(directory, "${level.id}.json"))
        val payload = envelope(level).toByteArray(Charsets.UTF_8)
        require(payload.size <= LevelDraft.MAX_JSON_BYTES + 8192)
        saveWithRecovery(file, payload)
    }

    fun saveDraft(level: LevelDraft) {
        saveWithRecovery(draftFile, envelope(level).toByteArray(Charsets.UTF_8))
    }

    fun loadDraft(): LevelDraft? = read(draftFile)
    fun clearDraft() { draftFile.delete(); recoveryFile(draftFile).delete() }
    fun list(): List<LevelDraft> = directory.listFiles { f -> f.isFile && f.name.endsWith(".json") }
        ?.mapNotNull { read(AtomicFile(it)) }?.sortedBy { it.name.lowercase() } ?: emptyList()

    fun delete(level: LevelDraft) {
        val file = AtomicFile(File(directory, "${level.id}.json"))
        file.delete(); recoveryFile(file).delete()
        level.sourcePhoto?.let { path ->
            val file = File(path)
            if (file.parentFile == File(context.filesDir, "photos")) file.delete()
        }
    }

    private fun envelope(level: LevelDraft): String = JSONObject()
        .put("storeVersion", 1)
        .put("id", level.id)
        .put("name", level.name)
        .put("sourcePhoto", level.sourcePhoto)
        .put("level", JSONObject(level.toJson())).toString()

    private fun recoveryFile(file: AtomicFile) = File(file.baseFile.absolutePath + ".good")

    private fun saveWithRecovery(file: AtomicFile, payload: ByteArray) {
        read(file)?.let { previous ->
            val fallback = AtomicFile(recoveryFile(file))
            val backup = fallback.startWrite()
            try { backup.write(envelope(previous).toByteArray(Charsets.UTF_8)); fallback.finishWrite(backup) }
            catch (error: IOException) { fallback.failWrite(backup); throw error }
        }
        val stream = file.startWrite()
        try { stream.write(payload); file.finishWrite(stream) }
        catch (error: IOException) { file.failWrite(stream); throw error }
    }

    private fun read(file: AtomicFile): LevelDraft? = readOne(file) ?: readOne(AtomicFile(recoveryFile(file)))

    private fun readOne(file: AtomicFile): LevelDraft? = try {
        val bytes = file.openRead().use { input ->
            val output = ByteArrayOutputStream()
            val chunk = ByteArray(8192)
            while (true) {
                val count = input.read(chunk)
                if (count < 0) break
                require(output.size() + count <= LevelDraft.MAX_JSON_BYTES + 8192)
                output.write(chunk, 0, count)
            }
            output.toByteArray()
        }
        val envelope = JSONObject(bytes.toString(Charsets.UTF_8))
        require(envelope.getInt("storeVersion") == 1)
        val draft = LevelDraft.fromJson(envelope.getJSONObject("level").toString())
        draft.id = envelope.getString("id").also { require(it.matches(Regex("[0-9a-fA-F-]{36}"))) }
        draft.name = envelope.optString("name", "My dungeon").take(80)
        draft.sourcePhoto = envelope.optString("sourcePhoto").ifBlank { null }
        draft
    } catch (_: Exception) { null }
}
