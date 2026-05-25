/*
 * LED Brightness Controller - VERSION WITHOUT VALIDATION
 * Use this until you generate tree_rules.h and test_cases.h from your trained model
 * 
 * Hardware Required:
 *   - Arduino Uno/Mega or ESP32
 *   - DS3231 RTC Module
 *   - HC-SR501 PIR Motion Sensor
 *   - LDR (Light Dependent Resistor) with 10k resistor
 *   - 10k Potentiometer (for manual override)
 *   - LED strip or PWM LED
 * 
 * Pin Configuration (ESP32):
 *   - RTC: SDA (GPIO21), SCL (GPIO22)
 *   - PIR: GPIO27
 *   - LDR: GPIO34 (ADC)
 *   - Potentiometer: GPIO33 (ADC)
 *   - LED: GPIO2 (PWM)
 */

#include <Wire.h>
#include <RTClib.h>
#include <math.h>

// COMMENT OUT these lines until you have the generated files:
// #include "tree_rules.h"      // Generated ML model
// #include "test_cases.h"      // Validation tests

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

// TEMPORARY: Simple placeholder prediction function
// Replace this when you have tree_rules.h
float predict_brightness(float ambient_light, int motion_detected, float sin_hour, float cos_hour, int time_period) {
  float brightness = 50.0f;
  
  // Inverse relationship with ambient light
  brightness += (1000.0f - ambient_light) / 20.0f;
  
  // Boost if motion detected
  if (motion_detected == 1) {
    brightness += 15.0f;
  }
  
  // Time period adjustments
  if (time_period == 4) {  // Night
    brightness += 10.0f;
    // Special: Night + High ambient = Lower brightness
    if (ambient_light > 500.0f) {
      brightness = 25.0f;
    }
  } else if (time_period == 2) {  // Afternoon
    brightness -= 15.0f;
  }
  
  // Clamp to valid range
  if (brightness < 0.0f) brightness = 0.0f;
  if (brightness > 100.0f) brightness = 100.0f;
  
  return brightness;
}

void setup() {
  Serial.begin(9600);
  while (!Serial) delay(10);
  
  Serial.println(F("\n==================================="));
  Serial.println(F("LED Brightness Controller"));
  Serial.println(F("TEMPORARY VERSION - No ML Model"));
  Serial.println(F("===================================\n"));
  
  // Initialize pins
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
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
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
  
  // Display current time
  DateTime now = rtc.now();
  Serial.print(F("Current time: "));
  printDateTime(now);
  Serial.println();
  
  Serial.println(F("\nUsing PLACEHOLDER prediction logic"));
  Serial.println(F("Train your model and generate tree_rules.h for real ML!"));
  
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
    
    // Get ML prediction (currently using placeholder)
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
    
    // Output to serial
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
  // ESP32: 12-bit ADC (0-4095), 3.3V reference
  int raw = analogRead(LDR_PIN);
  float voltage = raw * (3.3 / 4095.0);
  
  // Convert to approximate lux (calibrate for your LDR)
  float lux = voltage * 300.0;  // Adjust multiplier
  
  return lux;
}

int readManualOffset() {
  // ESP32: 12-bit ADC (0-4095)
  int pot_value = analogRead(POT_PIN);
  
  // Center position for 12-bit ADC
  int center = 2048;
  
  // Apply deadzone
  if (pot_value >= center - POT_DEADZONE && pot_value <= center + POT_DEADZONE) {
    return 0;
  }
  
  // Map to offset range (-100 to +100)
  int offset;
  if (pot_value < center - POT_DEADZONE) {
    offset = map(pot_value, 0, center - POT_DEADZONE, -100, 0);
  } else {
    offset = map(pot_value, center + POT_DEADZONE, 4095, 0, 100);
  }
  
  return offset;
}

int getTimePeriod(int hour) {
  // 0 = Early Morning (4-6)
  // 1 = Morning (6-12)
  // 2 = Afternoon (12-16)
  // 3 = Evening (16-20)
  // 4 = Night (20-4)
  
  if (hour >= 4 && hour <= 6) {
    return 0;
  } else if (hour > 6 && hour <= 12) {
    return 1;
  } else if (hour > 12 && hour <= 16) {
    return 2;
  } else if (hour > 16 && hour <= 20) {
    return 3;
  } else {
    return 4;
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
    DateTime now = rtc.now();
    Serial.print(F("Current time: "));
    printDateTime(now);
    Serial.println();
  } else if (cmd.startsWith("settime ")) {
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
      Serial.println(F("Error: Invalid format. Use: settime HH:MM:SS"));
    }
  } else if (cmd == "help") {
    Serial.println(F("\n=== Commands ==="));
    Serial.println(F("stats            - Show statistics"));
    Serial.println(F("time             - Show current time"));
    Serial.println(F("settime HH:MM:SS - Set time (24-hour)"));
    Serial.println(F("help             - Show this help"));
    Serial.println();
  } else if (cmd.length() > 0) {
    Serial.print(F("Unknown: "));
    Serial.println(cmd);
    Serial.println(F("Type 'help'"));
  }
}
