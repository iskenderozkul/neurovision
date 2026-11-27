package com.neurovision.ui

import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import com.neurovision.ar.ReconSurfaceView
import com.neurovision.recon.NeuroScreen
import com.neurovision.recon.ReconMetrics
import com.neurovision.recon.RenderMode
import com.neurovision.recon.ScanLog
import com.neurovision.ui.components.DepthPanel
import com.neurovision.ui.components.DiagnosticsPanel
import com.neurovision.ui.components.EventPanel
import com.neurovision.ui.components.ExportMeshSheet
import com.neurovision.ui.components.ReconControlBar
import com.neurovision.ui.components.WorldOverlay
import com.neurovision.ui.theme.DarkSurface
import com.neurovision.ui.theme.RoseError
import kotlinx.coroutines.delay
import java.util.Locale

private fun phaseName(phase: Int): String = when (phase) {
    1 -> "prepare"
    2 -> "fuse"
    3 -> "feed"
    4 -> "integrate"
    5 -> "mesh"
    6 -> "remesh"
    else -> "idle"
}

private fun f1(v: Float): String = String.format(Locale.US, "%.1f", v)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ReconScreen(scanLog: ScanLog) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current

    val surfaceView = remember(context) { ReconSurfaceView(context) }

    var metrics by remember { mutableStateOf(ReconMetrics(scanning = true)) }
    var screen by remember { mutableStateOf(NeuroScreen.LIVE) }
    var worldPoints by remember { mutableStateOf(false) }
    var eventThreshold by remember { mutableFloatStateOf(0.20f) }
    var eventDecay by remember { mutableFloatStateOf(0.86f) }
    var showExportSheet by remember { mutableStateOf(false) }
    var errorMessage by remember { mutableStateOf<String?>(null) }

    // The 3D screen swaps the AR camera for a free orbit camera. ARCore keeps
    // tracking and the reconstruction worker keeps integrating while it is open.
    LaunchedEffect(screen, worldPoints) {
        surfaceView.setWorldView(screen == NeuroScreen.WORLD, worldPoints)
        surfaceView.setRenderMode(RenderMode.AR_OVERLAY)
    }

    val exportSheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)

    DisposableEffect(lifecycleOwner, surfaceView) {
        val observer = LifecycleEventObserver { _, event ->
            when (event) {
                Lifecycle.Event.ON_RESUME -> surfaceView.onResumeSession()
                Lifecycle.Event.ON_PAUSE -> surfaceView.onPauseSession()
                else -> Unit
            }
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose {
            lifecycleOwner.lifecycle.removeObserver(observer)
            surfaceView.release()
        }
    }

    // Less frequent, lighter snapshots: UI noise is not worth stutter. Every
    // number that used to clutter the screen is written to the session log.
    LaunchedEffect(surfaceView) {
        var lastFrameCount = -1
        var lastProgressAt = System.currentTimeMillis()
        while (true) {
            metrics = surfaceView.snapshotMetrics()
            scanLog.log(
                "t fps=${f1(metrics.fps)} ms=${f1(metrics.processMs)} " +
                    "kf=${metrics.keyframes} tris=${metrics.triangles} " +
                    "blocks=${metrics.blocks} src=${metrics.depthSource} " +
                    "frames=${metrics.framesProcessed} phase=${phaseName(metrics.phase)} " +
                    "tracking=${metrics.tracking} scanning=${metrics.scanning} " +
                    "depth_valid=${f1(metrics.depthValidShare * 100f)} " +
                    "ev_rate=${f1(metrics.neuro.eventRate)} " +
                    "ev_pos=${metrics.neuro.posCount} ev_neg=${metrics.neuro.negCount} " +
                    "ev_ms=${f1(metrics.neuro.eventProcessMs)} " +
                    "flow=(${f1(metrics.neuro.flowDx)},${f1(metrics.neuro.flowDy)}) " +
                    "flow_px_s=${f1(metrics.neuro.flowSpeedPxPerS)} " +
                    "dir=${metrics.neuro.motionDirection} " +
                    "feat=${metrics.neuro.featureCount} " +
                    "motion_ms=${f1(metrics.neuro.motionProcessMs)}"
            )
            if (metrics.framesProcessed != lastFrameCount) {
                lastFrameCount = metrics.framesProcessed
                lastProgressAt = System.currentTimeMillis()
            } else if (metrics.tracking && metrics.scanning &&
                System.currentTimeMillis() - lastProgressAt > 6000
            ) {
                scanLog.log(
                    "stall frames=${metrics.framesProcessed} phase=${phaseName(metrics.phase)}"
                )
                lastProgressAt = System.currentTimeMillis()
            }
            scanLog.logBlock(surfaceView.drainDiagnostics())
            delay(1000)
        }
    }

    Box(modifier = Modifier.fillMaxSize()) {
        AndroidView(
            factory = {
                surfaceView.apply {
                    onError = { message ->
                        errorMessage = message
                        scanLog.log("error=$message")
                    }
                }
            },
            modifier = Modifier.fillMaxSize()
        )

        // Orbit gesture layer. Only on the 3D screen, below overlays/controls.
        if (screen == NeuroScreen.WORLD) {
            Box(
                modifier = Modifier
                    .fillMaxSize()
                    .pointerInput(Unit) {
                        detectTransformGestures { _, pan, zoom, _ ->
                            if (pan.x != 0f || pan.y != 0f) {
                                surfaceView.orbitBy(pan.x * 0.006f, pan.y * 0.006f)
                            }
                            if (zoom != 1f) surfaceView.zoomBy(zoom)
                        }
                    }
            )
        }

        when (screen) {
            NeuroScreen.EVENTS -> EventPanel(
                metrics = metrics,
                threshold = eventThreshold,
                decay = eventDecay,
                onThresholdChange = {
                    eventThreshold = it
                    surfaceView.setEventThreshold(it)
                },
                onDecayChange = {
                    eventDecay = it
                    surfaceView.setEventDecay(it)
                },
                modifier = Modifier
                    .fillMaxSize()
                    .padding(bottom = 118.dp)
            )

            NeuroScreen.DEPTH -> DepthPanel(
                metrics = metrics,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(bottom = 118.dp)
            )

            NeuroScreen.DIAG -> DiagnosticsPanel(
                metrics = metrics,
                modifier = Modifier
                    .fillMaxSize()
                    .padding(bottom = 118.dp)
            )

            NeuroScreen.WORLD -> WorldOverlay(
                metrics = metrics,
                onResetView = { surfaceView.resetOrbit() },
                modifier = Modifier
                    .fillMaxSize()
                    .padding(top = 40.dp)
            )

            NeuroScreen.LIVE -> Unit
        }

        errorMessage?.let { message ->
            Surface(
                modifier = Modifier
                    .align(Alignment.Center)
                    .padding(24.dp),
                shape = RoundedCornerShape(16.dp),
                color = DarkSurface.copy(alpha = 0.95f)
            ) {
                Text(
                    text = message,
                    color = RoseError,
                    fontSize = 13.sp,
                    modifier = Modifier.padding(16.dp)
                )
            }
        }

        ReconControlBar(
            isScanning = metrics.scanning,
            screen = screen,
            onSelectScreen = { screen = it },
            onToggleScan = {
                val next = !surfaceView.isScanning()
                surfaceView.setScanning(next)
                metrics = metrics.copy(scanning = next)
            },
            onOpenExport = { showExportSheet = true },
            onResetScan = { surfaceView.resetReconstruction() },
            modifier = Modifier.align(Alignment.BottomCenter)
        )

        if (showExportSheet) {
            ExportMeshSheet(
                sheetState = exportSheetState,
                triangleCount = metrics.triangles,
                vertexCount = metrics.vertices,
                onDismiss = { showExportSheet = false },
                onExport = { format -> surfaceView.exportMesh(format) }
            )
        }
    }
}
