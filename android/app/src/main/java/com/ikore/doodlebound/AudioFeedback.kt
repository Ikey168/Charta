package com.ikore.doodlebound

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.media.ToneGenerator
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.sin

/** Original eight-second ambient loop and short event cues, with independent volumes. */
internal class AudioFeedback(context: Context) {
    private val manager = context.getSystemService(Context.AUDIO_SERVICE) as AudioManager
    private var tone: ToneGenerator? = null
    private var musicTrack: AudioTrack? = null
    private var hasFocus = false
    private var active = false
    private var ducked = false
    private var enabled = true
    private var effectsVolume = 48
    private var musicVolume = 21f / 100f
    private val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
        .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
        .setOnAudioFocusChangeListener { change ->
            when (change) {
                AudioManager.AUDIOFOCUS_GAIN -> {
                    hasFocus = true; ducked = false; applyMusicVolume(); startMusic()
                }
                AudioManager.AUDIOFOCUS_LOSS_TRANSIENT_CAN_DUCK -> {
                    ducked = true; applyMusicVolume(); tone?.stopTone()
                }
                else -> {
                    hasFocus = false; tone?.stopTone(); musicTrack?.pause()
                }
            }
        }.build()

    fun configure(soundOn: Boolean, musicOn: Boolean, master: Int, effects: Int, music: Int) {
        enabled = soundOn
        effectsVolume = if (enabled) master.coerceIn(0, 100) * effects.coerceIn(0, 100) / 100 else 0
        musicVolume = if (enabled && musicOn) master.coerceIn(0, 100) * music.coerceIn(0, 100) / 10_000f else 0f
        tone?.release()
        tone = if (effectsVolume > 0) runCatching { ToneGenerator(AudioManager.STREAM_MUSIC, effectsVolume) }.getOrNull() else null
        if (musicVolume > 0f && musicTrack == null) musicTrack = createMusicTrack()
        applyMusicVolume()
        if (active && hasFocus) startMusic() else if (musicVolume == 0f) musicTrack?.pause()
    }

    fun resume() {
        active = true
        if (!enabled || (effectsVolume == 0 && musicVolume == 0f)) return
        hasFocus = manager.requestAudioFocus(request) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
        if (hasFocus) startMusic()
    }

    fun pause() {
        active = false
        tone?.stopTone()
        musicTrack?.pause()
        manager.abandonAudioFocusRequest(request)
        hasFocus = false
        ducked = false
    }

    fun close() {
        pause()
        tone?.release(); tone = null
        musicTrack?.release(); musicTrack = null
    }

    fun coin() { if (active && hasFocus && !ducked) tone?.startTone(ToneGenerator.TONE_PROP_BEEP, 85) }
    fun win() { if (active && hasFocus && !ducked) tone?.startTone(ToneGenerator.TONE_PROP_ACK, 250) }
    fun loss() { if (active && hasFocus && !ducked) tone?.startTone(ToneGenerator.TONE_PROP_NACK, 220) }

    private fun applyMusicVolume() { musicTrack?.setVolume(if (ducked) musicVolume * 0.25f else musicVolume) }
    private fun startMusic() {
        if (active && hasFocus && musicVolume > 0f && musicTrack?.playState != AudioTrack.PLAYSTATE_PLAYING)
            runCatching { musicTrack?.play() }
    }

    private fun createMusicTrack(): AudioTrack? = runCatching {
        val samples = ambientLoop()
        val track = AudioTrack.Builder()
            .setAudioAttributes(AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME)
                .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC).build())
            .setAudioFormat(AudioFormat.Builder().setSampleRate(SAMPLE_RATE)
                .setEncoding(AudioFormat.ENCODING_PCM_16BIT).setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
            .setBufferSizeInBytes(samples.size * 2)
            .setTransferMode(AudioTrack.MODE_STATIC)
            .build()
        check(track.state == AudioTrack.STATE_INITIALIZED)
        check(track.write(samples, 0, samples.size, AudioTrack.WRITE_BLOCKING) == samples.size)
        check(track.setLoopPoints(0, samples.size, -1) == AudioTrack.SUCCESS)
        track
    }.getOrNull()

    companion object {
        private const val SAMPLE_RATE = 22_050

        /** A soft four-chord motif; each chord fades to zero before the next, including the loop seam. */
        internal fun ambientLoop(): ShortArray {
            val chords = arrayOf(
                doubleArrayOf(220.00, 261.63, 329.63), // Am
                doubleArrayOf(174.61, 220.00, 261.63), // F
                doubleArrayOf(261.63, 329.63, 392.00), // C
                doubleArrayOf(196.00, 246.94, 293.66)  // G
            )
            val samples = ShortArray(SAMPLE_RATE * 8)
            for (i in samples.indices) {
                val time = i.toDouble() / SAMPLE_RATE
                val section = (time / 2.0).toInt().coerceAtMost(3)
                val local = time - section * 2.0
                val envelope = sin(PI * local / 2.0).let { it * it }
                var value = 0.0
                for (frequency in chords[section]) {
                    val phase = 2.0 * PI * frequency * local
                    value += (sin(phase) * 0.075 + sin(phase * 2.0) * 0.012) * envelope
                }
                val globalFade = (1.0 - cos(2.0 * PI * time / 8.0)) * 0.5
                value += sin(2.0 * PI * 110.0 * time) * 0.045 * globalFade
                samples[i] = (value.coerceIn(-0.9, 0.9) * Short.MAX_VALUE).toInt().toShort()
            }
            return samples
        }
    }
}
