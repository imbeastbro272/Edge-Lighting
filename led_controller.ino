/*
 * LED Brightness Controller with ML Decision Tree and Manual Override
 * 
 * Hardware Required:
 *   - Arduino Uno/Mega or ESP32
 *   - DS3231 RTC Module
 *   - HC-SR501 PIR Motion Sensor
 *   - LDR (Light Dependent Resistor) with 10k resistor
 *   - 10k Potentiometer (for manual override)
 *   - LED strip or PWM LED
 * 
 * Pin Configuration:
 *   - RTC: SDA (A4), SCL (A5)
 *   - PIR: D2
 *   - LDR: A0
 *   - Potentiometer: A1
 *   - LED: D9 (PWM)
 */

#include <Wire.h>
#include <RTClib.h>
#include <math.h>
#include "tree_rules.h"      // Generated ML model
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

// RTC
RTC_DS3231 rtc;

// State variables
float smoothed_ldr = 0;
float smoothed_brightness = 0;
int manual_offset = 0;
unsigned long last_update = 0;

// Statistics
unsigned long prediction_count = 0;
float avg_brightness = 0;

void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10);  // Wait for serial (ESP32)
  
  Serial.println(F("\n==================================="));
  Serial.println(F("LED Brightness Controller"));
  Serial.println(F("ML Decision Tree + Manual Override"));
  Serial.println(F("===================================\n"));
  
  // Initialize pins
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
  
  // Validate ML model
  validate_model();
  
  // Initialize smoothed values
  smoothed_ldr = readLDR();
  
  Serial.println(F("\n=== Manual Time Setting ==="));
  Serial.println(F("Type: settime HH:MM:SS (example: settime 14:30:00)"));
  Serial.println(F("Type: help (for all commands)"));
  
  Serial.println(F("\n=== Starting Continuous Monitoring ===\n"));
  Serial.println(F("┌──────────┬──────┬─────────┬────────┬────────┬──────┬────────┬────────┬─────┐"));
  Serial.println(F("│   Time   │ Hour │ Ambient │ Motion │ Period │  ML% │ Offset │ Final% │ PWM │"));
  Serial.println(F("├──────────┼──────┼─────────┼────────┼────────┼──────┼────────┼────────┼─────┤"));
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
    
    float ambient_light = readLDR();
    int motion_detected = digitalRead(PIR_PIN);
    
    // Smooth LDR reading
    smoothed_ldr = SMOOTHING_FACTOR * smoothed_ldr + (1 - SMOOTHING_FACTOR) * ambient_light;
    
    // Calculate time features
    float sin_hour = sin(2.0 * PI * hour / 24.0);
    float cos_hour = cos(2.0 * PI * hour / 24.0);
    int time_period = getTimePeriod(hour);
    
    // Get ML prediction
    float ml_brightness = predict_brightness(
      smoothed_ldr,
      motion_detected,
      sin_hour,
      cos_hour,
      time_period
    );
    
    // Read manual override
    manual_offset = readManualOffset();
    
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
    
    char line[100];
    sprintf(line, "│ %s │  %2d  │ %6.1f  │   %d    │   %d    │ %4.1f │  %+4d  │ %5.1f  │ %3d │",
            time_str, hour, smoothed_ldr, motion_detected, time_period, 
            ml_brightness, manual_offset, final_brightness, pwm_value);
    Serial.println(line);
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
  // V = VCC * R_LDR / (R_FIXED + R_LDR)
  // Rearranged: R_LDR = R_FIXED * V / (VCC - V)
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

void printDateTime(DateTime dt) {
  Serial.print(dt.year(), DEC);
  Serial.print('/');
  Serial.print(dt.month(), DEC);
  Serial.print('/');
  Serial.print(dt.day(), DEC);
  Serial.print(' ');
  Serial.print(dt.hour(), DEC);
  Serial.print(':');
  Serial.print(dt.minute(), DEC);
  Serial.print(':');
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
    Serial.println();
  } else if (cmd == "time") {
    // Print current time
    DateTime now = rtc.now();
    Serial.print(F("Current time: "));
    printDateTime(now);
    Serial.println();
  } else if (cmd == "test") {
    // Re-run validation
    validate_model();
  } else if (cmd.startsWith("settime ")) {
    // Set time manually: settime HH:MM:SS
    // Example: settime 14:30:45
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
    Serial.println(F("│                     Example: settime 22:15:00               │"));
    Serial.println(F("│                                                             │"));
    Serial.println(F("│ time              - Show current RTC time                   │"));
    Serial.println(F("│ stats             - Show prediction statistics              │"));
    Serial.println(F("│ test              - Re-run model validation                 │"));
    Serial.println(F("│ help              - Show this help menu                     │"));
    Serial.println(F("└─────────────────────────────────────────────────────────────┘"));
    Serial.println();
  } else if (cmd.length() > 0) {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help' for available commands"));
  }
}
