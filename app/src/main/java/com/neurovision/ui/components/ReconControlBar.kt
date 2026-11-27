package com.neurovision.ui.components

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.BookmarkAdd
import androidx.compose.material.icons.filled.DeleteSweep
import androidx.compose.material.icons.filled.FileDownload
import androidx.compose.material.icons.filled.Pause
import androidx.compose.material.icons.filled.Place
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.testTag
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.neurovision.recon.NeuroScreen
import com.neurovision.ui.theme.AmberAccent
import com.neurovision.ui.theme.CyanAccent
import com.neurovision.ui.theme.DarkSurface
import com.neurovision.ui.theme.DarkSurfaceElevated
import com.neurovision.ui.theme.EmeraldSuccess
import com.neurovision.ui.theme.RoseError
import com.neurovision.ui.theme.TextSecondary

/**
 * Bottom bar: the five screen tabs on top and the scan controls below. The
 * tabs replace the old render-mode chips, because the modes are now whole
 * screens (live AR, event map, depth map, free-orbit 3D world, diagnostics).
 */
@Composable
fun ReconControlBar(
    isScanning: Boolean,
    screen: NeuroScreen,
    onSelectScreen: (NeuroScreen) -> Unit,
    onToggleScan: () -> Unit,
    onAddAnchor: () -> Unit,
    onOpenAnchors: () -> Unit,
    onOpenExport: () -> Unit,
    onResetScan: () -> Unit,
    modifier: Modifier = Modifier
) {
    Surface(
        modifier = modifier
            .fillMaxWidth()
            .navigationBarsPadding()
            .padding(horizontal = 12.dp, vertical = 8.dp),
        shape = RoundedCornerShape(26.dp),
        color = DarkSurface.copy(alpha = 0.94f),
        tonalElevation = 8.dp,
        shadowElevation = 12.dp
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 10.dp, vertical = 8.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .padding(bottom = 6.dp),
                horizontalArrangement = Arrangement.SpaceEvenly,
                verticalAlignment = Alignment.CenterVertically
            ) {
                NeuroScreen.values().forEach { item ->
                    val selected = item == screen
                    Box(
                        modifier = Modifier
                            .clip(RoundedCornerShape(12.dp))
                            .background(
                                if (selected) CyanAccent.copy(alpha = 0.22f)
                                else DarkSurfaceElevated
                            )
                            .clickable { onSelectScreen(item) }
                            .padding(horizontal = 12.dp, vertical = 5.dp)
                    ) {
                        Text(
                            text = item.title,
                            color = if (selected) CyanAccent else TextSecondary,
                            fontSize = 11.sp,
                            style = MaterialTheme.typography.labelSmall
                        )
                    }
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically
            ) {
                ActionButton(
                    icon = Icons.Default.DeleteSweep,
                    label = "Sıfırla",
                    tint = RoseError,
                    onClick = onResetScan,
                    tag = "reset_scan_button"
                )

                ActionButton(
                    icon = Icons.Default.BookmarkAdd,
                    label = "Anchor",
                    tint = AmberAccent,
                    onClick = onAddAnchor,
                    tag = "add_anchor_button"
                )

                Box(
                    modifier = Modifier
                        .size(54.dp)
                        .clip(CircleShape)
                        .background(if (isScanning) EmeraldSuccess else RoseError)
                        .clickable(onClick = onToggleScan)
                        .testTag("toggle_scan_button"),
                    contentAlignment = Alignment.Center
                ) {
                    Icon(
                        imageVector = if (isScanning) Icons.Default.Pause else Icons.Default.PlayArrow,
                        contentDescription = if (isScanning) "Tarama duraklat" else "Tarama başlat",
                        tint = Color.Black,
                        modifier = Modifier.size(30.dp)
                    )
                }

                ActionButton(
                    icon = Icons.Default.Place,
                    label = "Anchor'lar",
                    tint = AmberAccent,
                    onClick = onOpenAnchors,
                    tag = "open_anchors_button"
                )

                ActionButton(
                    icon = Icons.Default.FileDownload,
                    label = "Dışa Aktar",
                    tint = CyanAccent,
                    onClick = onOpenExport,
                    tag = "export_mesh_button"
                )
            }
        }
    }
}

@Composable
private fun ActionButton(
    icon: ImageVector,
    label: String,
    tint: Color,
    onClick: () -> Unit,
    tag: String
) {
    Column(
        horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .clip(RoundedCornerShape(12.dp))
            .clickable(onClick = onClick)
            .padding(horizontal = 8.dp, vertical = 4.dp)
            .testTag(tag)
    ) {
        Icon(
            imageVector = icon,
            contentDescription = label,
            tint = tint,
            modifier = Modifier.size(23.dp)
        )
        Text(
            text = label,
            color = Color.LightGray,
            fontSize = 10.sp,
            style = MaterialTheme.typography.labelSmall
        )
    }
}
