#include "event/event_engine.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>

namespace nv {
namespace {

constexpr int kPixels = EventEngine::kWidth * EventEngine::kHeight;
constexpr uint32_t kBackground = 0xFF0A0E14u;  // ARGB, dark slate
constexpr float kAccCap = 8.f;                  // saturate hot pixels, stay visible
constexpr float kAccScale = 0.5f;               // preview k = min(1, acc / 2)
constexpr float kDensityScale = 0.25f;         // 4 accumulated events -> 255
constexpr float kRateAlpha = 0.2f;             // EMA weight of the new rate
constexpr float kVisibleFloor = 1.f / 255.f;   // below one 8-bit step: background

// Like clampf but maps NaN onto the lower bound (a NaN threshold would
// otherwise disable every event and a NaN decay would poison the preview).
inline float clampSetting(float v, float lo, float hi) {
    if (!(v > lo)) return lo;
    if (v > hi) return hi;
    return v;
}

inline float elapsedMs(std::chrono::steady_clock::time_point start) {
    return std::chrono::duration<float, std::milli>(
               std::chrono::steady_clock::now() - start).count();
}

}  // namespace

EventEngine::EventEngine()
    : luma_(kPixels, 0),
      ref_(kPixels, 0.f),
      posAcc_(kPixels, 0.f),
      negAcc_(kPixels, 0.f) {
    for (int v = 0; v < 256; ++v) {
        logLut_[v] = std::log(1.f + static_cast<float>(v));
    }
}

void EventEngine::downsample(const uint8_t* gray, int width, int height,
                             int rowStride) {
    // Integer box filter: output (ox, oy) owns the half-open source rectangle
    // [ox*width/kWidth, (ox+1)*width/kWidth) x [oy*height/kHeight, ...).
    // The boundaries round down so every source pixel is used exactly once.
    // The x ranges are shared by all rows; small stack arrays avoid allocation.
    int xStart[kWidth];
    int xEnd[kWidth];
    for (int ox = 0; ox < kWidth; ++ox) {
        int x0 = static_cast<int>(static_cast<int64_t>(ox) * width / kWidth);
        int x1 = static_cast<int>(static_cast<int64_t>(ox + 1) * width / kWidth);
        if (x0 >= x1) {  // fewer source columns than output columns
            x0 = std::min(x0, width - 1);
            x1 = x0 + 1;
        }
        xStart[ox] = x0;
        xEnd[ox] = x1;
    }

    for (int oy = 0; oy < kHeight; ++oy) {
        int y0 = static_cast<int>(static_cast<int64_t>(oy) * height / kHeight);
        int y1 = static_cast<int>(static_cast<int64_t>(oy + 1) * height / kHeight);
        if (y0 >= y1) {  // fewer source rows than output rows
            y0 = std::min(y0, height - 1);
            y1 = y0 + 1;
        }
        const int rows = y1 - y0;
        size_t dst = static_cast<size_t>(oy) * kWidth;
        for (int ox = 0; ox < kWidth; ++ox, ++dst) {
            const int x0 = xStart[ox];
            const int x1 = xEnd[ox];
            const int cols = x1 - x0;
            int sum = 0;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* row = gray + static_cast<size_t>(y) * rowStride;
                for (int x = x0; x < x1; ++x) sum += row[x];
            }
            luma_[dst] = static_cast<uint8_t>(sum / (cols * rows));
        }
    }
}

int EventEngine::process(const uint8_t* gray, int width, int height,
                         int rowStride, int64_t timestampNs) {
    const auto start = std::chrono::steady_clock::now();
    std::lock_guard<std::mutex> lock(mutex_);

    if (!gray || width < 1 || height < 1 || rowStride < width) {
        stats_.processMs = elapsedMs(start);
        return 0;
    }

    downsample(gray, width, height, rowStride);

    // First frame after reset seeds the references: a frame cannot be compared
    // with itself, so it emits no events and starts no rate estimate.
    const bool firstFrame = !initialized_;
    const float c = stats_.threshold;

    int posCount = 0;
    int negCount = 0;
    double magSum = 0.0;
    for (int i = 0; i < kPixels; ++i) {
        // Each pixel is visited once: decay and accumulate commute across
        // pixels, so folding both into this pass is identical to decaying the
        // whole buffer first.
        float pAcc = posAcc_[i] * decay_;
        float nAcc = negAcc_[i] * decay_;
        if (!std::isfinite(pAcc)) pAcc = 0.f;
        if (!std::isfinite(nAcc)) nAcc = 0.f;
        const float logVal = logLut_[luma_[i]];
        if (firstFrame) {
            ref_[i] = logVal;
        } else {
            const float d = logVal - ref_[i];
            if (d >= c) {           // positive threshold crossing
                ref_[i] = logVal;
                pAcc += 1.f;
                ++posCount;
                magSum += static_cast<double>(d);
            } else if (d <= -c) {   // negative threshold crossing
                ref_[i] = logVal;
                nAcc += 1.f;
                ++negCount;
                magSum += static_cast<double>(-d);
            }
        }
        posAcc_[i] = pAcc > kAccCap ? kAccCap : pAcc;
        negAcc_[i] = nAcc > kAccCap ? kAccCap : nAcc;
    }

    const int fired = posCount + negCount;
    stats_.posCount = posCount;
    stats_.negCount = negCount;
    stats_.density = static_cast<float>(fired) / static_cast<float>(kPixels);
    stats_.posShare = fired > 0 ? static_cast<float>(posCount) / fired : 0.5f;
    stats_.meanMagnitude = fired > 0 ? static_cast<float>(magSum / fired) : 0.f;

    if (firstFrame) {
        initialized_ = true;
        hasRate_ = false;
        stats_.eventRate = 0.f;
    } else {
        // EMA over real frame intervals. The clamp ignores out-of-order stamps
        // and implausible gaps; even a clamped 1 ms cannot invent events beyond
        // one crossing per pixel per frame, so the rate stays sensor-bound.
        const int64_t dtNs = timestampNs - stats_.timestampNs;
        double dt = static_cast<double>(dtNs) * 1e-9;
        if (dt < 0.001) dt = 0.001;
        else if (dt > 0.5) dt = 0.5;
        const float instant = static_cast<float>(static_cast<double>(fired) / dt);
        stats_.eventRate = hasRate_
                               ? stats_.eventRate + kRateAlpha * (instant - stats_.eventRate)
                               : instant;
        hasRate_ = true;
    }
    stats_.timestampNs = timestampNs;
    stats_.processMs = elapsedMs(start);
    return fired;
}

void EventEngine::setThreshold(float c) {
    std::lock_guard<std::mutex> lock(mutex_);
    stats_.threshold = clampSetting(c, 0.02f, 1.f);
}

float EventEngine::threshold() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_.threshold;
}

void EventEngine::setDecay(float d) {
    std::lock_guard<std::mutex> lock(mutex_);
    decay_ = clampSetting(d, 0.f, 0.995f);
}

void EventEngine::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    const float c = stats_.threshold;  // user settings survive a reset
    std::fill(luma_.begin(), luma_.end(), 0);
    std::fill(ref_.begin(), ref_.end(), 0.f);
    std::fill(posAcc_.begin(), posAcc_.end(), 0.f);
    std::fill(negAcc_.begin(), negAcc_.end(), 0.f);
    initialized_ = false;
    hasRate_ = false;
    stats_ = EventStats{};
    stats_.threshold = c;
}

void EventEngine::preview(uint32_t* argbOut) const {
    if (!argbOut) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < kPixels; ++i) {
        float kp = posAcc_[i] * kAccScale;
        float kn = negAcc_[i] * kAccScale;
        kp = (kp >= kVisibleFloor) ? std::min(1.f, kp) : 0.f;
        kn = (kn >= kVisibleFloor) ? std::min(1.f, kn) : 0.f;
        if (!std::isfinite(kp)) kp = 0.f;
        if (!std::isfinite(kn)) kn = 0.f;
        if (kp == 0.f && kn == 0.f) {
            argbOut[i] = kBackground;
            continue;
        }
        // Positive: cyan (0, 200, 255) * kp. Negative: magenta/orange
        // (255, 70, 150) * kn. Weight by k^2 so the stronger polarity wins
        // when both are active, while the brightness still follows decay.
        const float wp = kp * kp;
        const float wn = kn * kn;
        const float inv = 1.f / (wp + wn);
        const uint32_t r = static_cast<uint32_t>(255.f * kn * wn * inv + 0.5f);
        const uint32_t g = static_cast<uint32_t>(
            (200.f * kp * wp + 70.f * kn * wn) * inv + 0.5f);
        const uint32_t b = static_cast<uint32_t>(
            (255.f * kp * wp + 150.f * kn * wn) * inv + 0.5f);
        argbOut[i] = 0xFF000000u | ((r & 0xFFu) << 16) | ((g & 0xFFu) << 8) | (b & 0xFFu);
    }
}

void EventEngine::densityGrid(uint8_t* out) const {
    if (!out) return;
    std::lock_guard<std::mutex> lock(mutex_);
    for (int i = 0; i < kPixels; ++i) {
        float activity = (posAcc_[i] + negAcc_[i]) * kDensityScale;
        activity = (activity > 0.f) ? std::min(1.f, activity) : 0.f;
        if (!std::isfinite(activity)) activity = 0.f;
        out[i] = static_cast<uint8_t>(activity * 255.f + 0.5f);
    }
}

EventStats EventEngine::stats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

}  // namespace nv
