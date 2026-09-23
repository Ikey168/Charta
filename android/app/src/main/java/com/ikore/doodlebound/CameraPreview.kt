package com.ikore.doodlebound

import android.annotation.SuppressLint
import android.app.Activity
import android.content.Context
import android.graphics.ImageFormat
import android.graphics.SurfaceTexture
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.hardware.camera2.CaptureRequest
import android.hardware.camera2.params.StreamConfigurationMap
import android.media.ImageReader
import android.net.Uri
import android.os.Handler
import android.os.HandlerThread
import android.view.Surface
import android.view.TextureView
import java.io.File
import java.util.UUID

/** Single-purpose Camera2 preview. Permission is checked by DemoUi before construction. */
internal class CameraPreview(
    private val activity: Activity,
    val view: TextureView,
    private val onCaptured: (Uri) -> Unit,
    private val onError: (String) -> Unit
) : TextureView.SurfaceTextureListener {
    private val manager = activity.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    private val thread = HandlerThread("DoodleboundCamera").apply { start() }
    private val worker = Handler(thread.looper)
    private var camera: CameraDevice? = null
    private var session: CameraCaptureSession? = null
    private var reader: ImageReader? = null
    private var previewSurface: Surface? = null
    private var cameraId: String? = null
    @Volatile private var closed = false
    @Volatile private var busy = false

    fun start() {
        view.surfaceTextureListener = this
        if (view.isAvailable) open(view.width, view.height)
    }

    @SuppressLint("MissingPermission") // DemoUi verifies CAMERA grant immediately before start().
    private fun open(width: Int, height: Int) {
        if (closed || camera != null || width == 0 || height == 0) return
        try {
            val id = manager.cameraIdList.firstOrNull {
                manager.getCameraCharacteristics(it).get(CameraCharacteristics.LENS_FACING) == CameraCharacteristics.LENS_FACING_BACK
            } ?: throw IllegalStateException("No rear camera is available.")
            cameraId = id
            val characteristics = manager.getCameraCharacteristics(id)
            val sizes = characteristics.get(CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP)
                ?.getOutputSizes(ImageFormat.JPEG) ?: throw IllegalStateException("This camera cannot capture JPEG photos.")
            val size = sizes.filter { it.width >= 640 && it.height >= 480 && it.width.toLong() * it.height <= 4_000_000 }
                .maxByOrNull { it.width.toLong() * it.height } ?: sizes.minByOrNull { it.width.toLong() * it.height }
                ?: throw IllegalStateException("No supported camera size was found.")
            reader = ImageReader.newInstance(size.width, size.height, ImageFormat.JPEG, 2).apply {
                setOnImageAvailableListener({ source ->
                    val image = source.acquireLatestImage() ?: return@setOnImageAvailableListener
                    try {
                        val bytes = image.planes[0].buffer.let { buffer -> ByteArray(buffer.remaining()).also { buffer.get(it) } }
                        require(bytes.size <= 12 * 1024 * 1024) { "Photo exceeds the 12 MB import limit." }
                        val file = File(activity.cacheDir, "camera-${UUID.randomUUID()}.jpg")
                        file.outputStream().use { it.write(bytes) }
                        activity.runOnUiThread { if (!closed) onCaptured(Uri.fromFile(file)) else file.delete() }
                    } catch (error: Exception) { fail(error.message ?: "Could not save the camera photo.") }
                    finally { image.close() }
                }, worker)
            }
            manager.openCamera(id, object : CameraDevice.StateCallback() {
                override fun onOpened(device: CameraDevice) {
                    if (closed) { device.close(); return }
                    camera = device
                    createPreview(device)
                }
                override fun onDisconnected(device: CameraDevice) { device.close(); camera = null; fail("Camera disconnected. Try the photo picker or drawing tool.") }
                override fun onError(device: CameraDevice, error: Int) { device.close(); camera = null; fail("Camera could not open. Try another source.") }
            }, worker)
        } catch (error: Exception) { fail(error.message ?: "Camera is unavailable.") }
    }

    private fun createPreview(device: CameraDevice) {
        val texture = view.surfaceTexture ?: return fail("Camera preview surface is unavailable.")
        texture.setDefaultBufferSize(1280, 720)
        val surface = Surface(texture)
        previewSurface = surface
        val output = reader?.surface ?: return fail("Camera output is unavailable.")
        try {
            device.createCaptureSession(listOf(surface, output), object : CameraCaptureSession.StateCallback() {
                override fun onConfigured(cameraSession: CameraCaptureSession) {
                    if (closed) { cameraSession.close(); return }
                    session = cameraSession
                    try {
                        val request = device.createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
                            addTarget(surface)
                            set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                        }.build()
                        cameraSession.setRepeatingRequest(request, null, worker)
                    } catch (error: Exception) { fail(error.message ?: "Camera preview failed.") }
                }
                override fun onConfigureFailed(cameraSession: CameraCaptureSession) { cameraSession.close(); fail("Camera preview could not start.") }
            }, worker)
        } catch (error: Exception) { fail(error.message ?: "Camera preview could not start.") }
    }

    fun capture() {
        if (closed || busy) return
        val device = camera ?: return fail("Camera is not ready yet.")
        val cameraSession = session ?: return fail("Camera is not ready yet.")
        val output = reader?.surface ?: return fail("Camera output is unavailable.")
        busy = true
        try {
            val characteristics = manager.getCameraCharacteristics(cameraId ?: error("Camera closed."))
            val sensor = characteristics.get(CameraCharacteristics.SENSOR_ORIENTATION) ?: 0
            val rotation = when (activity.windowManager.defaultDisplay.rotation) {
                Surface.ROTATION_90 -> 90
                Surface.ROTATION_180 -> 180
                Surface.ROTATION_270 -> 270
                else -> 0
            }
            val orientation = (sensor - rotation + 360) % 360
            val request = device.createCaptureRequest(CameraDevice.TEMPLATE_STILL_CAPTURE).apply {
                addTarget(output)
                set(CaptureRequest.CONTROL_AF_MODE, CaptureRequest.CONTROL_AF_MODE_CONTINUOUS_PICTURE)
                set(CaptureRequest.JPEG_ORIENTATION, orientation)
            }.build()
            cameraSession.capture(request, object : CameraCaptureSession.CaptureCallback() {
                override fun onCaptureFailed(session: CameraCaptureSession, request: CaptureRequest, failure: android.hardware.camera2.CaptureFailure) {
                    busy = false; fail("Photo capture failed. Try again.")
                }
            }, worker)
        } catch (error: Exception) { busy = false; fail(error.message ?: "Photo capture failed.") }
    }

    fun close() {
        closed = true
        view.surfaceTextureListener = null
        worker.post {
            session?.close(); session = null
            camera?.close(); camera = null
            reader?.close(); reader = null
            previewSurface?.release(); previewSurface = null
            thread.quitSafely()
        }
    }

    private fun fail(text: String) { activity.runOnUiThread { if (!closed) onError(text) } }

    override fun onSurfaceTextureAvailable(surface: SurfaceTexture, width: Int, height: Int) { open(width, height) }
    override fun onSurfaceTextureSizeChanged(surface: SurfaceTexture, width: Int, height: Int) = Unit
    override fun onSurfaceTextureDestroyed(surface: SurfaceTexture): Boolean { close(); return true }
    override fun onSurfaceTextureUpdated(surface: SurfaceTexture) = Unit
}
