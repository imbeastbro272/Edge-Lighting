# LED Brightness Controller - Enhanced Implementation Guide

## Overview

This enhanced version of the LED brightness controller includes several advanced features:

1. **Day of Week Consideration**: The model now considers which day of the week it is for better predictions
2. **Enhanced Night Scenario Handling**: Special logic for night + high ambient light scenarios
3. **Online Learning**: The system learns from user preferences in real-time
4. **EEPROM Persistence**: Learning data persists across ESP32 reboots

## New Features Explained

### 1. Day of Week Consideration

The model now includes `day_of_week` as a feature (0=Monday, 6=Sunday). This allows the system to learn different lighting preferences for weekdays vs. weekends.

**Training**: The dataset can include a `day_of_week` column. If not present, random values will be assigned during training.

**Prediction**: The ESP32 RTC provides the day of week automatically.

### 2. Night + High Ambient Light Scenario

**Problem**: At night with high ambient light (e.g., street lights, moonlight), what should the LED brightness be?

**Solution**: 
- **WITH Motion Detection**: LED brightness changes to 20-50% (moderate illumination)
- **WITHOUT Motion Detection**: LED brightness stays minimal (0-10%) or turns off

This is implemented through:
- Data augmentation in `led_brightness_model.py` (adds synthetic training samples)
- The trained model learns this pattern
- ESP32 applies the learned model

### 3. Online Learning

The ESP32 monitors user's manual override behavior and adapts the model in real-time.

**How It Works**:

1. **Detection**: When the user adjusts the potentiometer significantly (±10% or more)
2. **Confirmation**: The system waits 5 seconds to confirm the adjustment is intentional
3. **Learning**: The system records:
   - Current sensor readings (ambient light, motion, time, day)
   - User's preferred brightness
4. **Application**: Future predictions in similar conditions are adjusted based on learned preferences
5. **Persistence**: Learning data is saved to ESP32 EEPROM every 5 learning events

**Algorithm**: k-Nearest Neighbors approach
- Computes similarity between current state and past learning records
- Applies weighted average adjustment based on similar past scenarios
- Learning rate: 0.1 (conservative, prevents overfitting to single instances)

### 4. EEPROM Persistence

Learning data survives power cycles:
- Header: Magic number, version, sample count, checksum
- Data: Up to 100 learning records (configurable)
- Automatic saving every 5 learning events
- Manual save via serial command: `savelearning`

## Setup Instructions

### Step 1: Prepare Dataset

Your dataset should have these columns:
```csv
timestamp,ambient_light,motion_detected,sin_hour,cos_hour,time_period,day_of_week,led_brightness
2024-01-01 00:30:00,15.5,0,-0.130526,0.991445,4,0,85.2
```

**Note**: If `day_of_week` column is missing, the training script will add random values.

### Step 2: Train the Model

```bash
python train_model.py --data your_dataset.csv --output led_model.pkl
```

This will:
- Load and validate your dataset
- Add synthetic data for night + high ambient light scenarios
- Train the decision tree with day_of_week feature
- Test predictions on various scenarios
- Save the model to `led_model.pkl`

### Step 3: Export to Arduino

```bash
python export_tree_rules.py --model led_model.pkl --output tree_rules.h --test-output test_cases.h
```

This generates:
- `tree_rules.h`: Decision tree implementation with day_of_week parameter
- `test_cases.h`: Validation test cases

### Step 4: Upload to ESP32

1. Copy files to your Arduino sketch folder:
   - `led_controller_online_learning.ino` (main sketch)
   - `tree_rules.h` (generated ML model)
   - `test_cases.h` (validation tests)

2. Install required libraries:
   - RTClib (for DS3231)
   - Wire (for I2C, built-in)
   - EEPROM (for persistence, built-in)

3. Configure pin definitions in the sketch if needed:
   ```cpp
   #define PIR_PIN 27
   #define LDR_PIN 34
   #define POT_PIN 33
   #define LED_PIN 2
   ```

4. Upload to ESP32

## Hardware Connections

| Component | ESP32 Pin | Notes |
|-----------|-----------|-------|
| DS3231 SDA | GPIO 21 | I2C Data |
| DS3231 SCL | GPIO 22 | I2C Clock |
| PIR Sensor | GPIO 27 | Digital input |
| LDR | GPIO 34 | Analog input (ADC) |
| Potentiometer | GPIO 33 | Analog input (ADC) |
| LED Strip | GPIO 2 | PWM output |

## Serial Commands

The ESP32 supports these commands via Serial Monitor:

| Command | Description |
|---------|-------------|
| `help` | Show all available commands |
| `stats` | Show prediction and learning statistics |
| `time` | Show current RTC time |
| `settime HH:MM:SS` | Set time manually (24-hour format) |
| `learning` | Show detailed learning data records |
| `savelearning` | Manually save learning data to EEPROM |
| `clearlearning` | Clear all learning data (reset) |

## Understanding the Serial Output

```
┌──────────┬─────┬──────┬─────────┬────────┬────────┬──────┬────────┬────────┬─────┬──────┐
│   Time   │ Day │ Hour │ Ambient │ Motion │ Period │  ML% │ Offset │ Final% │ PWM │ Learn│
├──────────┼─────┼──────┼─────────┼────────┼────────┼──────┼────────┼────────┼─────┼──────┤
│ 14:30:45 │ Mon │  14  │  450.3  │   1    │   2    │ 42.5 │  +15   │  57.5  │ 147 │      │
```

- **Time**: Current time from RTC
- **Day**: Day of week (Mon-Sun)
- **Hour**: Hour of day (0-23)
- **Ambient**: Ambient light level in lux
- **Motion**: Motion detected (0=no, 1=yes)
- **Period**: Time period code (0-4)
- **ML%**: ML model prediction (before manual offset)
- **Offset**: Manual offset from potentiometer
- **Final%**: Final brightness (ML + Offset + Learning)
- **PWM**: PWM value sent to LED (0-255)
- **Learn**: 'L' indicates learning is active

## Online Learning Indicators

When the system is learning from your preferences:

1. **'L' appears in the Learn column** - Override is being monitored
2. **Console message appears** - Learning event recorded
   ```
   >>> LEARNING EVENT RECORDED <<<
   ML predicted: 42.5%, User adjusted to: 57.5%
   ```

## Configuration Parameters

In `led_controller_online_learning.ino`:

```cpp
// Online Learning Configuration
#define LEARNING_ENABLED true          // Enable/disable online learning
#define LEARNING_RATE 0.1              // Learning rate (0-1)
#define OVERRIDE_THRESHOLD 10          // Min offset to trigger learning (%)
#define LEARNING_WINDOW 5000           // Confirmation time (ms)
#define MAX_LEARNING_SAMPLES 100       // Max EEPROM storage
```

**Tuning Tips**:
- **LEARNING_RATE**: Lower = more conservative, higher = faster adaptation
- **OVERRIDE_THRESHOLD**: Higher = only learn from significant overrides
- **LEARNING_WINDOW**: Longer = more certain the override is intentional

## Monitoring and Debugging

### Check Learning Statistics

```
> stats

=== Statistics ===
Predictions: 1523
Average Brightness: 54.32%
Current Offset: +12%
Learning Events: 7
Learning Samples: 7
```

### View Learning Records

```
> learning

=== Learning Data ===
Total samples: 7

Recent learning records:
  [0] Ambient=450.3, Motion=1, Period=2, Day=0 → User pref=57.5%
  [1] Ambient=120.5, Motion=1, Period=4, Day=1 → User pref=78.2%
  ...
```

### Clear Learning Data

```
> clearlearning

Clearing all learning data...
Learning data cleared.
```

## Troubleshooting

### Learning Not Working

1. **Check LEARNING_ENABLED** is true
2. **Verify override threshold**: Move potentiometer at least ±10%
3. **Wait for confirmation**: Hold override for 5+ seconds
4. **Check serial output**: Look for "LEARNING EVENT RECORDED"

### Model Predictions Seem Wrong

1. **Run validation**: Type `test` in serial monitor
2. **Check sensor readings**: Verify ambient light and motion values are reasonable
3. **Retrain model**: May need more diverse training data

### EEPROM Not Persisting

1. **Check EEPROM.commit()** is being called
2. **Verify EEPROM_SIZE** (4096 bytes on ESP32)
3. **Manual save**: Use `savelearning` command before power-off

## Advanced: Customizing the Night Scenario Logic

Edit `led_brightness_model.py`, function `augment_data()`:

```python
# CRITICAL LOGIC: Night + High Ambient Light
if motion == 1:
    # Motion detected: provide moderate brightness
    led_brightness = np.random.uniform(20, 50)  # Adjust range here
else:
    # No motion: reduce to minimum or stay minimal
    led_brightness = np.random.uniform(0, 10)   # Adjust range here
```

After editing, retrain the model and re-export to Arduino.

## Performance Characteristics

- **Prediction Time**: < 1ms (decision tree traversal)
- **Memory Usage**: 
  - Program: ~15-20 KB (depends on tree size)
  - RAM: ~8 KB (learning buffer + variables)
  - EEPROM: ~4 KB (learning persistence)
- **Learning Capacity**: 10 records in RAM, 100 in EEPROM
- **Update Rate**: 1 Hz (configurable via UPDATE_INTERVAL)

## Future Enhancements

Potential improvements:
1. **Adaptive learning rate** based on prediction confidence
2. **Anomaly detection** to ignore accidental overrides
3. **Cloud sync** of learning data across multiple devices
4. **Seasonal adjustments** (daylight savings, sunrise/sunset times)
5. **Energy monitoring** and optimization

## File Reference

| File | Purpose |
|------|---------|
| `led_brightness_model.py` | Model class with day_of_week and online learning support |
| `train_model.py` | Training script with data augmentation |
| `export_tree_rules.py` | Export model to Arduino with day_of_week |
| `led_controller_online_learning.ino` | ESP32 sketch with online learning |
| `tree_rules.h` | Generated decision tree (from export) |
| `test_cases.h` | Generated validation tests (from export) |

## Support

For issues or questions:
1. Check serial monitor output for error messages
2. Verify hardware connections
3. Validate model with `test` command
4. Review learning data with `learning` command
5. Clear and retrain if behavior is unexpected

---

**Version**: 2.0 (Online Learning)  
**Last Updated**: 2026-05-25
