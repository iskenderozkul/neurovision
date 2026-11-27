// Debug harness (host/emulator only, never shipped): includes the engine
// translation unit so the file-local LK helpers can be exercised directly.
#include <cstdio>
#include <vector>

#include "motion/motion_engine.cpp"

namespace {

void makePattern(std::vector<uint8_t>& img, int shift) {
    const int w = 320, h = 240;
    img.resize(static_cast<size_t>(w) * h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int sx = x - shift;
            int v = (((sx / 16) + (y / 16)) % 2 == 0) ? 215 : 45;
            v += ((sx * 13 + y * 7) % 15) - 7;
            if (v < 0) v = 0;
            if (v > 255) v = 255;
            img[static_cast<size_t>(y) * w + x] = static_cast<uint8_t>(v);
        }
    }
}

}  // namespace

int main() {
    const int w = 320, h = 240;
    std::vector<uint8_t> a, b;
    makePattern(a, 0);
    makePattern(b, 4);

    std::vector<float> gx(static_cast<size_t>(w) * h), gy(static_cast<size_t>(w) * h);
    nv::computeGradients(b.data(), w, h, gx.data(), gy.data());

    for (int px : {40, 100, 160, 220, 280}) {
        float x = static_cast<float>(px), y = 120.f;
        const bool ok = nv::trackLevel(a.data(), b.data(), gx.data(), gy.data(),
                                       w, h, static_cast<float>(px), 120.f, x, y);
        const float res = nv::windowResidual(a.data(), b.data(), w, h,
                                             static_cast<float>(px), 120.f, x, y);
        std::printf("track from %d: ok=%d -> (%.2f, %.2f) residual=%.2f\n", px, ok ? 1 : 0,
                    x, y, res);
    }

    nv::MotionEngine engine;
    engine.process(a.data(), w, h, w, 0);
    engine.process(b.data(), w, h, w, 33000000);
    const nv::MotionStats s = engine.stats();
    std::printf("engine: attempted=%d features=%d dx=%.2f dy=%.2f dir=%d ms=%.2f\n", s.attempted,
                s.featureCount, s.flowDx, s.flowDy, s.direction, s.processMs);

    // Probe: b is shift +4; returning to shift 0 is a true -4 px step.
    std::vector<uint8_t> c0;
    makePattern(c0, 0);
    engine.process(c0.data(), w, h, w, 66000000);
    const nv::MotionStats l0 = engine.stats();
    std::printf("probe b->shift0: features=%d dx=%.2f dy=%.2f dir=%d ms=%.2f\n",
                l0.featureCount, l0.flowDx, l0.flowDy, l0.direction, l0.processMs);
    std::vector<uint8_t> cMinus4;
    makePattern(cMinus4, -4);
    engine.process(cMinus4.data(), w, h, w, 99000000);
    const nv::MotionStats lm = engine.stats();
    std::printf("probe shift0->shift-4: features=%d dx=%.2f dy=%.2f dir=%d\n",
                lm.featureCount, lm.flowDx, lm.flowDy, lm.direction);
    return 0;
}
