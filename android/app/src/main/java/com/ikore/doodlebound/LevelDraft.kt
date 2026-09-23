package com.ikore.doodlebound

import org.json.JSONArray
import org.json.JSONObject
import java.util.UUID
import kotlin.math.abs

/** Editable, bounded subset of the portable doodle-level format. Coordinates are grid units. */
internal data class Point2(val x: Float, val z: Float)
internal data class Mark(val type: String, val x: Float, val z: Float)

internal class LevelDraft(
    var id: String = UUID.randomUUID().toString(),
    var name: String = "My dungeon",
    val walls: MutableList<MutableList<Point2>> = mutableListOf(),
    val marks: MutableList<Mark> = mutableListOf(),
    var sourcePhoto: String? = null
) {
    fun copyDraft(): LevelDraft = LevelDraft(id, name,
        walls.map { it.toMutableList() }.toMutableList(), marks.toMutableList(), sourcePhoto)

    fun issues(): List<String> = buildList {
        if (walls.none { it.size >= 2 }) add("Draw at least one wall.")
        if (marks.count { it.type == "player" } != 1) add("Place exactly one green start.")
        if (marks.count { it.type == "exit" } != 1) add("Place exactly one blue exit.")
        if (marks.any { it.type == "unknown" }) add("Erase or replace unrecognized marks.")
    }

    fun toJson(): String {
        val out = JSONObject()
        out.put("format", "doodle-level")
        out.put("version", 1)
        out.put("wallHeight", 3.0)
        out.put("wallThickness", 0.2)
        val wallArray = JSONArray()
        walls.filter { it.size >= 2 }.forEach { wall ->
            val polyline = JSONArray()
            wall.forEach { point -> polyline.put(JSONArray().put(point.x.toDouble()).put(point.z.toDouble())) }
            wallArray.put(JSONObject().put("polyline", polyline))
        }
        out.put("walls", wallArray)
        val symbols = JSONArray()
        marks.forEach { mark ->
            symbols.put(JSONObject().put("type", mark.type).put("x", mark.x.toDouble())
                .put("z", mark.z.toDouble()).put("yaw", 0))
        }
        out.put("symbols", symbols)
        return out.toString()
    }

    companion object {
        const val MAX_JSON_BYTES = 256 * 1024
        const val MAX_WALLS = 256
        const val MAX_POINTS = 4096
        const val MAX_MARKS = 256
        private val TYPES = setOf("player", "exit", "coin", "enemy", "enemy_flee", "enemy_ranged", "enemy_patrol",
            "key", "door", "lockeddoor", "switch", "toggle", "hazard", "block", "treasure", "unknown")

        fun fromJson(text: String): LevelDraft {
            require(text.toByteArray(Charsets.UTF_8).size <= MAX_JSON_BYTES) { "Level file is too large." }
            val root = JSONObject(text)
            require(root.optString("format") == "doodle-level") { "This is not a Doodlebound level." }
            require(root.optInt("version", -1) == 1) { "Unsupported level version." }
            val wallArray = root.getJSONArray("walls")
            val symbols = root.getJSONArray("symbols")
            require(wallArray.length() <= MAX_WALLS && symbols.length() <= MAX_MARKS) { "Level has too much geometry." }
            val draft = LevelDraft()
            var totalPoints = 0
            for (i in 0 until wallArray.length()) {
                val poly = wallArray.getJSONObject(i).getJSONArray("polyline")
                totalPoints += poly.length()
                require(totalPoints <= MAX_POINTS) { "Level has too many wall points." }
                val wall = mutableListOf<Point2>()
                for (j in 0 until poly.length()) {
                    val pair = poly.getJSONArray(j)
                    wall.add(Point2(bound(pair.getDouble(0)), bound(pair.getDouble(1))))
                }
                if (wall.size >= 2) draft.walls.add(wall)
            }
            for (i in 0 until symbols.length()) {
                val symbol = symbols.getJSONObject(i)
                val type = symbol.getString("type")
                require(type in TYPES) { "Unsupported symbol: $type" }
                draft.marks.add(Mark(type, bound(symbol.getDouble("x")), bound(symbol.getDouble("z"))))
            }
            return draft
        }

        private fun bound(value: Double): Float {
            require(value.isFinite() && abs(value) <= 256.0) { "Level coordinates are out of bounds." }
            return value.toFloat()
        }
    }
}
