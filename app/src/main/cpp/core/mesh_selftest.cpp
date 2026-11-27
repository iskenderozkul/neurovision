#include "core/mesh_selftest.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "core/frame.h"
#include "core/keyframe.h"
#include "mesh/mesher.h"
#include "tango/tango_recon.h"
#include "tsdf/integrator.h"
#include "tsdf/voxel_hash.h"

namespace recon {
namespace {

// ---------------------------------------------------------------------------
// Synthetic scene. World units are metres, OpenCV camera convention
// (X right, Y down, Z forward, so "up" in the room is -Y).
// ---------------------------------------------------------------------------

struct Scene {
    // Wall plane at z = 2 m, bounded to a 2 m x 2 m patch.
    static constexpr float kWallZ = 2.0f;
    static constexpr float kWallHalf = 1.0f;
    static constexpr float kBoxSize = 0.40f;
    static constexpr float kSphereR = 0.20f;

    // Plain functions instead of constexpr members: Vec3 is not a literal type.
    static Vec3 boxCenter() { return Vec3{0.30f, 0.20f, 1.20f}; }
    static Vec3 sphereCenter() { return Vec3{-0.40f, -0.20f, 1.00f}; }
};

float sdBox(const Vec3& p, const Vec3& c, float s) {
    const Vec3 d{p.x - c.x, p.y - c.y, p.z - c.z};
    const float dx = std::fabs(d.x) - s * 0.5f;
    const float dy = std::fabs(d.y) - s * 0.5f;
    const float dz = std::fabs(d.z) - s * 0.5f;
    const float ox = std::max(dx, 0.f);
    const float oy = std::max(dy, 0.f);
    const float oz = std::max(dz, 0.f);
    const float outside = std::sqrt(ox * ox + oy * oy + oz * oz);
    const float inside = std::min(std::max(dx, std::max(dy, dz)), 0.f);
    return outside + inside;
}

float sdSphere(const Vec3& p, const Vec3& c, float r) { return (p - c).length() - r; }

// Distance from p to the nearest *surface* of the scene. A signed field built
// as min(wall half-space, box, sphere) cannot be used for that: it describes
// the union of the solid half-space z < wall with the objects, so every point
// in front of the wall reads as "1.2 m inside" and every metric built on it is
// meaningless. The wall here is a 2 m x 2 m patch, so its distance is the
// distance to that rectangle.
float surfaceDistance(const Vec3& p) {
    const float dx = std::max(0.f, std::fabs(p.x) - Scene::kWallHalf);
    const float dy = std::max(0.f, std::fabs(p.y) - Scene::kWallHalf);
    const float dz = std::fabs(p.z - Scene::kWallZ);
    const float wall = std::sqrt(dx * dx + dy * dy + dz * dz);
    const float box = std::fabs(sdBox(p, Scene::boxCenter(), Scene::kBoxSize));
    const float sphere = std::fabs(sdSphere(p, Scene::sphereCenter(), Scene::kSphereR));
    return std::min(wall, std::min(box, sphere));
}

bool intersectPlane(const Vec3& ro, const Vec3& rd, float& t) {
    if (std::fabs(rd.z) < 1e-6f) return false;
    const float tt = (Scene::kWallZ - ro.z) / rd.z;
    if (tt <= 0.f) return false;
    const Vec3 p = ro + rd * tt;
    if (std::fabs(p.x) > Scene::kWallHalf || std::fabs(p.y) > Scene::kWallHalf) return false;
    t = tt;
    return true;
}

bool intersectBox(const Vec3& ro, const Vec3& rd, float& t) {
    const Vec3 c = Scene::boxCenter();
    const float h = Scene::kBoxSize * 0.5f;
    const Vec3 lo{c.x - h, c.y - h, c.z - h};
    const Vec3 hi{c.x + h, c.y + h, c.z + h};
    float tmin = 0.f;
    float tmax = 1e9f;
    for (int i = 0; i < 3; ++i) {
        const float o = (&ro.x)[i];
        const float d = (&rd.x)[i];
        const float l = (&lo.x)[i];
        const float hh = (&hi.x)[i];
        if (std::fabs(d) < 1e-8f) {
            if (o < l || o > hh) return false;
        } else {
            float t1 = (l - o) / d;
            float t2 = (hh - o) / d;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    t = tmin;
    return true;
}

bool intersectSphere(const Vec3& ro, const Vec3& rd, float& t) {
    const Vec3 oc = ro - Scene::sphereCenter();
    const float b = oc.dot(rd);
    const float c = oc.dot(oc) - Scene::kSphereR * Scene::kSphereR;
    const float disc = b * b - c;
    if (disc < 0.f) return false;
    const float sq = std::sqrt(disc);
    if (-b - sq > 0.f) {
        t = -b - sq;
        return true;
    }
    if (-b + sq > 0.f) {
        t = -b + sq;
        return true;
    }
    return false;
}

// Nearest surface hit along a normalised ray, or -1 when the ray misses.
float raycast(const Vec3& ro, const Vec3& rd) {
    float best = -1.f;
    float t;
    if (intersectPlane(ro, rd, t) && (best < 0.f || t < best)) best = t;
    if (intersectBox(ro, rd, t) && (best < 0.f || t < best)) best = t;
    if (intersectSphere(ro, rd, t) && (best < 0.f || t < best)) best = t;
    return best;
}

// ---------------------------------------------------------------------------
// Deterministic noise (no <random>, identical on every run and device).
// ---------------------------------------------------------------------------

float hash01(int n) {
    uint32_t x = static_cast<uint32_t>(n) * 2654435761u + 2246822519u;
    x ^= x >> 13;
    x *= 2654435761u;
    x ^= x >> 16;
    return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
}

float noise(int n) { return (hash01(n) - 0.5f) * 2.f; }

// ---------------------------------------------------------------------------
// Camera helpers.
// ---------------------------------------------------------------------------

Mat3 lookAt(const Vec3& eye, const Vec3& target) {
    const Vec3 f = (target - eye).normalized();
    const Vec3 r = cross(f, Vec3{0.f, -1.f, 0.f}).normalized();
    const Vec3 u = cross(f, r).normalized();
    Mat3 R;
    R.m[0] = r.x;
    R.m[1] = u.x;
    R.m[2] = f.x;
    R.m[3] = r.y;
    R.m[4] = u.y;
    R.m[5] = f.y;
    R.m[6] = r.z;
    R.m[7] = u.z;
    R.m[8] = f.z;
    return R;
}

void matToQuat(const Mat3& R, float& qx, float& qy, float& qz, float& qw) {
    const float trace = R.m[0] + R.m[4] + R.m[8];
    if (trace > 0.f) {
        const float s = std::sqrt(trace + 1.f) * 2.f;
        qw = 0.25f * s;
        qx = (R.m[7] - R.m[5]) / s;
        qy = (R.m[2] - R.m[6]) / s;
        qz = (R.m[3] - R.m[1]) / s;
    } else if (R.m[0] > R.m[4] && R.m[0] > R.m[8]) {
        const float s = std::sqrt(1.f + R.m[0] - R.m[4] - R.m[8]) * 2.f;
        qw = (R.m[7] - R.m[5]) / s;
        qx = 0.25f * s;
        qy = (R.m[1] + R.m[3]) / s;
        qz = (R.m[2] + R.m[6]) / s;
    } else if (R.m[4] > R.m[8]) {
        const float s = std::sqrt(1.f + R.m[4] - R.m[0] - R.m[8]) * 2.f;
        qw = (R.m[2] - R.m[6]) / s;
        qx = (R.m[1] + R.m[3]) / s;
        qy = 0.25f * s;
        qz = (R.m[5] + R.m[7]) / s;
    } else {
        const float s = std::sqrt(1.f + R.m[8] - R.m[0] - R.m[4]) * 2.f;
        qw = (R.m[3] - R.m[1]) / s;
        qx = (R.m[2] + R.m[6]) / s;
        qy = (R.m[5] + R.m[7]) / s;
        qz = 0.25f * s;
    }
}

// ---------------------------------------------------------------------------
// Synthetic frames.
// ---------------------------------------------------------------------------

constexpr int kColorW = 320;
constexpr int kColorH = 240;
constexpr int kDepthW = 160;
constexpr int kDepthH = 120;
constexpr float kDepthFx = 140.f;
constexpr float kDepthFy = 140.f;
constexpr int kFrames = 12;

struct SyntheticFrame {
    FrameInput frame;
    Mat3 Rcv;
    Vec3 eye;
    int64_t timestampNs = 0;
};

SyntheticFrame makeFrame(int index, int count) {
    const float kCx = (kDepthW - 1) * 0.5f;
    const float kCy = (kDepthH - 1) * 0.5f;

    const float angle = 2.f * kPi * static_cast<float>(index) / static_cast<float>(count);
    // The whole 2 m wall patch must be inside the frame: with fy = 140 and
    // h = 120 the vertical half-FOV is 23.2 deg, so the camera needs ~2.3 m of
    // standoff to cover +/-1 m. A tighter orbit aimed at the wall centre gives
    // every frame the same surface instead of a sliver of it.
    const Vec3 eye{0.35f * std::cos(angle), 0.35f * std::sin(angle), -0.30f};
    const Vec3 target{0.f, 0.f, 1.70f};
    const Mat3 Rcv = lookAt(eye, target);

    FrameInput f;
    f.width = kColorW;
    f.height = kColorH;
    f.y.assign(static_cast<size_t>(kColorW) * kColorH, 128);
    f.u.assign(static_cast<size_t>(kColorW / 2) * (kColorH / 2), 128);
    f.v.assign(static_cast<size_t>(kColorW / 2) * (kColorH / 2), 128);
    f.timestampNs = 1000000000LL + static_cast<int64_t>(index) * 33000000LL;
    f.depthApiActive = true;
    f.depthWidth = kDepthW;
    f.depthHeight = kDepthH;
    f.depthMm.assign(static_cast<size_t>(kDepthW) * kDepthH, 0);
    f.depthTimestampNs = f.timestampNs;
    f.tracking = true;

    // Colour intrinsics for the 4:3 CPU image (used only for vertex colour).
    f.K.fx = 280.f;
    f.K.fy = 280.f;
    f.K.cx = (kColorW - 1) * 0.5f;
    f.K.cy = (kColorH - 1) * 0.5f;
    f.K.width = kColorW;
    f.K.height = kColorH;

    // Depth intrinsics: the grid the synthetic depth map is rendered with.
    f.Kdepth.fx = kDepthFx;
    f.Kdepth.fy = kDepthFy;
    f.Kdepth.cx = kCx;
    f.Kdepth.cy = kCy;
    f.Kdepth.width = kDepthW;
    f.Kdepth.height = kDepthH;

    const Mat3 Rgl = cvToArcoreRotation(Rcv);
    matToQuat(Rgl, f.qx, f.qy, f.qz, f.qw);
    f.tx = eye.x;
    f.ty = eye.y;
    f.tz = eye.z;

    for (int y = 0; y < kDepthH; ++y) {
        for (int x = 0; x < kDepthW; ++x) {
            Vec3 dc((static_cast<float>(x) - kCx) / kDepthFx,
                    (static_cast<float>(y) - kCy) / kDepthFy, 1.f);
            dc = dc.normalized();
            const Vec3 rd = Rcv * dc;
            const float t = raycast(eye, rd);
            float depth = (t > 0.f) ? t * dc.z : 0.f;
            // A fixed rectangular hole in the wall: the mesh must show a hole
            // there, not a filled patch.
            const bool inHole = x >= 60 && x < 100 && y >= 40 && y < 80;
            if (inHole) depth = 0.f;
            // Deterministic 3 mm noise on valid samples.
            if (depth > 0.f) depth += noise(index * 100000 + y * 1000 + x) * 0.003f;
            if (depth <= 0.f) {
                f.depthMm[static_cast<size_t>(y) * kDepthW + x] = 0;
            } else {
                const float mm = depth * 1000.f;
                f.depthMm[static_cast<size_t>(y) * kDepthW + x] =
                    mm > 65535.f ? 65535 : static_cast<uint16_t>(mm);
            }
        }
    }

    SyntheticFrame sf;
    sf.frame = std::move(f);
    sf.Rcv = Rcv;
    sf.eye = eye;
    sf.timestampNs = 1000000000LL + static_cast<int64_t>(index) * 33000000LL;
    return sf;
}

// ---------------------------------------------------------------------------
// Metrics.
// ---------------------------------------------------------------------------

struct MeshMetrics {
    int tris = 0;
    int verts = 0;
    int boundaryEdges = 0;
    int components = 0;
    int degenerate = 0;
    int hallucination = 0;
    int overlap = 0;
    float surfErrMean = 0.f;
    float surfErrP95 = 0.f;
    float normalAgreement = 0.f;
    float bboxMin[3] = {0.f, 0.f, 0.f};
    float bboxMax[3] = {0.f, 0.f, 0.f};
    float within3cm = 0.f;
};

int findRoot(std::vector<int>& parent, int x) {
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

MeshMetrics computeMetrics(const MeshData& mesh) {
    MeshMetrics m;
    m.verts = static_cast<int>(mesh.vertices.size());
    m.tris = static_cast<int>(mesh.indices.size() / 3);

    std::map<std::pair<uint32_t, uint32_t>, int> edgeUse;
    std::vector<int> parent(static_cast<size_t>(m.verts), 0);
    for (int i = 0; i < m.verts; ++i) parent[i] = i;

    auto addEdge = [&](uint32_t a, uint32_t b) {
        if (a == b) return;
        if (a > b) std::swap(a, b);
        ++edgeUse[{a, b}];
    };
    auto unite = [&](uint32_t a, uint32_t b) {
        const int ra = findRoot(parent, static_cast<int>(a));
        const int rb = findRoot(parent, static_cast<int>(b));
        if (ra != rb) parent[ra] = rb;
    };

    double normalDotSum = 0.0;
    long long normalSamples = 0;

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const uint32_t a = mesh.indices[i];
        const uint32_t b = mesh.indices[i + 1];
        const uint32_t c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() ||
            c >= mesh.vertices.size()) {
            ++m.degenerate;
            continue;
        }
        addEdge(a, b);
        addEdge(b, c);
        addEdge(c, a);
        unite(a, b);
        unite(b, c);

        const MeshVertex& va = mesh.vertices[a];
        const MeshVertex& vb = mesh.vertices[b];
        const MeshVertex& vc = mesh.vertices[c];
        const Vec3 e1{vb.px - va.px, vb.py - va.py, vb.pz - va.pz};
        const Vec3 e2{vc.px - va.px, vc.py - va.py, vc.pz - va.pz};
        const Vec3 n = cross(e1, e2);
        if (n.length() < 1e-9f) ++m.degenerate;

        // Normal agreement: a face normal must point the same way as the
        // vertex normals it is built from. Disagreement is the signature of the
        // broken-normal triangles reported on device.
        const Vec3 fn = n.normalized();
        const Vec3 vn{va.nx + vb.nx + vc.nx, va.ny + vb.ny + vc.ny,
                      va.nz + vb.nz + vc.nz};
        if (vn.lengthSq() > 1e-8f) {
            normalDotSum += std::fabs(fn.dot(vn.normalized()));
            ++normalSamples;
        }

        const Vec3 centroid{(va.px + vb.px + vc.px) / 3.f,
                            (va.py + vb.py + vc.py) / 3.f,
                            (va.pz + vb.pz + vc.pz) / 3.f};
        if (sdBox(centroid, Scene::boxCenter(), Scene::kBoxSize) < -0.02f ||
            sdSphere(centroid, Scene::sphereCenter(), Scene::kSphereR) < -0.02f ||
            centroid.z > Scene::kWallZ + 0.02f) {
            ++m.overlap;
        }

        float worst = surfaceDistance(Vec3{va.px, va.py, va.pz});
        worst = std::max(worst, surfaceDistance(Vec3{vb.px, vb.py, vb.pz}));
        worst = std::max(worst, surfaceDistance(Vec3{vc.px, vc.py, vc.pz}));
        if (worst > 0.05f) ++m.hallucination;
    }

    for (const auto& e : edgeUse) {
        if (e.second == 1) ++m.boundaryEdges;
    }

    std::vector<float> errors;
    errors.reserve(static_cast<size_t>(m.verts));
    int near = 0;
    for (int i = 0; i < m.verts; ++i) {
        const MeshVertex& v = mesh.vertices[i];
        const float e = surfaceDistance(Vec3{v.px, v.py, v.pz});
        errors.push_back(e);
        if (e <= 0.03f) ++near;
        if (i == 0) {
            m.bboxMin[0] = m.bboxMax[0] = v.px;
            m.bboxMin[1] = m.bboxMax[1] = v.py;
            m.bboxMin[2] = m.bboxMax[2] = v.pz;
        } else {
            m.bboxMin[0] = std::min(m.bboxMin[0], v.px);
            m.bboxMax[0] = std::max(m.bboxMax[0], v.px);
            m.bboxMin[1] = std::min(m.bboxMin[1], v.py);
            m.bboxMax[1] = std::max(m.bboxMax[1], v.py);
            m.bboxMin[2] = std::min(m.bboxMin[2], v.pz);
            m.bboxMax[2] = std::max(m.bboxMax[2], v.pz);
        }
    }
    if (m.verts > 0) m.within3cm = 100.f * static_cast<float>(near) / m.verts;
    if (!errors.empty()) {
        std::sort(errors.begin(), errors.end());
        double sum = 0.0;
        for (float e : errors) sum += e;
        m.surfErrMean = static_cast<float>(sum / errors.size());
        m.surfErrP95 = errors[static_cast<size_t>(errors.size() * 95 / 100)];
    }
    if (normalSamples > 0) {
        m.normalAgreement = static_cast<float>(normalDotSum / normalSamples);
    }

    // Connected components over the vertices that actually take part in a
    // triangle. Isolated vertices are not a disconnected surface.
    std::vector<bool> used(static_cast<size_t>(m.verts), false);
    for (size_t i = 0; i < mesh.indices.size(); ++i) used[mesh.indices[i]] = true;
    std::unordered_set<int> usedRoots;
    for (int i = 0; i < m.verts; ++i) {
        if (used[i]) usedRoots.insert(findRoot(parent, i));
    }
    m.components = static_cast<int>(usedRoots.size());
    return m;
}

// A coarse occupancy map of the wall patch (10 x 10 cells, 20 cm each). A hole
// in the reconstruction is visible here as a run of '.' even when the summary
// numbers look healthy, and a border-only mesh is obvious at a glance.
std::string wallCoverageMap(const MeshData& mesh) {
    constexpr int kN = 10;
    int cells[kN][kN] = {{0}};
    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        if (mesh.indices[i] >= mesh.vertices.size() ||
            mesh.indices[i + 1] >= mesh.vertices.size() ||
            mesh.indices[i + 2] >= mesh.vertices.size()) {
            continue;
        }
        const MeshVertex& a = mesh.vertices[mesh.indices[i]];
        const MeshVertex& b = mesh.vertices[mesh.indices[i + 1]];
        const MeshVertex& c = mesh.vertices[mesh.indices[i + 2]];
        const float cx = (a.px + b.px + c.px) / 3.f;
        const float cy = (a.py + b.py + c.py) / 3.f;
        const float cz = (a.pz + b.pz + c.pz) / 3.f;
        if (std::fabs(cz - Scene::kWallZ) > 0.05f) continue;
        if (std::fabs(cx) > Scene::kWallHalf || std::fabs(cy) > Scene::kWallHalf) continue;
        const int ix = std::min(kN - 1, static_cast<int>((cx + Scene::kWallHalf) / (2.f * Scene::kWallHalf) * kN));
        const int iy = std::min(kN - 1, static_cast<int>((cy + Scene::kWallHalf) / (2.f * Scene::kWallHalf) * kN));
        if (cells[iy][ix] < 9) ++cells[iy][ix];
    }
    std::string out = "selftest wallmap (20cm cells, 0-9 = triangles/8):\n";
    for (int y = 0; y < kN; ++y) {
        std::string row = "selftest   ";
        for (int x = 0; x < kN; ++x) {
            row += cells[y][x] == 0 ? '.' : static_cast<char>('0' + std::min(9, cells[y][x] / 8));
        }
        out += row + "\n";
    }
    return out;
}

std::string formatMetrics(const char* path, const MeshMetrics& m, double ms) {
    char buf[900];
    std::snprintf(buf, sizeof(buf),
                  "selftest path=%s tris=%d verts=%d boundary_edges=%d components=%d "
                  "degenerate=%d hallucination=%d overlap=%d normal_agreement=%.3f "
                  "surf_err_mean=%.4f surf_err_p95=%.4f within3cm=%.0f%% "
                  "bbox=%.2f..%.2f,%.2f..%.2f,%.2f..%.2f ms=%.0f",
                  path, m.tris, m.verts, m.boundaryEdges, m.components, m.degenerate,
                  m.hallucination, m.overlap, static_cast<double>(m.normalAgreement),
                  static_cast<double>(m.surfErrMean), static_cast<double>(m.surfErrP95),
                  static_cast<double>(m.within3cm),
                  static_cast<double>(m.bboxMin[0]), static_cast<double>(m.bboxMax[0]),
                  static_cast<double>(m.bboxMin[1]), static_cast<double>(m.bboxMax[1]),
                  static_cast<double>(m.bboxMin[2]), static_cast<double>(m.bboxMax[2]), ms);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Pipelines.
// ---------------------------------------------------------------------------

std::string runTangoPath(const std::vector<SyntheticFrame>& frames) {
    const auto start = std::chrono::steady_clock::now();
    TangoRecon tango;
    if (!tango.init(0.03, 6.0, 0.20)) return "selftest path=tango unavailable";
    int integrated = 0;
    for (const SyntheticFrame& sf : frames) {
        if (tango.integrate(sf.frame, nullptr)) ++integrated;
    }
    tango.publish(true);
    MeshData mesh;
    if (!tango.buildFullMesh(mesh) || mesh.indices.empty()) {
        return "selftest path=tango mesh_empty integrated=" + std::to_string(integrated);
    }
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
    return formatMetrics("tango", computeMetrics(mesh), ms) +
           " integrated=" + std::to_string(integrated) + "\n" + wallCoverageMap(mesh);
}

std::string runLegacyPath(const std::vector<SyntheticFrame>& frames) {
    const auto start = std::chrono::steady_clock::now();
    VoxelHash hash(0.03f, 20000);
    TSDFIntegrator integrator(1);
    MeshBuilder mesher;
    std::vector<VoxelBlock*> dirty;
    std::string growth;
    for (const SyntheticFrame& sf : frames) {
        Keyframe kf;
        kf.timestampNs = sf.timestampNs;
        kf.w = sf.frame.depthWidth;
        kf.h = sf.frame.depthHeight;
        // The integrator samples the keyframe colour for every voxel it fuses,
        // so the buffers must exist even though this test only checks geometry.
        kf.luma.assign(static_cast<size_t>(kf.w) * kf.h, 128);
        kf.rgb.assign(static_cast<size_t>(kf.w) * kf.h * 3, 200);
        kf.R_gl = cvToArcoreRotation(sf.Rcv);
        kf.t = sf.eye;
        kf.K = sf.frame.Kdepth;

        DepthResult depth;
        depth.resize(kf.w, kf.h);
        for (size_t i = 0; i < depth.depth.size(); ++i) {
            const uint16_t mm = sf.frame.depthMm[i];
            depth.depth[i] = static_cast<float>(mm) * 0.001f;
            depth.confidence[i] = mm > 0 ? 1.f : 0.f;
        }
        integrator.integrate(kf, depth, hash, dirty);
        mesher.extractDirty(hash, dirty);
        growth += std::to_string(mesher.triangleCount()) + ",";
    }
    mesher.rebuildAll(hash);
    const auto mesh = mesher.snapshot();
    if (!mesh || mesh->indices.empty()) {
        return "selftest path=legacy mesh_empty blocks=" + std::to_string(hash.size());
    }
    // How many surface voxels carry only one observation? Marching cubes drops
    // a triangle unless both of its corners were seen at least twice, so a low
    // share here is the direct explanation for a sparse mesh.
    long long surfaceVoxels = 0, weight1 = 0, weight2 = 0;
    long long negVoxels = 0, posVoxels = 0, crossings = 0;
    constexpr int kCells = 10;
    int crossCells[kCells][kCells] = {{0}};
    const float vs = hash.voxelSize();
    for (const auto& owned : hash.blocks()) {
        for (int i = 0; i < VoxelBlock::kTotal; ++i) {
            if (owned->weight[i] == 0) continue;
            if (owned->tsdf[i] < 0) ++negVoxels; else ++posVoxels;
            if (std::abs(owned->tsdf[i]) > VoxelHash::kFixedScale / 10) continue;
            ++surfaceVoxels;
            if (owned->weight[i] == 1) ++weight1;
            else ++weight2;
        }
        // Cubes whose corners disagree about the sign are exactly the ones
        // marching cubes has to turn into triangles. Comparing this count with
        // the triangle count separates an integration problem (few crossings)
        // from an extraction problem (many crossings, few triangles).
        for (int z = 0; z + 1 < VoxelBlock::kSize; ++z) {
            for (int y = 0; y + 1 < VoxelBlock::kSize; ++y) {
                for (int x = 0; x + 1 < VoxelBlock::kSize; ++x) {
                    bool anyNeg = false, anyPos = false, allKnown = true;
                    for (int c = 0; c < 8; ++c) {
                        const int idx = VoxelBlock::index(x + (c & 1), y + ((c >> 1) & 1),
                                                          z + ((c >> 2) & 1));
                        if (owned->weight[idx] == 0) { allKnown = false; break; }
                        if (owned->tsdf[idx] < 0) anyNeg = true; else anyPos = true;
                    }
                    if (allKnown && anyNeg && anyPos) {
                        ++crossings;
                        // Bin by the cube centre so the map below shows where
                        // the sign changes actually are, not where the mesher
                        // decided to draw.
                        const float cx = static_cast<float>(owned->bx * VoxelBlock::kSize + x + 1) * vs;
                        const float cy = static_cast<float>(owned->by * VoxelBlock::kSize + y + 1) * vs;
                        const float cz = static_cast<float>(owned->bz * VoxelBlock::kSize + z + 1) * vs;
                        if (std::fabs(cz - Scene::kWallZ) < 0.06f &&
                            std::fabs(cx) <= Scene::kWallHalf && std::fabs(cy) <= Scene::kWallHalf) {
                            const int ix = std::min(
                                kCells - 1, static_cast<int>((cx + Scene::kWallHalf) /
                                                            (2.f * Scene::kWallHalf) * kCells));
                            const int iy = std::min(
                                kCells - 1, static_cast<int>((cy + Scene::kWallHalf) /
                                                            (2.f * Scene::kWallHalf) * kCells));
                            if (crossCells[iy][ix] < 999) ++crossCells[iy][ix];
                        }
                    }
                }
            }
        }
    }    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - start)
                          .count();
    char extra[192];
    std::snprintf(extra, sizeof(extra),
                  " blocks=%zu surface_voxels=%lld w1=%lld w2plus=%lld neg=%lld pos=%lld "
                  "crossings=%lld growth=%s",
                  hash.size(), surfaceVoxels, weight1, weight2, negVoxels, posVoxels,
                  crossings, growth.c_str());

    std::string crossMap = "selftest crossmap (20cm cells, 0-9 = crossings/5):\n";
    for (int y = 0; y < kCells; ++y) {
        std::string row = "selftest   ";
        for (int x = 0; x < kCells; ++x) {
            row += crossCells[y][x] == 0
                       ? '.'
                       : static_cast<char>('0' + std::min(9, crossCells[y][x] / 5));
        }
        crossMap += row + "\n";
    }
    return formatMetrics("legacy", computeMetrics(*mesh), ms) + extra + "\n" +
           crossMap + wallCoverageMap(*mesh);
}

}  // namespace

std::string runMeshSelfTest() {
    std::vector<SyntheticFrame> frames;
    frames.reserve(kFrames);
    for (int i = 0; i < kFrames; ++i) frames.push_back(makeFrame(i, kFrames));

    std::string out;
    out += "selftest scene=wall+box+sphere frames=" + std::to_string(kFrames) +
           " voxel=30mm noise=3mm hole=40x40 depth=" + std::to_string(kDepthW) + "x" +
           std::to_string(kDepthH) + "\n";

    // The synthetic depth map is checked against the analytic surface before it
    // is used. If this line is not near zero every mesh metric below is
    // meaningless, because the input itself does not describe the scene.
    {
        long long total = 0, valid = 0;
        double sum = 0.0;
        std::vector<float> errs;
        for (const SyntheticFrame& sf : frames) {
            const Intrinsics& K = sf.frame.Kdepth;
            for (int y = 0; y < K.height; ++y) {
                for (int x = 0; x < K.width; ++x) {
                    ++total;
                    const uint16_t mm =
                        sf.frame.depthMm[static_cast<size_t>(y) * K.width + x];
                    if (mm == 0) continue;
                    ++valid;
                    const Vec3 pCv = K.unproject(static_cast<float>(x), static_cast<float>(y),
                                                 static_cast<float>(mm) * 0.001f);
                    const Vec3 pWorld = sf.Rcv * pCv + sf.eye;
                    const float e = surfaceDistance(pWorld);
                    sum += e;
                    errs.push_back(e);
                }
            }
        }
        std::sort(errs.begin(), errs.end());
        const float p95 = errs.empty() ? 0.f : errs[errs.size() * 95 / 100];
        long long bad = 0;
        for (float e : errs) if (e > 0.05f) ++bad;
        char line[224];
        std::snprintf(line, sizeof(line),
                      "selftest depthcheck valid=%.0f%% err_mean=%.4f err_p95=%.4f bad=%.0f%%",
                      total > 0 ? 100.0 * static_cast<double>(valid) / total : 0.0,
                      valid > 0 ? sum / static_cast<double>(valid) : 0.0,
                      static_cast<double>(p95),
                      valid > 0 ? 100.0 * static_cast<double>(bad) / static_cast<double>(valid)
                                : 0.0);
        out += line;
        out += "\n";

        // Worst single sample, with its pixel and world point: a systematic
        // geometry or calibration mistake shows up as one repeating pattern
        // (a band of pixels, a whole side of the image) instead of noise.
        float worstErr = -1.f;
        int worstX = 0, worstY = 0, worstFrame = 0;
        float worstD = 0.f;
        Vec3 worstP;
        for (size_t fi = 0; fi < frames.size(); ++fi) {
            const SyntheticFrame& sf = frames[fi];
            const Intrinsics& K = sf.frame.Kdepth;
            for (int y = 0; y < K.height; ++y) {
                for (int x = 0; x < K.width; ++x) {
                    const uint16_t mm =
                        sf.frame.depthMm[static_cast<size_t>(y) * K.width + x];
                    if (mm == 0) continue;
                    const float d = static_cast<float>(mm) * 0.001f;
                    const Vec3 pWorld = sf.Rcv * K.unproject(static_cast<float>(x),
                                                             static_cast<float>(y), d) +
                                        sf.eye;
                    const float e = surfaceDistance(pWorld);
                    if (e > worstErr) {
                        worstErr = e;
                        worstX = x;
                        worstY = y;
                        worstFrame = static_cast<int>(fi);
                        worstD = d;
                        worstP = pWorld;
                    }
                }
            }
        }
        std::snprintf(line, sizeof(line),
                      "selftest depthworst frame=%d u=%d v=%d d=%.3f p=%.3f,%.3f,%.3f err=%.3f",
                      worstFrame, worstX, worstY, static_cast<double>(worstD),
                      static_cast<double>(worstP.x), static_cast<double>(worstP.y),
                      static_cast<double>(worstP.z), static_cast<double>(worstErr));
        out += line;
        out += "\n";
    }
    out += runTangoPath(frames) + "\n";
    out += runLegacyPath(frames) + "\n";
    out += "selftest done\n";
    return out;
}

std::string liveMeshQualityLine(const MeshData& mesh, const char* tag) {
    const int tris = static_cast<int>(mesh.indices.size() / 3);
    const int verts = static_cast<int>(mesh.vertices.size());
    if (tris <= 0) return std::string("meshq tag=") + tag + " tris=0 verts=0";

    // Stride-sampled so a 1 M vertex snapshot costs microseconds: this runs on
    // the worker thread next to the fusion, it must never become the frame cost.
    constexpr size_t kStride = 97;
    long long sampled = 0;
    long long degenerate = 0;
    double edgeSum = 0.0;
    long long edgeCount = 0;
    double normalSum = 0.0;
    long long normalCount = 0;
    double minEdge = 1e9;
    double maxEdge = 0.0;

    for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3 * kStride) {
        const uint32_t a = mesh.indices[i];
        const uint32_t b = mesh.indices[i + 1];
        const uint32_t c = mesh.indices[i + 2];
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() ||
            c >= mesh.vertices.size()) {
            ++degenerate;
            ++sampled;
            continue;
        }
        ++sampled;
        const MeshVertex& va = mesh.vertices[a];
        const MeshVertex& vb = mesh.vertices[b];
        const MeshVertex& vc = mesh.vertices[c];
        const Vec3 e1{vb.px - va.px, vb.py - va.py, vb.pz - va.pz};
        const Vec3 e2{vc.px - va.px, vc.py - va.py, vc.pz - va.pz};
        const Vec3 n = cross(e1, e2);
        const float area2 = n.length();
        if (area2 < 1e-9f) {
            ++degenerate;
            continue;
        }
        const double e = 0.5 * (static_cast<double>(e1.length()) +
                                static_cast<double>(e2.length()) +
                                static_cast<double>((e2 - e1).length()));
        edgeSum += e;
        minEdge = std::min(minEdge, e);
        maxEdge = std::max(maxEdge, e);
        ++edgeCount;

        const Vec3 vn{va.nx + vb.nx + vc.nx, va.ny + vb.ny + vc.ny, va.nz + vb.nz + vc.nz};
        if (vn.lengthSq() > 1e-8f) {
            normalSum += std::fabs(n.normalized().dot(vn.normalized()));
            ++normalCount;
        }
    }

    char buf[320];
    std::snprintf(buf, sizeof(buf),
                  "meshq tag=%s tris=%d verts=%d v_per_tri=%.2f degenerate=%.1f%% "
                  "edge_mean=%.3f edge_min=%.3f edge_max=%.3f normal_agreement=%.3f",
                  tag, tris, verts, tris > 0 ? static_cast<double>(verts) / tris : 0.0,
                  sampled > 0 ? 100.0 * static_cast<double>(degenerate) / sampled : 0.0,
                  edgeCount > 0 ? edgeSum / edgeCount : 0.0, minEdge == 1e9 ? 0.0 : minEdge,
                  maxEdge, normalCount > 0 ? normalSum / normalCount : 0.0);
    return std::string(buf);
}

}  // namespace recon
