package com.neurovision

import android.Manifest
import android.content.pm.PackageManager
import android.os.Bundle
import android.util.Log
import android.view.WindowManager
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.ContextCompat
import com.neurovision.recon.NativeRecon
import com.neurovision.recon.ScanLog
import com.neurovision.ui.ReconScreen
import com.neurovision.ui.theme.CyanAccent
import com.neurovision.ui.theme.DarkBackground
import com.neurovision.ui.theme.TextPrimary
import com.neurovision.ui.theme.TextSecondary
import com.neurovision.ui.theme.NeuroVisionTheme
import com.google.ar.core.ArCoreApk
import com.google.ar.core.exceptions.UnavailableDeviceNotCompatibleException
import com.google.ar.core.exceptions.UnavailableUserDeclinedInstallationException
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

class MainActivity : ComponentActivity() {

    private var cameraGranted by mutableStateOf(false)
    private var arReady by mutableStateOf(false)
    private var statusMessage by mutableStateOf("Preparing the AR scannerâ€¦")

    /** Set when the previous run died without writing its clean end marker. */
    private var crashedSession by mutableStateOf<File?>(null)

    private lateinit var sessionLog: ScanLog

    private val permissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { granted ->
        cameraGranted = granted
        sessionLog.log("stage=camera_permission granted=$granted")
        if (granted) {
            ensureArCore()
        } else {
            statusMessage = "Çevrenizi taramak için kamera izni gerekli."
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        installCrashHandlers()

        sessionLog = ScanLog(this)
        // Checked before this run's log is created, so the newest file is the
        // previous session. A missing clean-end marker means that run crashed.
        crashedSession = ScanLog.findUncleanSession(this)
        sessionLog.start()
        sessionLog.log("stage=activity_created")
        sessionLog.log(
            "previous_session=" + (crashedSession?.name ?: "clean")
        )
        runMeshSelfTestAsync()

        // A scan is continuous work with no touch input; without this flag the
        // system display timeout puts the screen (and the AR session) to sleep
        // mid-scan, which users read as the app doing "power saving".
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
        enableEdgeToEdge()

        setContent {
            NeuroVisionTheme {
                Surface(
                    modifier = Modifier.fillMaxSize(),
                    color = DarkBackground
                ) {
                    val crashed = crashedSession
                    when {
                        crashed != null -> RecoveryScreen(
                            logFile = crashed,
                            onShare = {
                                ScanLog.markRecovered(crashed)
                                sessionLog.log("stage=recovery_share")
                                ScanLog.shareSessionLog(this, crashed)
                            },
                            onContinue = {
                                ScanLog.markRecovered(crashed)
                                sessionLog.log("stage=recovery_continue")
                                crashedSession = null
                                requestCameraPermission()
                            }
                        )
                        arReady -> ReconScreen(sessionLog)
                        else -> StartupScreen(
                            message = statusMessage,
                            onRequestPermission = { requestCameraPermission() }
                        )
                    }
                }
            }
        }

        if (crashedSession == null) {
            requestCameraPermission()
        }
    }

    // One synthetic mesh check per session, off the main thread. It builds its
    // own native reconstruction instances, so it never touches the live scan,
    // and it needs no camera, no ARCore and no permissions: the numbers are
    // available even when the device cannot scan. The report goes into the
    // session log and into its own file so two builds can be compared line by
    // line without rerunning a whole scan.
    private fun runMeshSelfTestAsync() {
        Thread({
            // Let the first camera frames and the ARCore session settle: the
            // check allocates its own reconstruction volume, and two volumes
            // growing at the same instant is the only moment this could add
            // memory pressure to a scan that is already running.
            runCatching { Thread.sleep(1500) }
            val report = runCatching { NativeRecon.runMeshSelfTest() }
                .getOrElse { "selftest error=${it.javaClass.simpleName}: ${it.message}" }
            val file = runCatching { sessionLog.writeReport("selftest", report) }.getOrNull()
            val perception = runCatching { NativeRecon.runPerceptionSelfTest() }
                .getOrElse { "perception_selftest error=${it.javaClass.simpleName}: ${it.message}" }
            val perceptionFile =
                runCatching { sessionLog.writeReport("selftest_perception", perception) }.getOrNull()
            runOnUiThread {
                if (this::sessionLog.isInitialized) {
                    sessionLog.logBlock(report)
                    sessionLog.log("selftest_report=${file?.name ?: "unwritten"}")
                    sessionLog.logBlock(perception)
                    sessionLog.log(
                        "selftest_perception_report=${perceptionFile?.name ?: "unwritten"}"
                    )
                }
            }
        }, "mesh-selftest").start()
    }

    override fun onStart() {
        super.onStart()
        if (this::sessionLog.isInitialized) sessionLog.resume()
    }

    override fun onResume() {
        super.onResume()
        sessionLog.log("stage=activity_resumed")
        if (crashedSession == null && cameraGranted && !arReady) ensureArCore()
    }

    /**
     * Closing the log here means a normal exit (including swiping the app away
     * from recents) always leaves a finished, shareable file. Without this the
     * user had to wait for a crash to get a log at all.
     */
    override fun onStop() {
        if (this::sessionLog.isInitialized) {
            sessionLog.log("stage=activity_stopped")
            sessionLog.suspend("background")
        }
        super.onStop()
    }

    override fun onDestroy() {
        if (this::sessionLog.isInitialized) {
            sessionLog.log("stage=activity_destroyed")
            sessionLog.stop()
        }
        super.onDestroy()
    }

    /**
     * A fatal crash previously left no trace. Uncaught Java exceptions are
     * written into the app's crash directory; the native signal handler was
     * removed because replacing ART's SIGSEGV handler crashed at startup.
     * The continuous [ScanLog] covers hangs and native crashes instead.
     */
    private fun installCrashHandlers() {
        if (crashHandlersInstalled) return
        crashHandlersInstalled = true

        val directory = File(getExternalFilesDir(null) ?: filesDir, "crash")
        if (!directory.exists()) directory.mkdirs()

        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, throwable ->
            runCatching {
                val stamp = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US).format(Date())
                File(directory, "java_crash_$stamp.txt").writeText(
                    buildString {
                        appendLine("thread=${thread.name}")
                        appendLine("device=${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}")
                        appendLine(
                            "android=${android.os.Build.VERSION.RELEASE} " +
                                "sdk=${android.os.Build.VERSION.SDK_INT}"
                        )
                        appendLine(Log.getStackTraceString(throwable))
                    }
                )
            }
            if (previous != null) {
                previous.uncaughtException(thread, throwable)
            } else {
                android.os.Process.killProcess(android.os.Process.myPid())
                kotlin.system.exitProcess(10)
            }
        }
    }

    private fun requestCameraPermission() {
        if (ContextCompat.checkSelfPermission(this, Manifest.permission.CAMERA) ==
            PackageManager.PERMISSION_GRANTED
        ) {
            cameraGranted = true
            sessionLog.log("stage=camera_permission granted=true (already)")
            ensureArCore()
        } else {
            sessionLog.log("stage=camera_permission request")
            permissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    private fun ensureArCore() {
        sessionLog.log("stage=arcore_request")
        try {
            when (val status = ArCoreApk.getInstance().requestInstall(this, true)) {
                ArCoreApk.InstallStatus.INSTALLED -> {
                    sessionLog.log("stage=arcore_installed")
                    arReady = true
                }
                ArCoreApk.InstallStatus.INSTALL_REQUESTED -> {
                    sessionLog.log("stage=arcore_install_requested status=$status")
                    statusMessage = "Installing Google Play Services for ARâ€¦"
                }
            }
        } catch (e: UnavailableDeviceNotCompatibleException) {
            sessionLog.log("stage=arcore_error incompatible")
            statusMessage = "Bu cihaz ARCore desteklemiyor; 3D tarayıcı için gerekli."
        } catch (e: UnavailableUserDeclinedInstallationException) {
            sessionLog.log("stage=arcore_error declined")
            statusMessage = "Google Play Services for AR gerekli. Kurup uygulamayı yeniden başlatın."
        } catch (e: Exception) {
            sessionLog.log("stage=arcore_error ${e.javaClass.simpleName}: ${e.message}")
            statusMessage = "ARCore başlatılamadı: ${e.message}"
        }
    }

    companion object {
        @Volatile private var crashHandlersInstalled = false
    }
}

@Composable
private fun StartupScreen(message: String, onRequestPermission: () -> Unit) {
    Box(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier
                .align(Alignment.Center)
                .padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp)
        ) {
            Text(
                text = "NEUROVISION",
                color = TextPrimary,
                fontSize = 22.sp
            )
            Text(
                text = message,
                color = TextSecondary,
                fontSize = 13.sp,
                textAlign = TextAlign.Center
            )
            Button(
                onClick = onRequestPermission,
                colors = ButtonDefaults.buttonColors(containerColor = CyanAccent)
            ) {
                Text(text = "Kamera izni ver", fontSize = 13.sp)
            }
        }
    }
}

/**
 * Safe mode: the previous run crashed, so the app starts here instead of
 * going straight into the AR session. The log can be shared before anything
 * that might crash again is started.
 */
@Composable
private fun RecoveryScreen(logFile: File, onShare: () -> Unit, onContinue: () -> Unit) {
    Box(modifier = Modifier.fillMaxSize()) {
        Column(
            modifier = Modifier
                .align(Alignment.Center)
                .padding(28.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(14.dp)
        ) {
            Text(
                text = "SAFE MODE",
                color = TextPrimary,
                fontSize = 22.sp
            )
            Text(
                text = "Önceki oturum beklenmedik şekilde kapandı. Log kaydedildi: " +
                    "tarayıcı yeniden başlamadan paylaşılabilir.",
                color = TextSecondary,
                fontSize = 13.sp,
                textAlign = TextAlign.Center
            )
            Text(
                text = logFile.name,
                color = CyanAccent,
                fontSize = 11.sp
            )
            Spacer(modifier = Modifier.height(4.dp))
            Button(
                onClick = onShare,
                colors = ButtonDefaults.buttonColors(containerColor = CyanAccent)
            ) {
                Text(text = "Önceki logu paylaş", fontSize = 13.sp)
            }
            OutlinedButton(onClick = onContinue) {
                Text(text = "Tarayıcıya devam et", color = TextPrimary, fontSize = 13.sp)
            }
        }
    }
}
