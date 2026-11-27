#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace nv {

struct MotionStats {
    int featureCount = 0;       // successfully tracked features this frame
    int attempted = 0;          // features attempted this frame
    float trackedShare = 0.f;   // featureCount / attempted
    float flowDx = 0.f;         // median flow in px/frame (current - previous)
    float flowDy = 0.f;
    float speedPxPerS = 0.f;    // |median flow| / dt
    float processMs = 0.f;
    int direction = 0;          // 0 static, 1 right, 2 left, 3 down, 4 up
    float sceneChange = 0.f;    // 0..1 mean absolute luma difference / 255
    int64_t timestampNs = 0;
};

// Pyramidal Lucas-Kanade sparse flow on the 8-bit Y plane.
//
// Honesty caveat: this measures image-space motion in pixels per frame. It is
// NOT metric camera motion (metres / radians per second); ARCore supplies the
// metric pose. Here the flow only drives UI cues: a dominant direction, a
// rough speed and a scene-change flag.
class MotionEngine {
public:
    static constexpr int kWidth = 320;
    static constexpr int kHeight = 240;
    static constexpr int kMaxFeatures = 96;

    MotionEngine();

    // gray: 8-bit luma, width x height, rowStride bytes (>= width).
    void process(const uint8_t* gray, int width, int height, int rowStride,
                 int64_t timestampNs);

    MotionStats stats() const;

    // Copies up to maxTracks quadruples (x0,y0,x1,y1) in kWidth/kHeight pixel
    // coordinates, previous -> current. Returns the number written.
    int copyTracks(float* out, int maxTracks) const;

    void reset();

private:
    static constexpr int kLevels = 4;
    static constexpr int kTrackLevels = 3;  // levels 2 -> 1 -> 0

    int detectFeatures(const uint8_t* img);
    void buildPyramid(uint8_t* const layers[kLevels]) const;
    int trackAll(const uint8_t* const prev[kLevels],
                 const uint8_t* const cur[kLevels]);
    static float medianOf(float* values, int n);

    mutable std::mutex mutex_;

    // Double-buffered pyramids: currBuf_ holds this frame, 1-currBuf_ the last.
    std::vector<uint8_t> pyramid_[2][kLevels];
    int currBuf_ = 0;
    bool hasPrev_ = false;
    int64_t prevTs_ = 0;
    int frameIndex_ = 0;

    // Feature set in current-frame coordinates (previous positions next frame).
    float featX_[kMaxFeatures]{};
    float featY_[kMaxFeatures]{};
    float nextX_[kMaxFeatures]{};
    float nextY_[kMaxFeatures]{};
    int featCount_ = 0;

    // Last measured tracks, exposed through copyTracks().
    float trackPrevX_[kMaxFeatures]{};
    float trackPrevY_[kMaxFeatures]{};
    float trackCurrX_[kMaxFeatures]{};
    float trackCurrY_[kMaxFeatures]{};
    int trackCount_ = 0;

    // Scratch for medians and the previous-frame flow prior.
    float medX_[kMaxFeatures]{};
    float medY_[kMaxFeatures]{};
    float lastDx_ = 0.f;
    float lastDy_ = 0.f;

    // Detection scratch (all kWidth*kHeight, allocated once).
    std::vector<float> gradX_, gradY_;
    std::vector<float> tensorXX_, tensorXY_, tensorYY_, boxTmp_;

    // Gradients of the current pyramid, sampled by LK at each tracked level.
    std::vector<float> curGradX_[kLevels];
    std::vector<float> curGradY_[kLevels];

    MotionStats stats_{};
};

}  // namespace nv
