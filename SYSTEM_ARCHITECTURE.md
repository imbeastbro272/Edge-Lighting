# LED Brightness Controller - System Architecture

## Overview Diagram

```
┌──────────────────────────────────────────────────────────────────┐
│                         ESP32 System                             │
│                                                                  │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐  ┌──────────┐ │
│  │   RTC      │  │   PIR      │  │    LDR     │  │   POT    │ │
│  │  (Time)    │  │  (Motion)  │  │  (Light)   │  │(Override)│ │
│  └─────┬──────┘  └─────┬──────┘  └─────┬──────┘  └─────┬────┘ │
│        │               │               │               │        │
│        └───────────────┴───────────────┴───────────────┘        │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │  Feature Extractor │                       │
│                    │ - Hour, Day of Week│                       │
│                    │ - Sin/Cos encoding │                       │
│                    │ - Time Period      │                       │
│                    └─────────┬─────────┘                        │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │   ML Decision Tree │                       │
│                    │   Prediction (%)   │                       │
│                    └─────────┬─────────┘                        │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │  Learning System   │                       │
│                    │  (k-NN Adjustment) │                       │
│                    └─────────┬─────────┘                        │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │  Manual Override   │                       │
│                    │   (Potentiometer)  │                       │
│                    └─────────┬─────────┘                        │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │  Final Brightness  │                       │
│                    │    Smoothing       │                       │
│                    └─────────┬─────────┘                        │
│                              │                                   │
│                    ┌─────────▼─────────┐                        │
│                    │   PWM to LED       │                       │
│                    └───────────────────┘                        │
│                                                                  │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │              Learning & Persistence                      │  │
│  │  ┌─────────────────┐         ┌──────────────────┐      │  │
│  │  │ Learning Buffer │ ◄─────► │  EEPROM Storage  │      │  │
│  │  │  (10 records)   │         │  (100 records)   │      │  │
│  │  └─────────────────┘         └──────────────────┘      │  │
│  └──────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────────┘
```

## Data Flow

### 1. Normal Prediction Flow

```
Sensors → Feature Extraction → ML Model → Learning Adjustment → Manual Override → LED
```

**Step by step**:
1. **Sensors**: Read ambient light, motion, time, day
2. **Feature Extraction**: Convert to model features (sin/cos hour, time period, etc.)
3. **ML Model**: Decision tree predicts base brightness
4. **Learning Adjustment**: Apply learned preferences from similar past scenarios
5. **Manual Override**: Add potentiometer offset
6. **Smoothing**: Apply exponential moving average
7. **LED**: Convert to PWM (0-255) and output

### 2. Learning Flow

```
Manual Override Detected → Wait 5s → Record → Update Buffer → Save to EEPROM
                            ↓
                      Apply k-NN Adjustment to Future Predictions
```

**Step by step**:
1. **Detection**: User adjusts pot ±10% or more
2. **Confirmation**: Wait 5 seconds (override sustained?)
3. **Record**: Capture current state + user preference
4. **Buffer Update**: Add to ring buffer (10 records max)
5. **EEPROM Save**: Persist every 5 learning events
6. **Future Predictions**: Use k-NN to adjust similar scenarios

## Night + High Ambient Light Logic

### Training Phase (Python)

```python
if time_period == 4 (Night) AND ambient_light > 75th_percentile:
    if motion_detected == 1:
        led_brightness = random(20, 50)  # Moderate brightness
    else:
        led_brightness = random(0, 10)   # Minimal brightness
```

**Result**: Model learns the pattern through training data

### Inference Phase (ESP32)

```cpp
// No special logic needed!
// The trained decision tree naturally follows the learned pattern
float ml_brightness = predict_brightness(ambient, motion, sin_hour, cos_hour, period, day);
```

## Feature Engineering

### Temporal Features

```
Hour (0-23) → Sin/Cos Encoding → Circular representation
              sin(2π*hour/24)
              cos(2π*hour/24)

Day (0-6)   → Direct encoding → 0=Monday, 6=Sunday

Hour Range  → Time Period    → 0: Early Morning (4-6)
                                1: Morning (6-12)
                                2: Afternoon (12-16)
                                3: Evening (16-20)
                                4: Night (20-4)
```

### Sensor Features

```
LDR Reading    → Ambient Light (lux)  → 0-100,000
PIR Reading    → Motion Detected      → 0 or 1
Potentiometer  → Manual Offset        → -100 to +100
```

## Learning Algorithm: k-Nearest Neighbors

### Distance Calculation

```cpp
For each learning record:
  distance = sqrt(
    (ambient - record.ambient)² / 1000² +
    (motion - record.motion)² +
    (sin_hour - record.sin_hour)² +
    (cos_hour - record.cos_hour)² +
    (period - record.period)² / 4² +
    (day - record.day)² / 6²
  )
```

### Weighted Adjustment

```cpp
For each learning record:
  weight = 1 / distance
  adjustment = (record.user_preference - ml_prediction) * weight

weighted_avg = sum(adjustments) / sum(weights)
final_adjustment = weighted_avg * LEARNING_RATE

final_prediction = ml_prediction + final_adjustment
```

## Memory Layout

### RAM (ESP32)

```
┌─────────────────────────────────────────────┐
│ Program Variables           ~2 KB           │
├─────────────────────────────────────────────┤
│ Learning Buffer (10 records) ~2 KB          │
├─────────────────────────────────────────────┤
│ ML Model (tree structure)   ~3 KB           │
├─────────────────────────────────────────────┤
│ Stack                       ~1 KB           │
├─────────────────────────────────────────────┤
│ Free                        ~512 KB         │
└─────────────────────────────────────────────┘
Total RAM: 520 KB (ESP32)
Used: ~8 KB
```

### EEPROM (ESP32)

```
┌─────────────────────────────────────────────┐
│ Address 0x0000                              │
├─────────────────────────────────────────────┤
│ Header (8 bytes)                            │
│ - Magic: 0xA5B3                             │
│ - Version: 1                                │
│ - Sample Count: 0-100                       │
│ - Checksum: uint32                          │
├─────────────────────────────────────────────┤
│ Learning Record 1 (40 bytes)                │
├─────────────────────────────────────────────┤
│ Learning Record 2 (40 bytes)                │
├─────────────────────────────────────────────┤
│ ...                                         │
├─────────────────────────────────────────────┤
│ Learning Record 100 (40 bytes)              │
├─────────────────────────────────────────────┤
│ Unused                                      │
└─────────────────────────────────────────────┘
Total: 4096 bytes
Used: 8 + (100 × 40) = 4008 bytes
```

## Decision Tree Structure

### Example Tree Node

```
if ambient_light <= 450.5:
    if motion_detected <= 0.5:
        if time_period <= 3.5:
            return 25.3  # Leaf: 25.3% brightness
        else:
            return 78.6  # Leaf: 78.6% brightness
    else:
        if day_of_week <= 4.5:
            return 42.1  # Weekday
        else:
            return 38.7  # Weekend
else:
    ...
```

### Tree Characteristics

- **Depth**: ~10 levels (configurable)
- **Leaves**: ~50-100 (depends on training)
- **Comparisons per prediction**: Max 10 (worst case)
- **Prediction time**: < 1ms

## Serial Communication Protocol

### Output Format

```
┌──────────┬─────┬──────┬─────────┬────────┬────────┬──────┬────────┬────────┬─────┬──────┐
│   Time   │ Day │ Hour │ Ambient │ Motion │ Period │  ML% │ Offset │ Final% │ PWM │ Learn│
├──────────┼─────┼──────┼─────────┼────────┼────────┼──────┼────────┼────────┼─────┼──────┤
│ 14:30:45 │ Mon │  14  │  450.3  │   1    │   2    │ 42.5 │  +15   │  57.5  │ 147 │      │
│ 22:15:30 │ Tue │  22  │  850.0  │   0    │   4    │  8.2 │   +0   │   8.2  │  21 │      │
│ 22:15:35 │ Tue │  22  │  850.0  │   1    │   4    │ 35.8 │   +0   │  35.8  │  91 │      │
└──────────┴─────┴──────┴─────────┴────────┴────────┴──────┴────────┴────────┴─────┴──────┘
                                                                                      ↑
                                                                                      'L' when learning
```

### Commands

```
┌──────────────────┬────────────────────────────────────┐
│ Command          │ Response                           │
├──────────────────┼────────────────────────────────────┤
│ stats            │ Show prediction & learning stats   │
│ time             │ Show current RTC time              │
│ learning         │ Show learning data records         │
│ savelearning     │ Manually save to EEPROM            │
│ clearlearning    │ Clear all learning data            │
│ settime HH:MM:SS │ Set RTC time                       │
│ help             │ Show command list                  │
└──────────────────┴────────────────────────────────────┘
```

## Training Pipeline

```
┌─────────────────┐
│  Raw Dataset    │ (CSV with ambient_light, motion, hour, brightness)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Data Validation │ (Check ranges, missing values)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Add day_of_week│ (If missing, add random values)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Augment Data   │ (Add night + high ambient synthetic samples)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Train Model    │ (DecisionTreeRegressor with cross-validation)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Export to C/C++ │ (tree_rules.h + test_cases.h)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│  Upload ESP32   │ (Arduino IDE)
└─────────────────┘
```

## Deployment Architecture

```
┌──────────────────────────────────────────────────────┐
│                   Development PC                      │
│                                                       │
│  ┌──────────────┐    ┌──────────────┐               │
│  │ Python       │    │ Arduino IDE  │               │
│  │ Training     │───►│ Compilation  │               │
│  │ Environment  │    │              │               │
│  └──────────────┘    └──────┬───────┘               │
│                             │                        │
└─────────────────────────────┼────────────────────────┘
                              │ USB Upload
                              ▼
                    ┌──────────────────┐
                    │      ESP32       │
                    │                  │
                    │  ┌────────────┐  │
                    │  │ ML Model   │  │
                    │  └────────────┘  │
                    │  ┌────────────┐  │
                    │  │ Learning   │  │
                    │  │ System     │  │
                    │  └────────────┘  │
                    │  ┌────────────┐  │
                    │  │ EEPROM     │  │
                    │  └────────────┘  │
                    └────────┬─────────┘
                             │
                    ┌────────▼─────────┐
                    │   LED Hardware   │
                    └──────────────────┘
```

## Performance Metrics

### Timing

```
┌─────────────────────────┬──────────────┐
│ Operation               │ Time         │
├─────────────────────────┼──────────────┤
│ Sensor Reading          │ ~50ms        │
│ Feature Extraction      │ <1ms         │
│ ML Prediction           │ <1ms         │
│ Learning Adjustment     │ ~2ms         │
│ Manual Override Reading │ ~50ms        │
│ PWM Output              │ <1ms         │
│ Total Loop Time         │ ~100-150ms   │
└─────────────────────────┴──────────────┘
Update Rate: ~7-10 Hz
```

### Accuracy

```
┌─────────────────────────┬──────────────┐
│ Metric                  │ Value        │
├─────────────────────────┼──────────────┤
│ Test MAE                │ 3-5%         │
│ Test R²                 │ 0.92-0.97    │
│ Cross-Val MAE           │ 4-6%         │
│ Learning Adjustment     │ ±10-20%      │
└─────────────────────────┴──────────────┘
```

## State Machine

```
┌─────────────────────────────────────────────────────┐
│                   SYSTEM STATES                      │
└─────────────────────────────────────────────────────┘

        ┌──────────────┐
        │  STARTUP     │
        └──────┬───────┘
               │ Initialize
               ▼
        ┌──────────────┐
    ┌──►│  NORMAL      │◄──┐
    │   │  OPERATION   │   │
    │   └──────┬───────┘   │
    │          │            │
    │          │ Override   │ Timeout
    │          │ Detected   │ or Released
    │          ▼            │
    │   ┌──────────────┐   │
    │   │  LEARNING    │───┘
    │   │  MODE        │
    │   └──────┬───────┘
    │          │ Confirmed
    │          │ (5 seconds)
    │          ▼
    │   ┌──────────────┐
    │   │  RECORD      │
    │   │  LEARNING    │
    │   └──────┬───────┘
    │          │
    └──────────┘
```

## Error Handling

```
┌─────────────────────────┬──────────────────────────────┐
│ Error                   │ Recovery                     │
├─────────────────────────┼──────────────────────────────┤
│ RTC Not Found           │ Halt with error message      │
│ RTC Lost Power          │ Set to compile time          │
│ Sensor Read Failure     │ Use last valid reading       │
│ EEPROM Corrupt          │ Clear and restart learning   │
│ Learning Buffer Full    │ Overwrite oldest (ring)      │
│ Invalid Time Command    │ Show error, continue         │
└─────────────────────────┴──────────────────────────────┘
```

---

**This architecture supports**:
- Real-time ML inference on ESP32
- Continuous learning from user behavior
- Persistent storage across power cycles
- Extensible feature engineering
- Efficient memory usage
- Robust error handling
