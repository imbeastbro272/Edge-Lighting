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

// Pin definitions
#define PIR_PIN 2
#define LDR_PIN A0
#define POT_PIN A1
#define LED_PIN 9

// Configuration
#define UPDATE_INTERVAL 1000  // Update every 1 second (ms)
#define SMOOTHING_FACTOR 0.7  // For exponential moving average (0-1)
#define POT_DEADZONE 20       // Deadzone around center (512 +/- 20)

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
  
  Serial.println(F("\nStarting main loop...\n"));
  Serial.println(F("Time\t\tHour\tAmbient\tMotion\tPeriod\tML%\tOffset\tFinal%\tPWM"));
  Serial.println(F("--------\t----\t-------\t------\t------\t---\t------\t------\t---"));
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
    
    // Output to serial (CSV format for easy logging)
    char time_str[9];
    sprintf(time_str, "%02d:%02d:%02d", hour, minute, now.second());
    
    Serial.print(time_str);
    Serial.print(F("\t"));
    Serial.print(hour);
    Serial.print(F("\t"));
    Serial.print(smoothed_ldr, 1);
    Serial.print(F("\t"));
    Serial.print(motion_detected);
    Serial.print(F("\t"));
    Serial.print(time_period);
    Serial.print(F("\t"));
    Serial.print(ml_brightness, 1);
    Serial.print(F("\t"));
    Serial.print(manual_offset);
    Serial.print(F("\t"));
    Serial.print(final_brightness, 1);
    Serial.print(F("\t"));
    Serial.println(pwm_value);
  }
  
  // Check for serial commands
  if (Serial.available()) {
    handleSerialCommand();
  }
}

float readLDR() {
  // Read LDR voltage (0-5V mapped to 0-1023)
  int raw = analogRead(LDR_PIN);
  float voltage = raw * (5.0 / 1023.0);
  
  // Convert to approximate lux
  // This is a simplified conversion - calibrate for your specific LDR
  // Typical relationship: Lux = 10^((V_ref - V_ldr) / sensitivity)
  // For this example, using linear mapping
  float lux = voltage * 200.0;  // Adjust multiplier based on your LDR
  
  return lux;
}

int readManualOffset() {
  int pot_value = analogRead(POT_PIN);
  
  // Apply deadzone around center (512)
  int center = 512;
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
    offset = map(pot_value, center + POT_DEADZONE, 1023, 0, 100);
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
  } else if (cmd == "help") {
    Serial.println(F("\n=== Commands ==="));
    Serial.println(F("stats  - Show statistics"));
    Serial.println(F("time   - Show current time"));
    Serial.println(F("test   - Re-run model validation"));
    Serial.println(F("help   - Show this help"));
    Serial.println();
  } else if (cmd.length() > 0) {
    Serial.print(F("Unknown command: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help' for available commands"));
  }
}
