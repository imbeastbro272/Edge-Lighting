# LED Brightness Controller - Version 2.0 Changelog

## Major Changes from Previous Version

### 1. Day of Week Feature ✅

**What Changed**:
- Added `day_of_week` (0=Monday, 6=Sunday) as a new feature in the model
- Model can now learn different preferences for weekdays vs weekends

**Files Modified**:
- `led_brightness_model.py`: Added `day_of_week` to feature columns
- `train_model.py`: Auto-generates day_of_week if not in dataset
- `export_tree_rules.py`: Updated function signatures to include day_of_week
- `led_controller_online_learning.ino`: Reads day_of_week from RTC

**Impact**:
- Better predictions based on weekly patterns
- User might prefer brighter lights on weekends, dimmer during work week
- Model learns temporal patterns beyond just hour of day

### 2. Night + High Ambient Light Scenario Logic ✅

**Problem Addressed**:
Your requirement: "At night with high ambient light, LED brightness should only change IF motion is detected. Without motion, LED should stay at same level or reduce to minimum."

**Implementation**:

**In Training** (`led_brightness_model.py`):
```python
# Night (period=4) + High Ambient Light scenarios:
if motion == 1:
    # Motion detected: LED changes brightness (20-50%)
    led_brightness = np.random.uniform(20, 50)
else:
    # No motion: LED stays minimal (0-10%)
    led_brightness = np.random.uniform(0, 10)
```

**Synthetic Data Augmentation**:
- Generates 15% synthetic samples for night + high ambient scenarios
- 50/50 split between motion/no-motion cases
- High ambient = above 75th percentile of dataset

**Result**: Model learns the rule through training data, no hardcoded logic needed in Arduino.

### 3. Online Learning from User Preferences ✅

**What It Does**:
The ESP32 monitors when users manually adjust brightness and learns their preferences in real-time.

**Learning Process**:

1. **Detection**: User adjusts potentiometer ±10% or more
2. **Confirmation**: System waits 5 seconds to confirm it's intentional
3. **Recording**: Stores current conditions + user's preferred brightness
4. **Application**: Uses k-NN to adjust future predictions in similar scenarios
5. **Persistence**: Saves to EEPROM every 5 learning events

**Algorithm Details**:
```cpp
// k-Nearest Neighbors approach
for each learning record:
  1. Compute distance (similarity) to current state
  2. Weight = 1 / distance
  3. Calculate adjustment = user_preference - ml_prediction
  4. Apply weighted average with learning_rate = 0.1

final_prediction = ml_prediction + weighted_adjustments
```

**User Experience**:
- Completely transparent - works in background
- 'L' indicator in serial output when learning is active
- Console message confirms each learning event
- Data persists across power cycles

### 4. EEPROM Persistence ✅

**What's Stored**:
- Header: Magic number (0xA5B3), version, sample count, checksum
- Up to 100 learning records (configurable)
- Each record: ambient_light, motion, time features, day, user preference

**Storage Format**:
```cpp
struct EEPROMHeader {
  uint16_t magic;           // Validation
  uint8_t version;          // Version tracking
  uint8_t samples_count;    // Number of records
  uint32_t checksum;        // Data integrity
};

struct LearningRecord {
  float ambient_light;
  int motion_detected;
  float sin_hour, cos_hour;
  int time_period;
  int day_of_week;
  float user_preferred_brightness;
  unsigned long timestamp;
  bool valid;
};
```

**Auto-Save**: Every 5 learning events  
**Manual Save**: `savelearning` command  
**Clear**: `clearlearning` command

## New Files

1. **`led_controller_online_learning.ino`** - Enhanced ESP32 sketch
   - Includes all features from version 1
   - Adds online learning capabilities
   - EEPROM persistence
   - Extended serial commands

2. **`IMPLEMENTATION_GUIDE.md`** - Comprehensive documentation
   - Setup instructions
   - Feature explanations
   - Configuration parameters
   - Troubleshooting guide

3. **`CHANGELOG_V2.md`** - This file
   - What changed and why
   - Migration guide
   - Breaking changes

## Modified Files

### `led_brightness_model.py`

**Changes**:
1. Added `day_of_week` to feature columns
2. Enhanced `augment_data()` with motion-dependent night logic
3. Updated `predict()` to accept `day_of_week` parameter
4. Updated `calculate_time_features()` to return day_of_week

**Backward Compatibility**: None - requires retraining

### `train_model.py`

**Changes**:
1. Added check for `day_of_week` column
2. Auto-generates random day_of_week if missing
3. Enhanced test scenarios with day_of_week
4. Added two specific test cases for night + high ambient (motion vs no motion)

**Backward Compatibility**: Works with old datasets (adds day_of_week automatically)

### `export_tree_rules.py`

**Changes**:
1. Updated `tree_to_code()` function signature to include day_of_week
2. Updated function documentation
3. Added day_of_week to test case structure
4. Enhanced validation output with day names

**Backward Compatibility**: None - requires model retrained with day_of_week

## Migration from Version 1 to Version 2

### If You Have Existing Dataset:

**Option 1: Add day_of_week column manually**
```python
import pandas as pd
df = pd.read_csv('your_dataset.csv')
df['timestamp'] = pd.to_datetime(df['timestamp'])
df['day_of_week'] = (df['timestamp'].dt.dayofweek + 1) % 7  # 0=Mon, 6=Sun
df.to_csv('dataset_with_dow.csv', index=False)
```

**Option 2: Let training script add it** (uses random values)
```bash
python train_model.py --data your_dataset.csv --output led_model.pkl
```

### Retrain Model:

```bash
# Step 1: Train with new features
python train_model.py --data your_dataset.csv --output led_model_v2.pkl

# Step 2: Export to Arduino
python export_tree_rules.py --model led_model_v2.pkl --output tree_rules.h

# Step 3: Upload new sketch
# Use led_controller_online_learning.ino
```

### Hardware:

No hardware changes required! Same pin configuration as version 1.

## Breaking Changes

1. **Model Format**: Old `.pkl` files are incompatible (different feature count)
2. **Arduino Function Signature**: `predict_brightness()` now requires day_of_week parameter
3. **Training Data**: Requires retraining even with same dataset

## Configuration Options

### In Arduino Sketch:

```cpp
// Enable/disable online learning
#define LEARNING_ENABLED true

// How fast the system learns (0-1, lower = more conservative)
#define LEARNING_RATE 0.1

// Minimum adjustment to trigger learning (%)
#define OVERRIDE_THRESHOLD 10

// How long to wait before confirming override (ms)
#define LEARNING_WINDOW 5000

// Maximum samples stored in EEPROM
#define MAX_LEARNING_SAMPLES 100
```

### In Python Training:

```bash
# Control tree depth (affects memory usage)
python train_model.py --data dataset.csv --max-depth 10

# Control minimum samples per split (affects overfitting)
python train_model.py --data dataset.csv --min-samples-split 10
```

## Performance Impact

| Metric | Version 1 | Version 2 | Change |
|--------|-----------|-----------|--------|
| Features | 5 | 6 | +1 (day_of_week) |
| Tree Depth | ~8 | ~10 | +2 |
| Prediction Time | <1ms | <1ms | No change |
| RAM Usage | ~2KB | ~8KB | +6KB (learning buffer) |
| EEPROM Usage | 0 | ~4KB | +4KB (persistence) |
| Program Size | ~15KB | ~20KB | +5KB (learning code) |

**Note**: All values are approximate and depend on dataset and tree parameters.

## Testing Recommendations

### After Upgrading:

1. **Validate Model**:
   ```
   > test
   ```
   Should show "ALL TESTS PASSED"

2. **Monitor Initial Predictions**:
   Watch serial output for reasonable brightness values

3. **Test Learning**:
   - Adjust potentiometer significantly (+20%)
   - Hold for 5+ seconds
   - Look for "LEARNING EVENT RECORDED" message

4. **Test Persistence**:
   ```
   > savelearning
   > (power cycle ESP32)
   > learning
   ```
   Should show saved learning records

### Night + High Ambient Test:

**Setup**:
- Time: 10 PM (night period)
- Shine bright light on LDR (simulate high ambient)

**Test 1**: No motion
- Expected: LED brightness should be minimal (0-10%)

**Test 2**: Trigger PIR sensor
- Expected: LED brightness should increase (20-50%)

## Known Issues and Limitations

1. **Learning Buffer Size**: Only 10 records in RAM, older records overwritten
   - Workaround: Increase buffer size in code (uses more RAM)

2. **EEPROM Wear**: ESP32 EEPROM has ~100k write cycles
   - Mitigation: Saves every 5 learning events, not every event

3. **Day of Week Requires RTC**: Must have DS3231 with correct date set
   - No fallback to NTP or compile-time date

4. **Learning Rate Fixed**: LEARNING_RATE is constant
   - Future: Could be adaptive based on prediction confidence

## Advantages of New Version

### For Users:
- ✅ System learns your preferences automatically
- ✅ Better predictions for weekday/weekend patterns
- ✅ Smarter night-time behavior with ambient light
- ✅ Preferences persist across power loss

### For Developers:
- ✅ More features = better model accuracy
- ✅ Online learning = continuous improvement
- ✅ EEPROM = no cloud dependency
- ✅ k-NN approach = simple and effective

## Future Roadmap

### Version 2.1 (Planned):
- Adaptive learning rate
- Anomaly detection for accidental overrides
- Statistics dashboard command

### Version 3.0 (Conceptual):
- WiFi sync of learning data
- Mobile app for configuration
- Seasonal adjustments
- Energy usage tracking

## Rollback to Version 1

If you need to revert:

1. Use original `led_controller.ino` (without online learning)
2. Retrain model without day_of_week:
   ```python
   # In led_brightness_model.py, change:
   self.feature_columns = ['ambient_light', 'motion_detected', 'sin_hour', 'cos_hour', 'time_period']
   ```
3. Re-export to Arduino

## Support and Feedback

If you encounter issues:
1. Check serial monitor for error messages
2. Use `stats` command to see system state
3. Use `learning` command to inspect learning data
4. Clear learning with `clearlearning` if behavior is wrong
5. Retrain model with more diverse data

---

**Version**: 2.0  
**Release Date**: 2026-05-25  
**Author**: Enhanced with Online Learning and Day-of-Week Features  
**Compatibility**: ESP32 (requires RTC + EEPROM)
