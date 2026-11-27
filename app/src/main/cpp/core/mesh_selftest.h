#pragma once

#include <string>

#include "mesh/mesher.h"

namespace recon {

// Runs a deterministic synthetic-scene test through the reconstruction
// pipelines and returns a multi-line log of mesh-quality metrics.
//
// The test builds its own TangoRecon / VoxelHash / MeshBuilder instances and
// never touches the live engine, so it is safe to call from a background
// thread while a scan is idle. It needs no camera, no ARCore session and no
// permissions. When the Tango core cannot be loaded the legacy TSDF path is
// tested instead, so a device without the core still produces a result.
std::string runMeshSelfTest();

// Compact one-line quality summary of a live mesh snapshot, sampled so the
// cost stays negligible even for a million-vertex scan. Used by the engine to
// keep the session log useful for comparing builds.
std::string liveMeshQualityLine(const MeshData& mesh, const char* tag);

}  // namespace recon
