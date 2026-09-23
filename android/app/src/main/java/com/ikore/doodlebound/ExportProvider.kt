package com.ikore.doodlebound

import android.content.ContentProvider
import android.content.ContentValues
import android.database.Cursor
import android.database.MatrixCursor
import android.net.Uri
import android.os.ParcelFileDescriptor
import android.provider.OpenableColumns
import java.io.File
import java.io.FileNotFoundException

/** Read-only URI provider for short-lived image and level exports in private cache. */
class ExportProvider : ContentProvider() {
    override fun onCreate() = true

    private fun file(uri: Uri): File {
        val name = uri.lastPathSegment ?: throw FileNotFoundException()
        if (!name.matches(Regex("[a-zA-Z0-9_-]{1,80}\\.(png|ddl)"))) throw FileNotFoundException()
        val directory = File(requireNotNull(context).cacheDir, "exports")
        return File(directory, name).also { if (!it.isFile) throw FileNotFoundException() }
    }

    override fun getType(uri: Uri) = if (uri.lastPathSegment?.endsWith(".png") == true) "image/png" else "application/vnd.doodlebound.level"
    override fun openFile(uri: Uri, mode: String): ParcelFileDescriptor {
        if (mode != "r") throw FileNotFoundException()
        return ParcelFileDescriptor.open(file(uri), ParcelFileDescriptor.MODE_READ_ONLY)
    }
    override fun query(uri: Uri, projection: Array<out String>?, selection: String?, selectionArgs: Array<out String>?, sortOrder: String?): Cursor {
        val source = file(uri)
        return MatrixCursor(arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE)).apply { addRow(arrayOf(source.name, source.length())) }
    }
    override fun insert(uri: Uri, values: ContentValues?): Uri? = null
    override fun delete(uri: Uri, selection: String?, selectionArgs: Array<out String>?): Int = 0
    override fun update(uri: Uri, values: ContentValues?, selection: String?, selectionArgs: Array<out String>?): Int = 0
}
