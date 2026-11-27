#include "motion/motion_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace nv {
namespace {

// LK parameters. The 15x15 window is used at every pyramid level (in that
// level's pixels); tracking runs coarse-to-fine over levels 2, 1, 0.
constexpr int kHalfWin = 7;
constexpr int kWinSize = 2 * kHalfWin + 1;
constexpr float kWinPixels = static_cast<float>(kWinSize * kWinSize);  // 225
constexpr float kMaxStep = 2.f;          // clamp one Gauss-Newton step
constexpr float kMaxResidual = 12.f;     // mean |luma diff| rejection
constexpr float kMinDet = 25.f;          // ill-conditioned 2x2 guard
constexpr float kInitFlowClamp = 8.f;    // previous median flow prior
constexpr int kIterations = 4;           // GN steps per level (coarse-to-fine handles the rest)

// Corner detection parameters (level 0, 8x6 grid of 40x40 cells).
constexpr int kDetectRadius = 3;         // 7x7 response box
constexpr int kGridCols = 8;
constexpr int kGridRows = 6;
constexpr int kPerCell = 2;              // 48 cells x 2 = 96 features
constexpr float kCornerMinResponse = 2000.f;
constexpr float kMinFeatureDist2 = 100.f;  // 10 px squared
constexpr int kFeatureMin = 40;          // replenish before tracking
constexpr int kFeatureReplenish = 50;    // replenish after tracking
constexpr int kDetectInterval = 12;      // periodic refresh

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Bilinear sample with clamped edges; works for uint8_t images and float
// gradient maps alike. Coordinates outside the image are harmless here.
template <typename T>
inline float sampleBilinear(const T* img, int w, int h, float x, float y) {
    x = clampf(x, 0.f, static_cast<float>(w - 1));
    y = clampf(y, 0.f, static_cast<float>(h - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = x0 + 1 < w ? x0 + 1 : x0;
    const int y1 = y0 + 1 < h ? y0 + 1 : y0;
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float a = static_cast<float>(img[y0 * w + x0]);
    const float b = static_cast<float>(img[y0 * w + x1]);
    const float c = static_cast<float>(img[y1 * w + x0]);
    const float d = static_cast<float>(img[y1 * w + x1]);
    return (a + (b - a) * fx) * (1.f - fy) + (c + (d - c) * fx) * fy;
}

void computeGradients(const uint8_t* img, int w, int h, float* gx, float* gy) {
    for (int y = 0; y < h; ++y) {
        const int yu = y > 0 ? y - 1 : y;
        const int yd = y + 1 < h ? y + 1 : y;
        const uint8_t* row = img + y * w;
        const uint8_t* up = img + yu * w;
        const uint8_t* down = img + yd * w;
        for (int x = 0; x < w; ++x) {
            const int xl = x > 0 ? x - 1 : x;
            const int xr = x + 1 < w ? x + 1 : x;
            gx[y * w + x] =
                static_cast<float>(static_cast<int>(row[xr]) - static_cast<int>(row[xl]));
            gy[y * w + x] =
                static_cast<float>(static_cast<int>(down[x]) - static_cast<int>(up[x]));
        }
    }
}

// Separable box filter with clamped borders; src and dst may alias (tmp may
// not). Used for the Shi-Tomasi structure tensor sums.
void boxFilter(const float* src, float* tmp, float* dst, int w, int h, int r) {
    for (int y = 0; y < h; ++y) {
        const float* row = src + y * w;
        float* out = tmp + y * w;
        float sum = 0.f;
        for (int u = -r; u <= r; ++u) {
            const int xx = u < 0 ? 0 : (u >= w ? w - 1 : u);
            sum += row[xx];
        }
        for (int x = 0; x < w; ++x) {
            out[x] = sum;
            const int add = x + r + 1 >= w ? w - 1 : x + r + 1;
            const int rem = x - r < 0 ? 0 : x - r;
            sum += row[add] - row[rem];
        }
    }
    for (int x = 0; x < w; ++x) {
        float sum = 0.f;
        for (int v = -r; v <= r; ++v) {
            const int yy = v < 0 ? 0 : (v >= h ? h - 1 : v);
            sum += tmp[yy * w + x];
        }
        for (int y = 0; y < h; ++y) {
            dst[y * w + x] = sum;
            const int add = y + r + 1 >= h ? h - 1 : y + r + 1;
            const int rem = y - r < 0 ? 0 : y - r;
            sum += tmp[add * w + x] - tmp[rem * w + x];
        }
    }
}

// Arbitrary input size -> kWidth x kHeight with integer box averaging.
void downsample(const uint8_t* gray, int width, int height, int rowStride,
                int outW, int outH, uint8_t* out) {
    for (int y = 0; y < outH; ++y) {
        int sy0 = (y * height) / outH;
        int sy1 = ((y + 1) * height) / outH;
        if (sy0 >= height) sy0 = height - 1;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > height) sy1 = height;
        for (int x = 0; x < outW; ++x) {
            int sx0 = (x * width) / outW;
            int sx1 = ((x + 1) * width) / outW;
            if (sx0 >= width) sx0 = width - 1;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            if (sx1 > width) sx1 = width;
            uint32_t sum = 0;
            int count = 0;
            for (int sy = sy0; sy < sy1; ++sy) {
                const uint8_t* row = gray + sy * rowStride;
                for (int sx = sx0; sx < sx1; ++sx) {
                    sum += row[sx];
                    ++count;
                }
            }
            out[y * outW + x] = static_cast<uint8_t>(sum / static_cast<uint32_t>(count));
        }
    }
}

// Forward-additive Gauss-Newton iterations at one pyramid level. The template
// is the previous-image window centred at (origX, origY); the search image is
// the current-image window centred at the running estimate (x, y). The
// residual is prev(orig+u) - cur(x+u) and the gradient is grad(cur) at (x+u):
// solving A*d = b then moves x so that cur(x+d+u) matches prev(orig+u).
// Using grad(prev) at (x+u) AND sampling both images at (x+u) instead aligns
// coincidental intensities at equal coordinates rather than estimating the
// translation, and converges to the wrong place on moving content.
// The 2x2 structure tensor is accumulated over the window; its determinant
// (and trace ratio) guards against edges and flat patches where the linear
// system is ill-conditioned.
bool trackLevel(const uint8_t* prev, const uint8_t* cur,
                const float* gx, const float* gy, int w, int h,
                float origX, float origY, float& x, float& y) {
    for (int iter = 0; iter < kIterations; ++iter) {
        float a11 = 0.f, a12 = 0.f, a22 = 0.f, b1 = 0.f, b2 = 0.f;
        for (int v = -kHalfWin; v <= kHalfWin; ++v) {
            const float sy = y + static_cast<float>(v);
            const float ty = origY + static_cast<float>(v);
            for (int u = -kHalfWin; u <= kHalfWin; ++u) {
                const float sx = x + static_cast<float>(u);
                const float tx = origX + static_cast<float>(u);
                const float gxv = sampleBilinear(gx, w, h, sx, sy);
                const float gyv = sampleBilinear(gy, w, h, sx, sy);
                const float it = sampleBilinear(prev, w, h, tx, ty) -
                                 sampleBilinear(cur, w, h, sx, sy);
                a11 += gxv * gxv;
                a12 += gxv * gyv;
                a22 += gyv * gyv;
                b1 += gxv * it;
                b2 += gyv * it;
            }
        }
        const float det = a11 * a22 - a12 * a12;
        const float trace = a11 + a22;
        if (det <= kMinDet || det < 1e-4f * trace * trace) return false;
        float du = (a22 * b1 - a12 * b2) / det;
        float dv = (-a12 * b1 + a11 * b2) / det;
        du = clampf(du, -kMaxStep, kMaxStep);
        dv = clampf(dv, -kMaxStep, kMaxStep);
        x += du;
        y += dv;
    }
    return true;
}

// Photometric consistency check for one tracked point. The template must be
// read at the ORIGINAL feature position (prevX/prevY) and the search image at
// the TRACKED position (x/y). Sampling both images at the tracked position
// compares two differently shifted copies of the pattern: on translating
// content that yields a large residual and rejects every otherwise valid
// track.
float windowResidual(const uint8_t* prev, const uint8_t* cur,
                     int w, int h, float prevX, float prevY, float x, float y) {
    float sum = 0.f;
    for (int v = -kHalfWin; v <= kHalfWin; ++v) {
        const float sy = y + static_cast<float>(v);
        const float spy = prevY + static_cast<float>(v);
        for (int u = -kHalfWin; u <= kHalfWin; ++u) {
            const float sx = x + static_cast<float>(u);
            const float spx = prevX + static_cast<float>(u);
            const float diff = sampleBilinear(cur, w, h, sx, sy) -
                               sampleBilinear(prev, w, h, spx, spy);
            sum += diff < 0.f ? -diff : diff;
        }
    }
    return sum / kWinPixels;
}

}  // namespace

MotionEngine::MotionEngine() {
    for (int level = 0; level < kLevels; ++level) {
        const int w = kWidth >> level;
        const int h = kHeight >> level;
        for (int bank = 0; bank < 2; ++bank) {
            pyramid_[bank][level].resize(w * h);
        }
        curGradX_[level].resize(w * h);
        curGradY_[level].resize(w * h);
    }
    const int n0 = kWidth * kHeight;
    gradX_.resize(n0);
    gradY_.resize(n0);
    tensorXX_.resize(n0);
    tensorXY_.resize(n0);
    tensorYY_.resize(n0);
    boxTmp_.resize(n0);
}

void MotionEngine::buildPyramid(uint8_t* const layers[kLevels]) const {
    for (int level = 1; level < kLevels; ++level) {
        const int w = kWidth >> level;
        const int h = kHeight >> level;
        const int pw = w << 1;
        const uint8_t* src = layers[level - 1];
        uint8_t* dst = layers[level];
        for (int y = 0; y < h; ++y) {
            const uint8_t* s0 = src + (2 * y) * pw;
            const uint8_t* s1 = s0 + pw;
            uint8_t* d = dst + y * w;
            for (int x = 0; x < w; ++x) {
                const int sum = s0[2 * x] + s0[2 * x + 1] +
                                s1[2 * x] + s1[2 * x + 1];
                d[x] = static_cast<uint8_t>((sum + 2) >> 2);
            }
        }
    }
}

int MotionEngine::detectFeatures(const uint8_t* img) {
    const int w = kWidth;
    const int h = kHeight;
    const int nPixels = w * h;

    // Shi-Tomasi response = smaller eigenvalue of the structure tensor.
    computeGradients(img, w, h, gradX_.data(), gradY_.data());
    for (int i = 0; i < nPixels; ++i) {
        const float gx = gradX_[i];
        const float gy = gradY_[i];
        tensorXX_[i] = gx * gx;
        tensorXY_[i] = gx * gy;
        tensorYY_[i] = gy * gy;
    }
    boxFilter(tensorXX_.data(), boxTmp_.data(), tensorXX_.data(), w, h, kDetectRadius);
    boxFilter(tensorXY_.data(), boxTmp_.data(), tensorXY_.data(), w, h, kDetectRadius);
    boxFilter(tensorYY_.data(), boxTmp_.data(), tensorYY_.data(), w, h, kDetectRadius);
    for (int i = 0; i < nPixels; ++i) {
        const float a = tensorXX_[i];
        const float b = tensorXY_[i];
        const float c = tensorYY_[i];
        const float t = 0.5f * (a + c);
        const float disc = t * t - (a * c - b * b);
        tensorXX_[i] = t - std::sqrt(disc > 0.f ? disc : 0.f);
    }
    const float* response = tensorXX_.data();

    int count = 0;
    const int cellW = w / kGridCols;
    const int cellH = h / kGridRows;
    const int margin = kHalfWin + 1;  // keep features away from the border
    for (int cy = 0; cy < kGridRows; ++cy) {
        const int y0 = cy * cellH;
        const int y1 = y0 + cellH;
        for (int cx = 0; cx < kGridCols; ++cx) {
            const int x0 = cx * cellW;
            const int x1 = x0 + cellW;
            float bestScore[kPerCell];
            float bestX[kPerCell];
            float bestY[kPerCell];
            int localCount = 0;

            for (int y = y0 > margin ? y0 : margin;
                 y < y1 && y < h - margin; ++y) {
                for (int x = x0 > margin ? x0 : margin;
                     x < x1 && x < w - margin; ++x) {
                    const float score = response[y * w + x];
                    if (score <= kCornerMinResponse) continue;
                    bool tooClose = false;
                    for (int j = 0; j < count; ++j) {
                        const float dx = static_cast<float>(x) - featX_[j];
                        const float dy = static_cast<float>(y) - featY_[j];
                        if (dx * dx + dy * dy < kMinFeatureDist2) {
                            tooClose = true;
                            break;
                        }
                    }
                    if (tooClose) continue;
                    for (int j = 0; j < localCount; ++j) {
                        const float dx = static_cast<float>(x) - bestX[j];
                        const float dy = static_cast<float>(y) - bestY[j];
                        if (dx * dx + dy * dy < kMinFeatureDist2) {
                            tooClose = true;
                            break;
                        }
                    }
                    if (tooClose) continue;

                    int pos = localCount < kPerCell ? localCount : kPerCell - 1;
                    while (pos > 0 && bestScore[pos - 1] < score) {
                        bestScore[pos] = bestScore[pos - 1];
                        bestX[pos] = bestX[pos - 1];
                        bestY[pos] = bestY[pos - 1];
                        --pos;
                    }
                    bestScore[pos] = score;
                    bestX[pos] = static_cast<float>(x);
                    bestY[pos] = static_cast<float>(y);
                    if (localCount < kPerCell) ++localCount;
                }
            }
            for (int j = 0; j < localCount && count < kMaxFeatures; ++j) {
                featX_[count] = bestX[j];
                featY_[count] = bestY[j];
                ++count;
            }
        }
    }
    featCount_ = count;
    return count;
}

int MotionEngine::trackAll(const uint8_t* const prev[kLevels],
                           const uint8_t* const cur[kLevels]) {
    int success = 0;
    const float priorX = clampf(lastDx_, -kInitFlowClamp, kInitFlowClamp);
    const float priorY = clampf(lastDy_, -kInitFlowClamp, kInitFlowClamp);

    for (int i = 0; i < featCount_; ++i) {
        // Initial guess in level-0 pixels: last frame's median flow.
        float x = featX_[i] + priorX;
        float y = featY_[i] + priorY;
        bool ok = true;
        for (int level = kTrackLevels - 1; level >= 0; --level) {
            const int scale = 1 << level;
            const int lw = kWidth >> level;
            const int lh = kHeight >> level;
            float lx = x / static_cast<float>(scale);
            float ly = y / static_cast<float>(scale);
            const float ox = featX_[i] / static_cast<float>(scale);
            const float oy = featY_[i] / static_cast<float>(scale);
            if (!trackLevel(prev[level], cur[level],
                            curGradX_[level].data(), curGradY_[level].data(),
                            lw, lh, ox, oy, lx, ly)) {
                ok = false;
                break;
            }
            x = lx * static_cast<float>(scale);
            y = ly * static_cast<float>(scale);
        }
        if (!ok) continue;

        const float margin = static_cast<float>(kHalfWin + 1);
        if (x < margin || y < margin ||
            x > static_cast<float>(kWidth) - margin ||
            y > static_cast<float>(kHeight) - margin) {
            continue;
        }
        if (windowResidual(prev[0], cur[0], kWidth, kHeight,
                           featX_[i], featY_[i], x, y) > kMaxResidual) {
            continue;
        }

        const int j = success++;
        nextX_[j] = x;
        nextY_[j] = y;
        trackPrevX_[j] = featX_[i];
        trackPrevY_[j] = featY_[i];
        trackCurrX_[j] = x;
        trackCurrY_[j] = y;
        medX_[j] = x - featX_[i];
        medY_[j] = y - featY_[i];
    }
    return success;
}

float MotionEngine::medianOf(float* values, int n) {
    if (n <= 0) return 0.f;
    std::sort(values, values + n);
    const int mid = n >> 1;
    return (n & 1) ? values[mid] : 0.5f * (values[mid - 1] + values[mid]);
}

void MotionEngine::process(const uint8_t* gray, int width, int height,
                           int rowStride, int64_t timestampNs) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto start = std::chrono::steady_clock::now();

    if (gray == nullptr || width < 1 || height < 1) {
        stats_.processMs = 0.f;
        stats_.timestampNs = timestampNs;
        return;
    }
    if (rowStride < width) rowStride = width;

    uint8_t* cur[kLevels];
    const uint8_t* prv[kLevels];
    for (int level = 0; level < kLevels; ++level) {
        cur[level] = pyramid_[currBuf_][level].data();
        prv[level] = pyramid_[1 - currBuf_][level].data();
    }

    downsample(gray, width, height, rowStride, kWidth, kHeight, cur[0]);
    buildPyramid(cur);

    float sceneChange = 0.f;
    if (hasPrev_) {
        uint64_t diffSum = 0;
        for (int i = 0; i < kWidth * kHeight; ++i) {
            const int diff = static_cast<int>(cur[0][i]) - static_cast<int>(prv[0][i]);
            diffSum += static_cast<uint64_t>(diff < 0 ? -diff : diff);
        }
        sceneChange = static_cast<float>(
            static_cast<double>(diffSum) /
            (static_cast<double>(kWidth * kHeight) * 255.0));
    }

    int attempted = 0;
    int featureCount = 0;
    float flowDx = 0.f;
    float flowDy = 0.f;

    // Detect before tracking only on the first frame or when the kept feature
    // set has become too small to track usefully.
    if (!hasPrev_ || featCount_ < kFeatureMin) {
        featureCount = detectFeatures(cur[0]);
        trackCount_ = featureCount;
        for (int i = 0; i < featureCount; ++i) {
            trackPrevX_[i] = trackCurrX_[i] = featX_[i];
            trackPrevY_[i] = trackCurrY_[i] = featY_[i];
        }
        lastDx_ = 0.f;
        lastDy_ = 0.f;
    } else {
        // Forward-additive LK needs the gradient of the SEARCH image (current
        // frame), evaluated at the tracked position.
        for (int level = 0; level < kTrackLevels; ++level) {
            const int lw = kWidth >> level;
            const int lh = kHeight >> level;
            computeGradients(cur[level], lw, lh,
                             curGradX_[level].data(), curGradY_[level].data());
        }
        attempted = featCount_;
        const int success = trackAll(prv, cur);
        featureCount = success;
        if (success > 0) {
            flowDx = medianOf(medX_, success);
            flowDy = medianOf(medY_, success);
            lastDx_ = flowDx;
            lastDy_ = flowDy;
            for (int i = 0; i < success; ++i) {
                featX_[i] = nextX_[i];
                featY_[i] = nextY_[i];
            }
        }
        featCount_ = success;
        // Refresh on a fixed cadence or when too few points survived. The
        // measured tracks stay in copyTracks(); only the next frame's seeds
        // are replaced by freshly detected corners.
        if (success < kFeatureReplenish || (frameIndex_ % kDetectInterval == 0)) {
            detectFeatures(cur[0]);
        }
    }

    const int64_t dtNs = hasPrev_ ? (timestampNs - prevTs_) : 33333333;
    const float dtSec = clampf(static_cast<float>(dtNs), 1000000.f, 500000000.f) *
                        1e-9f;
    const float speed = std::sqrt(flowDx * flowDx + flowDy * flowDy) / dtSec;
    const float ax = std::fabs(flowDx);
    const float ay = std::fabs(flowDy);
    int direction = 0;
    if (!(ax < 1.2f && ay < 1.2f)) {
        direction = (ax >= ay) ? (flowDx > 0.f ? 1 : 2)
                               : (flowDy > 0.f ? 3 : 4);
    }

    stats_.featureCount = featureCount;
    stats_.attempted = attempted;
    stats_.trackedShare = attempted > 0
        ? static_cast<float>(featureCount) / static_cast<float>(attempted)
        : 0.f;
    stats_.flowDx = flowDx;
    stats_.flowDy = flowDy;
    stats_.speedPxPerS = speed;
    stats_.processMs = std::chrono::duration<float, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    stats_.direction = direction;
    stats_.sceneChange = sceneChange;
    stats_.timestampNs = timestampNs;

    currBuf_ ^= 1;
    hasPrev_ = true;
    prevTs_ = timestampNs;
    ++frameIndex_;
}

MotionStats MotionEngine::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

int MotionEngine::copyTracks(float* out, int maxTracks) const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (out == nullptr || maxTracks <= 0) return 0;
    const int n = trackCount_ < maxTracks ? trackCount_ : maxTracks;
    for (int i = 0; i < n; ++i) {
        float* q = out + 4 * i;
        q[0] = trackPrevX_[i];
        q[1] = trackPrevY_[i];
        q[2] = trackCurrX_[i];
        q[3] = trackCurrY_[i];
    }
    return n;
}

void MotionEngine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    hasPrev_ = false;
    prevTs_ = 0;
    frameIndex_ = 0;
    featCount_ = 0;
    trackCount_ = 0;
    lastDx_ = 0.f;
    lastDy_ = 0.f;
    stats_ = MotionStats{};
}

}  // namespace nv
