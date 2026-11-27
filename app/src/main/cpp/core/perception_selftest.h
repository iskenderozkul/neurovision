#pragma once

#include <string>

namespace recon {

// Deterministic, camera-free check of the two stage-1/2 perception modules.
// It synthesises a moving high-contrast pattern (clearly a test input, never
// presented as camera data), feeds it through the real EventEngine and
// MotionEngine, and reports the measured numbers. This is how the algorithms
// are verified on a device that cannot scan, or before the camera is granted.
std::string runPerceptionSelfTest();

}  // namespace recon
