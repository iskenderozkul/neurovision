#include "tango/tango_recon.h"

#include <android/log.h>
#include <dlfcn.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

#include "tango_3d_reconstruction_api.h"

#define LOG_TAG "TangoRecon"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace recon {
namespace {

// Taubin λ|μ smoothing flattens the high-frequency marching-cubes noise but the
// adjacency build and four passes run on the live worker thread that also
// fuses depth. At ~200k triangles it costs hundreds of ms per publish and
// collapsed fps from ~30 to ~1-10 (oturum_scan_20261115_141012.txt, ms=1539-
// 1876), which the user read as "the mesh never updates". Keep the code for
// evaluation but do not call it on the live path.
constexpr bool kEnableTaubinSmooth = false;

inline double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

// The profile is chosen by the engine (room vs street) and passed to init();
// the grid cell used for nearest-first ordering follows the active profile.

// Packs a 16^3 grid index into one key so the pending queue can deduplicate
// cells that the core reports again before they are extracted.
inline uint64_t gridKey(const std::array<int, 3>& index) {
    const uint64_t x = static_cast<uint64_t>(index[0] & 0x1FFFFF);
    const uint64_t y = static_cast<uint64_t>(index[1] & 0x1FFFFF);
    const uint64_t z = static_cast<uint64_t>(index[2] & 0x1FFFFF);
    return (x << 42) | (y << 21) | z;
}

inline uint64_t edgeKey(uint32_t a, uint32_t b) {
    const uint32_t lo = std::min(a, b);
    const uint32_t hi = std::max(a, b);
    return (static_cast<uint64_t>(lo) << 32) | static_cast<uint64_t>(hi);
}

uint32_t findRoot(std::vector<uint32_t>& parent, uint32_t i) {
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

// Grid key for the vertex weld pass. Cell size equals the weld tolerance and
// the neighbour scan covers the surrounding cells, so points within the
// tolerance that straddle a cell border still find each other.
struct WeldKey {
    int x, y, z;
    bool operator==(const WeldKey& o) const { return x == o.x && y == o.y && z == o.z; }
};
struct WeldKeyHash {
    size_t operator()(const WeldKey& k) const {
        uint64_t h = 1469598103934665603ULL;
        const uint32_t v[3] = {static_cast<uint32_t>(k.x), static_cast<uint32_t>(k.y),
                               static_cast<uint32_t>(k.z)};
        for (int i = 0; i < 3; ++i) {
            h ^= v[i];
            h *= 1099511628211ULL;
        }
        return static_cast<size_t>(h);
    }
};

// The library has no C++ ABI of its own (plain C entry points), and loading it
// with DT_NEEDED would make the whole app fail to start if the device cannot
// load it. All entry points are resolved through dlopen instead; a partially
// resolved table leaves TangoRecon unready and the engine falls back.
struct TangoApi {
    void* lib = nullptr;

    decltype(&Tango3DR_Config_create) configCreate = nullptr;
    decltype(&Tango3DR_Config_setBool) configSetBool = nullptr;
    decltype(&Tango3DR_Config_setInt32) configSetInt32 = nullptr;
    decltype(&Tango3DR_Config_setDouble) configSetDouble = nullptr;
    decltype(&Tango3DR_Config_destroy) configDestroy = nullptr;
    decltype(&Tango3DR_ReconstructionContext_create) contextCreate = nullptr;
    decltype(&Tango3DR_ReconstructionContext_destroy) contextDestroy = nullptr;
    decltype(&Tango3DR_ReconstructionContext_setDepthCalibration) setDepthCalibration = nullptr;
    decltype(&Tango3DR_ReconstructionContext_setColorCalibration) setColorCalibration = nullptr;
    decltype(&Tango3DR_updateFromPointCloud) updateFromPointCloud = nullptr;
    decltype(&Tango3DR_extractMeshSegment) extractMeshSegment = nullptr;
    decltype(&Tango3DR_extractFullMesh) extractFullMesh = nullptr;
    decltype(&Tango3DR_Mesh_initEmpty) meshInitEmpty = nullptr;
    decltype(&Tango3DR_Mesh_destroy) meshDestroy = nullptr;
    decltype(&Tango3DR_GridIndexArray_initEmpty) gridInitEmpty = nullptr;
    decltype(&Tango3DR_GridIndexArray_destroy) gridDestroy = nullptr;

    bool ok = false;

    void load() {
        if (lib) return;
        lib = dlopen("libtango_3d_reconstruction.so", RTLD_NOW | RTLD_LOCAL);
        if (!lib) {
            LOGE("dlopen failed: %s", dlerror());
            return;
        }
        auto sym = [this](const char* name) { return dlsym(lib, name); };
#define RESOLVE(member, type) \
    member = reinterpret_cast<decltype(member)>(sym(#type))
        RESOLVE(configCreate, Tango3DR_Config_create);
        RESOLVE(configSetBool, Tango3DR_Config_setBool);
        RESOLVE(configSetInt32, Tango3DR_Config_setInt32);
        RESOLVE(configSetDouble, Tango3DR_Config_setDouble);
        RESOLVE(configDestroy, Tango3DR_Config_destroy);
        RESOLVE(contextCreate, Tango3DR_ReconstructionContext_create);
        RESOLVE(contextDestroy, Tango3DR_ReconstructionContext_destroy);
        RESOLVE(setDepthCalibration, Tango3DR_ReconstructionContext_setDepthCalibration);
        RESOLVE(setColorCalibration, Tango3DR_ReconstructionContext_setColorCalibration);
        RESOLVE(updateFromPointCloud, Tango3DR_updateFromPointCloud);
        RESOLVE(extractMeshSegment, Tango3DR_extractMeshSegment);
        RESOLVE(extractFullMesh, Tango3DR_extractFullMesh);
        RESOLVE(meshInitEmpty, Tango3DR_Mesh_initEmpty);
        RESOLVE(meshDestroy, Tango3DR_Mesh_destroy);
        RESOLVE(gridInitEmpty, Tango3DR_GridIndexArray_initEmpty);
        RESOLVE(gridDestroy, Tango3DR_GridIndexArray_destroy);
#undef RESOLVE
        ok = configCreate && configSetBool && configSetInt32 && configSetDouble &&
             configDestroy && contextCreate && contextDestroy && setDepthCalibration &&
             setColorCalibration && updateFromPointCloud && extractMeshSegment &&
             extractFullMesh && meshInitEmpty && meshDestroy && gridInitEmpty && gridDestroy;
        if (!ok) {
            LOGE("libtango_3d_reconstruction.so is missing entry points");
        } else {
            LOGI("Tango 3DR loaded");
        }
    }
};

TangoApi& api() {
    static TangoApi instance = [] {
        TangoApi a;
        a.load();
        return a;
    }();
    return instance;
}

Tango3DR_CameraCalibration makeCalibration(const Intrinsics& K, int width, int height) {
    Tango3DR_CameraCalibration calib{};
    calib.calibration_type = TANGO_3DR_CALIBRATION_POLYNOMIAL_3_PARAMETERS;
    calib.width = static_cast<uint32_t>(width);
    calib.height = static_cast<uint32_t>(height);
    calib.fx = static_cast<double>(K.fx);
    calib.fy = static_cast<double>(K.fy);
    calib.cx = static_cast<double>(K.cx);
    calib.cy = static_cast<double>(K.cy);
    calib.distortion[0] = 0.0;
    calib.distortion[1] = 0.0;
    calib.distortion[2] = 0.0;
    calib.distortion[3] = 0.0;
    calib.distortion[4] = 0.0;
    return calib;
}

// ARCore unprojects its depth map with the camera-texture intrinsics scaled to
// the depth grid. Older bridges send no Kdepth; then the CPU intrinsics scaled
// to the same grid are the best available estimate.
Intrinsics depthIntrinsics(const FrameInput& frame) {
    Intrinsics K = frame.Kdepth;
    const bool valid = K.fx > 1.f && K.fy > 1.f && K.cx > 0.f && K.cy > 0.f &&
                       K.width == frame.depthWidth && K.height == frame.depthHeight;
    if (valid) return K;
    const float sx = static_cast<float>(frame.depthWidth) /
                     static_cast<float>(frame.width);
    const float sy = static_cast<float>(frame.depthHeight) /
                     static_cast<float>(frame.height);
    K.fx = frame.K.fx * sx;
    K.fy = frame.K.fy * sy;
    K.cx = frame.K.cx * sx;
    K.cy = frame.K.cy * sy;
    K.width = frame.depthWidth;
    K.height = frame.depthHeight;
    return K;
}

}  // namespace

TangoRecon::~TangoRecon() {
    if (!context_) return;
    TangoApi& a = api();
    if (a.ok && a.contextDestroy) a.contextDestroy(context_);
    context_ = nullptr;
}

bool TangoRecon::init(double resolution, double maxDepth, double minDepth) {
    resolution_ = resolution;
    maxDepth_ = maxDepth;
    minDepth_ = minDepth;
    if (context_) return true;
    TangoApi& a = api();
    if (!a.ok) return false;

    Tango3DR_Config config = a.configCreate(TANGO_3DR_CONFIG_RECONSTRUCTION);
    if (!config) {
        LOGE("Tango3DR_Config_create failed");
        return false;
    }
    // 3 cm voxels keep leaves, mug rims and chair edges; the grid is fed by a
    // 160x90 metric depth map whose footprint is 1-2 cm at room range. The
    // ceiling matches the engine's feed cap: past 6 m ARCore extrapolates and
    // the extra volume only stores noise.
    a.configSetDouble(config, "resolution", resolution_);
    a.configSetDouble(config, "min_depth", minDepth_);
    a.configSetDouble(config, "max_depth", maxDepth_);
    a.configSetBool(config, "generate_color", true);
    // Tango's own visibility clearing replaces our hand-written carve; it is
    // the same step that makes the reference scanner survive walking around a
    // room without turning surfaces into ghosts.
    a.configSetBool(config, "use_space_clearing", true);
    a.configSetBool(config, "use_parallel_integration", true);
    // Reject only tiny extraction debris, not real objects.
    a.configSetInt32(config, "min_num_vertices", 9);
    // The reconstruction's default traversal update marches a ray from the
    // camera to each measured point and updates every voxel it crosses. That is
    // what makes a floor seen at a grazing angle close into one surface: the
    // projective update only marks voxels that project inside the pixel
    // footprint, so at a grazing angle it leaves a row of holes and the floor
    // comes out as torn islands (the cloud-like debris seen up to v2.7). It is
    // also the pair used by 3DLiveScanner on ARCore phones: traversal update
    // with Tango3DR_updateFromPointCloud. The cost grows with the point count,
    // but the filtered feed carries only a few thousand points per frame.
    a.configSetInt32(config, "update_method", TANGO_3DR_TRAVERSAL_UPDATE);

    context_ = a.contextCreate(config);
    a.configDestroy(config);
    if (!context_) {
        LOGE("Tango3DR_ReconstructionContext_create failed");
        return false;
    }
    LOGI("Tango 3DR context ready (%.0f mm voxels, max %.1f m)",
         resolution_ * 1000.0, maxDepth_);
    return true;
}

void TangoRecon::applyProfile(double resolution, double maxDepth, double minDepth) {
    if (context_ &&
        std::fabs(resolution - resolution_) < 1e-6 &&
        std::fabs(maxDepth - maxDepth_) < 1e-6 &&
        std::fabs(minDepth - minDepth_) < 1e-6) {
        return;
    }
    TangoApi& a = api();
    if (context_ && a.ok) a.contextDestroy(context_);
    context_ = nullptr;
    segments_.clear();
    segmentVertices_ = 0;
    std::priority_queue<SegmentTask, std::vector<SegmentTask>, SegmentTaskGreater> empty;
    pending_.swap(empty);
    pendingFifo_.clear();
    pendingSet_.clear();
    deferred_.clear();
    pendingCount_.store(0);
    dirty_ = false;
    lastPublishSeconds_ = 0.0;
    lastUpdateMs_.store(0.0);
    lastExtractMs_.store(0.0);
    integrated_.store(0);
    clearSnapshot();
    init(resolution, maxDepth, minDepth);
}

void TangoRecon::clearSnapshot() {
    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_ = std::make_shared<MeshData>();
    }
    triangles_.store(0);
    vertices_.store(0);
    segmentCount_.store(0);
}

void TangoRecon::reset() {
    TangoApi& a = api();
    if (context_ && a.ok) a.contextDestroy(context_);
    context_ = nullptr;
    segments_.clear();
    segmentVertices_ = 0;
    std::priority_queue<SegmentTask, std::vector<SegmentTask>, SegmentTaskGreater> empty;
    pending_.swap(empty);
    pendingFifo_.clear();
    pendingSet_.clear();
    deferred_.clear();
    pendingCount_.store(0);
    dirty_ = false;
    lastPublishSeconds_ = 0.0;
    lastUpdateMs_.store(0.0);
    lastExtractMs_.store(0.0);
    integrated_.store(0);
    clearSnapshot();
    // Keep the profile the engine selected (macro vs room); re-initialising
    // with the defaults here silently reverted a macro scan to 3 cm voxels
    // after every user reset.
    init(resolution_, maxDepth_, minDepth_);
}

bool TangoRecon::integrate(const FrameInput& frame, const DepthFeed* feed) {
    if (!context_) return false;
    if (!frame.hasDepth() || !frame.valid()) return false;
    int dw = frame.depthWidth;
    int dh = frame.depthHeight;
    Intrinsics depthK = depthIntrinsics(frame);
    const uint16_t* depthSource = frame.depthMm.data();
    if (feed && feed->mm && feed->width > 1 && feed->height > 1 &&
        feed->mm->size() == static_cast<size_t>(feed->width) * feed->height) {
        dw = feed->width;
        dh = feed->height;
        depthK = feed->K;
        depthK.width = dw;
        depthK.height = dh;
        depthSource = feed->mm->data();
    }
    const int w = frame.width;
    const int h = frame.height;
    if (w < 2 || h < 2 || (w & 1) != 0 || (h & 1) != 0) return false;

    TangoApi& a = api();
    if (!a.ok) return false;

    lastCamera_[0] = frame.tx;
    lastCamera_[1] = frame.ty;
    lastCamera_[2] = frame.tz;

    const double timestamp =
        static_cast<double>(frame.depthTimestampNs > 0 ? frame.depthTimestampNs
                                                       : frame.timestampNs) * 1e-9;

    // Camera-space point cloud, exactly like the reference scanner, which
    // multiplies its world-space points by inverse(pose) before handing them to
    // Tango3DR_updateFromPointCloud. Feeding world-space points instead made the
    // core apply the pose a second time: every point landed roughly 2x the pose
    // offset away from the camera, outside max_depth, so no voxel was ever
    // touched (tris=0 for the whole session, then the legacy fallback). Traversal
    // update marches voxels from the camera pose to each point, so the points
    // must be in the depth camera's own frame: X right, Y down, Z forward.
    if (!(depthK.fx > 1.f && depthK.fy > 1.f && depthK.cx > 0.f && depthK.cy > 0.f)) {
        return false;
    }
    const float minDepthF = static_cast<float>(minDepth_);
    const float maxDepthF = static_cast<float>(maxDepth_);
    cloudPoints_.clear();
    cloudPoints_.reserve(static_cast<size_t>(dw) * dh * 4);
    for (int y = 0; y < dh; ++y) {
        const uint16_t* row = depthSource + static_cast<size_t>(y) * dw;
        for (int x = 0; x < dw; ++x) {
            const uint16_t mm = row[x];
            if (mm == 0) continue;
            const float z = static_cast<float>(mm) * 0.001f;
            if (z < minDepthF || z > maxDepthF) continue;
            const float px = (static_cast<float>(x) - depthK.cx) / depthK.fx * z;
            const float py = (static_cast<float>(y) - depthK.cy) / depthK.fy * z;
            cloudPoints_.push_back(px);
            cloudPoints_.push_back(py);
            cloudPoints_.push_back(z);
            cloudPoints_.push_back(1.f);
        }
    }
    const size_t pointCount = cloudPoints_.size() / 4;
    lastPointCount_.store(static_cast<int>(pointCount));
    if (pointCount == 0) return false;

    // NV21: Y plane followed by interleaved V,U. Tango samples vertex colours
    // from this image with the colour intrinsics, so the 4:3 CPU frame keeps
    // its own calibration instead of being force-fitted to the depth crop.
    const size_t ySize = static_cast<size_t>(w) * h;
    colorImage_.resize(ySize + ySize / 2);
    std::memcpy(colorImage_.data(), frame.y.data(), ySize);
    uint8_t* vu = colorImage_.data() + ySize;
    const int cw = w / 2;
    const int ch = h / 2;
    for (int i = 0; i < cw * ch; ++i) {
        vu[i * 2 + 0] = frame.v[static_cast<size_t>(i)];
        vu[i * 2 + 1] = frame.u[static_cast<size_t>(i)];
    }

    Tango3DR_PointCloud cloud{};
    cloud.timestamp = timestamp;
    cloud.num_points = static_cast<uint32_t>(pointCount);
    cloud.points = reinterpret_cast<Tango3DR_Vector4*>(cloudPoints_.data());

    Tango3DR_ImageBuffer colorImage{};
    colorImage.width = static_cast<uint32_t>(w);
    colorImage.height = static_cast<uint32_t>(h);
    colorImage.stride = static_cast<uint32_t>(w);
    colorImage.timestamp = timestamp;
    colorImage.format = TANGO_3DR_HAL_PIXEL_FORMAT_YCrCb_420_SP;
    colorImage.data = colorImage_.data();

    // ARCore reports the camera pose in the OpenGL frame (X right, Y up,
    // -Z forward). Tango 3DR reconstructs in the camera optical frame
    // (X right, Y down, +Z forward), so the pose must be pre-rotated by 180
    // degrees about the camera X axis. Without this every depth map is fused
    // behind the camera and the volume fills with mirrored shells; this is
    // exactly the conversion 3DLiveScanner applies before Extract3DRPose.
    // q_t = q_gl * q_x180  =>  (x,y,z,w) = (qw, qz, -qy, -qx).
    Tango3DR_Pose pose{};
    pose.translation[0] = static_cast<double>(frame.tx);
    pose.translation[1] = static_cast<double>(frame.ty);
    pose.translation[2] = static_cast<double>(frame.tz);
    pose.orientation[0] = static_cast<double>(frame.qw);
    pose.orientation[1] = static_cast<double>(frame.qz);
    pose.orientation[2] = -static_cast<double>(frame.qy);
    pose.orientation[3] = -static_cast<double>(frame.qx);

    const Tango3DR_CameraCalibration depthCalib =
        makeCalibration(depthK, dw, dh);
    const Tango3DR_CameraCalibration colorCalib = makeCalibration(frame.K, w, h);
    a.setDepthCalibration(context_, &depthCalib);
    a.setColorCalibration(context_, &colorCalib);

    Tango3DR_GridIndexArray updated{};
    if (a.gridInitEmpty(&updated) != TANGO_3DR_SUCCESS) return false;

    const double start = nowSeconds();
    const Tango3DR_Status status =
        a.updateFromPointCloud(context_, &cloud, &pose, &colorImage, &pose, &updated);
    lastUpdateMs_.store((nowSeconds() - start) * 1000.0);
    if (status != TANGO_3DR_SUCCESS) {
        static int failures = 0;
        if (failures < 3) {
            LOGE("updateFromPointCloud failed: %d (%u points, color %dx%d)", status,
                 static_cast<unsigned>(pointCount), w, h);
            ++failures;
        }
        a.gridDestroy(&updated);
        return false;
    }

    // A fast sweep over a large room can enqueue more segments than the frame
    // budget can ever drain. The old cap cleared the whole queue, so a region
    // the user had just swept could lose every pending cell and stay unmeshed
    // until they happened to aim at it again. Now only the farthest quarter is
    // dropped from the live heap, and those cells are parked in deferred_ so
    // they can be re-queued when the backlog drains. Without that memory the
    // drop was permanent and showed up as an empty patch in an otherwise
    // completed wall.
    constexpr size_t kMaxPending = 1536;
    constexpr size_t kMaxDeferred = 8192;
    if (pending_.size() > kMaxPending) {
        std::vector<SegmentTask> keep;
        keep.reserve(pending_.size());
        while (!pending_.empty()) {
            keep.push_back(pending_.top());
            pending_.pop();
        }
        constexpr size_t kKeep = 1200;
        std::nth_element(keep.begin(), keep.begin() + kKeep, keep.end(),
                         [](const SegmentTask& a, const SegmentTask& b) {
                             return a.distance2 < b.distance2;
                         });
        // keep[kKeep..] is the farthest quarter, in unspecified order.
        for (size_t i = kKeep; i < keep.size(); ++i) {
            if (deferred_.size() >= kMaxDeferred) deferred_.pop_front();
            deferred_.push_back(keep[i].index);
        }
        keep.resize(kKeep);
        pendingSet_.clear();
        pendingFifo_.clear();
        for (const SegmentTask& t : keep) {
            pendingSet_.insert(gridKey(t.index));
            pending_.push(t);
            pendingFifo_.push_back(t.index);
        }
    }

    lastUpdatedCells_.store(static_cast<int>(updated.num_indices));
    for (uint32_t i = 0; i < updated.num_indices; ++i) {
        const Tango3DR_GridIndex& index = updated.indices[i];
        const std::array<int, 3> key{index[0], index[1], index[2]};
        if (!pendingSet_.insert(gridKey(key)).second) continue;

        // Nearest-first: the user sees the surface he is aiming at grow, not a
        // segment from a corner scanned a minute ago.
        const float cell = static_cast<float>(16.0 * resolution_);
        const float cx = (static_cast<float>(index[0]) + 0.5f) * cell;
        const float cy = (static_cast<float>(index[1]) + 0.5f) * cell;
        const float cz = (static_cast<float>(index[2]) + 0.5f) * cell;
        const float dx = cx - frame.tx;
        const float dy = cy - frame.ty;
        const float dz = cz - frame.tz;
        pending_.push(SegmentTask{key, dx * dx + dy * dy + dz * dz});
        pendingFifo_.push_back(key);
    }
    a.gridDestroy(&updated);
    pendingCount_.store(static_cast<int>(pending_.size()));

    // Most FIFO entries are spent the moment the heap serves their cell in the
    // same frame, so the queue is compacted before it can grow without bound.
    if (pendingFifo_.size() > 2048) {
        std::deque<std::array<int, 3>> live;
        for (const std::array<int, 3>& key : pendingFifo_) {
            if (pendingSet_.count(gridKey(key)) > 0) live.push_back(key);
        }
        pendingFifo_.swap(live);
    }

    // Meshing must never stall depth fusion: spend a bounded slice of the
    // frame and let the rest of the queue wait for the next one. At 10-15 Hz
    // a 30 ms slice keeps the surface in front of the camera growing while
    // leaving the worker enough budget to accept the next depth map. One slot
    // in four is served oldest-first so cells the user already passed stop
    // waiting forever.
    constexpr double kExtractBudgetMs = 30.0;
    const double extractStart = nowSeconds();
    bool extracted = false;
    int slots = 0;
    while (!pending_.empty()) {
        if ((nowSeconds() - extractStart) * 1000.0 >= kExtractBudgetMs) break;
        ++slots;
        bool tookOldest = false;
        std::array<int, 3> oldest{};
        if ((slots % 4) == 0 && pendingFifo_.size() > 64) {
            for (int tries = 0; tries < 32 && !pendingFifo_.empty(); ++tries) {
                const std::array<int, 3> key = pendingFifo_.front();
                pendingFifo_.pop_front();
                if (pendingSet_.erase(gridKey(key)) == 0) continue;
                oldest = key;
                tookOldest = true;
                break;
            }
        }
        if (tookOldest) {
            extractSegment(oldest);
            extracted = true;
            continue;
        }
        const SegmentTask task = pending_.top();
        pending_.pop();
        // The same cell can still sit in the heap after the FIFO served it.
        if (pendingSet_.erase(gridKey(task.index)) == 0) continue;
        extractSegment(task.index);
        extracted = true;
    }
    lastExtractMs_.store((nowSeconds() - extractStart) * 1000.0);
    // Once the live backlog is short, pull a bounded batch of the cells the
    // cap parked earlier. They are still meshed in the core, so extraction
    // closes the hole; the batch is small enough that the next frame's depth
    // integration still has budget.
    constexpr size_t kPendingLowWater = 384;
    constexpr size_t kDeferredPerFrame = 96;
    if (pending_.size() < kPendingLowWater && !deferred_.empty()) {
        const float cell = static_cast<float>(16.0 * resolution_);
        for (size_t n = 0; n < kDeferredPerFrame && !deferred_.empty(); ++n) {
            const std::array<int, 3> key = deferred_.front();
            deferred_.pop_front();
            if (!pendingSet_.insert(gridKey(key)).second) continue;
            const float cx = (static_cast<float>(key[0]) + 0.5f) * cell;
            const float cy = (static_cast<float>(key[1]) + 0.5f) * cell;
            const float cz = (static_cast<float>(key[2]) + 0.5f) * cell;
            const float dx = cx - frame.tx;
            const float dy = cy - frame.ty;
            const float dz = cz - frame.tz;
            pending_.push(SegmentTask{key, dx * dx + dy * dy + dz * dz});
            pendingFifo_.push_back(key);
        }
    }
    pendingCount_.store(static_cast<int>(pending_.size()));

    if (extracted) dirty_ = true;
    evictSegments();
    integrated_.fetch_add(1);
    publish();
    return true;
}

void TangoRecon::extractSegment(const std::array<int, 3>& key) {
    if (!context_) return;
    TangoApi& a = api();
    if (!a.ok) return;

    const Tango3DR_GridIndex index{key[0], key[1], key[2]};
    Tango3DR_Mesh mesh{};
    if (a.meshInitEmpty(&mesh) != TANGO_3DR_SUCCESS) return;
    if (a.extractMeshSegment(context_, index, &mesh) != TANGO_3DR_SUCCESS) {
        a.meshDestroy(&mesh);
        return;
    }

    const uint32_t numVertices = mesh.num_vertices;
    const uint32_t numFaces = mesh.num_faces;

    // A reflective screen, a ceiling or a floor-to-balcony transition can make
    // the core advertise more geometry than it can actually deliver, and a
    // cell is bounded by construction. An empty or absurd mesh is dropped
    // instead of being copied (null pointers here used to be a SIGSEGV).
    constexpr uint32_t kMaxSegmentVertices = 60000;
    constexpr uint32_t kMaxSegmentFaces = 120000;
    const bool sane = mesh.vertices && mesh.faces && numVertices > 0 && numFaces > 0 &&
                      numVertices <= kMaxSegmentVertices && numFaces <= kMaxSegmentFaces;

    if (!sane) {
        // An empty cell is real (the surface moved out of it), so forget the
        // cached copy; an unusable non-empty answer keeps the last good one.
        if (numFaces == 0) {
            auto it = segments_.find(key);
            if (it != segments_.end()) {
                segmentVertices_ -= it->second.vertices.size();
                segments_.erase(it);
            }
        }
        a.meshDestroy(&mesh);
        return;
    }

    Segment segment;
    segment.vertices.reserve(numVertices);
    segment.indices.reserve(static_cast<size_t>(numFaces) * 3);
    for (uint32_t v = 0; v < numVertices; ++v) {
        MeshVertex vertex{};
        vertex.px = mesh.vertices[v][0];
        vertex.py = mesh.vertices[v][1];
        vertex.pz = mesh.vertices[v][2];
        if (mesh.normals) {
            vertex.nx = mesh.normals[v][0];
            vertex.ny = mesh.normals[v][1];
            vertex.nz = mesh.normals[v][2];
        } else {
            vertex.nx = 0.f;
            vertex.ny = 1.f;
            vertex.nz = 0.f;
        }
        if (mesh.colors) {
            vertex.r = mesh.colors[v][0];
            vertex.g = mesh.colors[v][1];
            vertex.b = mesh.colors[v][2];
            vertex.a = 255;
        } else {
            vertex.r = 200;
            vertex.g = 200;
            vertex.b = 200;
            vertex.a = 255;
        }
        segment.vertices.push_back(vertex);
    }
    for (uint32_t f = 0; f < numFaces; ++f) {
        const uint32_t i0 = mesh.faces[f][0];
        const uint32_t i1 = mesh.faces[f][1];
        const uint32_t i2 = mesh.faces[f][2];
        if (i0 >= numVertices || i1 >= numVertices || i2 >= numVertices) continue;
        segment.indices.push_back(i0);
        segment.indices.push_back(i1);
        segment.indices.push_back(i2);
    }
    if (segment.indices.empty()) {
        a.meshDestroy(&mesh);
        return;
    }
    Segment& slot = segments_[key];
    segmentVertices_ -= slot.vertices.size();
    slot = std::move(segment);
    segmentVertices_ += slot.vertices.size();
    a.meshDestroy(&mesh);
}

void TangoRecon::evictSegments() {
    // The core keeps integrating the whole walk, but the cached copy that is
    // rebuilt and uploaded for display is bounded: past the budget the cells
    // farthest from the current camera are dropped first, so the surface the
    // user is looking at always survives. A dropped cell comes back the next
    // time the depth frame touches it.
    constexpr size_t kMaxSegments = 1500;
    constexpr size_t kMaxVertices = 1000000;
    if (segments_.size() <= kMaxSegments && segmentVertices_ <= kMaxVertices) return;

    const float cell = static_cast<float>(16.0 * resolution_);
    std::vector<std::pair<float, std::array<int, 3>>> order;
    order.reserve(segments_.size());
    for (const auto& entry : segments_) {
        const float cx = (static_cast<float>(entry.first[0]) + 0.5f) * cell;
        const float cy = (static_cast<float>(entry.first[1]) + 0.5f) * cell;
        const float cz = (static_cast<float>(entry.first[2]) + 0.5f) * cell;
        const float dx = cx - lastCamera_[0];
        const float dy = cy - lastCamera_[1];
        const float dz = cz - lastCamera_[2];
        order.emplace_back(dx * dx + dy * dy + dz * dz, entry.first);
    }
    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b) { return a.first > b.first; });

    const size_t targetSegments = kMaxSegments * 4 / 5;
    const size_t targetVertices = kMaxVertices * 4 / 5;
    size_t removed = 0;
    for (const auto& item : order) {
        if (segments_.size() <= targetSegments && segmentVertices_ <= targetVertices) break;
        auto it = segments_.find(item.second);
        if (it == segments_.end()) continue;
        segmentVertices_ -= it->second.vertices.size();
        segments_.erase(it);
        ++removed;
    }
    if (removed > 0) {
        LOGI("evicted %zu far cells (%zu cells, %zu verts remain)", removed,
             segments_.size(), segmentVertices_);
    }
}

bool TangoRecon::publishLocked() {
    auto mesh = std::make_shared<MeshData>();
    size_t totalVertices = 0;
    size_t totalFaces = 0;
    for (const auto& entry : segments_) {
        totalVertices += entry.second.vertices.size();
        totalFaces += entry.second.indices.size() / 3;
    }
    mesh->vertices.reserve(totalVertices);
    mesh->indices.reserve(totalFaces * 3);

    for (const auto& entry : segments_) {
        const Segment& segment = entry.second;
        const uint32_t base = static_cast<uint32_t>(mesh->vertices.size());
        mesh->vertices.insert(mesh->vertices.end(), segment.vertices.begin(),
                              segment.vertices.end());
        for (const uint32_t index : segment.indices) {
            mesh->indices.push_back(base + index);
        }
    }
    mesh->triangleCount = static_cast<uint32_t>(totalFaces);
    static uint32_t version = 1;
    mesh->version = ++version;

    // Segment boundaries are where a surface looks cut in half: every cached
    // segment is an independent marching-cubes output, so the same wall can come
    // out as two chains of triangles with different corner normals. Weld the
    // coincident vertices, close the short dropout rims the weld leaves behind,
    // and average the face normals before publishing.
    weldMesh(*mesh);
    fillSmallHoles(*mesh);
    dropSmallBlobs(*mesh);
    if (kEnableTaubinSmooth) smoothMesh(*mesh);
    recomputeVertexNormals(*mesh);
    mesh->triangleCount = static_cast<uint32_t>(mesh->indices.size() / 3);

    {
        std::lock_guard<std::mutex> lock(snapshotMutex_);
        snapshot_ = mesh;
    }
    triangles_.store(static_cast<int>(mesh->triangleCount));
    vertices_.store(static_cast<int>(mesh->vertices.size()));
    segmentCount_.store(static_cast<int>(segments_.size()));
    dirty_ = false;
    lastPublishSeconds_ = nowSeconds();
    return true;
}

void TangoRecon::weldMesh(MeshData& mesh) {
    if (mesh.vertices.size() < 3 || mesh.indices.size() < 3) return;
    const float kEps = 5e-4f;
    std::unordered_map<WeldKey, uint32_t, WeldKeyHash> cellIndex;
    cellIndex.reserve(mesh.vertices.size() * 2);
    std::vector<uint32_t> rep(mesh.vertices.size(), 0);
    std::vector<MeshVertex> welded;
    welded.reserve(mesh.vertices.size());

    for (size_t i = 0; i < mesh.vertices.size(); ++i) {
        const MeshVertex& v = mesh.vertices[i];
        const int baseX = static_cast<int>(std::lround(v.px / kEps));
        const int baseY = static_cast<int>(std::lround(v.py / kEps));
        const int baseZ = static_cast<int>(std::lround(v.pz / kEps));
        uint32_t r = 0;
        bool found = false;
        for (int dx = -1; !found && dx <= 1; ++dx) {
            for (int dy = -1; !found && dy <= 1; ++dy) {
                for (int dz = -1; !found && dz <= 1; ++dz) {
                    const auto it = cellIndex.find(WeldKey{baseX + dx, baseY + dy, baseZ + dz});
                    if (it == cellIndex.end()) continue;
                    r = it->second;
                    found = true;
                }
            }
        }
        if (!found) {
            r = static_cast<uint32_t>(welded.size());
            cellIndex.emplace(WeldKey{baseX, baseY, baseZ}, r);
            welded.push_back(mesh.vertices[i]);
        }
        rep[i] = r;
    }

    std::vector<Vec3> normalAcc(welded.size());
    std::vector<int> normalCnt(welded.size(), 0);
    std::vector<uint32_t> newIndices;
    newIndices.reserve(mesh.indices.size());
    const size_t triCount = mesh.indices.size() / 3;
    for (size_t t = 0; t < triCount; ++t) {
        const uint32_t ia = mesh.indices[t * 3 + 0];
        const uint32_t ib = mesh.indices[t * 3 + 1];
        const uint32_t ic = mesh.indices[t * 3 + 2];
        const uint32_t ra = rep[ia];
        const uint32_t rb = rep[ib];
        const uint32_t rc = rep[ic];
        const Vec3 pa{welded[ra].px, welded[ra].py, welded[ra].pz};
        const Vec3 pb{welded[rb].px, welded[rb].py, welded[rb].pz};
        const Vec3 pc{welded[rc].px, welded[rc].py, welded[rc].pz};
        const Vec3 n = cross(pb - pa, pc - pa);
        const float nl = n.length();
        if (nl < 1e-9f) continue;  // welded corners collapsed into one point
        const Vec3 nn = n / nl;
        newIndices.push_back(ra);
        newIndices.push_back(rb);
        newIndices.push_back(rc);
        normalAcc[ra] += nn;
        normalCnt[ra]++;
        normalAcc[rb] += nn;
        normalCnt[rb]++;
        normalAcc[rc] += nn;
        normalCnt[rc]++;
    }

    std::vector<MeshVertex> out;
    out.reserve(welded.size());
    for (size_t r = 0; r < welded.size(); ++r) {
        MeshVertex nv = welded[r];
        if (normalCnt[r] > 0) {
            const Vec3 n = normalAcc[r];
            const float nl = n.length();
            if (nl > 1e-8f) {
                const Vec3 nn = n / nl;
                nv.nx = nn.x;
                nv.ny = nn.y;
                nv.nz = nn.z;
            }
        }
        out.push_back(nv);
    }

    mesh.vertices.swap(out);
    mesh.indices.swap(newIndices);
    mesh.triangleCount = static_cast<uint32_t>(newIndices.size() / 3);
}

void TangoRecon::fillSmallHoles(MeshData& mesh) {
    constexpr size_t kMaxLoopVertices = 32u;
    constexpr float kMaxPerimeter = 0.12f;

    if (mesh.vertices.size() < 3 || mesh.indices.size() < 3) return;
    const size_t triCount = mesh.indices.size() / 3;
    std::unordered_map<uint64_t, int> edgeUses;
    edgeUses.reserve(triCount * 2 + 1);
    for (size_t i = 0; i < triCount; ++i) {
        const uint32_t ia = mesh.indices[i * 3 + 0];
        const uint32_t ib = mesh.indices[i * 3 + 1];
        const uint32_t ic = mesh.indices[i * 3 + 2];
        ++edgeUses[edgeKey(ia, ib)];
        ++edgeUses[edgeKey(ib, ic)];
        ++edgeUses[edgeKey(ic, ia)];
    }

    // Directed boundary edges: a triangle wound (a,b,c) has the outside of the
    // surface on its right, so the rim successor of b is a.
    std::unordered_map<uint32_t, uint32_t> succ;
    std::unordered_set<uint32_t> ambiguous;
    succ.reserve(4096);
    for (size_t i = 0; i < triCount; ++i) {
        const uint32_t ia = mesh.indices[i * 3 + 0];
        const uint32_t ib = mesh.indices[i * 3 + 1];
        const uint32_t ic = mesh.indices[i * 3 + 2];
        const uint32_t pairs[3][2] = {{ia, ib}, {ib, ic}, {ic, ia}};
        for (int k = 0; k < 3; ++k) {
            const uint32_t a = pairs[k][0];
            const uint32_t b = pairs[k][1];
            if (edgeUses[edgeKey(a, b)] != 1) continue;
            if (!succ.emplace(b, a).second) ambiguous.insert(b);
        }
    }
    if (succ.empty()) return;

    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    std::unordered_set<uint32_t> visited;
    visited.reserve(succ.size());
    std::vector<uint32_t> loop;
    std::vector<MeshVertex> fillVerts;
    std::vector<uint32_t> fillIndex;

    for (const auto& start : succ) {
        if (visited.count(start.first) || ambiguous.count(start.first)) continue;
        loop.clear();
        uint32_t v = start.first;
        float perimeter = 0.f;
        bool ok = true;
        while (visited.insert(v).second) {
            loop.push_back(v);
            if (loop.size() > kMaxLoopVertices) break;
            if (ambiguous.count(v)) {
                ok = false;
                break;
            }
            const auto next = succ.find(v);
            if (next == succ.end()) break;
            const uint32_t a = v;
            const uint32_t b = next->second;
            const float dx = mesh.vertices[a].px - mesh.vertices[b].px;
            const float dy = mesh.vertices[a].py - mesh.vertices[b].py;
            const float dz = mesh.vertices[a].pz - mesh.vertices[b].pz;
            perimeter += std::sqrt(dx * dx + dy * dy + dz * dz);
            v = next->second;
            if (v == start.first) break;
        }
        if (!ok || v != start.first || loop.size() < 3 || loop.size() > kMaxLoopVertices) continue;
        if (perimeter > kMaxPerimeter) continue;

        float cx = 0.f, cy = 0.f, cz = 0.f;
        float nx = 0.f, ny = 0.f, nz = 0.f;
        float cr = 0.f, cg = 0.f, cb = 0.f;
        for (const uint32_t index : loop) {
            const MeshVertex& iv = mesh.vertices[index];
            cx += iv.px;
            cy += iv.py;
            cz += iv.pz;
            nx += iv.nx;
            ny += iv.ny;
            nz += iv.nz;
            cr += iv.r;
            cg += iv.g;
            cb += iv.b;
        }
        const float inv = 1.f / static_cast<float>(loop.size());
        const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (nl < 1e-6f) continue;
        const uint32_t centre = base + static_cast<uint32_t>(fillVerts.size());
        MeshVertex cmv;
        cmv.px = cx * inv;
        cmv.py = cy * inv;
        cmv.pz = cz * inv;
        cmv.nx = nx / nl;
        cmv.ny = ny / nl;
        cmv.nz = nz / nl;
        cmv.r = static_cast<uint8_t>(cr * inv);
        cmv.g = static_cast<uint8_t>(cg * inv);
        cmv.b = static_cast<uint8_t>(cb * inv);
        cmv.a = 255;
        fillVerts.push_back(cmv);

        for (size_t i = 0; i < loop.size(); ++i) {
            const uint32_t a = loop[i];
            const uint32_t b = loop[(i + 1) % loop.size()];
            fillIndex.push_back(centre);
            fillIndex.push_back(b);
            fillIndex.push_back(a);
        }
    }

    if (fillIndex.empty()) return;

    mesh.vertices.insert(mesh.vertices.end(), fillVerts.begin(), fillVerts.end());
    mesh.indices.insert(mesh.indices.end(), fillIndex.begin(), fillIndex.end());
}

void TangoRecon::dropSmallBlobs(MeshData& mesh) {
    constexpr uint32_t kMinBlobFaces = 12u;
    constexpr float kMinBlobExtent = 0.04f;

    if (mesh.vertices.size() == 0 || mesh.indices.size() == 0) return;
    const size_t verts = mesh.vertices.size();
    const size_t faces = mesh.indices.size() / 3;
    if (faces == 0) return;

    std::vector<uint32_t> parent(verts);
    for (uint32_t i = 0; i < verts; ++i) parent[i] = i;
    for (size_t i = 0; i < faces; ++i) {
        const uint32_t a = findRoot(parent, mesh.indices[i * 3]);
        for (int k = 1; k < 3; ++k) {
            const uint32_t b = findRoot(parent, mesh.indices[i * 3 + k]);
            if (b != a) parent[b] = a;
        }
    }
    for (uint32_t i = 0; i < verts; ++i) parent[i] = findRoot(parent, i);

    std::vector<uint32_t> facesPerRoot(verts, 0);
    std::vector<float> lo(verts * 3, 0.f);
    std::vector<float> hi(verts * 3, 0.f);
    std::vector<uint8_t> seen(verts, 0);
    for (size_t i = 0; i < faces; ++i) ++facesPerRoot[parent[mesh.indices[i * 3]]];
    for (uint32_t v = 0; v < verts; ++v) {
        const uint32_t r = parent[v];
        const MeshVertex& mv = mesh.vertices[v];
        const float c[3] = {mv.px, mv.py, mv.pz};
        for (int k = 0; k < 3; ++k) {
            const float val = c[k];
            if (!seen[r]) {
                lo[static_cast<size_t>(r) * 3 + k] = val;
                hi[static_cast<size_t>(r) * 3 + k] = val;
            } else {
                lo[static_cast<size_t>(r) * 3 + k] = std::min(lo[static_cast<size_t>(r) * 3 + k], val);
                hi[static_cast<size_t>(r) * 3 + k] = std::max(hi[static_cast<size_t>(r) * 3 + k], val);
            }
        }
        seen[r] = 1;
    }

    uint32_t mainRoot = 0;
    for (uint32_t v = 0; v < verts; ++v) {
        if (facesPerRoot[v] > facesPerRoot[mainRoot]) mainRoot = v;
    }

    std::vector<uint32_t> kept;
    kept.reserve(mesh.indices.size());
    size_t droppedFaces = 0;
    for (size_t i = 0; i < faces; ++i) {
        const uint32_t r = parent[mesh.indices[i * 3]];
        bool keep = (r == mainRoot) || (facesPerRoot[r] >= kMinBlobFaces);
        if (!keep) {
            const float dx = hi[static_cast<size_t>(r) * 3] - lo[static_cast<size_t>(r) * 3];
            const float dy = hi[static_cast<size_t>(r) * 3 + 1] - lo[static_cast<size_t>(r) * 3 + 1];
            const float dz = hi[static_cast<size_t>(r) * 3 + 2] - lo[static_cast<size_t>(r) * 3 + 2];
            keep = std::sqrt(dx * dx + dy * dy + dz * dz) >= kMinBlobExtent;
        }
        if (keep) {
            kept.push_back(mesh.indices[i * 3]);
            kept.push_back(mesh.indices[i * 3 + 1]);
            kept.push_back(mesh.indices[i * 3 + 2]);
        } else {
            ++droppedFaces;
        }
    }
    if (droppedFaces > 0) mesh.indices.assign(kept.begin(), kept.end());
}

void TangoRecon::smoothMesh(MeshData& mesh) {
    if (mesh.vertices.size() < 4 || mesh.indices.size() < 9) return;
    const size_t triCount = mesh.indices.size() / 3;
    const size_t vCount = mesh.vertices.size();

    // Count each edge's usage across the whole mesh: an edge that appears in
    // exactly one triangle is on the open boundary and its vertices never move.
    std::unordered_map<uint64_t, int> edgeUses;
    edgeUses.reserve(triCount * 2 + 1);
    for (size_t i = 0; i < triCount; ++i) {
        const uint32_t a = mesh.indices[i * 3 + 0];
        const uint32_t b = mesh.indices[i * 3 + 1];
        const uint32_t c = mesh.indices[i * 3 + 2];
        ++edgeUses[edgeKey(a, b)];
        ++edgeUses[edgeKey(b, c)];
        ++edgeUses[edgeKey(c, a)];
    }
    std::vector<uint8_t> boundary(vCount, 0);
    for (const auto& entry : edgeUses) {
        if (entry.second != 1) continue;
        boundary[static_cast<uint32_t>(entry.first >> 32)] = 1;
        boundary[static_cast<uint32_t>(entry.first & 0xFFFFFFFFull)] = 1;
    }

    std::vector<std::vector<uint32_t>> neighbors(vCount);
    for (size_t i = 0; i < triCount; ++i) {
        const uint32_t a = mesh.indices[i * 3 + 0];
        const uint32_t b = mesh.indices[i * 3 + 1];
        const uint32_t c = mesh.indices[i * 3 + 2];
        neighbors[a].push_back(b);
        neighbors[a].push_back(c);
        neighbors[b].push_back(a);
        neighbors[b].push_back(c);
        neighbors[c].push_back(a);
        neighbors[c].push_back(b);
    }

    std::vector<Vec3> pos(vCount);
    for (size_t i = 0; i < vCount; ++i) {
        pos[i] = Vec3{mesh.vertices[i].px, mesh.vertices[i].py, mesh.vertices[i].pz};
    }

    constexpr float kLambda = 0.5f;
    constexpr float kMu = -0.53f;
    auto pass = [&](float factor) {
        std::vector<Vec3> next = pos;
        for (size_t i = 0; i < vCount; ++i) {
            if (boundary[i] || neighbors[i].empty()) continue;
            Vec3 acc;
            for (const uint32_t n : neighbors[i]) acc += pos[n];
            const float inv = 1.f / static_cast<float>(neighbors[i].size());
            next[i] += (acc * inv - pos[i]) * factor;
        }
        pos.swap(next);
    };

    // Alternate expansion and contraction so sharp corners are not swallowed.
    pass(kLambda);
    pass(kMu);
    pass(kLambda);
    pass(kMu);

    for (size_t i = 0; i < vCount; ++i) {
        mesh.vertices[i].px = pos[i].x;
        mesh.vertices[i].py = pos[i].y;
        mesh.vertices[i].pz = pos[i].z;
    }
}

void TangoRecon::recomputeVertexNormals(MeshData& mesh) {
    const size_t nv = mesh.vertices.size();
    std::vector<Vec3> acc(nv);
    std::vector<int> cnt(nv, 0);
    const size_t triCount = mesh.indices.size() / 3;
    for (size_t i = 0; i < triCount; ++i) {
        const uint32_t ia = mesh.indices[i * 3 + 0];
        const uint32_t ib = mesh.indices[i * 3 + 1];
        const uint32_t ic = mesh.indices[i * 3 + 2];
        if (ia >= nv || ib >= nv || ic >= nv) continue;
        const Vec3 pa{mesh.vertices[ia].px, mesh.vertices[ia].py, mesh.vertices[ia].pz};
        const Vec3 pb{mesh.vertices[ib].px, mesh.vertices[ib].py, mesh.vertices[ib].pz};
        const Vec3 pc{mesh.vertices[ic].px, mesh.vertices[ic].py, mesh.vertices[ic].pz};
        const Vec3 n = cross(pb - pa, pc - pa);
        const float nl = n.length();
        if (nl < 1e-9f) continue;
        const Vec3 nn = n / nl;
        acc[ia] += nn;
        cnt[ia]++;
        acc[ib] += nn;
        cnt[ib]++;
        acc[ic] += nn;
        cnt[ic]++;
    }
    for (size_t i = 0; i < nv; ++i) {
        if (cnt[i] > 0) {
            const Vec3 n = acc[i];
            const float nl = n.length();
            if (nl > 1e-8f) {
                const Vec3 nn = n / nl;
                mesh.vertices[i].nx = nn.x;
                mesh.vertices[i].ny = nn.y;
                mesh.vertices[i].nz = nn.z;
            }
        }
    }
}

void TangoRecon::publish(bool force) {
    if (!dirty_ && !force) return;
    // 0.2 s is one publish every second or third fused frame: the mesh still
    // grows visibly while the whole cached surface is not re-copied more often
    // than the worker can afford (the copy is up to 1 M vertices by the end of
    // a long scan, and it sits in the same thread that fuses depth).
    if (!force && nowSeconds() - lastPublishSeconds_ < 0.20) return;
    publishLocked();
}

std::shared_ptr<const MeshData> TangoRecon::mesh() const {
    std::lock_guard<std::mutex> lock(snapshotMutex_);
    return snapshot_;
}

bool TangoRecon::buildFullMesh(MeshData& out) const {
    out = MeshData();
    if (!context_) return false;
    TangoApi& a = api();
    if (!a.ok) return false;

    Tango3DR_Mesh mesh{};
    if (a.meshInitEmpty(&mesh) != TANGO_3DR_SUCCESS) return false;
    if (a.extractFullMesh(context_, &mesh) != TANGO_3DR_SUCCESS) {
        a.meshDestroy(&mesh);
        return false;
    }

    // Export is the one place that copies the entire volume; a huge or
    // malformed result is refused instead of doubling the peak memory.
    constexpr uint32_t kMaxFullVertices = 3000000;
    constexpr uint32_t kMaxFullFaces = 5000000;
    if (!mesh.vertices || !mesh.faces || mesh.num_vertices == 0 || mesh.num_faces == 0 ||
        mesh.num_vertices > kMaxFullVertices || mesh.num_faces > kMaxFullFaces) {
        LOGE("extractFullMesh returned an unusable mesh (%u verts, %u faces)",
             mesh.num_vertices, mesh.num_faces);
        a.meshDestroy(&mesh);
        return false;
    }

    out.vertices.reserve(mesh.num_vertices);
    for (uint32_t v = 0; v < mesh.num_vertices; ++v) {
        MeshVertex vertex{};
        vertex.px = mesh.vertices[v][0];
        vertex.py = mesh.vertices[v][1];
        vertex.pz = mesh.vertices[v][2];
        if (mesh.normals) {
            vertex.nx = mesh.normals[v][0];
            vertex.ny = mesh.normals[v][1];
            vertex.nz = mesh.normals[v][2];
        } else {
            vertex.ny = 1.f;
        }
        if (mesh.colors) {
            vertex.r = mesh.colors[v][0];
            vertex.g = mesh.colors[v][1];
            vertex.b = mesh.colors[v][2];
        } else {
            vertex.r = vertex.g = vertex.b = 200;
        }
        vertex.a = 255;
        out.vertices.push_back(vertex);
    }
    out.indices.reserve(static_cast<size_t>(mesh.num_faces) * 3);
    for (uint32_t f = 0; f < mesh.num_faces; ++f) {
        const uint32_t i0 = mesh.faces[f][0];
        const uint32_t i1 = mesh.faces[f][1];
        const uint32_t i2 = mesh.faces[f][2];
        if (i0 >= mesh.num_vertices || i1 >= mesh.num_vertices || i2 >= mesh.num_vertices) {
            continue;
        }
        out.indices.push_back(i0);
        out.indices.push_back(i1);
        out.indices.push_back(i2);
    }
    out.triangleCount = static_cast<uint32_t>(out.indices.size() / 3);
    a.meshDestroy(&mesh);
    return !out.indices.empty();
}

}  // namespace recon
