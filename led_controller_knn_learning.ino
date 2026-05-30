/*
 * LED Brightness Controller with KNN-BASED INCREMENTAL LEARNING
 *
 * APPROACH (Option A: Tree + KNN Residual Correction):
 *   - The pre-trained Decision Tree (2000 samples) remains FROZEN as the base predictor.
 *     It is never modified; tree_rules.h is the source of truth for "general behavior".
 *   - User feedback samples are stored in EEPROM AND mirrored to a RAM cache for speed.
 *   - On every prediction:
 *       1. tree_pred       = decision tree output for current features
 *       2. residuals[i]    = sample[i].target - tree_pred(at sample[i])
 *       3. dist[i]         = importance-weighted distance from current input to sample[i]
 *       4. correction      = Gaussian-kernel-weighted average of residuals from K nearest
 *       5. confidence      = exp(-min_dist / sigma_conf)  (drops if no nearby samples)
 *       6. final           = tree_pred + confidence * correction
 *
 * PROPERTIES:
 *   - Adding ANY user sample influences ONLY predictions near that point in feature space.
 *     There is no global leakage like the previous linear-correction model had.
 *   - Contradictory samples are handled gracefully: only co-located samples average.
 *   - Works immediately after a single sample is added; no "retraining" pass required.
 *   - O(N) per prediction for N samples. With N=100 samples the cost is microseconds.
 *
 * COMMANDS:
 *   help, stats, samples, clearsamples, modelinfo, knninfo, resetmodel
 *   addsample HH DD AMBIENT MOTION BRIGHTNESS
 *   test HH DD AMBIENT MOTION POT
 *   setk N           - change number of neighbors (1..MAX_TRAINING_SAMPLES)
 *   setsigma X       - change kernel bandwidth (e.g. 0.25)
 */

#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <math.h>
#include "tree_rules.h"

// ============================================
// PIN DEFINITIONS
// ============================================

#define PIR_PIN 27
#define LDR_PIN 34
#define POT_PIN 33
#define LED_PIN 2

// ============================================
// CONFIGURATION
// ============================================

#define UPDATE_INTERVAL    1000
#define SMOOTHING_FACTOR   0.7
#define POT_DEADZONE       200

#define LEARNING_ENABLED   true

#define POT_STABLE_DURATION    5000
#define POT_CHANGE_THRESHOLD   50
#define MAX_CHANGES_BEFORE_REFRESH 20

#define EEPROM_SIZE        4096

// ============================================
// FEATURE IMPORTANCE (from DT analysis)
// Used as weights in the KNN distance metric.
// ============================================

#define IMPORTANCE_AMBIENT      0.254848f
#define IMPORTANCE_MOTION       0.229156f
#define IMPORTANCE_SIN_HOUR     0.048563f
#define IMPORTANCE_COS_HOUR     0.040571f
#define IMPORTANCE_TIME_PERIOD  0.018088f
#define IMPORTANCE_DAY_OF_WEEK  0.001244f

// Normalisation scale for ambient (lux) -> roughly unit range
#define AMBIENT_SCALE      1000.0f

// ============================================
// TRAINING SAMPLE STORAGE
// ============================================

#define MAX_TRAINING_SAMPLES        100
#define SAMPLE_SIZE                 32     // sizeof(TrainingSample) on ESP32
#define EEPROM_SAMPLE_COUNT_ADDR    0
#define EEPROM_NEXT_WRITE_IDX_ADDR  4
#define EEPROM_KNN_CONFIG_ADDR      8
#define EEPROM_SAMPLES_START        100

struct TrainingSample {
  float    ambient_light;
  float    sin_hour;
  float    cos_hour;
  int      motion_detected;
  int      time_period;
  int      day_of_week;
  float    target_brightness;
  uint32_t timestamp;
};

// In-RAM cache of all stored samples for fast KNN lookup.
TrainingSample samples_cache[MAX_TRAINING_SAMPLES];

int training_sample_count = 0;   // valid entries in cache (0..MAX)
int next_write_idx        = 0;   // circular-buffer write head

// ============================================
// KNN CONFIG (persisted)
// ============================================

struct KnnConfig {
  int    k_neighbors;       // number of neighbors used in correction
  float  sigma_kernel;      // bandwidth of the Gaussian kernel over distances
  float  sigma_confidence;  // bandwidth controlling fall-off to tree-only when far
  uint32_t magic;           // sanity check value
};

KnnConfig knn_cfg;

#define KNN_MAGIC  0xCAFEBABE

// ============================================
// MANUAL INPUT MODE
// ============================================

bool manual_input_mode = false;

// ============================================
// RTC
// ============================================

RTC_DS3231 rtc;

// ============================================
// STATE VARIABLES
// ============================================

float smoothed_ldr        = 0;
float smoothed_brightness = 0;
int   manual_offset       = 0;
unsigned long last_update = 0;

// ============================================
// POT CHANGE TRACKING (for sample capture)
// ============================================

int  last_stable_pot_value     = -1;
int  current_pot_candidate     = -1;
unsigned long pot_stable_start = 0;
bool pot_candidate_active      = false;
int  pot_change_count_today    = 0;
int  last_check_day            = -1;

// ============================================
// STATS
// ============================================

unsigned long prediction_count = 0;
float avg_brightness           = 0;

// ============================================
// FORWARD DECLARATIONS
// ============================================

float predict_knn(float ambient, int motion, float sin_h, float cos_h,
                  int period, int day,
                  float *out_tree_pred, float *out_correction,
                  float *out_confidence, float *out_min_dist,
                  int *out_neighbors_used);
float readLDR();
int   readManualOffset();
int   getTimePeriod(int hour);
void  printDateTime(DateTime dt);
void  handleSerialCommand();
void  monitorPotForSampleCapture(unsigned long current_time, float ml_pred, float user_brightness);
void  checkDailyRollover(DateTime &now);

// ============================================
// EEPROM / CACHE MANAGEMENT
// ============================================

void loadSamplesFromEEPROM() {
  for (int i = 0; i < training_sample_count; i++) {
    int addr = EEPROM_SAMPLES_START + (i * SAMPLE_SIZE);
    EEPROM.get(addr, samples_cache[i]);
  }
  Serial.print(F("[cache] Loaded "));
  Serial.print(training_sample_count);
  Serial.println(F(" sample(s) into RAM"));
}

void initializeTrainingStorage() {
  EEPROM.get(EEPROM_SAMPLE_COUNT_ADDR,   training_sample_count);
  EEPROM.get(EEPROM_NEXT_WRITE_IDX_ADDR, next_write_idx);

  if (training_sample_count < 0 || training_sample_count > MAX_TRAINING_SAMPLES) {
    training_sample_count = 0;
    next_write_idx        = 0;
    EEPROM.put(EEPROM_SAMPLE_COUNT_ADDR,   training_sample_count);
    EEPROM.put(EEPROM_NEXT_WRITE_IDX_ADDR, next_write_idx);
    EEPROM.commit();
  }
  if (next_write_idx < 0 || next_write_idx >= MAX_TRAINING_SAMPLES) {
    next_write_idx = training_sample_count % MAX_TRAINING_SAMPLES;
    EEPROM.put(EEPROM_NEXT_WRITE_IDX_ADDR, next_write_idx);
    EEPROM.commit();
  }

  Serial.print(F("Training samples in EEPROM: "));
  Serial.println(training_sample_count);

  loadSamplesFromEEPROM();
}

void saveTrainingSample(const TrainingSample &sample) {
  // Circular buffer: write at next_write_idx, increment, advance count up to MAX.
  int slot = next_write_idx;

  samples_cache[slot] = sample;

  int addr = EEPROM_SAMPLES_START + (slot * SAMPLE_SIZE);
  EEPROM.put(addr, sample);

  next_write_idx = (next_write_idx + 1) % MAX_TRAINING_SAMPLES;
  if (training_sample_count < MAX_TRAINING_SAMPLES) {
    training_sample_count++;
  }

  EEPROM.put(EEPROM_SAMPLE_COUNT_ADDR,   training_sample_count);
  EEPROM.put(EEPROM_NEXT_WRITE_IDX_ADDR, next_write_idx);
  EEPROM.commit();

  Serial.print(F("[v] Sample stored. Total in memory: "));
  Serial.println(training_sample_count);
}

TrainingSample loadTrainingSample(int index) {
  // Read from RAM cache (fast). EEPROM is the persistence backing store only.
  return samples_cache[index];
}

void captureTrainingSample(float tree_pred, float user_brightness) {
  DateTime now = rtc.now();

  TrainingSample sample;
  sample.ambient_light     = smoothed_ldr;
  sample.sin_hour          = sin(2.0 * PI * now.hour() / 24.0);
  sample.cos_hour          = cos(2.0 * PI * now.hour() / 24.0);
  sample.motion_detected   = digitalRead(PIR_PIN);
  sample.time_period       = getTimePeriod(now.hour());
  sample.day_of_week       = (now.dayOfTheWeek() + 6) % 7;
  sample.target_brightness = user_brightness;
  sample.timestamp         = now.unixtime();

  saveTrainingSample(sample);

  Serial.print(F("[capture] Ambient="));
  Serial.print(sample.ambient_light);
  Serial.print(F(" Tree="));
  Serial.print(tree_pred);
  Serial.print(F("% User="));
  Serial.print(user_brightness);
  Serial.println(F("%"));
}

void addManualSample(int hour, int day, float ambient, int motion, float target_brightness) {
  TrainingSample sample;
  sample.ambient_light     = ambient;
  sample.sin_hour          = sin(2.0 * PI * hour / 24.0);
  sample.cos_hour          = cos(2.0 * PI * hour / 24.0);
  sample.motion_detected   = motion;
  sample.time_period       = getTimePeriod(hour);
  sample.day_of_week       = day;
  sample.target_brightness = target_brightness;
  sample.timestamp         = rtc.now().unixtime();

  saveTrainingSample(sample);

  const char *day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  Serial.println();
  Serial.println(F("[v] MANUAL SAMPLE ADDED"));
  Serial.print(F("    Hour: "));
  Serial.print(hour);
  Serial.print(F(":00, Day: "));
  Serial.println(day_names[day]);
  Serial.print(F("    Ambient: "));
  Serial.print(ambient);
  Serial.print(F(" lux, Motion: "));
  Serial.println(motion ? "YES" : "NO");
  Serial.print(F("    Target Brightness: "));
  Serial.print(target_brightness);
  Serial.println(F("%"));
  Serial.println();
}

// ============================================
// KNN CONFIG (persistence)
// ============================================

void initializeKnnConfig() {
  knn_cfg.k_neighbors      = 5;
  knn_cfg.sigma_kernel     = 0.25f;   // Gaussian bandwidth for neighbor weights
  knn_cfg.sigma_confidence = 0.40f;   // Bandwidth for tree-vs-knn blending
  knn_cfg.magic            = KNN_MAGIC;
}

void saveKnnConfig() {
  EEPROM.put(EEPROM_KNN_CONFIG_ADDR, knn_cfg);
  EEPROM.commit();
}

void loadKnnConfig() {
  EEPROM.get(EEPROM_KNN_CONFIG_ADDR, knn_cfg);
  if (knn_cfg.magic != KNN_MAGIC ||
      knn_cfg.k_neighbors      < 1 || knn_cfg.k_neighbors      > MAX_TRAINING_SAMPLES ||
      knn_cfg.sigma_kernel     <= 0.0f ||
      knn_cfg.sigma_confidence <= 0.0f) {
    Serial.println(F("[knn] No valid saved config, using defaults"));
    initializeKnnConfig();
    saveKnnConfig();
  } else {
    Serial.print(F("[knn] Loaded config: K="));
    Serial.print(knn_cfg.k_neighbors);
    Serial.print(F(" sigma_k="));
    Serial.print(knn_cfg.sigma_kernel, 3);
    Serial.print(F(" sigma_c="));
    Serial.println(knn_cfg.sigma_confidence, 3);
  }
}

// ============================================
// KNN DISTANCE METRIC
// Importance-weighted squared distance over normalized features.
// Categorical features (period, day) use 0/1 distance.
// ============================================

static inline float knnDistanceSq(
    float a_amb, int a_mot, float a_sin, float a_cos, int a_per, int a_day,
    float b_amb, int b_mot, float b_sin, float b_cos, int b_per, int b_day) {
  float d_amb = (a_amb - b_amb) / AMBIENT_SCALE;
  float d_mot = (float)(a_mot - b_mot);
  float d_sin = a_sin - b_sin;
  float d_cos = a_cos - b_cos;
  float d_per = (a_per == b_per) ? 0.0f : 1.0f;
  float d_day = (a_day == b_day) ? 0.0f : 1.0f;

  return  IMPORTANCE_AMBIENT     * d_amb * d_amb
        + IMPORTANCE_MOTION      * d_mot * d_mot
        + IMPORTANCE_SIN_HOUR    * d_sin * d_sin
        + IMPORTANCE_COS_HOUR    * d_cos * d_cos
        + IMPORTANCE_TIME_PERIOD * d_per
        + IMPORTANCE_DAY_OF_WEEK * d_day;
}

// ============================================
// KNN PREDICTION (Tree + Local Residual Correction)
// ============================================

float predict_knn(float ambient, int motion, float sin_h, float cos_h,
                  int period, int day,
                  float *out_tree_pred, float *out_correction,
                  float *out_confidence, float *out_min_dist,
                  int *out_neighbors_used) {

  // 1. Base prediction from frozen decision tree
  float tree_pred = predict_brightness(ambient, motion, sin_h, cos_h, period, day);

  if (out_tree_pred)      *out_tree_pred      = tree_pred;
  if (out_correction)     *out_correction     = 0.0f;
  if (out_confidence)     *out_confidence     = 0.0f;
  if (out_min_dist)       *out_min_dist       = INFINITY;
  if (out_neighbors_used) *out_neighbors_used = 0;

  if (training_sample_count == 0) {
    return tree_pred;
  }

  // 2. Compute distances and residuals against every cached sample
  float distsq[MAX_TRAINING_SAMPLES];
  int   order[MAX_TRAINING_SAMPLES];

  for (int i = 0; i < training_sample_count; i++) {
    const TrainingSample &s = samples_cache[i];
    distsq[i] = knnDistanceSq(
      ambient, motion, sin_h, cos_h, period, day,
      s.ambient_light, s.motion_detected, s.sin_hour, s.cos_hour,
      s.time_period, s.day_of_week);
    order[i] = i;
  }

  // 3. Partial selection sort to find K nearest (K is small, ~3..7).
  int k_actual = knn_cfg.k_neighbors;
  if (k_actual > training_sample_count) k_actual = training_sample_count;

  for (int k = 0; k < k_actual; k++) {
    int min_idx = k;
    for (int j = k + 1; j < training_sample_count; j++) {
      if (distsq[order[j]] < distsq[order[min_idx]]) {
        min_idx = j;
      }
    }
    int tmp = order[k]; order[k] = order[min_idx]; order[min_idx] = tmp;
  }

  // 4. Gaussian-kernel-weighted average of residuals from K nearest
  float sigma_k = knn_cfg.sigma_kernel;
  float sigma_c = knn_cfg.sigma_confidence;
  float two_sigma_k_sq = 2.0f * sigma_k * sigma_k;

  float weight_sum   = 0.0f;
  float residual_sum = 0.0f;

  for (int k = 0; k < k_actual; k++) {
    int idx = order[k];
    const TrainingSample &s = samples_cache[idx];

    float tree_pred_at_s = predict_brightness(
      s.ambient_light, s.motion_detected, s.sin_hour, s.cos_hour,
      s.time_period, s.day_of_week);

    float residual = s.target_brightness - tree_pred_at_s;
    float w        = expf(-distsq[idx] / two_sigma_k_sq);

    residual_sum += w * residual;
    weight_sum   += w;
  }

  float correction = (weight_sum > 1e-6f) ? (residual_sum / weight_sum) : 0.0f;

  // 5. Confidence: drops with the distance to the nearest neighbor.
  float min_dist_sq = distsq[order[0]];
  float confidence  = expf(-min_dist_sq / (2.0f * sigma_c * sigma_c));

  float final_pred = tree_pred + confidence * correction;
  if (final_pred < 0)   final_pred = 0;
  if (final_pred > 100) final_pred = 100;

  if (out_tree_pred)      *out_tree_pred      = tree_pred;
  if (out_correction)     *out_correction     = correction;
  if (out_confidence)     *out_confidence     = confidence;
  if (out_min_dist)       *out_min_dist       = sqrtf(min_dist_sq);
  if (out_neighbors_used) *out_neighbors_used = k_actual;

  return final_pred;
}

// Convenience overload (no diagnostics)
float predict_knn(float ambient, int motion, float sin_h, float cos_h,
                  int period, int day) {
  return predict_knn(ambient, motion, sin_h, cos_h, period, day,
                     NULL, NULL, NULL, NULL, NULL);
}

// ============================================
// SETUP
// ============================================

void setup() {
  Serial.begin(9600);
  while (!Serial) { delay(10); }

  Serial.println(F("==================================="));
  Serial.println(F("LED Controller v4.0 - KNN INCREMENTAL LEARNING"));
  Serial.println(F("==================================="));

  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(POT_PIN, INPUT);

  EEPROM.begin(EEPROM_SIZE);

  Serial.print(F("Initializing RTC... "));
  if (!rtc.begin()) {
    Serial.println(F("FAILED"));
    while (1) { delay(1000); }
  }
  Serial.println(F("OK"));

  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power. Setting compile time."));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  Serial.print(F("Current Time: "));
  printDateTime(rtc.now());
  Serial.println();

  initializeTrainingStorage();
  loadKnnConfig();

  smoothed_ldr = readLDR();

  Serial.println(F("=== MODEL ==="));
  Serial.println(F("Base       : Decision Tree (2000 samples, frozen)"));
  Serial.println(F("Personalize: KNN residual correction over user samples"));

  Serial.println(F("=== Commands ==="));
  Serial.println(F("help, stats, samples, clearsamples, modelinfo"));
  Serial.println(F("knninfo, resetmodel"));
  Serial.println(F("addsample HH DD AMBIENT MOTION BRIGHTNESS"));
  Serial.println(F("test HH DD AMBIENT MOTION POT"));
  Serial.println(F("setk N | setsigma X"));

  Serial.println(F("=== Monitoring Started ==="));
  Serial.println(F("|   Time   | Day | Hour | Ambient | Motion | Period |  Tree |  Adj |  ML% | Off | Final% | PWM |"));
}

// ============================================
// LOOP
// ============================================

void loop() {
  unsigned long current_time = millis();

  if (!manual_input_mode && current_time - last_update >= UPDATE_INTERVAL) {
    last_update = current_time;

    DateTime now = rtc.now();

    int hour                 = now.hour();
    int minute               = now.minute();
    int day_of_week          = now.dayOfTheWeek();
    int day_of_week_adjusted = (day_of_week + 6) % 7;

    float ambient_light  = readLDR();
    int   motion_detected = digitalRead(PIR_PIN);

    smoothed_ldr = SMOOTHING_FACTOR * smoothed_ldr +
                   (1 - SMOOTHING_FACTOR) * ambient_light;

    float sin_hour    = sin(2.0 * PI * hour / 24.0);
    float cos_hour    = cos(2.0 * PI * hour / 24.0);
    int   time_period = getTimePeriod(hour);

    float tree_pred, correction, confidence, min_dist;
    int   neighbors_used;
    float ml_brightness = predict_knn(
      smoothed_ldr, motion_detected, sin_hour, cos_hour,
      time_period, day_of_week_adjusted,
      &tree_pred, &correction, &confidence, &min_dist, &neighbors_used);

    manual_offset = readManualOffset();

    float final_brightness = ml_brightness + manual_offset;
    if (final_brightness < 0)   final_brightness = 0;
    if (final_brightness > 100) final_brightness = 100;

    smoothed_brightness = SMOOTHING_FACTOR * smoothed_brightness +
                          (1 - SMOOTHING_FACTOR) * final_brightness;

    int pwm_value = map(smoothed_brightness, 0, 100, 0, 255);
    analogWrite(LED_PIN, pwm_value);

    prediction_count++;
    avg_brightness = (avg_brightness * (prediction_count - 1) + final_brightness) / prediction_count;

    char time_str[9];
    sprintf(time_str, "%02d:%02d:%02d", hour, minute, now.second());

    const char *day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

    char line[180];
    sprintf(line,
      "| %s | %3s | %2d | %6.1f |   %d   |   %d   | %4.1f | %+5.1f | %4.1f | %+3d | %5.1f | %3d |",
      time_str,
      day_names[day_of_week_adjusted],
      hour,
      smoothed_ldr,
      motion_detected,
      time_period,
      tree_pred,
      confidence * correction,
      ml_brightness,
      manual_offset,
      final_brightness,
      pwm_value);
    Serial.println(line);

    monitorPotForSampleCapture(current_time, ml_brightness, final_brightness);
    checkDailyRollover(now);
  }

  if (Serial.available()) {
    handleSerialCommand();
  }
}

// ============================================
// SENSOR READS
// ============================================

float readLDR() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(LDR_PIN);
    delay(2);
  }
  float avg     = sum / 20.0f;
  float voltage = (avg / 4095.0f) * 3.3f;

  if (voltage <= 0.01f) return 0;

  float r_ldr = 10000.0f * voltage / (3.3f - voltage);
  float lux   = 32768000.0f * powf(r_ldr, -1.4f);
  if (lux < 0)        lux = 0;
  if (lux > 100000.0) lux = 100000.0;
  return lux;
}

int readManualOffset() {
  int pot_value = analogRead(POT_PIN);
  int center    = 2048;

  if (pot_value >= center - POT_DEADZONE && pot_value <= center + POT_DEADZONE) {
    return 0;
  }
  if (pot_value < center - POT_DEADZONE) {
    return map(pot_value, 0, center - POT_DEADZONE, -100, 0);
  }
  return map(pot_value, center + POT_DEADZONE, 4095, 0, 100);
}

int getTimePeriod(int hour) {
  if (hour >= 4 && hour <= 6)  return 0;   // Early Morning
  if (hour >  6 && hour <= 12) return 1;   // Morning
  if (hour > 12 && hour <= 16) return 2;   // Afternoon
  if (hour > 16 && hour <= 20) return 3;   // Evening
  return 4;                                // Night
}

// ============================================
// SAMPLE-CAPTURE TRIGGER (pot adjustment)
// ============================================

void monitorPotForSampleCapture(unsigned long current_time,
                                float ml_pred, float user_brightness) {
  int current_pot_raw = analogRead(POT_PIN);

  if (pot_candidate_active) {
    if (abs(current_pot_raw - current_pot_candidate) <= POT_CHANGE_THRESHOLD) {
      if (current_time - pot_stable_start >= POT_STABLE_DURATION) {
        if (last_stable_pot_value == -1 ||
            abs(current_pot_raw - last_stable_pot_value) > POT_CHANGE_THRESHOLD) {

          last_stable_pot_value = current_pot_raw;
          pot_candidate_active  = false;
          pot_change_count_today++;

          if (abs(manual_offset) > 5) {
            captureTrainingSample(ml_pred, user_brightness);
          }

          Serial.print(F("[note] Pot change #"));
          Serial.println(pot_change_count_today);
        }
      }
    } else {
      current_pot_candidate = current_pot_raw;
      pot_stable_start      = current_time;
    }
  } else {
    if (last_stable_pot_value == -1) {
      last_stable_pot_value = current_pot_raw;
    } else if (abs(current_pot_raw - last_stable_pot_value) > POT_CHANGE_THRESHOLD) {
      pot_candidate_active  = true;
      current_pot_candidate = current_pot_raw;
      pot_stable_start      = current_time;
    }
  }
}

void checkDailyRollover(DateTime &now) {
  int current_day = now.day();
  if (last_check_day != -1 && current_day != last_check_day) {
    pot_change_count_today = 0;
  }
  last_check_day = current_day;
}

// ============================================
// SERIAL COMMAND HANDLER
// ============================================

void testManualPrediction(int hour, int day, float ambient, int motion, int pot_value) {
  float sin_hour    = sin(2.0 * PI * hour / 24.0);
  float cos_hour    = cos(2.0 * PI * hour / 24.0);
  int   time_period = getTimePeriod(hour);

  int center = 2048;
  int offset = 0;
  if (pot_value < center - POT_DEADZONE) {
    offset = map(pot_value, 0, center - POT_DEADZONE, -100, 0);
  } else if (pot_value > center + POT_DEADZONE) {
    offset = map(pot_value, center + POT_DEADZONE, 4095, 0, 100);
  }

  float tree_pred, correction, confidence, min_dist;
  int   neighbors_used;
  float knn_pred = predict_knn(ambient, motion, sin_hour, cos_hour, time_period, day,
                               &tree_pred, &correction, &confidence,
                               &min_dist, &neighbors_used);

  float tree_final = tree_pred + offset; if (tree_final < 0) tree_final = 0; if (tree_final > 100) tree_final = 100;
  float knn_final  = knn_pred  + offset; if (knn_final  < 0) knn_final  = 0; if (knn_final  > 100) knn_final  = 100;

  const char *day_names[]   = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  const char *period_names[]= {"Early Morning", "Morning", "Afternoon", "Evening", "Night"};

  Serial.println();
  Serial.println(F("=== MANUAL PREDICTION TEST ==="));
  Serial.print(F("  Time: "));   Serial.print(hour); Serial.print(F(":00 (")); Serial.print(period_names[time_period]); Serial.println(F(")"));
  Serial.print(F("  Day:  "));   Serial.println(day_names[day]);
  Serial.print(F("  Light:"));   Serial.print(ambient); Serial.println(F(" lux"));
  Serial.print(F("  Motion:"));  Serial.println(motion ? F("YES") : F("NO"));
  Serial.print(F("  Pot:   ")); Serial.print(pot_value); Serial.print(F(" (offset ")); Serial.print(offset); Serial.println(F("%)"));

  Serial.println();
  Serial.println(F("Model breakdown:"));
  Serial.print(F("  Tree raw       : ")); Serial.println(tree_pred, 2);
  Serial.print(F("  KNN correction : ")); Serial.println(correction, 2);
  Serial.print(F("  Confidence     : ")); Serial.println(confidence, 3);
  Serial.print(F("  KNN raw output : ")); Serial.println(knn_pred, 2);
  Serial.print(F("  Neighbors used : ")); Serial.println(neighbors_used);
  Serial.print(F("  Min distance   : "));
  if (isinf(min_dist)) Serial.println(F("inf (no samples)"));
  else                 Serial.println(min_dist, 4);

  Serial.println();
  Serial.print(F("  Final w/ pot : tree="));  Serial.print(tree_final, 2);
  Serial.print(F("%  knn="));                  Serial.print(knn_final, 2);
  Serial.println(F("%"));
  Serial.println();
}

void cmdSamples() {
  Serial.println();
  Serial.println(F("=== TRAINING SAMPLES ==="));
  if (training_sample_count == 0) {
    Serial.println(F("No samples yet."));
    Serial.println();
    return;
  }
  const char *day_names[]    = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
  const char *period_names[] = {"E.Morn", "Morn", "Aftern", "Even", "Night"};

  Serial.print(F("Total: "));
  Serial.println(training_sample_count);
  Serial.println(F(" #  | Ambient | Mot | Period | Day | Target% | Timestamp"));
  for (int i = 0; i < training_sample_count; i++) {
    const TrainingSample &s = samples_cache[i];
    char line[100];
    sprintf(line, "%2d  | %7.1f |  %d  | %6s | %3s | %6.1f  | %lu",
      i + 1, s.ambient_light, s.motion_detected,
      period_names[s.time_period], day_names[s.day_of_week],
      s.target_brightness, (unsigned long)s.timestamp);
    Serial.println(line);
  }
  Serial.println();
}

void cmdKnnInfo() {
  Serial.println();
  Serial.println(F("=== KNN CONFIG ==="));
  Serial.print(F("  K (neighbors)        : ")); Serial.println(knn_cfg.k_neighbors);
  Serial.print(F("  sigma_kernel         : ")); Serial.println(knn_cfg.sigma_kernel, 4);
  Serial.print(F("  sigma_confidence     : ")); Serial.println(knn_cfg.sigma_confidence, 4);
  Serial.print(F("  Stored samples       : ")); Serial.println(training_sample_count);
  Serial.print(F("  Effective K          : ")); Serial.println(min(knn_cfg.k_neighbors, training_sample_count));
  Serial.println(F("  Distance metric: importance-weighted squared distance"));
  Serial.println(F("  Categorical features (period, day) use 0/1 distance"));
  Serial.print(F("  AMBIENT_SCALE        : ")); Serial.println(AMBIENT_SCALE);
  Serial.println();
}

void cmdModelInfo() {
  Serial.println();
  Serial.println(F("=== MODEL INFO ==="));
  Serial.println(F("Base model     : Decision Tree (2000 samples, FROZEN)"));
  Serial.println(F("Correction     : KNN residual over user samples"));
  Serial.print  (F("Stored samples : ")); Serial.println(training_sample_count);
  Serial.print  (F("Personalization: "));
  Serial.println(training_sample_count > 0 ? F("ACTIVE (Tree + KNN)")
                                           : F("INACTIVE (Tree only - no samples)"));
  Serial.println();
  Serial.println(F("Feature importance (used as KNN distance weights):"));
  Serial.print(F("  Ambient   : ")); Serial.println(IMPORTANCE_AMBIENT, 4);
  Serial.print(F("  Motion    : ")); Serial.println(IMPORTANCE_MOTION, 4);
  Serial.print(F("  Sin(hour) : ")); Serial.println(IMPORTANCE_SIN_HOUR, 4);
  Serial.print(F("  Cos(hour) : ")); Serial.println(IMPORTANCE_COS_HOUR, 4);
  Serial.print(F("  Period    : ")); Serial.println(IMPORTANCE_TIME_PERIOD, 4);
  Serial.print(F("  Day       : ")); Serial.println(IMPORTANCE_DAY_OF_WEEK, 4);
  Serial.println();
}

void cmdStats() {
  Serial.println();
  Serial.println(F("=== STATS ==="));
  Serial.print(F("Total predictions : ")); Serial.println(prediction_count);
  Serial.print(F("Average brightness: ")); Serial.print(avg_brightness); Serial.println(F("%"));
  Serial.print(F("Pot changes today : ")); Serial.println(pot_change_count_today);
  Serial.print(F("Stored samples    : ")); Serial.println(training_sample_count);
  Serial.print(F("Personalization   : "));
  Serial.println(training_sample_count > 0 ? F("ACTIVE") : F("INACTIVE"));
  Serial.println();
}

void cmdHelp() {
  Serial.println();
  Serial.println(F("=== COMMANDS ==="));
  Serial.println(F("help            - this message"));
  Serial.println(F("stats           - runtime statistics"));
  Serial.println(F("samples         - list stored user samples"));
  Serial.println(F("clearsamples    - delete all stored samples"));
  Serial.println(F("modelinfo       - model summary"));
  Serial.println(F("knninfo         - KNN configuration"));
  Serial.println(F("resetmodel      - alias of clearsamples"));
  Serial.println(F("setk N          - set K neighbors (1..MAX)"));
  Serial.println(F("setsigma X      - set kernel bandwidth (e.g. 0.25)"));
  Serial.println(F("addsample HH DD AMBIENT MOTION BRIGHTNESS"));
  Serial.println(F("                  HH=0..23 DD=0(Mon)..6(Sun)"));
  Serial.println(F("                  AMBIENT lux, MOTION 0/1, BRIGHTNESS 0..100"));
  Serial.println(F("test HH DD AMBIENT MOTION POT"));
  Serial.println(F("                  POT raw 0..4095 (2048=center)"));
  Serial.println();
}

void handleSerialCommand() {
  String command = Serial.readStringUntil('\n');
  command.trim();

  if (command == "help") {
    cmdHelp();
  }
  else if (command == "stats") {
    cmdStats();
  }
  else if (command == "samples") {
    cmdSamples();
  }
  else if (command == "modelinfo") {
    cmdModelInfo();
  }
  else if (command == "knninfo") {
    cmdKnnInfo();
  }
  else if (command == "clearsamples" || command == "resetmodel") {
    training_sample_count = 0;
    next_write_idx        = 0;
    EEPROM.put(EEPROM_SAMPLE_COUNT_ADDR,   training_sample_count);
    EEPROM.put(EEPROM_NEXT_WRITE_IDX_ADDR, next_write_idx);
    EEPROM.commit();
    Serial.println();
    Serial.println(F("[v] All samples cleared. Tree-only mode active."));
    Serial.println();
  }
  else if (command.startsWith("setk ")) {
    int k = command.substring(5).toInt();
    if (k < 1 || k > MAX_TRAINING_SAMPLES) {
      Serial.print(F("[x] K must be 1..")); Serial.println(MAX_TRAINING_SAMPLES);
    } else {
      knn_cfg.k_neighbors = k;
      saveKnnConfig();
      Serial.print(F("[v] K set to ")); Serial.println(k);
    }
  }
  else if (command.startsWith("setsigma ")) {
    float s = command.substring(9).toFloat();
    if (s <= 0.0f) {
      Serial.println(F("[x] sigma must be > 0"));
    } else {
      knn_cfg.sigma_kernel = s;
      saveKnnConfig();
      Serial.print(F("[v] sigma_kernel set to ")); Serial.println(s, 4);
    }
  }
  else if (command.startsWith("addsample ")) {
    int   hour, day, motion;
    float ambient, brightness;
    int parsed = sscanf(command.c_str(), "addsample %d %d %f %d %f",
                        &hour, &day, &ambient, &motion, &brightness);
    if (parsed == 5) {
      if (hour < 0 || hour > 23)         { Serial.println(F("[x] Hour 0..23")); return; }
      if (day < 0 || day > 6)            { Serial.println(F("[x] Day 0..6"));   return; }
      if (ambient < 0 || ambient > 100000){ Serial.println(F("[x] Ambient 0..100000")); return; }
      if (motion != 0 && motion != 1)    { Serial.println(F("[x] Motion 0/1")); return; }
      if (brightness < 0 || brightness > 100) { Serial.println(F("[x] Brightness 0..100")); return; }
      addManualSample(hour, day, ambient, motion, brightness);
    } else {
      Serial.println(F("[x] Use: addsample HH DD AMBIENT MOTION BRIGHTNESS"));
    }
  }
  else if (command.startsWith("test ")) {
    int   hour, day, motion, pot;
    float ambient;
    int parsed = sscanf(command.c_str(), "test %d %d %f %d %d",
                        &hour, &day, &ambient, &motion, &pot);
    if (parsed == 5) {
      if (hour < 0 || hour > 23)            { Serial.println(F("[x] Hour 0..23")); return; }
      if (day < 0 || day > 6)               { Serial.println(F("[x] Day 0..6"));   return; }
      if (ambient < 0 || ambient > 100000)  { Serial.println(F("[x] Ambient 0..100000")); return; }
      if (motion != 0 && motion != 1)       { Serial.println(F("[x] Motion 0/1")); return; }
      if (pot < 0 || pot > 4095)            { Serial.println(F("[x] Pot 0..4095")); return; }
      testManualPrediction(hour, day, ambient, motion, pot);
    } else {
      Serial.println(F("[x] Use: test HH DD AMBIENT MOTION POT"));
    }
  }
  else if (command.length() > 0) {
    Serial.print(F("[x] Unknown: ")); Serial.println(command);
    Serial.println(F("Type 'help' for available commands"));
  }
}

// ============================================
// MISC
// ============================================

void printDateTime(DateTime dt) {
  char buffer[25];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
          dt.year(), dt.month(), dt.day(),
          dt.hour(), dt.minute(), dt.second());
  Serial.print(buffer);
}
