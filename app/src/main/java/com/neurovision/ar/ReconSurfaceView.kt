package com.neurovision.ar

import android.content.Context
import android.graphics.Bitmap
import android.hardware.display.DisplayManager
import android.opengl.GLSurfaceView
import android.util.AttributeSet
import android.util.Log
import android.view.Display
import android.view.Surface
import com.neurovision.recon.NeuroMetrics
import com.neurovision.recon.ReconAnchor
import com.neurovision.recon.ReconMetrics
import com.neurovision.recon.RenderMode
import com.google.ar.core.Config
import com.google.ar.core.Session
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Hosts the ARCore session and the native GL renderer. The session is created
 * on the UI thread (ARCore requirement) while update/submit/render happen on
 * the GLSurfaceView thread inside [ReconGlRenderer].
 */
class ReconSurfaceView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null
) : GLSurfaceView(context, attrs) {

    var onError: ((String) -> Unit)? = null
    var onAnchorFailure: ((String) -> Unit)? = null

    private val glRenderer = ReconGlRenderer()
    private var session: Session? = null
    private var sessionResumed = false
    private var scanning = true
    private var overlayMode = RenderMode.AR_OVERLAY

    private val depthPixels = IntArray(PREVIEW_W * PREVIEW_H)
    private val confidencePixels = IntArray(PREVIEW_W * PREVIEW_H)
    private val rotatedDepthPixels = IntArray(PREVIEW_W * PREVIEW_H)
    private val rotatedConfidencePixels = IntArray(PREVIEW_W * PREVIEW_H)
    private val eventPixels = IntArray(EVENT_W * EVENT_H)
    private val rotatedEventPixels = IntArray(EVENT_W * EVENT_H)
    private val neuroFloats = FloatArray(12)
    private val neuroInts = IntArray(4)
    private val trackBuffer = FloatArray(96 * 4)

    init {
        setEGLContextClientVersion(3)
        setEGLConfigChooser(8, 8, 8, 8, 16, 0)
        preserveEGLContextOnPause = true
        setRenderer(glRenderer)
        renderMode = RENDERMODE_CONTINUOUSLY
        glRenderer.onError = { message -> post { onError?.invoke(message) } }
        glRenderer.onAnchorFailure = { message -> post { onAnchorFailure?.invoke(message) } }
    }

    fun onResumeSession() {
        super.onResume()
        glRenderer.displayRotation = currentDisplayRotation()

        val existing = session
        if (existing != null) {
            try {
                if (!sessionResumed) {
                    existing.resume()
                    sessionResumed = true
                }
                glRenderer.session = existing
                return
            } catch (e: Exception) {
                Log.w(TAG, "ARCore resume failed", e)
                closeSession()
            }
        }

        try {
            val created = Session(context.applicationContext)
            val config = Config(created).apply {
                updateMode = Config.UpdateMode.LATEST_CAMERA_IMAGE
                focusMode = Config.FocusMode.AUTO
                // Plane finding (RoomAR-style) gives the anchor placement a
                // surface to hit-test against instead of dead-reckoning a point
                // in front of the camera.
                planeFindingMode = Config.PlaneFindingMode.HORIZONTAL_AND_VERTICAL
                // Real per-frame depth from ARCore's motion stereo when the
                // device supports it; the native engine falls back to its own
                // multi-view stereo otherwise.
                depthMode = if (created.isDepthModeSupported(Config.DepthMode.AUTOMATIC)) {
                    Config.DepthMode.AUTOMATIC
                } else {
                    Config.DepthMode.DISABLED
                }
                lightEstimationMode = Config.LightEstimationMode.DISABLED
            }
            try {
                created.configure(config)
            } catch (e: Exception) {
                config.focusMode = Config.FocusMode.FIXED
                created.configure(config)
            }
            created.resume()
            session = created
            sessionResumed = true
            glRenderer.depthApiEnabled = created.isDepthModeSupported(Config.DepthMode.AUTOMATIC)
            glRenderer.session = created
        } catch (e: Exception) {
            Log.e(TAG, "ARCore session creation failed", e)
            onError?.invoke("ARCore is unavailable on this device: ${e.message}")
        }
    }

    fun onPauseSession() {
        // Stop the GL thread first so no session.update() can race the pause.
        super.onPause()
        glRenderer.session = null
        if (sessionResumed) {
            try {
                session?.pause()
            } catch (e: Exception) {
                Log.w(TAG, "ARCore pause failed", e)
            }
            sessionResumed = false
        }
    }

    fun release() {
        super.onPause()
        closeSession()
        glRenderer.release()
    }

    fun setScanning(value: Boolean) {
        scanning = value
        glRenderer.setScanning(value)
    }

    fun isScanning(): Boolean = scanning

    fun setRenderMode(mode: RenderMode) {
        overlayMode = mode
        glRenderer.setRenderMode(mode)
    }

    fun currentRenderMode(): RenderMode = overlayMode

    fun addAnchor() {
        glRenderer.requestAnchor()
    }

    fun removeAnchor(id: Long) {
        glRenderer.removeAnchor(id)
    }

    fun resetReconstruction() {
        glRenderer.resetReconstruction()
    }

    fun setWorldView(active: Boolean, points: Boolean) {
        glRenderer.setWorldView(active, points)
    }

    fun orbitBy(dYaw: Float, dPitch: Float) {
        glRenderer.orbitBy(dYaw, dPitch)
    }

    fun zoomBy(factor: Float) {
        glRenderer.zoomBy(factor)
    }

    fun resetOrbit() {
        glRenderer.resetOrbit()
    }

    fun setEventThreshold(value: Float) {
        glRenderer.setEventThreshold(value)
    }

    fun setEventDecay(value: Float) {
        glRenderer.setEventDecay(value)
    }

    fun anchors(): List<ReconAnchor> = glRenderer.anchors()

    fun drainDiagnostics(): String = glRenderer.drainDiagnostics()

    fun depthApiSupported(): Boolean = glRenderer.depthApiEnabled

    fun isTracking(): Boolean = glRenderer.tracking

    fun snapshotMetrics(): ReconMetrics {
        val (floats, ints) = glRenderer.snapshotMetrics()
        val hasNative = glRenderer.nativeHandle != 0L

        var depthBitmap: Bitmap? = null
        var confidenceBitmap: Bitmap? = null
        var neuro = NeuroMetrics()
        if (hasNative) {
            glRenderer.fetchPreview(depthPixels, confidencePixels)
            // Same 90° CW display rotation as the event map: the depth grid is
            // in sensor orientation, the app is portrait.
            rotatePreview(depthPixels, rotatedDepthPixels, PREVIEW_W, PREVIEW_H)
            rotatePreview(confidencePixels, rotatedConfidencePixels, PREVIEW_W, PREVIEW_H)
            depthBitmap = Bitmap.createBitmap(
                rotatedDepthPixels, PREVIEW_H, PREVIEW_W, Bitmap.Config.ARGB_8888
            )
            confidenceBitmap = Bitmap.createBitmap(
                rotatedConfidencePixels, PREVIEW_H, PREVIEW_W, Bitmap.Config.ARGB_8888
            )

            glRenderer.neuroStats(neuroFloats, neuroInts)
            glRenderer.fetchEventPreview(eventPixels)
            val trackCount = glRenderer.fetchMotionTracks(trackBuffer)
            // The CPU camera image is in sensor orientation (landscape), while
            // the app is locked to portrait. The event map is rotated 90° CW
            // for display, and the flow vector is rotated with it, so the
            // direction label matches what the user sees. This is a display
            // transform, not new data.
            val displayFlowDx = -neuroFloats[7]
            val displayFlowDy = neuroFloats[6]
            rotateEventPreview(eventPixels, rotatedEventPixels)
            neuro = NeuroMetrics(
                eventRate = neuroFloats[0],
                eventDensity = neuroFloats[1],
                positiveShare = neuroFloats[2],
                meanMagnitude = neuroFloats[3],
                eventThreshold = neuroFloats[4],
                eventProcessMs = neuroFloats[5],
                flowDx = displayFlowDx,
                flowDy = displayFlowDy,
                flowSpeedPxPerS = neuroFloats[8],
                sceneChange = neuroFloats[9],
                trackedShare = neuroFloats[10],
                motionProcessMs = neuroFloats[11],
                posCount = neuroInts[0],
                negCount = neuroInts[1],
                featureCount = neuroInts[2],
                motionDirection = displayDirection(displayFlowDx, displayFlowDy),
                eventPreview = Bitmap.createBitmap(
                    rotatedEventPixels, EVENT_H, EVENT_W, Bitmap.Config.ARGB_8888
                ),
                motionTracks = trackBuffer.take(trackCount * 4),
                trackToNdc = glRenderer.trackTransform()
            )
        }

        return ReconMetrics(
            fps = floats[1],
            processMs = floats[0],
            triangles = ints[1],
            vertices = ints[2],
            keyframes = ints[0],
            blocks = ints[3],
            depthSource = ints[5],
            framesProcessed = ints[4],
            phase = ints[6],
            depthValidShare = (ints.getOrNull(7) ?: 0) / 1000f,
            tracking = glRenderer.tracking,
            scanning = scanning,
            depthPreview = depthBitmap,
            confidencePreview = confidenceBitmap,
            neuro = neuro
        )
    }

    fun exportMesh(format: String): File? {
        val directory = File(context.getExternalFilesDir(null) ?: context.filesDir, "exports")
        if (!directory.exists() && !directory.mkdirs()) return null
        val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
        val file = File(directory, "scan_$stamp.${format.lowercase(Locale.US)}")
        return if (glRenderer.exportMesh(file.absolutePath, format)) file else null
    }

    fun hasGeometry(): Boolean = glRenderer.hasGeometry()

    override fun onDetachedFromWindow() {
        super.onDetachedFromWindow()
        release()
    }

    private fun closeSession() {
        val active = session ?: return
        try {
            if (sessionResumed) active.pause()
            active.close()
        } catch (e: Exception) {
            Log.w(TAG, "ARCore close failed", e)
        }
        session = null
        sessionResumed = false
        glRenderer.session = null
    }

    private fun currentDisplayRotation(): Int {
        val manager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        return when (manager.getDisplay(Display.DEFAULT_DISPLAY)?.rotation) {
            Surface.ROTATION_90 -> 1
            Surface.ROTATION_180 -> 2
            Surface.ROTATION_270 -> 3
            else -> 0
        }
    }

    /** Rotates a sensor-orientation image 90° CW for the portrait display. */
    private fun rotatePreview(src: IntArray, dst: IntArray, w: Int, h: Int) {
        for (sy in 0 until h) {
            val dx = h - 1 - sy
            val row = sy * w
            for (sx in 0 until w) {
                dst[sx * h + dx] = src[row + sx]
            }
        }
    }

    private fun rotateEventPreview(src: IntArray, dst: IntArray) {
        rotatePreview(src, dst, EVENT_W, EVENT_H)
    }

    private fun displayDirection(dx: Float, dy: Float): Int {
        if (kotlin.math.abs(dx) < 1.2f && kotlin.math.abs(dy) < 1.2f) return 0
        return if (kotlin.math.abs(dx) >= kotlin.math.abs(dy)) {
            if (dx > 0) 1 else 2
        } else {
            if (dy > 0) 3 else 4
        }
    }

    companion object {
        private const val TAG = "ReconSurface"
        const val PREVIEW_W = 128
        const val PREVIEW_H = 96
        // Must match nv::EventEngine::kWidth / kHeight.
        const val EVENT_W = 320
        const val EVENT_H = 240
    }
}
