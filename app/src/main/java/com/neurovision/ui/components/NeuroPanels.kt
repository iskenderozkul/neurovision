package com.neurovision.ui.components

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsPadding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.neurovision.recon.ReconMetrics
import com.neurovision.ui.theme.AmberAccent
import com.neurovision.ui.theme.CyanAccent
import com.neurovision.ui.theme.DarkBackground
import com.neurovision.ui.theme.DarkSurface
import com.neurovision.ui.theme.DarkSurfaceElevated
import com.neurovision.ui.theme.EmeraldSuccess
import com.neurovision.ui.theme.RoseError
import com.neurovision.ui.theme.TextPrimary
import com.neurovision.ui.theme.TextSecondary
import java.util.Locale

private fun f1(v: Float): String = String.format(Locale.US, "%.1f", v)
private fun f2(v: Float): String = String.format(Locale.US, "%.2f", v)
private fun pct(v: Float): String = "${(v * 100f).toInt()}%"

/**
 * Event View. The bitmap is produced by the native software-DVS engine from
 * real camera luma: cyan = positive log-intensity change, magenta = negative.
 * There is no synthetic motion here; a static scene produces no events.
 */
@Composable
fun EventPanel(
    metrics: ReconMetrics,
    threshold: Float,
    decay: Float,
    onThresholdChange: (Float) -> Unit,
    onDecayChange: (Float) -> Unit,
    modifier: Modifier = Modifier
) {
    val neuro = metrics.neuro

    Column(
        modifier = modifier
            .fillMaxSize()
            .background(DarkBackground)
            .statusBarsPadding()
            .padding(horizontal = 14.dp, vertical = 8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "EVENT VIEW",
                    color = TextPrimary,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Yazılımsal DVS · log parlaklık değişimi",
                    color = TextSecondary,
                    fontSize = 11.sp
                )
            }
            Surface(
                shape = RoundedCornerShape(12.dp),
                color = DarkSurface
            ) {
                Text(
                    text = "C = ${f2(neuro.eventThreshold)}",
                    color = CyanAccent,
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp)
                )
            }
        }

        Spacer(Modifier.height(8.dp))

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(3f / 4f)
                .clip(RoundedCornerShape(14.dp))
                .background(Color.Black)
                .border(1.dp, CyanAccent.copy(alpha = 0.35f), RoundedCornerShape(14.dp))
        ) {
            val bitmap = neuro.eventPreview
            if (bitmap != null) {
                Image(
                    bitmap = bitmap.asImageBitmap(),
                    contentDescription = "Event haritası",
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Fit
                )
            } else {
                Text(
                    text = "Kamera karesi bekleniyor…",
                    color = TextSecondary,
                    fontSize = 12.sp,
                    modifier = Modifier.align(Alignment.Center)
                )
            }
            Row(
                modifier = Modifier
                    .align(Alignment.BottomStart)
                    .padding(8.dp),
                verticalAlignment = Alignment.CenterVertically
            ) {
                LegendDot(CyanAccent, "pozitif")
                Spacer(Modifier.width(10.dp))
                LegendDot(Color(0xFFFF4696), "negatif")
            }
        }

        // Event counters are not shown; they are written to the session log.

        Spacer(Modifier.height(8.dp))

        Surface(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(16.dp),
            color = DarkSurface
        ) {
            Column(modifier = Modifier.padding(horizontal = 14.dp, vertical = 10.dp)) {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("Eşik (log Δ)", color = TextPrimary, fontSize = 12.sp)
                    Text(f2(threshold), color = CyanAccent, fontSize = 12.sp)
                }
                Slider(
                    value = threshold,
                    onValueChange = onThresholdChange,
                    valueRange = 0.02f..0.80f
                )
                Text(
                    text = "Düşük eşik = daha çok event, daha çok gürültü. " +
                        "Yüksek eşik = yalnızca belirgin değişimler.",
                    color = TextSecondary,
                    fontSize = 10.sp
                )
                Spacer(Modifier.height(8.dp))
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween
                ) {
                    Text("İz kalıcılığı (sönüm)", color = TextPrimary, fontSize = 12.sp)
                    Text(f2(decay), color = CyanAccent, fontSize = 12.sp)
                }
                Slider(
                    value = decay,
                    onValueChange = onDecayChange,
                    valueRange = 0.40f..0.98f
                )
                Text(
                    text = "Event motoru işlem süresi: ${f1(neuro.eventProcessMs)} ms/kare",
                    color = TextSecondary,
                    fontSize = 10.sp
                )
            }
        }
    }
}

/**
 * Depth View. The heat map comes from the reconstruction core's depth grid;
 * on devices without the ARCore Depth API the same map is produced by the
 * on-device multi-view stereo fallback. Colours are inverse-depth: warm is
 * near, cold is far. It never invents samples the sensor did not return.
 */
@Composable
fun DepthPanel(metrics: ReconMetrics, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .background(DarkBackground)
            .statusBarsPadding()
            .padding(horizontal = 14.dp, vertical = 8.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "DEPTH VIEW",
                    color = TextPrimary,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Yakın sıcak · uzak soğuk · renk = ters derinlik",
                    color = TextSecondary,
                    fontSize = 11.sp
                )
            }
            Surface(shape = RoundedCornerShape(12.dp), color = DarkSurface) {
                Text(
                    text = metrics.depthSourceLabel,
                    color = when (metrics.depthSource) {
                        1 -> EmeraldSuccess
                        2 -> AmberAccent
                        else -> TextSecondary
                    },
                    fontSize = 12.sp,
                    fontWeight = FontWeight.Bold,
                    modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp)
                )
            }
        }

        Spacer(Modifier.height(8.dp))

        val depth = metrics.depthPreview
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(3f / 4f)
                .clip(RoundedCornerShape(14.dp))
                .background(Color.Black)
                .border(1.dp, CyanAccent.copy(alpha = 0.35f), RoundedCornerShape(14.dp))
        ) {
            if (depth != null) {
                Image(
                    bitmap = depth.asImageBitmap(),
                    contentDescription = "Derinlik haritası",
                    modifier = Modifier.fillMaxSize(),
                    contentScale = ContentScale.Fit
                )
            } else {
                Text(
                    text = "Derinlik yok. ARCore Depth API bu cihazda desteklenmiyor olabilir; " +
                        "MVS yedeği ilk karelerden sonra devreye girer.",
                    color = TextSecondary,
                    fontSize = 12.sp,
                    modifier = Modifier
                        .align(Alignment.Center)
                        .padding(24.dp)
                )
            }
        }

        // Depth statistics are not shown; they are written to the session log.

        Spacer(Modifier.height(10.dp))

        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(10.dp)
        ) {
            val confidence = metrics.confidencePreview
            if (confidence != null) {
                Column(modifier = Modifier.weight(1f)) {
                    Text("Güven haritası", color = TextSecondary, fontSize = 11.sp)
                    Image(
                        bitmap = confidence.asImageBitmap(),
                        contentDescription = "Güven haritası",
                        modifier = Modifier
                            .fillMaxWidth()
                            .aspectRatio(3f / 4f)
                            .clip(RoundedCornerShape(10.dp))
                            .border(1.dp, DarkSurfaceElevated, RoundedCornerShape(10.dp)),
                        contentScale = ContentScale.Fit
                    )
                    Text(
                        text = "ARCore güveni; düşük güvenli pikseller füzyona alınmaz.",
                        color = TextSecondary,
                        fontSize = 10.sp
                    )
                }
            }
        }
    }
}

/** Compact live perception strip shown over the AR camera. */
@Composable
fun LivePerceptionBar(metrics: ReconMetrics, modifier: Modifier = Modifier) {
    val neuro = metrics.neuro
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(14.dp),
        color = DarkSurface.copy(alpha = 0.86f),
        tonalElevation = 4.dp
    ) {
        Row(
            modifier = Modifier.padding(horizontal = 12.dp, vertical = 7.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Text(
                text = neuro.directionLabel,
                color = when (neuro.motionDirection) {
                    0 -> TextSecondary
                    else -> CyanAccent
                },
                fontSize = 11.sp,
                fontWeight = FontWeight.Bold
            )
            Spacer(Modifier.width(10.dp))
            Text(
                text = "${f1(neuro.flowSpeedPxPerS)} px/s",
                color = TextSecondary,
                fontSize = 11.sp
            )
            Spacer(Modifier.width(10.dp))
            Text(
                text = "${neuro.featureCount} özellik",
                color = TextSecondary,
                fontSize = 11.sp
            )
            Spacer(Modifier.width(10.dp))
            Text(
                text = "${neuro.eventRate.toInt()} ev/s",
                color = CyanAccent,
                fontSize = 11.sp
            )
        }
    }
}

/**
 * Measured sparse-flow vectors drawn over the live camera. Each line is a
 * feature the native Lucas-Kanade tracker actually followed between the last
 * two frames; the start/end points come from the tracker, and [toNdc] is
 * ARCore's measured 320x240-grid -> screen transform, so the lines land on the
 * same pixels the camera preview shows. When no transform is available the
 * overlay stays empty instead of guessing.
 */
@Composable
fun MotionTracksOverlay(
    tracks: List<Float>,
    toNdc: FloatArray?,
    modifier: Modifier = Modifier
) {
    if (toNdc == null || toNdc.size < 6 || tracks.size < 4) return
    Canvas(modifier = modifier) {
        fun project(x: Float, y: Float): Offset {
            val nx = toNdc[0] * x + toNdc[1] * y + toNdc[2]
            val ny = toNdc[3] * x + toNdc[4] * y + toNdc[5]
            return Offset((nx + 1f) * 0.5f * size.width, (1f - ny) * 0.5f * size.height)
        }
        val stroke = 2.dp.toPx()
        val dot = 2.4.dp.toPx()
        val count = tracks.size / 4
        for (i in 0 until count) {
            val start = project(tracks[i * 4], tracks[i * 4 + 1])
            val end = project(tracks[i * 4 + 2], tracks[i * 4 + 3])
            drawLine(CyanAccent.copy(alpha = 0.65f), start, end, strokeWidth = stroke)
            drawCircle(CyanAccent, radius = dot, center = end)
        }
    }
}

/** Free-orbit controls for the 3D world screen, drawn over the GL surface. */@Composable
fun WorldOverlay(
    metrics: ReconMetrics,
    points: Boolean,
    onTogglePoints: () -> Unit,
    onResetView: () -> Unit,
    modifier: Modifier = Modifier
) {
    Column(modifier = modifier) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 14.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically
        ) {
            Column {
                Text(
                    text = "3D WORLD",
                    color = TextPrimary,
                    fontSize = 18.sp,
                    fontWeight = FontWeight.Bold
                )
                Text(
                    text = "Sürükle: döndür · Pinch: yakınlaştır",
                    color = TextSecondary,
                    fontSize = 11.sp
                )
            }
            Row(horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                Chip(if (points) "Nokta" else "Mesh", CyanAccent, onTogglePoints)
                Chip("Sıfırla", TextSecondary, onResetView)
            }
        }

        // Geometry counts are not shown; they are written to the session log.

    }
}

/** Technical diagnostics: every row is a measured counter, not a guess. */
@Composable
fun DiagnosticsPanel(metrics: ReconMetrics, modifier: Modifier = Modifier) {
    Column(
        modifier = modifier
            .fillMaxSize()
            .background(DarkBackground)
            .statusBarsPadding()
            .padding(horizontal = 14.dp, vertical = 8.dp)
    ) {
        Text(
            text = "DIAGNOSTICS",
            color = TextPrimary,
            fontSize = 18.sp,
            fontWeight = FontWeight.Bold
        )
        Text(
            text = "Tüm ölçümler oturum loguna (scan_*.txt) yazılır; ekranda sayı gösterilmez.",
            color = TextSecondary,
            fontSize = 11.sp
        )
        Spacer(Modifier.height(10.dp))
        Text(
            text = "Event'ler yazılımsaldır ve kamera FPS'ini aşamaz. " +
                "Derinlik ARCore Depth API veya cihaz içi MVS'ten gelir; " +
                "görülmeyen yüzeyler bilinmiyormuş gibi işaretlenmez. " +
                "Ölçek, ARCore metrik takibiyle sağlanır.",
            color = TextSecondary,
            fontSize = 10.sp
        )
    }
}

@Composable

private fun LegendDot(color: Color, label: String) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            modifier = Modifier
                .size(8.dp)
                .clip(RoundedCornerShape(4.dp))
                .background(color)
        )
        Spacer(Modifier.width(4.dp))
        Text(text = label, color = Color.LightGray, fontSize = 10.sp)
    }
}

@Composable
fun StatChip(label: String, value: String, tint: Color, modifier: Modifier = Modifier) {
    Surface(
        modifier = modifier,
        shape = RoundedCornerShape(12.dp),
        color = DarkSurface
    ) {
        Column(
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 7.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Text(
                text = value,
                color = tint,
                fontSize = 13.sp,
                fontWeight = FontWeight.Bold,
                style = MaterialTheme.typography.labelSmall
            )
            Text(
                text = label,
                color = TextSecondary,
                fontSize = 9.sp,
                style = MaterialTheme.typography.labelSmall
            )
        }
    }
}

@Composable
private fun Chip(label: String, tint: Color, onClick: () -> Unit) {
    Surface(
        shape = RoundedCornerShape(12.dp),
        color = DarkSurface,
        modifier = Modifier.clickable(onClick = onClick)
    ) {
        Text(
            text = label,
            color = tint,
            fontSize = 11.sp,
            fontWeight = FontWeight.Bold,
            modifier = Modifier.padding(horizontal = 10.dp, vertical = 6.dp)
        )
    }
}

@Composable
private fun DiagSection(title: String) {
    Spacer(Modifier.height(12.dp))
    Text(
        text = title.uppercase(),
        color = CyanAccent,
        fontSize = 11.sp,
        fontWeight = FontWeight.Bold
    )
    Spacer(Modifier.height(4.dp))
}

@Composable
private fun DiagRow(label: String, value: String, valueColor: Color = TextPrimary) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .padding(vertical = 3.dp),
        horizontalArrangement = Arrangement.SpaceBetween
    ) {
        Text(text = label, color = TextSecondary, fontSize = 12.sp)
        Text(text = value, color = valueColor, fontSize = 12.sp, fontWeight = FontWeight.Medium)
    }
}

private fun phaseName(phase: Int): String = when (phase) {
    1 -> "prepare"
    2 -> "fuse"
    3 -> "feed"
    4 -> "integrate"
    5 -> "mesh"
    6 -> "remesh"
    else -> "idle"
}
