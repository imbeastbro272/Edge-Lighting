/*
 * utility_agent.h
 *
 * Classical (non-ML) UTILITY-BASED AGENT for LED brightness control.
 *
 * This header is completely self-contained and hardware independent. It does
 * NOT call Serial / analogRead / RTC / EEPROM and it does NOT depend on any
 * global from the .ino. Every input is passed in as a parameter and the result
 * is returned in a struct. This guarantees the agent can only READ values it is
 * given - it cannot modify the Decision-Tree + KNN prediction pipeline.
 *
 * DECISION PROBLEM (constrained utility maximization):
 *
 *   Soft objective (maximize):
 *     U(b) = w_comfort*Comfort(b) - w_energy*Energy(b) - w_smooth*Smooth(b)
 *       Comfort(b) = 1 - ((b - ml_prediction)/100)^2   // anchored on ML output
 *       Energy(b)  = b/100
 *       Smooth(b)  = ((b - prev_brightness)/100)^2
 *
 *   Hard constraint (guaranteed):
 *     if (is_night AND motion)  =>  b >= min_safe_night   // default 40%
 *
 *   Decision:
 *     argmax over b in {0, 5, 10, ..., 100}  subject to the hard constraint.
 *
 * The "comfort anchor" ml_prediction is the existing ML brightness (Tree+KNN).
 * The agent only reads it; it never feeds anything back into the model.
 *
 * Host-side unit testing:
 *   Define ARDUINO_STUB before including this header to skip <Arduino.h>, so the
 *   file can be compiled and exercised with a plain host C++ compiler (g++).
 *   On the ESP32 build ARDUINO_STUB is NOT defined, so <Arduino.h> is included
 *   normally. The header does not actually use anything from <Arduino.h>; the
 *   include is purely conventional/defensive.
 */

#ifndef UTILITY_AGENT_H
#define UTILITY_AGENT_H

#ifndef ARDUINO_STUB
  #include <Arduino.h>
#endif
#include <math.h>

// ============================================
// CONFIGURATION (RAM-only; never persisted to EEPROM)
// ============================================

struct UtilityConfig {
  float w_comfort;       // weight on staying near the ML anchor (>= 0)
  float w_energy;        // weight penalizing higher brightness (>= 0)
  float w_smooth;        // weight penalizing change from previous brightness (>= 0)
  float min_safe_night;  // hard floor (%) enforced when night AND motion (0..100)
};

// ============================================
// RESULT (full breakdown for inspection / Serial reporting)
// ============================================

struct UtilityResult {
  float recommended;       // argmax brightness in 0..100 (constraint-satisfying)
  float ml_anchor;         // comfort anchor used (the ML prediction, clamped)
  float prev_brightness;   // previous brightness used (clamped)

  bool  is_night;          // night flag passed in
  bool  motion;            // motion flag passed in
  bool  floor_active;      // true if the night+motion hard floor was applied
  float floor_value;       // the floor value (cfg.min_safe_night)

  // Raw term values evaluated AT the recommended brightness b*
  float comfort;           // Comfort(b*)  in (-inf, 1]
  float energy;            // Energy(b*)   in [0, 1]
  float smooth;            // Smooth(b*)   in [0, ~1]

  // Signed contributions to U(b*)  (so they sum to `utility`)
  float comfort_contrib;   // +w_comfort * comfort
  float energy_contrib;    // -w_energy  * energy
  float smooth_contrib;    // -w_smooth  * smooth

  float utility;           // U(b*) = comfort_contrib + energy_contrib + smooth_contrib
};

// ============================================
// TERM FUNCTIONS
// ============================================

static inline float utilComfort(float b, float ml) {
  float d = (b - ml) / 100.0f;
  return 1.0f - d * d;
}

static inline float utilEnergy(float b) {
  return b / 100.0f;
}

static inline float utilSmooth(float b, float prev) {
  float d = (b - prev) / 100.0f;
  return d * d;
}

static inline float utilScore(float b, float ml, float prev,
                              const UtilityConfig &c) {
  return c.w_comfort * utilComfort(b, ml)
       - c.w_energy  * utilEnergy(b)
       - c.w_smooth  * utilSmooth(b, prev);
}

// ============================================
// DEFAULT CONFIG
// ============================================

static inline UtilityConfig utilityDefaultConfig() {
  UtilityConfig c;
  c.w_comfort      = 1.0f;
  c.w_energy       = 0.3f;
  c.w_smooth       = 0.2f;
  c.min_safe_night = 40.0f;
  return c;
}

// ============================================
// MAIN EVALUATION
//
// Pure function: given the ML anchor, previous brightness, the night/motion
// flags and the config, returns the constrained-argmax recommendation plus a
// full breakdown. No side effects.
// ============================================

static inline UtilityResult utilityEvaluate(float ml_prediction,
                                             float prev_brightness,
                                             bool is_night, bool motion,
                                             const UtilityConfig &cfg) {
  // Defensive clamping of inputs into the valid brightness range.
  if (ml_prediction < 0.0f)   ml_prediction = 0.0f;
  if (ml_prediction > 100.0f) ml_prediction = 100.0f;
  if (prev_brightness < 0.0f)   prev_brightness = 0.0f;
  if (prev_brightness > 100.0f) prev_brightness = 100.0f;

  // Hard constraint: night + motion enforces a minimum safe brightness.
  float lo = 0.0f;
  bool  floor_active = false;
  if (is_night && motion) {
    lo = cfg.min_safe_night;
    floor_active = true;
  }

  // argmax over the discrete grid b in {0,5,...,100}, b >= lo.
  float best_b = -1.0f;
  float best_u = -INFINITY;

  for (int bi = 0; bi <= 100; bi += 5) {
    float b = (float)bi;
    if (b < lo) continue;                 // enforce hard constraint
    float u = utilScore(b, ml_prediction, prev_brightness, cfg);
    if (u > best_u) {
      best_u = u;
      best_b = b;
    }
  }

  // Fallback for pathological config (e.g. floor > 100): clamp to a valid value.
  if (best_b < 0.0f) {
    best_b = lo;
    if (best_b > 100.0f) best_b = 100.0f;
    best_u = utilScore(best_b, ml_prediction, prev_brightness, cfg);
  }

  UtilityResult r;
  r.recommended     = best_b;
  r.ml_anchor       = ml_prediction;
  r.prev_brightness = prev_brightness;
  r.is_night        = is_night;
  r.motion          = motion;
  r.floor_active    = floor_active;
  r.floor_value     = cfg.min_safe_night;

  r.comfort         = utilComfort(best_b, ml_prediction);
  r.energy          = utilEnergy(best_b);
  r.smooth          = utilSmooth(best_b, prev_brightness);

  r.comfort_contrib =  cfg.w_comfort * r.comfort;
  r.energy_contrib  = -cfg.w_energy  * r.energy;
  r.smooth_contrib  = -cfg.w_smooth  * r.smooth;

  r.utility         = best_u;
  return r;
}

#endif // UTILITY_AGENT_H
