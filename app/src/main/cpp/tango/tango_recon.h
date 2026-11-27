#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_set>
#include <vector>

#include "core/frame.h"
#include "mesh/mesher.h"
#include "tango_3d_reconstruction_api.h"

namespace recon {

// Thin, allocation-conscious wrapper around Google's Tango 3D Reconstruction
// library (Apache 2.0), the same reconstruction core used by 3DLiveScanner.
//
// ARCore gives us a metric depth map and a 6DoF pose per frame. The library
// turns the unprojected points into a TSDF (traversal update), runs marching
// cubes per 16^3 grid cell and returns only the cells a frame touched. We keep
// those cells as cached segments and publish them as one immutable MeshData for
// the GL thread.
//
// The library is loaded with dlopen instead of a DT_NEEDED entry: if a device
// cannot load it (for example an ABI/page-size mismatch), the engine can still
// fall back to its own integration path instead of failing to start.
class TangoRecon {
public:
    TangoRecon() = default;
    ~TangoRecon();

    TangoRecon(const TangoRecon&) = delete;
    TangoRecon& operator=(const TangoRecon&) = delete;

    // maxDepth matches the deepest sample the engine feeds; ARCore starts
    // extrapolating past ~6 m, so a larger ceiling only stores noise. minDepth
    // is lowered for macro scans so an object held close to the phone is not
    // clipped away before it can be integrated.
    bool init(double resolution = 0.03, double maxDepth = 6.0, double minDepth = 0.20);
    // Rebuilds the volume with another profile. The engine calls this once at
    // the start of a scan (before geometry exists) to switch to the close-range
    // profile; calling it mid-scan would wipe everything captured so far.
    void applyProfile(double resolution, double maxDepth, double minDepth = 0.20);
    double resolution() const { return resolution_; }
    bool ready() const { return context_ != nullptr; }
    void reset();

    // One depth grid handed to the reconstruction. The engine feeds its
    // temporally fused grid at the native resolution and passes the matching
    // intrinsics so the projection stays exact.
    struct DepthFeed {
        const std::vector<uint16_t>* mm = nullptr;
        int width = 0;
        int height = 0;
        Intrinsics K;
    };

    // Fuses one depth frame. Returns false when the frame was rejected by the
    // reconstruction (missing calibration, empty depth, internal error).
    bool integrate(const FrameInput& frame, const DepthFeed* feed = nullptr);

    // Rebuilds the consolidated snapshot from the cached segments. Throttled to
    // ~6-7 Hz unless force is set.
    void publish(bool force = false);

    std::shared_ptr<const MeshData> mesh() const;
    int triangleCount() const { return triangles_.load(); }
    int vertexCount() const { return vertices_.load(); }
    int segmentCount() const { return segmentCount_.load(); }
    int pendingSegments() const { return pendingCount_.load(); }
    double lastUpdateMs() const { return lastUpdateMs_.load(); }
    double lastExtractMs() const { return lastExtractMs_.load(); }
    int lastPointCount() const { return lastPointCount_.load(); }
    int lastUpdatedCells() const { return lastUpdatedCells_.load(); }
    uint64_t integratedFrames() const { return integrated_.load(); }

    // Extracts the whole surface in one consistent state for export.
    bool buildFullMesh(MeshData& out) const;

private:
    struct Segment {
        std::vector<MeshVertex> vertices;
        std::vector<uint32_t> indices;
    };

    // Work item for one 16^3 grid cell. distance2 is measured from the camera
    // at the moment the cell changed, so extraction can serve the area the
    // user is actually looking at first.
    struct SegmentTask {
        std::array<int, 3> index;
        float distance2;
    };
    struct SegmentTaskGreater {
        bool operator()(const SegmentTask& a, const SegmentTask& b) const {
            return a.distance2 > b.distance2;
        }
    };

    bool publishLocked();
    // Merges vertices that coincide across segment boundaries into single
    // representatives and averages the incident face normals into the vertex
    // normals, so a surface is one continuous, smoothly shaded mesh instead of
    // a chain of independently shaded segments.
    void weldMesh(MeshData& mesh);
    // Closes short boundary rims (dropout tears, seams that welding could not
    // merge) with a fan to the rim centroid. Same conservative rule the exporter
    // applies, so the live preview and the exported file stay comparable.
    void fillSmallHoles(MeshData& mesh);
    // Drops debris: faces that form a component of their own that is both tiny
    // and/or short (a floating fragment, a dropout tear that never closed) while
    // the dominant component survives. Same rule the exporter applies, so live
    // and exported meshes stay comparable.
    void dropSmallBlobs(MeshData& mesh);
    // Punches the high-frequency "spiky" noise out of the marching-cubes output
    // with a Taubin (lambda|mu) pass. Vertices on the open boundary (silhouette /
    // ripped edges) never move, so the outline stays exact while inner bumps
    // collapse toward the surface.
    void smoothMesh(MeshData& mesh);
    // Rebuilds the vertex normals as the average of the incident face normals.
    // Run after welding and after rim filling so faces added later update the
    // neighbours they share.
    void recomputeVertexNormals(MeshData& mesh);
    void clearSnapshot();
    void extractSegment(const std::array<int, 3>& key);
    // Drops the farthest cached cells when the cached surface exceeds the
    // memory budget, so a long walk outdoors cannot grow the published mesh
    // without bound. The core volume keeps its state; only the copy used for
    // display/export is bounded.
    void evictSegments();

    Tango3DR_ReconstructionContext context_ = nullptr;
    double resolution_ = 0.03;
    double maxDepth_ = 6.0;
    double minDepth_ = 0.20;
    // Camera-space XYZC points (4 floats each), rebuilt for every integrated
    // frame from the filtered feed grid. The core applies the depth pose to
    // them, so they must not be pre-transformed into world coordinates.
    std::vector<float> cloudPoints_;
    std::vector<uint8_t> colorImage_;

    std::map<std::array<int, 3>, Segment> segments_;
    size_t segmentVertices_ = 0;
    float lastCamera_[3] = {0.f, 0.f, 0.f};
    std::priority_queue<SegmentTask, std::vector<SegmentTask>, SegmentTaskGreater> pending_;
    // Insertion order of the same cells. Nearest-first serves what the user is
    // aiming at, but on its own it starves everything behind the camera, which
    // is why parts of a room could stay unmeshed no matter how long the scan
    // ran. Every fourth extraction comes from this end instead.
    std::deque<std::array<int, 3>> pendingFifo_;
    std::unordered_set<uint64_t> pendingSet_;
    // Cells the backlog cap had to drop. They still exist in the core volume;
    // re-queuing them when the live backlog drains is what heals a swept
    // region instead of leaving it as a permanent hole until the user happens
    // to aim at it again.
    std::deque<std::array<int, 3>> deferred_;
    bool dirty_ = false;
    double lastPublishSeconds_ = 0.0;

    mutable std::mutex snapshotMutex_;
    std::shared_ptr<const MeshData> snapshot_ = std::make_shared<MeshData>();

    std::atomic<int> triangles_{0};
    std::atomic<int> vertices_{0};
    std::atomic<int> segmentCount_{0};
    std::atomic<int> pendingCount_{0};
    std::atomic<double> lastUpdateMs_{0.0};
    std::atomic<double> lastExtractMs_{0.0};
    std::atomic<int> lastPointCount_{0};
    std::atomic<int> lastUpdatedCells_{0};
    std::atomic<uint64_t> integrated_{0};
};

}  // namespace recon
