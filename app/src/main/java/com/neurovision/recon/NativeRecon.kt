package com.neurovision.recon

import java.nio.ByteBuffer

/**
 * JNI surface of the native reconstruction core (librecon.so).
 *
 * Every function is a thin, allocation-free bridge into the C++ engine; the
 * handle is created once per GL context and owned by the GL renderer.
 */
object NativeRecon {

    init {
        System.loadLibrary("recon")
    }

    external fun create(): Long
    external fun destroy(handle: Long)

    external fun glInit(handle: Long): Boolean
    external fun glShutdown(handle: Long)

    external fun submitFrame(
        handle: Long,
        y: ByteBuffer, yRowStride: Int, yPixelStride: Int,
        u: ByteBuffer, v: ByteBuffer, uvRowStride: Int, uvPixelStride: Int,
        width: Int, height: Int, timestampNs: Long,
        qx: Float, qy: Float, qz: Float, qw: Float,
        tx: Float, ty: Float, tz: Float,
        fx: Float, fy: Float, cx: Float, cy: Float,
        tracking: Boolean,
        depth: ByteBuffer?, depthRowStride: Int, depthWidth: Int, depthHeight: Int,
        depthTimestampNs: Long, depthApiActive: Boolean,
        confidence: ByteBuffer?, confidenceRowStride: Int, confidencePixelStride: Int,
        confidenceWidth: Int, confidenceHeight: Int,
        depthInfo: FloatArray,
        smoothDepth: ByteBuffer?, smoothRowStride: Int,
        smoothWidth: Int, smoothHeight: Int
    )

    external fun render(
        handle: Long,
        cameraTexture: Int,
        viewMatrix: FloatArray,
        projectionMatrix: FloatArray,
        backgroundUv: FloatArray,
        width: Int,
        height: Int,
        mode: Int,
        anchors: FloatArray
    ): Boolean

    external fun getStats(handle: Long, floats: FloatArray, ints: IntArray)
    external fun setScanning(handle: Long, scanning: Boolean)
    external fun reset(handle: Long)
    external fun exportMesh(handle: Long, path: String, format: String): Boolean
    external fun depthPreview(handle: Long, depthOut: IntArray, confidenceOut: IntArray)
    external fun hasMesh(handle: Long): Boolean
    external fun getDiagnostics(handle: Long): String

    // Stage 1/2 perception. The native engines run on the reconstruction
    // worker thread; these calls only read snapshots.
    external fun neuroStats(handle: Long, floats: FloatArray, ints: IntArray)
    external fun eventPreview(handle: Long, out: IntArray)
    external fun setEventThreshold(handle: Long, threshold: Float)
    external fun setEventDecay(handle: Long, decay: Float)
    external fun resetNeuro(handle: Long)
    external fun motionTracks(handle: Long, out: FloatArray): Int

    // Free orbit camera for the 3D world screen. Bounds are in ARCore world
    // metres; the native side recomputes them only when the mesh changes.
    external fun meshBounds(handle: Long, out: FloatArray): Boolean
    external fun orbitView(
        handle: Long,
        yaw: Float,
        pitch: Float,
        zoom: Float,
        aspect: Float,
        viewMatrix: FloatArray,
        projectionMatrix: FloatArray
    ): Boolean

    /**
     * Runs the deterministic synthetic-scene mesh test in native code and
     * returns its multi-line report. Static in the native sense: it builds its
     * own reconstruction instances, so no handle is needed and the live scan is
     * untouched. Call it from a background thread.
     */
    external fun runMeshSelfTest(): String

    /**
     * Deterministic synthetic check of the software event engine and the
     * pyramidal Lucas-Kanade flow. Needs no camera and no ARCore.
     */
    external fun runPerceptionSelfTest(): String
}
