/*
 * LED Brightness Controller with ML Decision Tree, Manual Override, and Online Learning
 * 
 * NEW FEATURES:
 *   - Day of week consideration for predictions
 *   - Night + High Ambient Light scenario: Motion-dependent brightness
 *   - Online learning: User preference tracking and model adaptation
 *   - EEPROM storage for learned preferences
 * 
 * Hardware Required:
 *   - ESP32 Dev Board
 *   - DS3231 RTC Module
 *   - HC-SR501 PIR Motion Sensor
 *   - LDR (Light Dependent Resistor) with 10k resistor
 *   - 10k Potentiometer (for manual override)
 *   - LED strip or PWM LED
 * 
 * Pin Configuration:
 *   - RTC: SDA (GPIO 21), SCL (GPIO 22)
 *   - PIR: GPIO 27
 *   - LDR: GPIO 34 (ADC)
 *   - Potentiometer: GPIO 33 (ADC)
 *   - LED: GPIO 2 (PWM)
 */

#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <math.h>
#include "tree_rules.h"      // Generated ML model

// Pin definitions (ESP32)
#define PIR_PIN 27
#define LDR_PIN 34
#define POT_PIN 33
#define LED_PIN 2

// Configuration
#define UPDATE_INTERVAL 1000  // Update every 1 second (ms)
#define SMOOTHING_FACTOR 0.7  // For exponential moving average (0-1)
#define POT_DEADZONE 200      // Deadzone around center (2048 +/- 200) for ESP32

// Online Learning Configuration
#define LEARNING_ENABLED true
#define LEARNING_RATE 0.1      // Learning rate for preference updates (0-1)
#define OVERRIDE_THRESHOLD 10  // Minimum offset to trigger learning (%)
#define LEARNING_WINDOW 5000   // Time window to confirm override (ms)
#define MAX_LEARNING_SAMPLES 100 // Maximum stored learning samples in EEPROM

// Potentiometer-based Retraining Configuration
#define POT_STABLE_DURATION 5000   // Pot value must remain same for 5 seconds (ms)
#define POT_CHANGE_THRESHOLD 50    // Minimum ADC change to consider pot value "changed"
#define MAX_CHANGES_BEFORE_RETRAIN 20  // Trigger immediate retraining after 20 changes
#define RETRAIN_HOUR 0             // Midnight retraining hour
#define RETRAIN_MINUTE 0           // Midnight retraining minute
#define RETRAIN_SECOND 1           // Midnight retraining second (00:00:01)

// EEPROM Configuration
#define EEPROM_SIZE 4096
#define EEPROM_MAGIC 0xA5B3    // Magic number to validate EEPROM data
#define EEPROM_VERSION 1

// RTC
RTC_DS3231 rtc;

// State variables
float smoothed_ldr = 0;
float smoothed_brightness = 0;
int manual_offset = 0;
int previous_offset = 0;
unsigned long last_update = 0;
unsigned long override_start_time = 0;
bool override_active = false;

// Potentiometer retraining state variables
int last_stable_pot_value = -1;       // Last confirmed stable pot value (-1 = not set)
int current_pot_candidate = -1;       // Current pot value being monitored for stability
unsigned long pot_stable_start = 0;   // When current candidate pot value started being stable
bool pot_candidate_active = false;    // Whether we are currently monitoring a candidate

// Retraining schedule state
int pot_change_count_today = 0;       // Number of pot changes recorded today
bool retrained_at_midnight = false;   // Flag to avoid multiple midnight retrains
bool retrained_at_20_changes = false; // Flag to avoid repeated 20-change retrains
int last_retrain_day = -1;            // Day of month when last daily reset occurred

// Online Learning Data Structure
struct LearningRecord {
  float ambient_light;
  int motion_detected;
  float sin_hour;
  float cos_hour;
  int time_period;
  int day_of_week;
  float user_preferred_brightness;
  unsigned long timestamp;
  bool valid;
};

// Learning storage
LearningRecord learning_buffer[10];  // Ring buffer for recent overrides
int learning_buffer_index = 0;
int learning_count = 0;

// Potentiometer Retraining Data Structure
struct PotChangeRecord {
  float ambient_light;
  int motion_detected;
  float sin_hour;
  float cos_hour;
  int time_period;
  int day_of_week;
  int pot_value;             // Raw ADC pot value at the time of change
  float final_brightness;    // The resulting brightness user set via pot
  unsigned long timestamp;
  bool valid;
};

// Potentiometer change buffer (stores up to 50 changes per day)
#define MAX_POT_CHANGES_BUFFER 50
PotChangeRecord pot_change_buffer[MAX_POT_CHANGES_BUFFER];
int pot_change_buffer_index = 0;

// EEPROM Learning Storage Header
struct EEPROMHeader {
  uint16_t magic;
  uint8_t version;
  uint8_t learning_samples_count;
  uint32_t checksum;
};

// Statistics
unsigned long prediction_count = 0;
float avg_brightness = 0;
int learning_events = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10);  // Wait for serial (ESP32)
  
  Serial.println(F("\n==================================="));
  Serial.println(F("LED Brightness Controller v2.0"));
  Serial.println(F("ML + Manual Override + Online Learning"));
  Serial.println(F("===================================\n"));
  
  // Initialize pins
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(POT_PIN, INPUT);
  
  // Initialize EEPROM
  EEPROM.begin(EEPROM_SIZE);
  
  // Initialize RTC
  Serial.print(F("Initializing RTC... "));
  if (!rtc.begin()) {
    Serial.println(F("FAILED!"));
    Serial.println(F("RTC not found. Check wiring!"));
    while (1) delay(1000);
  }
  Serial.println(F("OK"));
  
  // Check if RTC lost power
  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, setting time..."));
    // Set to compile time
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  // Display current time
  DateTime now = rtc.now();
  Serial.print(F("Current time: "));
  printDateTime(now);
  Serial.println();
  
  // Load learning data from EEPROM
  loadLearningData();
  
  // Initialize smoothed values
  smoothed_ldr = readLDR();
  
  Serial.println(F("\n=== Features ==="));
  Serial.println(F("✓ Day of week consideration"));
  Serial.println(F("✓ Night + High Ambient Light scenario handling"));
  Serial.println(F("✓ Online learning from user preferences"));
  Serial.println(F("✓ EEPROM persistence across reboots"));
  Serial.println(F("✓ Pot-based retraining (5s stable = change recorded)"));
  Serial.println(F("✓ Scheduled retraining at 00:00:01 daily"));
  Serial.println(F("✓ Immediate retraining after 20 pot changes/day"));
  
  Serial.println(F("\n=== Manual Time Setting ==="));
  Serial.println(F("Type: settime HH:MM:SS (example: settime 14:30:00)"));
  Serial.println(F("Type: help (for all commands)"));
  
  Serial.println(F("\n=== Starting Continuous Monitoring ===\n"));
  Serial.println(F("┌──────────┬─────┬──────┬─────────┬────────┬────────┬──────┬────────┬────────┬─────┬──────┐"));
  Serial.println(F("│   Time   │ Day │ Hour │ Ambient │ Motion │ Period │  ML% │ Offset │ Final% │ PWM │ Learn│"));
  Serial.println(F("├──────────┼─────┼──────┼─────────┼────────┼────────┼──────┼────────┼────────┼─────┼──────┤"));
}

void loop() {
  unsigned long current_time = millis();
  
  // Update at fixed interval
  if (current_time - last_update >= UPDATE_INTERVAL) {
    last_update = current_time;
    
    // Read sensors
    DateTime now = rtc.now();
    int hour = now.hour();
    int minute = now.minute();
    int day_of_week = now.dayOfTheWeek();  // 0=Sunday, 1=Monday, ..., 6=Saturday
    
    // Convert to our format: 0=Monday, 6=Sunday
    int day_of_week_adjusted = (day_of_week + 6) % 7;
    
    float ambient_light = readLDR();
    int motion_detected = digitalRead(PIR_PIN);
    
    // Smooth LDR reading
    smoothed_ldr = SMOOTHING_FACTOR * smoothed_ldr + (1 - SMOOTHING_FACTOR) * ambient_light;
    
    // Calculate time features
    float sin_hour = sin(2.0 * PI * hour / 24.0);
    float cos_hour = cos(2.0 * PI * hour / 24.0);
    int time_period = getTimePeriod(hour);
    
    // Get ML prediction with day_of_week
    float ml_brightness = predict_brightness(
      smoothed_ldr,
      motion_detected,
      sin_hour,
      cos_hour,
      time_period,
      day_of_week_adjusted
    );
    
    // Apply learned preferences
    if (LEARNING_ENABLED) {
      ml_brightness = applyLearning(ml_brightness, smoothed_ldr, motion_detected, 
                                   sin_hour, cos_hour, time_period, day_of_week_adjusted);
    }
    
    // Read manual override
    manual_offset = readManualOffset();
    
    // Check for user override and trigger learning
    if (LEARNING_ENABLED) {
      checkAndLearn(ml_brightness, manual_offset, smoothed_ldr, motion_detected,
                   sin_hour, cos_hour, time_period, day_of_week_adjusted, current_time);
    }
    
    // Apply manual override
    float final_brightness = ml_brightness + manual_offset;
    final_brightness = constrain(final_brightness, 0, 100);
    
    // Smooth brightness changes
    smoothed_brightness = SMOOTHING_FACTOR * smoothed_brightness + 
                         (1 - SMOOTHING_FACTOR) * final_brightness;
    
    // Convert to PWM (0-255)
    int pwm_value = map(smoothed_brightness, 0, 100, 0, 255);
    analogWrite(LED_PIN, pwm_value);
    
    // Update statistics
    prediction_count++;
    avg_brightness = (avg_brightness * (prediction_count - 1) + final_brightness) / prediction_count;
    
    // Output to serial (formatted table)
    char time_str[9];
    sprintf(time_str, "%02d:%02d:%02d", hour, minute, now.second());
    
    const char* day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    char learn_indicator = override_active ? 'L' : ' ';
    
    char line[120];
    sprintf(line, "│ %s │ %3s │  %2d  │ %6.1f  │   %d    │   %d    │ %4.1f │  %+4d  │ %5.1f  │ %3d │  %c   │",
            time_str, day_names[day_of_week_adjusted], hour, smoothed_ldr, motion_detected, 
            time_period, ml_brightness, manual_offset, final_brightness, pwm_value, learn_indicator);
    Serial.println(line);
    
    // === Potentiometer-based retraining logic ===
    monitorPotForRetraining(now, smoothed_ldr, motion_detected, sin_hour, cos_hour, 
                            time_period, day_of_week_adjusted, final_brightness, current_time);
    
    // === Scheduled retraining checks ===
    checkScheduledRetraining(now);
  }
  
  // Check for serial commands
  if (Serial.available()) {
    handleSerialCommand();
  }
}

float readLDR() {
  // ESP32: 12-bit ADC (0-4095), 3.3V reference
  // LDR Calibration Constants
  const float ADC_MAX = 4095.0;
  const float VCC = 3.3;
  const float R_FIXED = 10000.0;  // 10k fixed resistor
  const float LDR_A = 32768000.0; // Calibration constant
  const float LDR_B = -1.4;        // Calibration exponent
  const int SAMPLES = 20;          // Number of samples for averaging
  
  // Average multiple readings for stability
  long sum = 0;
  for (int i = 0; i < SAMPLES; i++) {
    sum += analogRead(LDR_PIN);
    delay(2);
  }
  float avg = sum / (float)SAMPLES;
  
  // Convert ADC to voltage
  float voltage = (avg / ADC_MAX) * VCC;
  
  // Calculate LDR resistance using voltage divider formula
  if (voltage <= 0.01) return 0.0;      // Avoid division by zero
  if (voltage >= VCC)  return 100000.0; // Maximum brightness
  
  float r_ldr = R_FIXED * voltage / (VCC - voltage);
  
  // Convert resistance to lux using power law: Lux = A * R^B
  float lux = LDR_A * pow(r_ldr, LDR_B);
  
  // Clamp to reasonable range
  if (lux < 0) lux = 0;
  if (lux > 100000) lux = 100000;
  
  return lux;
}

int readManualOffset() {
  // ESP32: 12-bit ADC (0-4095)
  int pot_value = analogRead(POT_PIN);
  
  // Center position for 12-bit ADC
  int center = 2048;
  if (pot_value >= center - POT_DEADZONE && pot_value <= center + POT_DEADZONE) {
    return 0;  // No offset in deadzone
  }
  
  // Map to offset range (-100 to +100)
  int offset;
  if (pot_value < center - POT_DEADZONE) {
    // Below center: map to -100 to 0
    offset = map(pot_value, 0, center - POT_DEADZONE, -100, 0);
  } else {
    // Above center: map to 0 to +100
    offset = map(pot_value, center + POT_DEADZONE, 4095, 0, 100);
  }
  
  return offset;
}

int getTimePeriod(int hour) {
  // Time Period Mapping:
  // 0 = Early Morning (4:01 - 6:00)
  // 1 = Morning (6:01 - 12:00)
  // 2 = Afternoon (12:01 - 16:00)
  // 3 = Evening (16:01 - 20:00)
  // 4 = Night (20:01 - 4:00)
  
  if (hour >= 4 && hour <= 6) {
    return 0;  // Early Morning
  } else if (hour > 6 && hour <= 12) {
    return 1;  // Morning
  } else if (hour > 12 && hour <= 16) {
    return 2;  // Afternoon
  } else if (hour > 16 && hour <= 20) {
    return 3;  // Evening
  } else {
    return 4;  // Night
  }
}

// ============================================
// ONLINE LEARNING FUNCTIONS
// ============================================

void checkAndLearn(float ml_brightness, int current_offset, float ambient_light, 
                   int motion_detected, float sin_hour, float cos_hour, 
                   int time_period, int day_of_week, unsigned long current_time) {
  /*
   * Monitor user's manual override behavior and learn preferences
   */
  
  // Check if user is applying significant override
  if (abs(current_offset) >= OVERRIDE_THRESHOLD) {
    if (!override_active) {
      // Start of override
      override_active = true;
      override_start_time = current_time;
      previous_offset = current_offset;
    } else {
      // Check if override is sustained
      if (current_time - override_start_time >= LEARNING_WINDOW) {
        // Override sustained for LEARNING_WINDOW ms - learn this preference!
        float user_preferred_brightness = ml_brightness + current_offset;
        user_preferred_brightness = constrain(user_preferred_brightness, 0, 100);
        
        // Store learning record
        storeLearningRecord(ambient_light, motion_detected, sin_hour, cos_hour,
                          time_period, day_of_week, user_preferred_brightness, current_time);
        
        // Reset override tracking
        override_active = false;
        learning_events++;
        
        Serial.println();
        Serial.println(F(">>> LEARNING EVENT RECORDED <<<"));
        Serial.print(F("ML predicted: "));
        Serial.print(ml_brightness, 1);
        Serial.print(F("%, User adjusted to: "));
        Serial.print(user_preferred_brightness, 1);
        Serial.println(F("%"));
        Serial.println();
      }
    }
  } else {
    // Override released
    if (override_active) {
      override_active = false;
    }
  }
}

void storeLearningRecord(float ambient_light, int motion_detected, float sin_hour,
                        float cos_hour, int time_period, int day_of_week,
                        float user_preferred_brightness, unsigned long timestamp) {
  /*
   * Store a learning record in the ring buffer
   */
  
  LearningRecord record;
  record.ambient_light = ambient_light;
  record.motion_detected = motion_detected;
  record.sin_hour = sin_hour;
  record.cos_hour = cos_hour;
  record.time_period = time_period;
  record.day_of_week = day_of_week;
  record.user_preferred_brightness = user_preferred_brightness;
  record.timestamp = timestamp;
  record.valid = true;
  
  // Store in ring buffer
  learning_buffer[learning_buffer_index] = record;
  learning_buffer_index = (learning_buffer_index + 1) % 10;
  
  if (learning_count < 10) {
    learning_count++;
  }
  
  // Periodically save to EEPROM
  if (learning_events % 5 == 0) {  // Save every 5 learning events
    saveLearningData();
  }
}

float applyLearning(float ml_brightness, float ambient_light, int motion_detected,
                   float sin_hour, float cos_hour, int time_period, int day_of_week) {
  /*
   * Apply learned preferences to adjust ML predictions
   * Uses k-nearest neighbors approach with recent learning samples
   */
  
  if (learning_count == 0) {
    return ml_brightness;  // No learning data yet
  }
  
  // Find similar past scenarios and compute weighted average adjustment
  float total_weight = 0;
  float weighted_adjustment = 0;
  
  for (int i = 0; i < learning_count; i++) {
    LearningRecord &record = learning_buffer[i];
    if (!record.valid) continue;
    
    // Compute similarity (inverse of distance)
    float distance = computeDistance(ambient_light, motion_detected, sin_hour, cos_hour,
                                     time_period, day_of_week, record);
    
    if (distance < 0.001) distance = 0.001;  // Avoid division by zero
    
    float weight = 1.0 / distance;
    
    // Calculate adjustment from this record
    float adjustment = record.user_preferred_brightness - ml_brightness;
    
    weighted_adjustment += weight * adjustment;
    total_weight += weight;
  }
  
  if (total_weight > 0) {
    float learned_adjustment = (weighted_adjustment / total_weight) * LEARNING_RATE;
    ml_brightness += learned_adjustment;
    ml_brightness = constrain(ml_brightness, 0, 100);
  }
  
  return ml_brightness;
}

float computeDistance(float ambient_light, int motion_detected, float sin_hour,
                     float cos_hour, int time_period, int day_of_week,
                     LearningRecord &record) {
  /*
   * Compute normalized Euclidean distance between current state and learning record
   */
  
  // Normalize features to similar scales
  float d_ambient = (ambient_light - record.ambient_light) / 1000.0;  // Normalize by max lux
  float d_motion = (motion_detected - record.motion_detected);
  float d_sin = (sin_hour - record.sin_hour);
  float d_cos = (cos_hour - record.cos_hour);
  float d_period = (time_period - record.time_period) / 4.0;  // 5 periods (0-4)
  float d_day = (day_of_week - record.day_of_week) / 6.0;    // 7 days (0-6)
  
  float distance = sqrt(d_ambient*d_ambient + d_motion*d_motion + 
                       d_sin*d_sin + d_cos*d_cos + 
                       d_period*d_period + d_day*d_day);
  
  return distance;
}

void saveLearningData() {
  /*
   * Save learning data to EEPROM for persistence
   */
  
  Serial.println(F("\nSaving learning data to EEPROM..."));
  
  // Write header
  EEPROMHeader header;
  header.magic = EEPROM_MAGIC;
  header.version = EEPROM_VERSION;
  header.learning_samples_count = min(learning_count, MAX_LEARNING_SAMPLES);
  header.checksum = 0;  // Calculate later
  
  int addr = 0;
  EEPROM.put(addr, header);
  addr += sizeof(EEPROMHeader);
  
  // Write learning records
  for (int i = 0; i < (int)header.learning_samples_count && addr < EEPROM_SIZE - sizeof(LearningRecord); i++) {
    EEPROM.put(addr, learning_buffer[i]);
    addr += sizeof(LearningRecord);
  }
  
  EEPROM.commit();
  Serial.println(F("Learning data saved."));
}

void loadLearningData() {
  /*
   * Load learning data from EEPROM
   */
  
  Serial.print(F("Loading learning data from EEPROM... "));
  
  int addr = 0;
  EEPROMHeader header;
  EEPROM.get(addr, header);
  addr += sizeof(EEPROMHeader);
  
  // Validate magic number
  if (header.magic != EEPROM_MAGIC) {
    Serial.println(F("No valid learning data found (first run)."));
    learning_count = 0;
    return;
  }
  
  // Check version
  if (header.version != EEPROM_VERSION) {
    Serial.println(F("Learning data version mismatch. Clearing..."));
    learning_count = 0;
    return;
  }
  
  // Load learning records
  learning_count = min((int)header.learning_samples_count, 10);
  for (int i = 0; i < learning_count && addr < EEPROM_SIZE - sizeof(LearningRecord); i++) {
    EEPROM.get(addr, learning_buffer[i]);
    addr += sizeof(LearningRecord);
  }
  
  learning_buffer_index = learning_count % 10;
  
  Serial.print(learning_count);
  Serial.println(F(" learning samples loaded."));
}

void clearLearningData() {
  /*
   * Clear all learning data from memory and EEPROM
   */
  
  Serial.println(F("\nClearing all learning data..."));
  
  // Clear memory
  learning_count = 0;
  learning_buffer_index = 0;
  learning_events = 0;
  
  for (int i = 0; i < 10; i++) {
    learning_buffer[i].valid = false;
  }
  
  // Clear EEPROM
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  
  Serial.println(F("Learning data cleared."));
}

// ============================================
// POTENTIOMETER-BASED RETRAINING FUNCTIONS
// ============================================

void monitorPotForRetraining(DateTime &now, float ambient_light, int motion_detected,
                             float sin_hour, float cos_hour, int time_period,
                             int day_of_week, float final_brightness, unsigned long current_time) {
  /*
   * Monitor potentiometer value for stable changes.
   * A change is recorded only when:
   *   1. The pot value differs from the last stable value
   *   2. The new value remains stable (unchanged) for 5 seconds
   */
  
  int current_pot_raw = analogRead(POT_PIN);
  
  // Check if pot value has changed from what we're currently tracking as candidate
  if (pot_candidate_active) {
    // Check if current reading is still close to the candidate value
    if (abs(current_pot_raw - current_pot_candidate) <= POT_CHANGE_THRESHOLD) {
      // Still stable at candidate value - check if 5 seconds elapsed
      if (current_time - pot_stable_start >= POT_STABLE_DURATION) {
        // Pot has been stable for 5 seconds!
        // Check if it's actually different from last confirmed stable value
        if (last_stable_pot_value == -1 || abs(current_pot_raw - last_stable_pot_value) > POT_CHANGE_THRESHOLD) {
          // This is a confirmed new pot change - record it
          recordPotChange(ambient_light, motion_detected, sin_hour, cos_hour,
                         time_period, day_of_week, current_pot_raw, final_brightness, current_time);
          
          last_stable_pot_value = current_pot_raw;
          pot_candidate_active = false;
          
          // Check if we hit 20 changes threshold
          if (pot_change_count_today >= MAX_CHANGES_BEFORE_RETRAIN && !retrained_at_20_changes) {
            Serial.println();
            Serial.println(F(">>> 20 POT CHANGES REACHED - TRIGGERING IMMEDIATE RETRAINING <<<"));
            performRetraining();
            retrained_at_20_changes = true;
          }
        } else {
          // Pot returned to same value as before - not a real change
          pot_candidate_active = false;
        }
      }
      // else: still waiting for 5 seconds to elapse, do nothing
    } else {
      // Pot value changed again while we were monitoring - restart stability timer
      current_pot_candidate = current_pot_raw;
      pot_stable_start = current_time;
    }
  } else {
    // No candidate active - check if pot moved away from last stable value
    if (last_stable_pot_value == -1) {
      // First time - set initial stable value
      last_stable_pot_value = current_pot_raw;
    } else if (abs(current_pot_raw - last_stable_pot_value) > POT_CHANGE_THRESHOLD) {
      // Pot value has changed from last stable - start monitoring candidate
      pot_candidate_active = true;
      current_pot_candidate = current_pot_raw;
      pot_stable_start = current_time;
    }
  }
}

void recordPotChange(float ambient_light, int motion_detected, float sin_hour,
                     float cos_hour, int time_period, int day_of_week,
                     int pot_value, float final_brightness, unsigned long timestamp) {
  /*
   * Record a confirmed potentiometer change into the daily buffer
   */
  
  if (pot_change_buffer_index < MAX_POT_CHANGES_BUFFER) {
    PotChangeRecord &record = pot_change_buffer[pot_change_buffer_index];
    record.ambient_light = ambient_light;
    record.motion_detected = motion_detected;
    record.sin_hour = sin_hour;
    record.cos_hour = cos_hour;
    record.time_period = time_period;
    record.day_of_week = day_of_week;
    record.pot_value = pot_value;
    record.final_brightness = final_brightness;
    record.timestamp = timestamp;
    record.valid = true;
    
    pot_change_buffer_index++;
  }
  
  pot_change_count_today++;
  
  Serial.println();
  Serial.print(F(">>> POT CHANGE #"));
  Serial.print(pot_change_count_today);
  Serial.print(F(" RECORDED (stable for 5s) | Pot ADC="));
  Serial.print(pot_value);
  Serial.print(F(" | Brightness="));
  Serial.print(final_brightness, 1);
  Serial.println(F("% <<<"));
  Serial.println();
}

void checkScheduledRetraining(DateTime &now) {
  /*
   * Check if it's time for scheduled retraining:
   *   - Daily at 00:00:01
   *   - Reset daily counters at midnight
   */
  
  int current_day = now.day();
  
  // Reset daily state when a new day starts
  if (current_day != last_retrain_day && last_retrain_day != -1) {
    // New day detected - reset daily counters
    pot_change_count_today = 0;
    retrained_at_midnight = false;
    retrained_at_20_changes = false;
    last_retrain_day = current_day;
  }
  
  // Initialize last_retrain_day on first run
  if (last_retrain_day == -1) {
    last_retrain_day = current_day;
  }
  
  // Check for midnight retraining (00:00:01)
  if (now.hour() == RETRAIN_HOUR && now.minute() == RETRAIN_MINUTE && 
      now.second() >= RETRAIN_SECOND && now.second() <= RETRAIN_SECOND + 2 &&
      !retrained_at_midnight) {
    
    Serial.println();
    Serial.println(F(">>> MIDNIGHT RETRAINING TRIGGERED (00:00:01) <<<"));
    Serial.print(F("Total pot changes today: "));
    Serial.println(pot_change_count_today);
    
    performRetraining();
    retrained_at_midnight = true;
    
    // Reset for new day
    pot_change_count_today = 0;
    retrained_at_20_changes = false;
  }
}

void performRetraining() {
  /*
   * Perform model retraining using collected potentiometer change data.
   * Transfers pot change records into the learning buffer for online adaptation.
   */
  
  Serial.println(F("─────────────────────────────────────────────"));
  Serial.println(F("    RETRAINING MODEL WITH POT CHANGE DATA    "));
  Serial.println(F("─────────────────────────────────────────────"));
  Serial.print(F("Records to process: "));
  Serial.println(pot_change_buffer_index);
  
  if (pot_change_buffer_index == 0) {
    Serial.println(F("No pot change data available for retraining. Skipping."));
    Serial.println(F("─────────────────────────────────────────────"));
    return;
  }
  
  // Transfer pot change data into the learning buffer
  int records_processed = 0;
  for (int i = 0; i < pot_change_buffer_index; i++) {
    PotChangeRecord &pot_record = pot_change_buffer[i];
    if (!pot_record.valid) continue;
    
    // Convert pot change to a learning record
    LearningRecord learn_record;
    learn_record.ambient_light = pot_record.ambient_light;
    learn_record.motion_detected = pot_record.motion_detected;
    learn_record.sin_hour = pot_record.sin_hour;
    learn_record.cos_hour = pot_record.cos_hour;
    learn_record.time_period = pot_record.time_period;
    learn_record.day_of_week = pot_record.day_of_week;
    learn_record.user_preferred_brightness = pot_record.final_brightness;
    learn_record.timestamp = pot_record.timestamp;
    learn_record.valid = true;
    
    // Store in learning ring buffer
    learning_buffer[learning_buffer_index] = learn_record;
    learning_buffer_index = (learning_buffer_index + 1) % 10;
    if (learning_count < 10) {
      learning_count++;
    }
    
    records_processed++;
    learning_events++;
  }
  
  Serial.print(F("Records processed into learning model: "));
  Serial.println(records_processed);
  
  // Save updated learning data to EEPROM
  saveLearningData();
  
  // Clear the pot change buffer after retraining
  for (int i = 0; i < MAX_POT_CHANGES_BUFFER; i++) {
    pot_change_buffer[i].valid = false;
  }
  pot_change_buffer_index = 0;
  
  Serial.println(F("Retraining complete. Learning model updated."));
  Serial.println(F("─────────────────────────────────────────────"));
  Serial.println();
}

// ============================================
// UTILITY FUNCTIONS
// ============================================

void printDateTime(DateTime dt) {
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  Serial.print(dt.day(), DEC);
  Serial.print(' ');
  const char* day_names[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  Serial.print(day_names[dt.dayOfTheWeek()]);
  Serial.print(' ');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  if (dt.minute() < 10) Serial.print('0');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
  if (dt.second() < 10) Serial.print('0');
  Serial.print(dt.second(), DEC);
}

void handleSerialCommand() {
  String cmd = Serial.readStringUntil('\n');
  cmd.trim();
  
  if (cmd == "stats") {
    // Print statistics
    Serial.println(F("\n=== Statistics ==="));
    Serial.print(F("Predictions: "));
    Serial.println(prediction_count);
    Serial.print(F("Average Brightness: "));
    Serial.print(avg_brightness, 2);
    Serial.println(F("%"));
    Serial.print(F("Current Offset: "));
    Serial.print(manual_offset);
    Serial.println(F("%"));
    Serial.print(F("Learning Events: "));
    Serial.println(learning_events);
    Serial.print(F("Learning Samples: "));
    Serial.println(learning_count);
    Serial.println(F("\n=== Retraining Status ==="));
    Serial.print(F("Pot Changes Today: "));
    Serial.print(pot_change_count_today);
    Serial.print(F("/"));
    Serial.println(MAX_CHANGES_BEFORE_RETRAIN);
    Serial.print(F("Pot Change Buffer: "));
    Serial.print(pot_change_buffer_index);
    Serial.print(F("/"));
    Serial.println(MAX_POT_CHANGES_BUFFER);
    Serial.print(F("Midnight Retrain Done: "));
    Serial.println(retrained_at_midnight ? "Yes" : "No");
    Serial.print(F("20-Change Retrain Done: "));
    Serial.println(retrained_at_20_changes ? "Yes" : "No");
    Serial.println();
  } else if (cmd == "time") {
    // Print current time
    DateTime now = rtc.now();
    Serial.print(F("Current time: "));
    printDateTime(now);
    Serial.println();
  } else if (cmd == "learning") {
    // Print learning data
    Serial.println(F("\n=== Learning Data ==="));
    Serial.print(F("Total samples: "));
    Serial.println(learning_count);
    Serial.println(F("\nRecent learning records:"));
    
    for (int i = 0; i < learning_count; i++) {
      LearningRecord &record = learning_buffer[i];
      if (record.valid) {
        Serial.print(F("  ["));
        Serial.print(i);
        Serial.print(F("] Ambient="));
        Serial.print(record.ambient_light, 1);
        Serial.print(F(", Motion="));
        Serial.print(record.motion_detected);
        Serial.print(F(", Period="));
        Serial.print(record.time_period);
        Serial.print(F(", Day="));
        Serial.print(record.day_of_week);
        Serial.print(F(" → User pref="));
        Serial.print(record.user_preferred_brightness, 1);
        Serial.println(F("%"));
      }
    }
    Serial.println();
  } else if (cmd == "clearlearning") {
    clearLearningData();
  } else if (cmd == "savelearning") {
    saveLearningData();
  } else if (cmd == "retrain") {
    // Manually trigger retraining
    Serial.println(F("\nManual retraining triggered..."));
    performRetraining();
  } else if (cmd.startsWith("settime ")) {
    // Set time manually: settime HH:MM:SS
    String timeStr = cmd.substring(8);
    timeStr.trim();
    
    if (timeStr.length() == 8 && timeStr.charAt(2) == ':' && timeStr.charAt(5) == ':') {
      int hour = timeStr.substring(0, 2).toInt();
      int minute = timeStr.substring(3, 5).toInt();
      int second = timeStr.substring(6, 8).toInt();
      
      if (hour >= 0 && hour < 24 && minute >= 0 && minute < 60 && second >= 0 && second < 60) {
        DateTime now = rtc.now();
        rtc.adjust(DateTime(now.year(), now.month(), now.day(), hour, minute, second));
        Serial.print(F("Time set to: "));
        Serial.print(hour);
        Serial.print(F(":"));
        Serial.print(minute);
        Serial.print(F(":"));
        Serial.println(second);
      } else {
        Serial.println(F("Error: Invalid time values"));
      }
    } else {
      Serial.println(F("Error: Invalid time format"));
      Serial.println(F("Format: settime HH:MM:SS"));
    }
  } else if (cmd == "help") {
    Serial.println(F("\n┌─────────────────────────────────────────────────────────────┐"));
    Serial.println(F("│                      COMMAND HELP                           │"));
    Serial.println(F("├─────────────────────────────────────────────────────────────┤"));
    Serial.println(F("│ settime HH:MM:SS  - Set time manually (24-hour format)      │"));
    Serial.println(F("│ time              - Show current RTC time                   │"));
    Serial.println(F("│ stats             - Show prediction & learning statistics   │"));
    Serial.println(F("│ learning          - Show learning data details              │"));
    Serial.println(F("│ clearlearning     - Clear all learning data                 │"));
    Serial.println(F("│ savelearning      - Manually save learning data to EEPROM   │"));
    Serial.println(F("│ retrain           - Manually trigger retraining now         │"));
    Serial.println(F("│ help              - Show this help menu                     │"));
    Serial.println(F("├─────────────────────────────────────────────────────────────┤"));
    Serial.println(F("│ RETRAINING SCHEDULE:                                        │"));
    Serial.println(F("│  - Pot change recorded when stable for 5 seconds            │"));
    Serial.println(F("│  - Auto retrain at 00:00:01 daily                           │"));
    Serial.println(F("│  - Immediate retrain if 20+ changes in 24 hours             │"));
    Serial.println(F("└─────────────────────────────────────────────────────────────┘"));
    Serial.println();
  } else if (cmd.length() > 0) {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help' for available commands"));
  }
}
