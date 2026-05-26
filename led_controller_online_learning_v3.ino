/*
 * LED Brightness Controller with LIVE Online Learning
 * Retrains at: 20 pot changes OR daily at 00:00:01
 * Shows learned predictions in manual mode
 */

#include <Wire.h>
#include <RTClib.h>
#include <EEPROM.h>
#include <math.h>

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
#define UPDATE_INTERVAL 1000
#define SMOOTHING_FACTOR 0.7
#define POT_DEADZONE 200

#define POT_STABLE_DURATION 5000
#define POT_CHANGE_THRESHOLD 50
#define MAX_CHANGES_BEFORE_RETRAIN 20

#define RETRAIN_HOUR 0
#define RETRAIN_MINUTE 0
#define RETRAIN_SECOND 1

#define EEPROM_SIZE 4096
#define EEPROM_MAGIC 0xA5B3
#define MAX_LEARNING_SAMPLES 50

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
float smoothed_ldr = 0;
float smoothed_brightness = 0;
int manual_offset = 0;
unsigned long last_update = 0;

// ============================================
// RETRAINING STATE
// ============================================
int last_stable_pot_value = -1;
int current_pot_candidate = -1;
unsigned long pot_stable_start = 0;
bool pot_candidate_active = false;
int pot_change_count_today = 0;
bool retrained_at_midnight = false;
bool retrained_at_20_changes = false;
int last_retrain_day = -1;

// ============================================
// LEARNING DATA STRUCTURES
// ============================================
struct TrainingRecord {
  float ambient_light;
  int motion_detected;
  int hour;
  int day_of_week;
  int time_period;
  float target_brightness;  // User's desired brightness
  bool valid;
};

TrainingRecord training_buffer[MAX_LEARNING_SAMPLES];
int training_count = 0;

// Learned model parameters (simple adjustment per time period)
struct LearnedAdjustment {
  float brightness_offset[5];  // Offset for each time period (0-4)
  int sample_count[5];         // Number of samples per period
};

LearnedAdjustment learned_model;

// ============================================
// STATISTICS
// ============================================
unsigned long prediction_count = 0;
float avg_brightness = 0;

// ============================================
// SETUP
// ============================================
void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10);
  
  Serial.println(F("==================================="));
  Serial.println(F("LED Controller with LIVE Learning"));
  Serial.println(F("Retrains at 20 changes OR 00:00:01"));
  Serial.println(F("==================================="));
  
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(LDR_PIN, INPUT);
  pinMode(POT_PIN, INPUT);
  
  EEPROM.begin(EEPROM_SIZE);
  
  Serial.print(F("Initializing RTC... "));
  if (!rtc.begin()) {
    Serial.println(F("FAILED"));
    while (1) delay(1000);
  }
  Serial.println(F("OK"));
  
  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power. Setting compile time."));
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  Serial.print(F("Current Time: "));
  printDateTime(rtc.now());
  Serial.println();
  
  // Load learned model from EEPROM
  loadLearnedModel();
  
  smoothed_ldr = readLDR();
  
  Serial.println(F("\n=== Features ==="));
  Serial.println(F("✓ Collect 20 pot changes"));
  Serial.println(F("✓ Retrain at 20 changes OR 00:00:01"));
  Serial.println(F("✓ Apply learning immediately"));
  Serial.println(F("✓ Manual mode shows learned values"));
  
  Serial.println(F("\n=== Commands ==="));
  Serial.println(F("help    - Show commands"));
  Serial.println(F("manual  - Enter manual prediction mode"));
  Serial.println(F("auto    - Return to auto mode"));
  Serial.println(F("predict HH MM DAY LUX MOTION POT"));
  Serial.println(F("stats   - Show statistics"));
  Serial.println(F("retrain - Force retrain now"));
  Serial.println(F("clear   - Clear learned model"));
  
  Serial.println(F("\n=== Monitoring Started ===\n"));
  Serial.println(F("┌──────────┬─────┬──────┬─────────┬────────┬────────┬───────┬────────┬────────┬─────┐"));
  Serial.println(F("│   Time   │ Day │ Hour │ Ambient │ Motion │ Period │ Base% │ Offset │ Final% │ PWM │"));
  Serial.println(F("├──────────┼─────┼──────┼─────────┼────────┼────────┬───────┼────────┼────────┼─────┤"));
}

// ============================================
// LOOP
// ============================================
void loop() {
  unsigned long current_time = millis();
  
  if (!manual_input_mode && current_time - last_update >= UPDATE_INTERVAL) {
    last_update = current_time;
    
    DateTime now = rtc.now();
    int hour = now.hour();
    int minute = now.minute();
    int day_of_week = now.dayOfTheWeek();
    int day_of_week_adjusted = (day_of_week + 6) % 7;
    
    float ambient_light = readLDR();
    int motion_detected = digitalRead(PIR_PIN);
    
    smoothed_ldr = SMOOTHING_FACTOR * smoothed_ldr + (1 - SMOOTHING_FACTOR) * ambient_light;
    
    int time_period = getTimePeriod(hour);
    
    // Get base prediction (simple rule-based)
    float base_brightness = getBasePrediction(smoothed_ldr, motion_detected, time_period);
    
    // Apply learned adjustments
    float learned_brightness = applyLearnedModel(base_brightness, time_period);
    
    // Read manual offset
    manual_offset = readManualOffset();
    
    // Final brightness
    float final_brightness = learned_brightness + manual_offset;
    final_brightness = constrain(final_brightness, 0, 100);
    
    smoothed_brightness = SMOOTHING_FACTOR * smoothed_brightness + 
                         (1 - SMOOTHING_FACTOR) * final_brightness;
    
    int pwm_value = map(smoothed_brightness, 0, 100, 0, 255);
    analogWrite(LED_PIN, pwm_value);
    
    prediction_count++;
    avg_brightness = (avg_brightness * (prediction_count - 1) + final_brightness) / prediction_count;
    
    // Print monitoring line
    char time_str[9];
    sprintf(time_str, "%02d:%02d:%02d", hour, minute, now.second());
    const char *day_names[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    
    char line[160];
    sprintf(line, "│ %s │ %3s │ %2d │ %6.1f │   %d    │   %d    │ %5.1f │ %+4d │ %5.1f │ %3d │",
            time_str, day_names[day_of_week_adjusted], hour, smoothed_ldr, motion_detected,
            time_period, learned_brightness, manual_offset, final_brightness, pwm_value);
    Serial.println(line);
    
    // Monitor pot for training data collection
    monitorPotForRetraining(current_time, smoothed_ldr, motion_detected, hour, 
                           day_of_week_adjusted, time_period, final_brightness);
    
    // Check scheduled retraining
    checkScheduledRetraining(now);
  }
  
  if (Serial.available()) {
    handleSerialCommand();
  }
}

// ============================================
// SENSOR READING
// ============================================
float readLDR() {
  long sum = 0;
  for (int i = 0; i < 20; i++) {
    sum += analogRead(LDR_PIN);
    delay(2);
  }
  float avg = sum / 20.0;
  float voltage = (avg / 4095.0) * 3.3;
  
  if (voltage <= 0.01) return 0;
  
  float r_ldr = 10000.0 * voltage / (3.3 - voltage);
  float lux = 32768000.0 * pow(r_ldr, -1.4);
  lux = constrain(lux, 0, 100000);
  
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
  if (hour >= 4 && hour <= 6) return 0;       // Early Morning
  else if (hour > 6 && hour <= 12) return 1;  // Morning
  else if (hour > 12 && hour <= 16) return 2; // Afternoon
  else if (hour > 16 && hour <= 20) return 3; // Evening
  else return 4;                               // Night
}

// ============================================
// BASE PREDICTION (Simple Rule-Based)
// ============================================
float getBasePrediction(float ambient_light, int motion_detected, int time_period) {
  // Simple baseline: brightness inversely proportional to ambient light
  float base = 0;
  
  if (ambient_light < 10) {
    base = 80;  // Very dark
  } else if (ambient_light < 50) {
    base = 60;  // Dark
  } else if (ambient_light < 200) {
    base = 40;  // Dim
  } else if (ambient_light < 500) {
    base = 20;  // Moderate
  } else {
    base = 10;  // Bright
  }
  
  // Boost if motion detected during night
  if (motion_detected && time_period == 4) {
    base += 20;
  }
  
  return constrain(base, 0, 100);
}

// ============================================
// LEARNED MODEL APPLICATION
// ============================================
float applyLearnedModel(float base_brightness, int time_period) {
  // Apply learned offset for this time period
  if (learned_model.sample_count[time_period] > 0) {
    float adjustment = learned_model.brightness_offset[time_period];
    return constrain(base_brightness + adjustment, 0, 100);
  }
  return base_brightness;
}

// ============================================
// POT MONITORING FOR TRAINING DATA
// ============================================
void monitorPotForRetraining(unsigned long current_time, float ambient_light, 
                             int motion_detected, int hour, int day_of_week,
                             int time_period, float final_brightness) {
  int current_pot_raw = analogRead(POT_PIN);
  
  if (pot_candidate_active) {
    if (abs(current_pot_raw - current_pot_candidate) <= POT_CHANGE_THRESHOLD) {
      if (current_time - pot_stable_start >= POT_STABLE_DURATION) {
        if (last_stable_pot_value == -1 || 
            abs(current_pot_raw - last_stable_pot_value) > POT_CHANGE_THRESHOLD) {
          
          // Record this training sample
          recordTrainingSample(ambient_light, motion_detected, hour, day_of_week,
                              time_period, final_brightness);
          
          last_stable_pot_value = current_pot_raw;
          pot_candidate_active = false;
          
          // Check for 20-change threshold
          if (pot_change_count_today >= MAX_CHANGES_BEFORE_RETRAIN && 
              !retrained_at_20_changes) {
            Serial.println();
            Serial.println(F(">>> 20 POT CHANGES REACHED - RETRAINING NOW <<<"));
            performRetraining();
            retrained_at_20_changes = true;
          }
        } else {
          pot_candidate_active = false;
        }
      }
    } else {
      current_pot_candidate = current_pot_raw;
      pot_stable_start = current_time;
    }
  } else {
    if (last_stable_pot_value == -1) {
      last_stable_pot_value = current_pot_raw;
    } else if (abs(current_pot_raw - last_stable_pot_value) > POT_CHANGE_THRESHOLD) {
      pot_candidate_active = true;
      current_pot_candidate = current_pot_raw;
      pot_stable_start = current_time;
    }
  }
}

void recordTrainingSample(float ambient_light, int motion_detected, int hour,
                         int day_of_week, int time_period, float target_brightness) {
  if (training_count < MAX_LEARNING_SAMPLES) {
    TrainingRecord &rec = training_buffer[training_count];
    rec.ambient_light = ambient_light;
    rec.motion_detected = motion_detected;
    rec.hour = hour;
    rec.day_of_week = day_of_week;
    rec.time_period = time_period;
    rec.target_brightness = target_brightness;
    rec.valid = true;
    training_count++;
  }
  
  pot_change_count_today++;
  
  Serial.println();
  Serial.print(F(">>> TRAINING SAMPLE #"));
  Serial.print(pot_change_count_today);
  Serial.print(F(" RECORDED | Period="));
  Serial.print(time_period);
  Serial.print(F(" | Target="));
  Serial.print(target_brightness, 1);
  Serial.println(F("% <<<"));
  Serial.println();
}

// ============================================
// SCHEDULED RETRAINING
// ============================================
void checkScheduledRetraining(DateTime &now) {
  int current_day = now.day();
  
  if (current_day != last_retrain_day && last_retrain_day != -1) {
    pot_change_count_today = 0;
    retrained_at_midnight = false;
    retrained_at_20_changes = false;
    last_retrain_day = current_day;
  }
  
  if (last_retrain_day == -1) {
    last_retrain_day = current_day;
  }
  
  if (now.hour() == RETRAIN_HOUR && now.minute() == RETRAIN_MINUTE &&
      now.second() == RETRAIN_SECOND && !retrained_at_midnight) {
    
    Serial.println();
    Serial.println(F(">>> MIDNIGHT RETRAINING (00:00:01) <<<"));
    Serial.print(F("Total samples today: "));
    Serial.println(pot_change_count_today);
    
    performRetraining();
    retrained_at_midnight = true;
    
    pot_change_count_today = 0;
    retrained_at_20_changes = false;
  }
}

// ============================================
// PERFORM RETRAINING (CORE LEARNING ALGORITHM)
// ============================================
void performRetraining() {
  Serial.println(F("─────────────────────────────────────────────"));
  Serial.println(F("        RETRAINING MODEL NOW                 "));
  Serial.println(F("─────────────────────────────────────────────"));
  Serial.print(F("Training samples: "));
  Serial.println(training_count);
  
  if (training_count == 0) {
    Serial.println(F("No training data. Skipping."));
    Serial.println(F("─────────────────────────────────────────────"));
    return;
  }
  
  // Reset learned model
  for (int p = 0; p < 5; p++) {
    learned_model.brightness_offset[p] = 0;
    learned_model.sample_count[p] = 0;
  }
  
  // Compute average target brightness per time period
  float sum_per_period[5] = {0, 0, 0, 0, 0};
  int count_per_period[5] = {0, 0, 0, 0, 0};
  float base_sum_per_period[5] = {0, 0, 0, 0, 0};
  
  for (int i = 0; i < training_count; i++) {
    TrainingRecord &rec = training_buffer[i];
    if (!rec.valid) continue;
    
    int p = rec.time_period;
    
    // Calculate what base prediction would have been
    float base_pred = getBasePrediction(rec.ambient_light, rec.motion_detected, p);
    
    // Store target and base
    sum_per_period[p] += rec.target_brightness;
    base_sum_per_period[p] += base_pred;
    count_per_period[p]++;
  }
  
  // Calculate learned offset = avg(target) - avg(base)
  Serial.println(F("\nLearned adjustments by time period:"));
  for (int p = 0; p < 5; p++) {
    if (count_per_period[p] > 0) {
      float avg_target = sum_per_period[p] / count_per_period[p];
      float avg_base = base_sum_per_period[p] / count_per_period[p];
      float offset = avg_target - avg_base;
      
      learned_model.brightness_offset[p] = offset;
      learned_model.sample_count[p] = count_per_period[p];
      
      Serial.print(F("  Period "));
      Serial.print(p);
      Serial.print(F(": Offset = "));
      Serial.print(offset, 1);
      Serial.print(F("% ("));
      Serial.print(count_per_period[p]);
      Serial.println(F(" samples)"));
    }
  }
  
  // Save to EEPROM
  saveLearnedModel();
  
  // Clear training buffer
  training_count = 0;
  for (int i = 0; i < MAX_LEARNING_SAMPLES; i++) {
    training_buffer[i].valid = false;
  }
  
  Serial.println(F("\nRetraining complete! Model updated."));
  Serial.println(F("─────────────────────────────────────────────"));
  Serial.println();
}

// ============================================
// EEPROM PERSISTENCE
// ============================================
void saveLearnedModel() {
  Serial.print(F("Saving model to EEPROM... "));
  
  int addr = 0;
  EEPROM.put(addr, EEPROM_MAGIC);
  addr += sizeof(uint16_t);
  
  EEPROM.put(addr, learned_model);
  addr += sizeof(LearnedAdjustment);
  
  EEPROM.commit();
  Serial.println(F("Done."));
}

void loadLearnedModel() {
  Serial.print(F("Loading model from EEPROM... "));
  
  int addr = 0;
  uint16_t magic;
  EEPROM.get(addr, magic);
  addr += sizeof(uint16_t);
  
  if (magic != EEPROM_MAGIC) {
    Serial.println(F("No saved model (first run)."));
    for (int p = 0; p < 5; p++) {
      learned_model.brightness_offset[p] = 0;
      learned_model.sample_count[p] = 0;
    }
    return;
  }
  
  EEPROM.get(addr, learned_model);
  
  int total_samples = 0;
  for (int p = 0; p < 5; p++) {
    total_samples += learned_model.sample_count[p];
  }
  
  Serial.print(total_samples);
  Serial.println(F(" samples loaded."));
  
  if (total_samples > 0) {
    Serial.println(F("Loaded adjustments:"));
    for (int p = 0; p < 5; p++) {
      if (learned_model.sample_count[p] > 0) {
        Serial.print(F("  Period "));
        Serial.print(p);
        Serial.print(F(": "));
        Serial.print(learned_model.brightness_offset[p], 1);
        Serial.println(F("%"));
      }
    }
  }
}

void clearLearnedModel() {
  Serial.println(F("Clearing learned model..."));
  
  for (int p = 0; p < 5; p++) {
    learned_model.brightness_offset[p] = 0;
    learned_model.sample_count[p] = 0;
  }
  
  training_count = 0;
  pot_change_count_today = 0;
  
  // Clear EEPROM
  for (int i = 0; i < 512; i++) {
    EEPROM.write(i, 0xFF);
  }
  EEPROM.commit();
  
  Serial.println(F("Model cleared."));
}

// ============================================
// MANUAL INPUT MODE
// ============================================
void enterManualInputMode() {
  manual_input_mode = true;
  
  Serial.println(F("\n==================================="));
  Serial.println(F("  MANUAL PREDICTION MODE ACTIVE  "));
  Serial.println(F("==================================="));
  Serial.println(F("Shows LEARNED brightness values!"));
  Serial.println(F("\nUsage:"));
  Serial.println(F("predict HH MM DAY LUX MOTION POT"));
  Serial.println(F("Example: predict 14 30 2 500.5 1 2048"));
  Serial.println(F("\nType 'auto' to return to live mode\n"));
}

void exitManualInputMode() {
  manual_input_mode = false;
  Serial.println(F("\nReturned to AUTOMATIC MODE\n"));
}

void processManualInput(int hour, int minute, int day_of_week,
                       float ambient_light, int motion_detected, int pot_value) {
  
  int time_period = getTimePeriod(hour);
  
  // Get base prediction
  float base_brightness = getBasePrediction(ambient_light, motion_detected, time_period);
  
  // Apply learned model
  float learned_brightness = applyLearnedModel(base_brightness, time_period);
  
  // Calculate pot offset
  int center = 2048;
  int offset = 0;
  
  if (pot_value >= center - POT_DEADZONE && pot_value <= center + POT_DEADZONE) {
    offset = 0;
  } else if (pot_value < center - POT_DEADZONE) {
    offset = map(pot_value, 0, center - POT_DEADZONE, -100, 0);
  } else {
    offset = map(pot_value, center + POT_DEADZONE, 4095, 0, 100);
  }
  
  float final_brightness = constrain(learned_brightness + offset, 0, 100);
  int pwm_value = map(final_brightness, 0, 100, 0, 255);
  
  Serial.println(F("\n==================================="));
  Serial.println(F("     PREDICTION RESULT (LEARNED)   "));
  Serial.println(F("==================================="));
  Serial.print(F("Time Period: "));
  Serial.println(time_period);
  Serial.print(F("Base Prediction: "));
  Serial.print(base_brightness, 1);
  Serial.println(F("%"));
  
  if (learned_model.sample_count[time_period] > 0) {
    Serial.print(F("Learned Adjustment: "));
    Serial.print(learned_brightness - base_brightness, 1);
    Serial.print(F("% (from "));
    Serial.print(learned_model.sample_count[time_period]);
    Serial.println(F(" samples)"));
  } else {
    Serial.println(F("Learned Adjustment: None (no training data)"));
  }
  
  Serial.print(F("Learned Brightness: "));
  Serial.print(learned_brightness, 1);
  Serial.println(F("%"));
  Serial.print(F("Manual Offset: "));
  Serial.print(offset);
  Serial.println(F("%"));
  Serial.print(F("Final Brightness: "));
  Serial.print(final_brightness, 1);
  Serial.println(F("%"));
  Serial.print(F("PWM Value: "));
  Serial.println(pwm_value);
  Serial.println(F("===================================\n"));
}

// ============================================
// SERIAL COMMAND HANDLER
// ============================================
void handleSerialCommand() {
  String command = Serial.readStringUntil('\n');
  command.trim();
  
  if (command == "help") {
    Serial.println(F("\n=== AVAILABLE COMMANDS ==="));
    Serial.println(F("help    - Show this help"));
    Serial.println(F("manual  - Enter manual prediction mode"));
    Serial.println(F("auto    - Return to automatic mode"));
    Serial.println(F("predict HH MM DAY LUX MOTION POT - Manual predict"));
    Serial.println(F("stats   - Show statistics"));
    Serial.println(F("retrain - Force retrain now"));
    Serial.println(F("clear   - Clear learned model"));
    Serial.println();
  }
  else if (command == "manual") {
    enterManualInputMode();
  }
  else if (command == "auto") {
    exitManualInputMode();
  }
  else if (command.startsWith("predict ")) {
    int hour, minute, day_of_week, motion_detected, pot_value;
    float ambient_light;
    
    int parsed = sscanf(command.c_str(), "predict %d %d %d %f %d %d",
                       &hour, &minute, &day_of_week, &ambient_light,
                       &motion_detected, &pot_value);
    
    if (parsed == 6) {
      processManualInput(hour, minute, day_of_week, ambient_light,
                        motion_detected, pot_value);
    } else {
      Serial.println(F("Invalid format. Use: predict HH MM DAY LUX MOTION POT"));
    }
  }
  else if (command == "stats") {
    Serial.println(F("\n=== STATISTICS ==="));
    Serial.print(F("Predictions: "));
    Serial.println(prediction_count);
    Serial.print(F("Average Brightness: "));
    Serial.print(avg_brightness, 1);
    Serial.println(F("%"));
    Serial.print(F("Training Samples Collected: "));
    Serial.println(training_count);
    Serial.print(F("Pot Changes Today: "));
    Serial.print(pot_change_count_today);
    Serial.print(F("/"));
    Serial.println(MAX_CHANGES_BEFORE_RETRAIN);
    
    Serial.println(F("\n=== LEARNED MODEL ==="));
    for (int p = 0; p < 5; p++) {
      Serial.print(F("Period "));
      Serial.print(p);
      Serial.print(F(": "));
      if (learned_model.sample_count[p] > 0) {
        Serial.print(learned_model.brightness_offset[p], 1);
        Serial.print(F("% ("));
        Serial.print(learned_model.sample_count[p]);
        Serial.println(F(" samples)"));
      } else {
        Serial.println(F("No data"));
      }
    }
    Serial.println();
  }
  else if (command == "retrain") {
    performRetraining();
  }
  else if (command == "clear") {
    clearLearnedModel();
  }
  else {
    Serial.print(F("Unknown command: "));
    Serial.println(command);
    Serial.println(F("Type 'help' for commands"));
  }
}

// ============================================
// UTILITY
// ============================================
void printDateTime(DateTime dt) {
  char buffer[25];
  sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d",
          dt.year(), dt.month(), dt.day(),
          dt.hour(), dt.minute(), dt.second());
  Serial.print(buffer);
}
