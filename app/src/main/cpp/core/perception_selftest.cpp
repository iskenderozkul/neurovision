#include "core/perception_selftest.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

#include "event/event_engine.h"
#include "motion/motion_engine.h"

namespace recon {
namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 240;

// High-contrast checkerboard with a small deterministic texture so the corner
// detector has real gradients and LK has a non-degenerate structure tensor.
// shift>0 moves the content right in the image.
void makePattern(std::vector<uint8_t>& img, int shift) {
    img.resize(static_cast<size_t>(kWidth) * kHeight);
    for (int y = 0; y < kHeight; ++y) {
        for (int x = 0; x < kWidth; ++x) {
            const int sx = x - shift;
            int v = (((sx / 16) + (y / 16)) % 2 == 0) ? 215 : 45;
            v += ((sx * 13 + y * 7) % 15) - 7;
            img[static_cast<size_t>(y) * kWidth + x] =
                static_cast<uint8_t>(std::clamp(v, 0, 255));
        }
    }
}

struct Check {
    bool ok;
    std::string line;
};

Check check(const char* name, bool ok, const std::string& detail) {
    Check r;
    r.ok = ok;
    r.line = std::string(name) + std::string(24 - std::min<size_t>(23, std::string(name).size()), ' ') +
             (ok ? "PASS  " : "FAIL  ") + detail;
    return r;
}

std::string fmt(const char* format, ...) {
    char buffer[256];
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    return std::string(buffer);
}

}  // namespace

std::string runPerceptionSelfTest() {
    std::vector<Check> checks;
    std::string report;

    // ---------------- Event engine ----------------
    {
        nv::EventEngine events;
        std::vector<uint8_t> frame;
        makePattern(frame, 0);
        const int seedEvents =
            events.process(frame.data(), kWidth, kHeight, kWidth, 0);
        const int sameEvents =
            events.process(frame.data(), kWidth, kHeight, kWidth, 33000000LL);
        checks.push_back(check(
            "event_static", seedEvents == 0 && sameEvents == 0,
            fmt("seed=%d identical_frame=%d expected=0", seedEvents, sameEvents)));

        makePattern(frame, 6);
        const int movedEvents =
            events.process(frame.data(), kWidth, kHeight, kWidth, 66000000LL);
        const nv::EventStats moved = events.stats();
        checks.push_back(check(
            "event_motion", movedEvents > 1000,
            fmt("events=%d pos=%d neg=%d", movedEvents, moved.posCount,
                moved.negCount)));
        report += fmt("  event stats: rate=%.0f ev/s density=%.3f mean|dL|=%.2f ms=%.2f\n",
                      moved.eventRate, moved.density, moved.meanMagnitude,
                      moved.processMs);

        // A higher threshold must not produce more events on the same content.
        std::vector<uint8_t> frameA;
        makePattern(frameA, 0);
        events.setThreshold(0.8f);
        events.reset();
        events.process(frameA.data(), kWidth, kHeight, kWidth, 99000000LL);
        std::vector<uint8_t> frameB;
        makePattern(frameB, 6);
        const int highThresholdEvents =
            events.process(frameB.data(), kWidth, kHeight, kWidth, 132000000LL);
        checks.push_back(check(
            "event_threshold", highThresholdEvents <= movedEvents,
            fmt("C=0.8 -> %d, C=0.2 -> %d", highThresholdEvents, movedEvents)));
    }

    // ---------------- Motion engine ----------------
    {
        nv::MotionEngine motion;
        std::vector<uint8_t> a;
        std::vector<uint8_t> b;
        makePattern(a, 0);
        makePattern(b, 4);
        motion.process(a.data(), kWidth, kHeight, kWidth, 0);
        motion.process(b.data(), kWidth, kHeight, kWidth, 33000000LL);
        const nv::MotionStats s = motion.stats();
        checks.push_back(check(
            "motion_right", s.flowDx > 2.f && s.flowDx < 6.f && s.direction == 1 &&
                                s.featureCount >= 20,
            fmt("dx=%.2f dy=%.2f dir=%d features=%d", s.flowDx, s.flowDy,
                s.direction, s.featureCount)));
        report += fmt("  motion right: attempted=%d tracked=%.0f%% speed=%.1f px/s ms=%.2f\n",
                      s.attempted, s.trackedShare * 100.f, s.speedPxPerS,
                      s.processMs);

        std::vector<uint8_t> c;
        // After frame b (shift +4), return to the original pattern: the true
        // frame-to-frame displacement back to `still` is -4 px.
        makePattern(c, 0);
        motion.process(c.data(), kWidth, kHeight, kWidth, 66000000LL);
        const nv::MotionStats l = motion.stats();
        checks.push_back(check(
            "motion_left", l.flowDx < -2.f && l.flowDx > -6.f && l.direction == 2,
            fmt("dx=%.2f dy=%.2f dir=%d", l.flowDx, l.flowDy, l.direction)));

        std::vector<uint8_t> still;
        makePattern(still, 0);
        motion.process(still.data(), kWidth, kHeight, kWidth, 99000000LL);
        motion.process(still.data(), kWidth, kHeight, kWidth, 132000000LL);
        const nv::MotionStats st = motion.stats();
        checks.push_back(check(
            "motion_static",
            std::fabs(st.flowDx) < 1.f && std::fabs(st.flowDy) < 1.f &&
                st.direction == 0,
            fmt("dx=%.2f dy=%.2f dir=%d", st.flowDx, st.flowDy, st.direction)));
    }

    int passed = 0;
    for (const auto& c : checks) {
        if (c.ok) ++passed;
    }
    report += fmt("perception_selftest=%s checks=%d/%d\n",
                  passed == static_cast<int>(checks.size()) ? "pass" : "fail",
                  passed, static_cast<int>(checks.size()));
    for (const auto& c : checks) {
        report += c.line;
        report += "\n";
    }
    return report;
}

}  // namespace recon
