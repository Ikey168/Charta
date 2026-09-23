package com.ikore.doodlebound

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.ToneGenerator

/** Short synthesized event cues; no bundled music or external audio asset is required. */
internal class AudioFeedback(context: Context) {
    private val manager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private var tone: ToneGenerator? = null
    private var focus = false
    private var enabled = true
    private var volume = 60
    private val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK)
        .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build())
        .setOnAudioFocusChangeListener { change ->
            focus = change == AudioManager.AUDIOFOCUS_GAIN || change == AudioManager.AUDIOFOCUS_GAIN_TRANSIENT || change == AudioManager.AUDIOFOCUS_GAIN_TRANSIENT_MAY_DUCK
            if (!focus) tone?.stopTone()
        }.build()

    fun configure(active: Boolean, master: Int, effects: Int) {
        enabled = active
        volume = (master.coerceIn(0, 100) * effects.coerceIn(0, 100) / 100)
        tone?.release()
        tone = if (enabled && volume > 0) runCatching { ToneGenerator(AudioManager.STREAM_MUSIC, volume) }.getOrNull() else null
    }

    fun resume() { if (enabled && volume > 0) focus = manager.requestAudioFocus(request) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED }
    fun pause() { tone?.stopTone(); manager.abandonAudioFocusRequest(request); focus = false }
    fun close() { pause(); tone?.release(); tone = null }
    fun coin() { if (focus) tone?.startTone(ToneGenerator.TONE_PROP_BEEP, 85) }
    fun win() { if (focus) tone?.startTone(ToneGenerator.TONE_PROP_ACK, 250) }
    fun loss() { if (focus) tone?.startTone(ToneGenerator.TONE_PROP_NACK, 220) }
}
