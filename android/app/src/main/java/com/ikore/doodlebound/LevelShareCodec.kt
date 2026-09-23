package com.ikore.doodlebound

import android.util.Base64

/** Kotlin implementation of the frozen DDL1 wire format in src/game/LevelShare.h. */
internal object LevelShareCodec {
    private const val alphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ"
    private const val maxCodeLength = LevelDraft.MAX_JSON_BYTES * 2

    fun encode(json: String): String {
        val bytes = json.toByteArray(Charsets.UTF_8)
        require(bytes.size <= LevelDraft.MAX_JSON_BYTES)
        val payload = Base64.encodeToString(bytes, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
        return "DDL1:${code(bytes)}:$payload"
    }

    fun decode(text: String): String {
        val share = text.trim()
        require(share.length <= maxCodeLength && share.startsWith("DDL1:")) { "Unsupported share code." }
        val parts = share.split(':', limit = 3)
        require(parts.size == 3 && parts[1].matches(Regex("DD-[0-9A-HJKMNP-TV-Z]{13}"))) { "Invalid share code." }
        val payload = parts[2]
        require(payload.isNotEmpty() && payload.length % 4 != 1 && payload.matches(Regex("[A-Za-z0-9_-]+"))) { "Damaged share payload." }
        val bytes = Base64.decode(payload, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING)
        require(bytes.size <= LevelDraft.MAX_JSON_BYTES && Base64.encodeToString(bytes, Base64.URL_SAFE or Base64.NO_WRAP or Base64.NO_PADDING) == payload) { "Damaged share payload." }
        require(code(bytes) == parts[1]) { "The share code does not match its level." }
        return bytes.toString(Charsets.UTF_8)
    }

    private fun code(bytes: ByteArray): String {
        var hash = 1469598103934665603L
        for (byte in bytes) {
            hash = hash xor (byte.toLong() and 0xff)
            hash *= 1099511628211L
        }
        val chars = CharArray(13)
        for (index in 12 downTo 0) {
            chars[index] = alphabet[(hash and 31L).toInt()]
            hash = hash ushr 5
        }
        return "DD-${String(chars)}"
    }
}
