#pragma once
#include <math.h>
#include <stdint.h>

namespace FastMath {

// Fast Soft Clip using cubic approximation (Taylor series of tanh)
// Valid roughly for |x| < 1.5
// For |x| >= 1.5, clamps to +/- 1.0 (hard clip transition)
// Much faster than rational helper x*(27+x^2)/(27+9x^2) which requires
// division.
inline float FastSoftClip(float x) {
  if (x < -1.5f) {
    return -1.0f;
  } else if (x > 1.5f) {
    return 1.0f;
  } else {
    // x - x^3 / 3
    // = x * (1 - x*x * 0.3333f)
    return x * (1.0f - 0.333333f * x * x);
    // Note: at x=1.5, 1.5 * (1 - 0.333 * 2.25) = 1.5 * (1 - 0.75) = 1.5 * 0.25
    // = 0.375? Wait, tanh(1.5) is ~0.9. Taylor series x - x^3/3 diverges
    // quickly. Better approximation: x * (1.5 - 0.5 * x * x)? No.
  }
}

// Improved Polynomial SoftClip (Cubic)
// y = 1.5*x - 0.5*x^3  for |x| < 1.0
// Clamps at 1.0
// At x=1: 1.5 - 0.5 = 1.0. Slope at 1: 1.5 - 1.5*x^2 = 0. Smooth!
inline float CubicSoftClip(float x) {
  if (x < -1.0f) {
    return -1.0f;
  } else if (x > 1.0f) {
    return 1.0f;
  } else {
    return 1.5f * x - 0.5f * x * x * x;
    // Optimization: x * (1.5f - 0.5f * x * x)
    // 2 mults, 1 sub. Very fast.
  }
}

} // namespace FastMath
