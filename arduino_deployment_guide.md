# Arduino/ESP32 Deployment Guide

## Overview
This guide explains how to deploy the trained Decision Tree model to a microcontroller (Arduino/ESP32) for real-time LED brightness control.

## Deployment Strategy

Since microcontrollers have limited resources, we'll use **model conversion** to extract the decision tree rules and implement them in C/C++.

### Step 1: Extract Decision Tree Rules

After training your model in Python, run the `export_tree_rules.py` script to extract the decision tree as C code:

```python
python export_tree_rules.py --model led_model.pkl --output tree_rules.h
```

This will generate a header file with the decision tree logic.

### Step 2: Hardware Setup

**Required Components:**
- Arduino Uno/ESP32/ESP8266
- RTC Module (DS3231 or similar)
- PIR Motion Sensor (HC-SR501)
- LDR (Light Dependent Resistor) with voltage divider
- Potentiometer (10kΩ) for manual override
- LED strip or PWM-capable LED
- Resistors and connecting wires

**Pin Connections (Example for Arduino Uno):**
```
RTC Module:
  - SDA → A4
  - SCL → A5
  - VCC → 5V
  - GND → GND

PIR Sensor:
  - OUT → D2
  - VCC → 5V
  - GND → GND

LDR Circuit:
  - LDR + 10kΩ resistor voltage divider → A0
  - VCC → 5V
  - GND → GND

Potentiometer:
  - Wiper → A1
  - VCC → 5V
  - GND → GND

LED:
  - PWM Pin (D9) → LED+ (through appropriate resistor/driver)
  - GND → LED-
```

### Step 3: Arduino Code Structure

```cpp
#include <Wire.h>
#include <RTClib.h>
#include <math.h>
#include "tree_rules.h"  // Generated decision tree

// Hardware pins
#define PIR_PIN 2
#define LDR_PIN A0
#define POT_PIN A1
#define LED_PIN 9

// RTC
RTC_DS3231 rtc;

// Global variables
int manual_offset = 0;  // -100 to +100

void setup() {
  Serial.begin(9600);
  
  // Initialize pins
  pinMode(PIR_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  
  // Initialize RTC
  if (!rtc.begin()) {
    Serial.println("RTC not found!");
    while (1);
  }
  
  Serial.println("LED Brightness Controller Ready");
}

void loop() {
  // Read sensors
  DateTime now = rtc.now();
  int hour = now.hour();
  int minute = now.minute();
  
  float ambient_light = readLDR();
  int motion_detected = digitalRead(PIR_PIN);
  
  // Calculate time features
  float sin_hour = sin(2 * PI * hour / 24.0);
  float cos_hour = cos(2 * PI * hour / 24.0);
  int time_period = getTimePeriod(hour);
  
  // Get ML prediction from decision tree
  float ml_brightness = predict_brightness(
    ambient_light, 
    motion_detected, 
    sin_hour, 
    cos_hour, 
    time_period
  );
  
  // Read manual override from potentiometer
  manual_offset = readManualOffset();
  
  // Apply manual override
  float final_brightness = ml_brightness + manual_offset;
  final_brightness = constrain(final_brightness, 0, 100);
  
  // Convert to PWM (0-255)
  int pwm_value = map(final_brightness, 0, 100, 0, 255);
  analogWrite(LED_PIN, pwm_value);
  
  // Debug output
  Serial.print("Hour: "); Serial.print(hour);
  Serial.print(" | Ambient: "); Serial.print(ambient_light);
  Serial.print(" | Motion: "); Serial.print(motion_detected);
  Serial.print(" | ML: "); Serial.print(ml_brightness);
  Serial.print(" | Offset: "); Serial.print(manual_offset);
  Serial.print(" | Final: "); Serial.println(final_brightness);
  
  delay(1000);  // Update every second
}

float readLDR() {
  int raw = analogRead(LDR_PIN);
  // Convert to lux (calibrate based on your LDR)
  // This is a simplified conversion
  float voltage = raw * (5.0 / 1023.0);
  float lux = voltage * 200;  // Adjust multiplier for your sensor
  return lux;
}

int readManualOffset() {
  int pot_value = analogRead(POT_PIN);
  // Map potentiometer (0-1023) to offset (-100 to +100)
  // Center position (512) = 0 offset
  int offset = map(pot_value, 0, 1023, -100, 100);
  return offset;
}

int getTimePeriod(int hour) {
  if (hour >= 4 && hour <= 6) return 0;      // Early Morning
  else if (hour > 6 && hour <= 12) return 1;  // Morning
  else if (hour > 12 && hour <= 16) return 2; // Afternoon
  else if (hour > 16 && hour <= 20) return 3; // Evening
  else return 4;                               // Night
}
```

### Step 4: Model Conversion Process

The `tree_rules.h` file will contain the decision tree logic in C format. Here's what it looks like conceptually:

```cpp
float predict_brightness(float ambient_light, int motion, float sin_hour, float cos_hour, int time_period) {
  // Decision tree rules extracted from trained model
  if (ambient_light <= 350.5) {
    if (time_period <= 3.5) {
      if (motion <= 0.5) {
        return 65.3;  // Leaf node value
      } else {
        return 78.9;
      }
    } else {
      // More conditions...
      return 82.1;
    }
  } else {
    // More branches...
    return 45.6;
  }
}
```

### Step 5: Calibration

1. **LDR Calibration:**
   - Measure actual lux values with a light meter
   - Adjust the conversion formula in `readLDR()`
   - Typical range: 0-1000 lux

2. **Potentiometer Calibration:**
   - Test the center position maps to 0 offset
   - Adjust dead zone if needed (e.g., 490-534 = 0)

3. **LED PWM Calibration:**
   - Ensure 0% = LED off, 100% = full brightness
   - May need gamma correction for perceptual linearity

### Step 6: Optimization Tips

**Memory Optimization:**
- Use `PROGMEM` for large decision tree arrays on Arduino
- Reduce tree depth if memory is limited

**Performance Optimization:**
- Update prediction every 500-1000ms (not every loop)
- Use integer math where possible
- Cache unchanged sensor values

**Power Optimization (for battery operation):**
- Use sleep modes between updates
- Reduce RTC polling frequency
- Use interrupts for PIR instead of polling

## Testing Procedure

1. **Sensor Testing:**
   - Verify LDR readings match actual light levels
   - Test PIR motion detection range
   - Check RTC time accuracy

2. **Model Testing:**
   - Compare Arduino predictions with Python model
   - Test edge cases (night + high light)
   - Verify manual override works correctly

3. **Integration Testing:**
   - Test full system over 24-hour period
   - Monitor serial output for anomalies
   - Verify LED brightness changes smoothly

## Troubleshooting

**Issue: Erratic brightness changes**
- Solution: Add smoothing/averaging to sensor readings
- Use exponential moving average: `smoothed = 0.8 * smoothed + 0.2 * new_value`

**Issue: Model predictions differ from Python**
- Solution: Check floating-point precision and rounding
- Verify sensor calibration matches training data

**Issue: Manual override not working**
- Solution: Check potentiometer wiring and ADC readings
- Verify map() function range

## Advanced Features

1. **Adaptive Learning:**
   - Store user override patterns in EEPROM
   - Adjust model bias based on frequent overrides

2. **Wireless Control:**
   - Add ESP32 WiFi/BLE for remote override
   - MQTT integration for home automation

3. **Multiple Zones:**
   - Control multiple LED strips independently
   - Different models for different rooms

## Next Steps

1. Run `export_tree_rules.py` to generate C code
2. Upload Arduino sketch to your board
3. Monitor serial output and calibrate sensors
4. Fine-tune model if needed and re-deploy
