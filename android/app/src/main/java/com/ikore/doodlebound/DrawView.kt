package com.ikore.doodlebound

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.view.MotionEvent
import android.view.View
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/** Finger editor shared by Draw and Review. The scene data remains independent of screen density. */
internal class DrawView(context: Context, var draft: LevelDraft, private val onChange: () -> Unit) : View(context) {
    enum class Tool(val label: String, val symbol: String? = null, val ink: Int = Color.DKGRAY) {
        WALL("Wall"), START("Start", "player", Color.rgb(30, 180, 30)),
        EXIT("Exit", "exit", Color.rgb(40, 80, 220)), COIN("Coin", "coin", Color.rgb(220, 180, 30)),
        ENEMY("Enemy", "enemy", Color.rgb(220, 40, 40)), MOVE("Move"), ERASE("Erase")
    }

    var tool = Tool.WALL
    private val paint = Paint(Paint.ANTI_ALIAS_FLAG)
    private val undo = ArrayDeque<LevelDraft>()
    private val redo = ArrayDeque<LevelDraft>()
    private var activeWall: MutableList<Point2>? = null
    private var activeMark = -1
    private var activePointer = -1
    private var zoom = 1f
    private var panX = 0f
    private var panY = 0f
    private var lastX = 0f
    private var lastY = 0f
    private var pinchDistance = 0f
    private var wasGesture = false
    private var sceneMinX = 0f
    private var sceneMinZ = 0f
    private var unitsX = 24f
    private var unitsY = 16f

    private fun updateBounds() {
        val xs = draft.walls.flatten().map { it.x } + draft.marks.map { it.x }
        val zs = draft.walls.flatten().map { it.z } + draft.marks.map { it.z }
        sceneMinX = min(0f, (xs.minOrNull() ?: 0f) - 1f)
        sceneMinZ = min(0f, (zs.minOrNull() ?: 0f) - 1f)
        unitsX = max(24f, (xs.maxOrNull() ?: 24f) - sceneMinX + 1f)
        unitsY = max(16f, (zs.maxOrNull() ?: 16f) - sceneMinZ + 1f)
    }

    fun canUndo() = undo.isNotEmpty()
    fun canRedo() = redo.isNotEmpty()
    fun undo() {
        if (undo.isEmpty()) return
        redo.addLast(draft.copyDraft())
        draft = undo.removeLast()
        invalidate(); onChange()
    }
    fun redo() {
        if (redo.isEmpty()) return
        undo.addLast(draft.copyDraft())
        draft = redo.removeLast()
        invalidate(); onChange()
    }
    fun clear() { checkpoint(); draft.walls.clear(); draft.marks.clear(); invalidate(); onChange() }
    fun replace(other: LevelDraft) { checkpoint(); draft = other; invalidate(); onChange() }

    private fun checkpoint() {
        undo.addLast(draft.copyDraft())
        if (undo.size > 40) undo.removeFirst()
        redo.clear()
    }

    private fun scale() = min(width / unitsX, height / unitsY) * zoom
    private fun originX() = (width - unitsX * scale()) / 2f + panX
    private fun originY() = (height - unitsY * scale()) / 2f + panY
    private fun sceneX(x: Float) = (sceneMinX + (x - originX()) / scale()).coerceIn(sceneMinX, sceneMinX + unitsX)
    private fun sceneZ(y: Float) = (sceneMinZ + (y - originY()) / scale()).coerceIn(sceneMinZ, sceneMinZ + unitsY)
    private fun screenX(x: Float) = originX() + (x - sceneMinX) * scale()
    private fun screenY(z: Float) = originY() + (z - sceneMinZ) * scale()

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        updateBounds()
        canvas.drawColor(Color.rgb(244, 240, 225))
        paint.style = Paint.Style.STROKE
        paint.strokeWidth = resources.displayMetrics.density
        paint.color = Color.rgb(212, 205, 185)
        for (i in sceneMinX.toInt()..(sceneMinX + unitsX).toInt()) canvas.drawLine(screenX(i.toFloat()), screenY(sceneMinZ), screenX(i.toFloat()), screenY(sceneMinZ + unitsY), paint)
        for (i in sceneMinZ.toInt()..(sceneMinZ + unitsY).toInt()) canvas.drawLine(screenX(sceneMinX), screenY(i.toFloat()), screenX(sceneMinX + unitsX), screenY(i.toFloat()), paint)
        paint.color = Color.rgb(33, 38, 43)
        paint.strokeWidth = max(5f, scale() * 0.2f)
        paint.strokeCap = Paint.Cap.ROUND
        paint.strokeJoin = Paint.Join.ROUND
        draft.walls.forEach { wall ->
            for (i in 1 until wall.size) canvas.drawLine(screenX(wall[i - 1].x), screenY(wall[i - 1].z), screenX(wall[i].x), screenY(wall[i].z), paint)
        }
        paint.style = Paint.Style.FILL
        draft.marks.forEach { mark ->
            val color = Tool.entries.firstOrNull { it.symbol == mark.type }?.ink ?: when {
                mark.type.startsWith("enemy") -> Tool.ENEMY.ink
                mark.type in setOf("door", "lockeddoor", "switch") -> Tool.EXIT.ink
                mark.type in setOf("key", "treasure") -> Tool.COIN.ink
                mark.type in setOf("toggle", "hazard") -> Tool.START.ink
                else -> Color.MAGENTA
            }
            paint.color = color
            val x = screenX(mark.x); val y = screenY(mark.z)
            canvas.drawCircle(x, y, max(12f, scale() * 0.4f), paint)
            paint.color = Color.BLACK
            paint.textAlign = Paint.Align.CENTER
            paint.textSize = max(13f, scale() * 0.42f)
            canvas.drawText(when (mark.type) { "player" -> "S"; "exit" -> "E"; "coin" -> "$"; "enemy" -> "!"; else -> mark.type.take(1).uppercase() }, x, y + paint.textSize * 0.35f, paint)
        }
    }

    override fun onTouchEvent(event: MotionEvent): Boolean {
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN -> {
                parent?.requestDisallowInterceptTouchEvent(true)
                activePointer = event.getPointerId(0)
                lastX = event.x; lastY = event.y
                wasGesture = false
                if (tool == Tool.WALL) {
                    if (draft.walls.size >= LevelDraft.MAX_WALLS) return true
                    checkpoint()
                    activeWall = mutableListOf(Point2(sceneX(event.x), sceneZ(event.y)))
                    draft.walls.add(activeWall!!)
                    invalidate()
                } else if (tool == Tool.MOVE) {
                    val p = Point2(sceneX(event.x), sceneZ(event.y))
                    val nearest = draft.marks.indices.minByOrNull { hypot(draft.marks[it].x - p.x, draft.marks[it].z - p.z) }
                    if (nearest != null && hypot(draft.marks[nearest].x - p.x, draft.marks[nearest].z - p.z) < 1.2f) {
                        checkpoint(); activeMark = nearest
                    }
                }
                return true
            }
            MotionEvent.ACTION_POINTER_DOWN -> {
                wasGesture = true
                activeWall?.let { draft.walls.remove(it); undo.removeLastOrNull(); activeWall = null }
                if (activeMark >= 0) { draft = undo.removeLast(); activeMark = -1; invalidate() }
                pinchDistance = if (event.pointerCount >= 2) hypot(event.getX(1) - event.getX(0), event.getY(1) - event.getY(0)) else 0f
                return true
            }
            MotionEvent.ACTION_MOVE -> {
                if (event.pointerCount >= 2) {
                    val d = hypot(event.getX(1) - event.getX(0), event.getY(1) - event.getY(0))
                    if (pinchDistance > 0f) zoom = (zoom * d / pinchDistance).coerceIn(0.75f, 3f)
                    pinchDistance = d
                    panX += event.getX(0) - lastX; panY += event.getY(0) - lastY
                    lastX = event.getX(0); lastY = event.getY(0)
                    invalidate()
                } else if (!wasGesture && activeWall != null) {
                    val index = event.findPointerIndex(activePointer)
                    if (index >= 0) {
                        val p = Point2(sceneX(event.getX(index)), sceneZ(event.getY(index)))
                        val wall = activeWall!!
                        if (wall.size < 256 && hypot(p.x - wall.last().x, p.z - wall.last().z) > 0.2f) { wall.add(p); invalidate() }
                    }
                } else if (!wasGesture && activeMark >= 0) {
                    val index = event.findPointerIndex(activePointer)
                    if (index >= 0) {
                        val old = draft.marks[activeMark]
                        draft.marks[activeMark] = Mark(old.type, sceneX(event.getX(index)), sceneZ(event.getY(index)))
                        invalidate()
                    }
                }
                return true
            }
            MotionEvent.ACTION_CANCEL -> {
                activeWall?.let { draft.walls.remove(it); undo.removeLastOrNull() }
                if (activeMark >= 0) { draft = undo.removeLast(); activeMark = -1 }
                activeWall = null; activePointer = -1; wasGesture = false; invalidate()
                return true
            }
            MotionEvent.ACTION_UP -> {
                if (!wasGesture) {
                    val p = Point2(sceneX(event.x), sceneZ(event.y))
                    val wall = activeWall
                    if (wall != null) {
                        if (wall.size == 1 && hypot(event.x - lastX, event.y - lastY) > 2f) wall.add(p)
                        if (wall.size < 2) { draft.walls.remove(wall); undo.removeLastOrNull() }
                        else onChange()
                    } else if (activeMark >= 0) {
                        val old = draft.marks[activeMark]
                        draft.marks[activeMark] = Mark(old.type, p.x, p.z)
                        onChange()
                    } else if (tool == Tool.ERASE) {
                        val closest = draft.marks.minByOrNull { hypot(it.x - p.x, it.z - p.z) }
                        if (closest != null && hypot(closest.x - p.x, closest.z - p.z) < 0.8f) {
                            checkpoint(); draft.marks.remove(closest); onChange()
                        } else {
                            val nearWall = draft.walls.firstOrNull { points -> points.any { hypot(it.x - p.x, it.z - p.z) < 0.6f } }
                            if (nearWall != null) { checkpoint(); draft.walls.remove(nearWall); onChange() }
                        }
                    } else if (tool.symbol != null) {
                        if (draft.marks.size >= LevelDraft.MAX_MARKS) return true
                        checkpoint()
                        if (tool == Tool.START || tool == Tool.EXIT) draft.marks.removeAll { it.type == tool.symbol }
                        draft.marks.add(Mark(tool.symbol!!, p.x, p.z)); onChange()
                    }
                }
                activeWall = null; activeMark = -1; activePointer = -1; wasGesture = false; invalidate()
                return true
            }
            MotionEvent.ACTION_POINTER_UP -> { wasGesture = true; return true }
        }
        return true
    }
}
