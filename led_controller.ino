/*
 * LED Brightness Controller with ML Decision Tree, Manual Override, and ON-DEVICE ONLINE LEARNING
 *
 * The base predictor is a static Decision Tree exported to tree_rules.h.
 * On top of it, this sketch maintains a small per-context "correction table"
 * that is updated online whenever the user overrides the brightness with
 * the potentiometer. Corrections are persisted to ESP32 NVS so that the
 * device "remembers" the user's preferences across reboots.
 *
 *   final_brightness = predict_brightness(...) + correction[ctx] + pot_offset
 *
 * The context is defined by (time_period, motion_detected, ambient_bucket).
 * Online update rule (gradient-style absorption of the user's offset):
 *
 *   correction[ctx] += LEARNING_RATE * pot_offset
 *
 * As correction[ctx] grows toward the user's preferred deviation, the LED
 * reaches the desired brightness even at pot=0; the user releases the pot,
 * pot_offset becomes 0, and learning naturally stops at the converged value.
 *
 * Hardware Required:
 *   - ESP32 (uses NVS via Preferences library)
 *   - DS3231 RTC Module
 *   - HC-SR501 PIR Motion Sensor
 *   - LDR with 10k resistor
 *   - 10k Potentiometer (manual override)
 *   - LED strip / PWM LED
 */

#include <Wire.h>
#include <RTClib.h>
#include <math.h>
#include <Preferences.h>
#include "tree_rules.h"      // Generated ML model (base predictor)
#include "test_cases.h"      // Validation tests

// Pin definitions (ESP32)
#define PIR_PIN 27
#define LDR_PIN 34
#define POT_PIN 33
#define LED_PIN 2

// Configuration
#define UPDATE_INTERVAL 1000  // Update every 1 second (ms)
#define SMOOTHING_FACTOR 0.7  // For exponential moving average (0-1)
#define POT_DEADZONE 200      // Deadzone around center (2048 +/- 200) for ESP32

// =====================================================================
// ONLINE LEARNING CONFIGURATION
// =====================================================================
#define NUM_TIME_PERIODS    5
#define NUM_MOTION_STATES   2
#define NUM_AMBIENT_BUCKETS 4
#define NUM_CONTEXTS        (NUM_TIME_PERIODS * NUM_MOTION_STATES * NUM_AMBIENT_BUCKETS)  // 40

const float         LEARNING_RATE         = 0.02f;   // Per-update absorption rate
const float         CORRECTION_CAP        = 50.0f;   // Max |correction| in %
const int           LEARN_THRESHOLD_PCT   = 5;       // Min |pot_offset| (%) to trigger learning
const int           LEARN_OFFSET_NOISE    = 3;       // Pot fluctuation tolerance (%) for "stable"
const unsigned long LEARN_STABLE_MS       = 2000UL;  // Pot must be stable this long before we learn
const unsigned long NVS_SAVE_INTERVAL_MS  = 30000UL; // Throttle flash writes to every 30 s
const char* const   NVS_NAMESPACE         = "led-learn";

// RTC
RTC_DS3231 rtc;

// State variables
float smoothed_ldr = 0;
float smoothed_brightness = 0;
int   manual_offset = 0;
unsigned long last_update = 0;

// Statistics
unsigned long prediction_count = 0;
float avg_brightness = 0;

// Online learning state
Preferences   prefs;
float         correction_table[NUM_CONTEXTS];
bool          correction_dirty[NUM_CONTEXTS];
bool          learning_enabled = true;
unsigned long last_nvs_save = 0;
unsigned long learn_update_count = 0;

// Pot-stability tracking (so we only learn from intentional, held overrides)
int           prev_offset_for_stability = 0;
unsigned long offset_stable_since = 0;

// =====================================================================
// SETUP
// =====================================================================
void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10);

  Serial.println(F("\n==================================="));
  Serial.println(F("LED Brightness Controller"));
  Serial.println(F("ML Decision Tree + Manual Override"));
  Serial.println(F("+ On-Device Online Learning"));
  Serial.println(F("===================================\n"));

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(POT_PIN, INPUT);

  // Initialize RTC
  Serial.print(F("Initializing RTC... "));
  if (!rtc.begin()) {
    Serial.println(F("FAILED!"));
    Serial.println(F("RTC not found. Check wiring!"));
    while (1) delay(1000);
  }
  Serial.println(F("OK"));

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, setting time..."));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  DateTime now = rtc.now();
  Serial.print(F("Current time: "));
  printDateTime(now);
  Serial.println();

  // Validate the static ML model
  validate_model();

  // Load online-learning corrections from NVS
  loadCorrections();

  smoothed_ldr = readLDR();
  prev_offset_for_stability = readManualOffset();
  offset_stable_since = millis();

  Serial.println(F("\n=== Manual Time Setting ==="));
  Serial.println(F("Type: settime HH:MM:SS (example: settime 14:30:00)"));
  Serial.println(F("Type: help (for all commands)"));

  Serial.println(F("\n=== Starting Continuous Monitoring ===\n"));
  Serial.println(F("Legend: ML% = base tree prediction, Corr = learned correction, "));
  Serial.println(F("        Off = current pot offset, L = learned this cycle"));
  Serial.println(F("┌──────────┬──────┬─────────┬─────┬──────┬─────┬──────┬──────┬──────┬────────┬─────┬───┐"));
  Serial.println(F("│   Time   │ Hour │ Ambient │ Mot │ Per. │ Bkt │  ML% │ Corr │ Off  │ Final% │ PWM │ L │"));
  Serial.println(F("├──────────┼──────┼─────────┼─────┼──────┼─────┼──────┼──────┼──────┼────────┼─────┼───┤"));
}

// =====================================================================
// MAIN LOOP
// =====================================================================
void loop() {
  unsigned long current_time = millis();

  if (current_time - last_update >= UPDATE_INTERVAL) {
    last_update = current_time;

    // ----- Read sensors -----
    DateTime now = rtc.now();
    int hour   = now.hour();
    int minute = now.minute();

    float ambient_light  = readLDR();
    int   motion_detected = digitalRead(PIR_PIN);

    smoothed_ldr = SMOOTHING_FACTOR * smoothed_ldr + (1 - SMOOTHING_FACTOR) * ambient_light;

    // ----- Time features -----
    float sin_hour = sin(2.0 * PI * hour / 24.0);
    float cos_hour = cos(2.0 * PI * hour / 24.0);
    int   time_period = getTimePeriod(hour);

    // ----- Context for online learning -----
    int ambient_bucket = getAmbientBucket(smoothed_ldr);
    int ctx_idx        = getContextIndex(time_period, motion_detected, ambient_bucket);

    // ----- Base ML prediction (static tree) -----
    float ml_brightness = predict_brightness(
      smoothed_ldr,
      motion_detected,
      sin_hour,
      cos_hour,
      time_period
    );

    // ----- Apply learned correction for this context -----
    float learned_correction = correction_table[ctx_idx];
    float corrected_ml = ml_brightness + learned_correction;

    // ----- Read manual override -----
    manual_offset = readManualOffset();

    // ----- Compute final brightness -----
    float raw_final = corrected_ml + manual_offset;
    float final_brightness = constrain(raw_final, 0, 100);

    // Smooth brightness changes
    smoothed_brightness = SMOOTHING_FACTOR * smoothed_brightness +
                          (1 - SMOOTHING_FACTOR) * final_brightness;

    // PWM out
    int pwm_value = map(smoothed_brightness, 0, 100, 0, 255);
    analogWrite(LED_PIN, pwm_value);

    // ----- ONLINE LEARNING -----
    bool learned_this_cycle = false;
    if (learning_enabled) {
      learned_this_cycle = maybeLearn(ctx_idx, manual_offset, raw_final, current_time);
    } else {
      // Keep the stability tracker fresh so a re-enable doesn't immediately fire
      prev_offset_for_stability = manual_offset;
      offset_stable_since = current_time;
    }

    // Periodic NVS save (throttled to protect flash)
    if (current_time - last_nvs_save >= NVS_SAVE_INTERVAL_MS) {
      last_nvs_save = current_time;
      saveDirtyCorrections(false);
    }

    // ----- Statistics -----
    prediction_count++;
    avg_brightness = (avg_brightness * (prediction_count - 1) + final_brightness) / prediction_count;

    // ----- Output table row -----
    char time_str[9];
    sprintf(time_str, "%02d:%02d:%02d", hour, minute, now.second());

    char line[160];
    sprintf(line,
      "│ %s │  %2d  │ %6.1f  │  %d  │  %d   │  %d  │ %4.1f │%+5.1f │%+5d │ %5.1f  │ %3d │ %s │",
      time_str, hour, smoothed_ldr, motion_detected, time_period, ambient_bucket,
      ml_brightness, learned_correction, manual_offset, final_brightness, pwm_value,
      learned_this_cycle ? "*" : " ");
    Serial.println(line);
  }

  if (Serial.available()) {
    handleSerialCommand();
  }
}

// =====================================================================
// ONLINE LEARNING HELPERS
// =====================================================================

// Map ambient lux to a discrete bucket. Tuned roughly to indoor scales.
int getAmbientBucket(float lux) {
  if (lux <  50.0f)  return 0;   // Very dark (typical night without artificial light)
  if (lux < 300.0f)  return 1;   // Dim     (low indoor lighting)
  if (lux < 800.0f)  return 2;   // Medium  (well-lit room)
  return 3;                      // Bright  (daylight / strong artificial)
}

// Compose a flat index from the three context dimensions.
int getContextIndex(int time_period, int motion, int ambient_bucket) {
  if (time_period   < 0 || time_period   >= NUM_TIME_PERIODS)    time_period   = 0;
  if (motion        < 0 || motion        >= NUM_MOTION_STATES)   motion        = 0;
  if (ambient_bucket< 0 || ambient_bucket>= NUM_AMBIENT_BUCKETS) ambient_bucket= 0;
  return (time_period * NUM_MOTION_STATES + motion) * NUM_AMBIENT_BUCKETS + ambient_bucket;
}

// Decide whether to apply a learning step this cycle, and apply it if so.
// Returns true if the correction table was updated.
bool maybeLearn(int ctx_idx, int pot_offset, float raw_final, unsigned long current_time) {
  // Track pot stability: reset the stability timer when the pot moves more than the noise band.
  if (abs(pot_offset - prev_offset_for_stability) > LEARN_OFFSET_NOISE) {
    prev_offset_for_stability = pot_offset;
    offset_stable_since = current_time;
    return false;
  }

  bool stable_long_enough = (current_time - offset_stable_since) >= LEARN_STABLE_MS;
  bool meaningful_offset  = abs(pot_offset) >= LEARN_THRESHOLD_PCT;

  // Skip learning when the output is saturated at 0/100 — the user's true
  // preference is unobservable past the clip, so we'd just drift forever.
  bool not_clipped = (raw_final > 0.5f) && (raw_final < 99.5f);

  if (!(stable_long_enough && meaningful_offset && not_clipped)) {
    return false;
  }

  applyLearningUpdate(ctx_idx, pot_offset);
  return true;
}

// Gradient-style update with hard caps.
void applyLearningUpdate(int ctx_idx, int pot_offset) {
  float old_corr = correction_table[ctx_idx];
  float new_corr = old_corr + LEARNING_RATE * (float)pot_offset;

  if (new_corr >  CORRECTION_CAP) new_corr =  CORRECTION_CAP;
  if (new_corr < -CORRECTION_CAP) new_corr = -CORRECTION_CAP;

  correction_table[ctx_idx] = new_corr;
  correction_dirty[ctx_idx] = true;
  learn_update_count++;
}

// Load all corrections from NVS into RAM. Missing keys default to 0.
void loadCorrections() {
  for (int i = 0; i < NUM_CONTEXTS; i++) {
    correction_table[i] = 0.0f;
    correction_dirty[i] = false;
  }

  if (!prefs.begin(NVS_NAMESPACE, true)) {
    // Namespace doesn't exist yet — first run.
    Serial.println(F("Online learning: no prior corrections found (first run)"));
    return;
  }

  int loaded_nonzero = 0;
  char key[8];
  for (int i = 0; i < NUM_CONTEXTS; i++) {
    snprintf(key, sizeof(key), "c%d", i);
    correction_table[i] = prefs.getFloat(key, 0.0f);
    if (correction_table[i] != 0.0f) loaded_nonzero++;
  }
  prefs.end();

  Serial.print(F("Online learning: loaded "));
  Serial.print(loaded_nonzero);
  Serial.print(F(" / "));
  Serial.print(NUM_CONTEXTS);
  Serial.println(F(" learned context corrections from NVS"));
}

// Persist any dirty corrections to NVS. If force=true, write the whole table.
void saveDirtyCorrections(bool force) {
  bool any_dirty = force;
  if (!any_dirty) {
    for (int i = 0; i < NUM_CONTEXTS; i++) {
      if (correction_dirty[i]) { any_dirty = true; break; }
    }
  }
  if (!any_dirty) return;

  if (!prefs.begin(NVS_NAMESPACE, false)) {
    Serial.println(F("[LEARN] WARN: failed to open NVS for write"));
    return;
  }

  int written = 0;
  char key[8];
  for (int i = 0; i < NUM_CONTEXTS; i++) {
    if (force || correction_dirty[i]) {
      snprintf(key, sizeof(key), "c%d", i);
      prefs.putFloat(key, correction_table[i]);
      correction_dirty[i] = false;
      written++;
    }
  }
  prefs.end();

  Serial.print(F("[LEARN] Persisted "));
  Serial.print(written);
  Serial.println(F(" correction(s) to NVS"));
}

// Wipe both RAM and NVS copies of the correction table.
void resetCorrections() {
  if (prefs.begin(NVS_NAMESPACE, false)) {
    prefs.clear();
    prefs.end();
  }
  for (int i = 0; i < NUM_CONTEXTS; i++) {
    correction_table[i] = 0.0f;
    correction_dirty[i] = false;
  }
  learn_update_count = 0;
  Serial.println(F("[LEARN] All learned corrections cleared (RAM + NVS)"));
}

// Print the non-zero learned corrections in a readable form.
void printCorrectionTable() {
  Serial.println(F("\n=== Learned Corrections (Online Personalisation) ==="));
  Serial.println(F("Period: 0=EarlyMorn 1=Morn 2=Aft 3=Eve 4=Night"));
  Serial.println(F("Motion: 0=None 1=Detected"));
  Serial.println(F("Bucket: 0=<50lx 1=50-300 2=300-800 3=>=800"));
  Serial.println(F("┌────────┬────────┬────────┬────────────┐"));
  Serial.println(F("│ Period │ Motion │ AmbBkt │ Correction │"));
  Serial.println(F("├────────┼────────┼────────┼────────────┤"));

  int nonzero = 0;
  for (int p = 0; p < NUM_TIME_PERIODS; p++) {
    for (int m = 0; m < NUM_MOTION_STATES; m++) {
      for (int b = 0; b < NUM_AMBIENT_BUCKETS; b++) {
        int idx = getContextIndex(p, m, b);
        float val = correction_table[idx];
        if (val != 0.0f) {
          char row[80];
          sprintf(row, "│   %d    │   %d    │   %d    │  %+8.2f%% │", p, m, b, val);
          Serial.println(row);
          nonzero++;
        }
      }
    }
  }
  Serial.println(F("└────────┴────────┴────────┴────────────┘"));
  Serial.print(F("Non-zero entries: "));
  Serial.print(nonzero);
  Serial.print(F(" / "));
  Serial.println(NUM_CONTEXTS);
  Serial.print(F("Total learning updates this session: "));
  Serial.println(learn_update_count);
  Serial.print(F("Learning currently: "));
  Serial.println(learning_enabled ? F("ENABLED") : F("DISABLED"));
  Serial.println();
}

// =====================================================================
// SENSOR HELPERS
// =====================================================================
float readLDR() {
  // ESP32: 12-bit ADC (0-4095), 3.3V reference
  const float ADC_MAX  = 4095.0;
  const float VCC      = 3.3;
  const float R_FIXED  = 10000.0;
  const float LDR_A    = 32768000.0;
  const float LDR_B    = -1.4;
  const int   SAMPLES  = 20;

  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(LDR_PIN);
    delay(2);
  }
  float avg = sum / (float)SAMPLES;

  float voltage = (avg / ADC_MAX) * VCC;

  if (voltage <= 0.01) return 0.0;
  if (voltage >= VCC)  return 100000.0;

  float r_ldr = R_FIXED * voltage / (VCC - voltage);
  float lux   = LDR_A * pow(r_ldr, LDR_B);

  if (lux < 0)       lux = 0;
  if (lux > 100000)  lux = 100000;
  return lux;
}

int readManualOffset() {
  int pot_value = analogRead(POT_PIN);
  int center = 2048;

  if (pot_value >= center - POT_DEADZONE && pot_value <= center + POT_DEADZONE) {
    return 0;
  }

  int offset;
  if (pot_value < center - POT_DEADZONE) {
    offset = map(pot_value, 0, center - POT_DEADZONE, -100, 0);
  } else {
    offset = map(pot_value, center + POT_DEADZONE, 4095, 0, 100);
  }
  return offset;
}

int getTimePeriod(int hour) {
  // 0=EarlyMorn(4-6), 1=Morn(6-12), 2=Aft(12-16), 3=Eve(16-20), 4=Night(20-4)
  if (hour >= 4 && hour <= 6)        return 0;
  else if (hour > 6 && hour <= 12)   return 1;
  else if (hour > 12 && hour <= 16)  return 2;
  else if (hour > 16 && hour <= 20)  return 3;
  else                               return 4;
}

void printDateTime(DateTime dt) {
  Serial.print(dt.year(), DEC);   Serial.print('/');
  Serial.print(dt.month(), DEC);  Serial.print('/');
  Serial.print(dt.day(), DEC);    Serial.print(' ');
  Serial.print(dt.hour(), DEC);   Serial.print(':');
  Serial.print(dt.minute(), DEC); Serial.print(':');
  Serial.print(dt.second(), DEC);
}

// =====================================================================
// SERIAL COMMANDS
// =====================================================================
void handleSerialCommand() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();

  if (cmd == "stats") {
    Serial.println(F("\n=== Statistics ==="));
    Serial.print(F("Predictions: "));        Serial.println(prediction_count);
    Serial.print(F("Average Brightness: ")); Serial.print(avg_brightness, 2); Serial.println(F("%"));
    Serial.print(F("Current Offset: "));     Serial.print(manual_offset);     Serial.println(F("%"));
    Serial.print(F("Learning updates: "));   Serial.println(learn_update_count);
    Serial.print(F("Learning state: "));     Serial.println(learning_enabled ? F("ENABLED") : F("DISABLED"));
    Serial.println();
  } else if (cmd == "time") {
    DateTime now = rtc.now();
    Serial.print(F("Current time: "));
    printDateTime(now);
    Serial.println();
  } else if (cmd == "test") {
    validate_model();
  } else if (cmd == "corrections" || cmd == "corr") {
    printCorrectionTable();
  } else if (cmd == "save") {
    saveDirtyCorrections(true);
  } else if (cmd == "reset_learn") {
    resetCorrections();
  } else if (cmd == "learn on") {
    learning_enabled = true;
    prev_offset_for_stability = manual_offset;
    offset_stable_since = millis();
    Serial.println(F("[LEARN] Online learning ENABLED"));
  } else if (cmd == "learn off") {
    learning_enabled = false;
    Serial.println(F("[LEARN] Online learning DISABLED"));
  } else if (cmd.startsWith("settime ")) {
    String timeStr = cmd.substring(8);
    timeStr.trim();
    if (timeStr.length() == 8 && timeStr.charAt(2) == ':' && timeStr.charAt(5) == ':') {
      int hour   = timeStr.substring(0, 2).toInt();
      int minute = timeStr.substring(3, 5).toInt();
      int second = timeStr.substring(6, 8).toInt();
      if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60) {
        DateTime now = rtc.now();
        rtc.adjust(DateTime(now.year(), now.month(), now.day(), hour, minute, second));
        Serial.print(F("Time set to: "));
        Serial.print(hour);   Serial.print(F(":"));
        Serial.print(minute); Serial.print(F(":"));
        Serial.println(second);
      } else {
        Serial.println(F("Error: Invalid time values"));
        Serial.println(F("Format: settime HH:MM:SS (24-hour format)"));
      }
    } else {
      Serial.println(F("Error: Invalid time format"));
      Serial.println(F("Format: settime HH:MM:SS"));
      Serial.println(F("Example: settime 14:30:45"));
    }
  } else if (cmd == "help") {
    Serial.println(F("\n┌─────────────────────────────────────────────────────────────┐"));
    Serial.println(F("│                      COMMAND HELP                           │"));
    Serial.println(F("├─────────────────────────────────────────────────────────────┤"));
    Serial.println(F("│ settime HH:MM:SS  - Set time manually (24-hour format)      │"));
    Serial.println(F("│                     Example: settime 14:30:45               │"));
    Serial.println(F("│ time              - Show current RTC time                   │"));
    Serial.println(F("│ stats             - Show prediction + learning statistics   │"));
    Serial.println(F("│ test              - Re-run model validation                 │"));
    Serial.println(F("│ corrections       - Print learned correction table          │"));
    Serial.println(F("│ corr              - (alias for corrections)                 │"));
    Serial.println(F("│ save              - Force-save corrections to NVS now       │"));
    Serial.println(F("│ reset_learn       - Wipe learned corrections (RAM + NVS)    │"));
    Serial.println(F("│ learn on          - Enable online learning                  │"));
    Serial.println(F("│ learn off         - Disable online learning                 │"));
    Serial.println(F("│ help              - Show this help menu                     │"));
    Serial.println(F("└─────────────────────────────────────────────────────────────┘"));
    Serial.println();
  } else if (cmd.length() > 0) {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help' for available commands"));
  }
}
