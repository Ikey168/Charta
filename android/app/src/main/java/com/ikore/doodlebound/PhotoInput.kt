package com.ikore.doodlebound

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.media.ExifInterface
import android.net.Uri
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.UUID
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicInteger

/** Bounded URI decoding and conversion. A generation counter suppresses stale callbacks. */
internal class PhotoInput(private val context: Context, private val session: () -> Long) {
    private val worker = Executors.newSingleThreadExecutor()
    private val generation = AtomicInteger(0)

    fun cancel() { generation.incrementAndGet() }
    fun close() { cancel(); worker.shutdownNow() }

    fun convert(uri: Uri, callback: (Result<Pair<LevelDraft, Bitmap>>) -> Unit) {
        val ticket = generation.incrementAndGet()
        worker.execute {
            val result = runCatching {
                val bytes = boundedRead(uri)
                val bitmap = decode(bytes)
                val pixels = IntArray(bitmap.width * bitmap.height)
                bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
                val json = NativeBridge.convertPhoto(session(), pixels, bitmap.width, bitmap.height)
                    ?.takeIf { it.isNotBlank() }
                    ?: error("Could not find a start, exit, and usable walls. Check the color legend and lighting, then retry or draw corrections.")
                val draft = LevelDraft.fromJson(json)
                val photos = File(context.filesDir, "photos").apply { mkdirs() }
                val file = File(photos, "${UUID.randomUUID()}.jpg")
                file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.JPEG, 88, it) }
                draft.sourcePhoto = file.absolutePath
                draft to bitmap
            }
            if (generation.get() == ticket) (context as android.app.Activity).runOnUiThread { callback(result) }
            else result.getOrNull()?.first?.sourcePhoto?.let { File(it).delete() }
        }
    }

    private fun boundedRead(uri: Uri): ByteArray {
        val output = ByteArrayOutputStream()
        context.contentResolver.openInputStream(uri)?.use { input ->
            val chunk = ByteArray(8192)
            while (true) {
                val count = input.read(chunk)
                if (count < 0) break
                require(output.size() + count <= 12 * 1024 * 1024) { "Image exceeds the 12 MB import limit." }
                output.write(chunk, 0, count)
            }
        } ?: error("The selected image cannot be opened.")
        return output.toByteArray()
    }

    private fun decode(bytes: ByteArray): Bitmap {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeByteArray(bytes, 0, bytes.size, bounds)
        require(bounds.outWidth in 1..12000 && bounds.outHeight in 1..12000) { "Image dimensions are unsupported." }
        val sample = generateSequence(1) { it * 2 }.first { bounds.outWidth / it <= 1024 && bounds.outHeight / it <= 1024 }
        val options = BitmapFactory.Options().apply { inSampleSize = sample; inPreferredConfig = Bitmap.Config.ARGB_8888 }
        val decoded = BitmapFactory.decodeByteArray(bytes, 0, bytes.size, options) ?: error("Image could not be decoded.")
        require(decoded.width * decoded.height <= 1024 * 1024) { "Image is too large after decoding." }
        val orientation = runCatching { ExifInterface(ByteArrayInputStream(bytes)).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL) }
            .getOrDefault(ExifInterface.ORIENTATION_NORMAL)
        val matrix = Matrix()
        when (orientation) {
            ExifInterface.ORIENTATION_ROTATE_90 -> matrix.postRotate(90f)
            ExifInterface.ORIENTATION_ROTATE_180 -> matrix.postRotate(180f)
            ExifInterface.ORIENTATION_ROTATE_270 -> matrix.postRotate(270f)
            ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> matrix.postScale(-1f, 1f)
            ExifInterface.ORIENTATION_FLIP_VERTICAL -> matrix.postScale(1f, -1f)
            ExifInterface.ORIENTATION_TRANSPOSE -> { matrix.postRotate(90f); matrix.postScale(-1f, 1f) }
            ExifInterface.ORIENTATION_TRANSVERSE -> { matrix.postRotate(270f); matrix.postScale(-1f, 1f) }
        }
        if (matrix.isIdentity) return decoded
        val rotated = Bitmap.createBitmap(decoded, 0, 0, decoded.width, decoded.height, matrix, true)
        decoded.recycle()
        return rotated
    }
}
