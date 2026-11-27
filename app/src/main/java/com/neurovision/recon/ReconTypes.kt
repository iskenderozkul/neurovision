package com.neurovision.recon

import android.graphics.Bitmap

/**
 * Render modes handed to the native overlay shader.
 * [nativeId] must stay in sync with GLRenderer::render in gl/renderer.cpp.
 *
 * 0 AR overlay, 1 wireframe, 2 normals, 3 depth heatmap, 4 world point cloud,
 * 5 world mesh. Modes 4/5 are drawn by the free orbit camera without the
 * camera background.
 */
enum class RenderMode(val title: String, val nativeId: Int) {
    AR_OVERLAY("AR", 0),
    WIREFRAME("Wire", 1),
    NORMALS("Normal", 2),
    DEPTH_HEATMAP("Depth", 3);

    fun next(): RenderMode = values()[(ordinal + 1) % values().size]
}

/** World-locked AR anchor as exposed to the UI. */
data class ReconAnchor(
    val id: Long,
    val label: String,
    val x: Float,
    val y: Float,
    val z: Float
)

/** Bottom navigation of the app. */
enum class NeuroScreen(val title: String) {
    LIVE("Live"),
    EVENTS("Event"),
    DEPTH("Depth"),
    WORLD("3D"),
    DIAG("Diag")
}

/**
 * Stage 1/2 perception snapshot. Every number is measured on the native
 * worker: event counts and rate from the software DVS, flow from pyramidal
 * Lucas-Kanade on the camera luma. Nothing here is synthesised.
 */
data class NeuroMetrics(
    val eventRate: Float = 0f,
    val eventDensity: Float = 0f,
    val positiveShare: Float = 0.5f,
    val meanMagnitude: Float = 0f,
    val eventThreshold: Float = 0.2f,
    val eventProcessMs: Float = 0f,
    val posCount: Int = 0,
    val negCount: Int = 0,
    // 0 static, 1 right, 2 left, 3 down, 4 up (image space).
    val motionDirection: Int = 0,
    val flowDx: Float = 0f,
    val flowDy: Float = 0f,
    val flowSpeedPxPerS: Float = 0f,
    val sceneChange: Float = 0f,
    val featureCount: Int = 0,
    val trackedShare: Float = 0f,
    val motionProcessMs: Float = 0f,
    val eventPreview: Bitmap? = null,
    // Quadruples (x0,y0,x1,y1) in the 320x240 event/flow grid.
    val motionTracks: List<Float> = emptyList(),
    // 320x240 grid -> screen NDC affine measured by ARCore, or null.
    val trackToNdc: FloatArray? = null
) {
    val directionLabel: String
        get() = when (motionDirection) {
            1 -> "RIGHT"
            2 -> "LEFT"
            3 -> "DOWN"
            4 -> "UP"
            else -> "STATIC"
        }
}

/**
 * Live pipeline numbers pulled from the native engine. Everything here is
 * measured: frame rate and processing time from the worker thread, geometry
 * counts from the incremental Marching Cubes snapshot, previews from the actual
 * stereo depth and confidence maps.
 */
data class ReconMetrics(
    val fps: Float = 0f,
    val processMs: Float = 0f,
    val triangles: Int = 0,
    val vertices: Int = 0,
    val keyframes: Int = 0,
    val blocks: Int = 0,
    // 0 = none yet, 1 = ARCore Depth API, 2 = native MVS fallback.
    val depthSource: Int = 0,
    // Frames the worker has processed; a value that stops advancing while
    // tracking is on identifies a stalled native stage together with [phase].
    val framesProcessed: Int = 0,
    // 0 idle, 1 prepare, 2 fuse, 3 feed, 4 integrate, 5 mesh, 6 remesh.
    val phase: Int = 0,
    // Share of the native depth grid that carried a valid sample, 0..1.
    val depthValidShare: Float = 0f,
    val tracking: Boolean = false,
    val scanning: Boolean = false,
    val depthPreview: Bitmap? = null,
    val confidencePreview: Bitmap? = null,
    val neuro: NeuroMetrics = NeuroMetrics()
) {
    val depthSourceLabel: String
        get() = when (depthSource) {
            1 -> "ARCore Depth"
            2 -> "MVS Depth"
            else -> "No depth"
        }
}
