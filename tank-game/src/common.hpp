// Shared helpers: sides, angles and random numbers.
#pragma once

#include <cmath>
#include <cstdint>
#include <random>

#include "raylib.h"
#include "raymath.h"

enum class Side { NATO = 0, WP = 1 };

inline Side enemyOf(Side s) { return s == Side::NATO ? Side::WP : Side::NATO; }
inline int sideIndex(Side s) { return static_cast<int>(s); }

// One world unit is two metres. Ranges in the vehicle table are in world units.
constexpr float kMetersPerUnit = 2.0f;

inline const Color kNatoBlue{70, 140, 230, 255};
inline const Color kWpRed{220, 60, 50, 255};
inline Color sideColor(Side s) { return s == Side::NATO ? kNatoBlue : kWpRed; }

inline float wrapAngle(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}

inline float angleTo(Vector2 from, Vector2 to) { return atan2f(to.y - from.y, to.x - from.x); }

inline float turnTowards(float current, float target, float maxStep) {
  float d = wrapAngle(target - current);
  if (fabsf(d) <= maxStep) return wrapAngle(target);
  return wrapAngle(current + (d > 0 ? maxStep : -maxStep));
}

inline Vector2 fromAngle(float a, float len = 1.0f) { return {cosf(a) * len, sinf(a) * len}; }

// Point at local offset (forward, right) from pos in a frame rotated by angle.
inline Vector2 localPoint(Vector2 pos, float angle, float forward, float right) {
  float c = cosf(angle), s = sinf(angle);
  return {pos.x + c * forward - s * right, pos.y + s * forward + c * right};
}

class Rng {
 public:
  explicit Rng(uint32_t seed = 1) : eng_(seed) {}
  void seed(uint32_t s) { eng_.seed(s); }
  float uniform(float lo = 0.0f, float hi = 1.0f) {
    return std::uniform_real_distribution<float>(lo, hi)(eng_);
  }
  int range(int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(eng_); }
  bool chance(float p) { return uniform() < p; }
  std::mt19937& engine() { return eng_; }

 private:
  std::mt19937 eng_;
};

// Simulation randomness (seeded per battle) and cosmetic randomness.
Rng& simRng();
Rng& fxRng();
