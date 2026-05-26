# Live Retraining Implementation Guide

## Overview

The `led_controller_live_retraining.ino` implements **actual live retraining** with active brightness control on ESP32. Unlike the previous version that only printed "Retraining Started/Completed", this version:

- ✅ **Collects real training samples** when users adjust the potentiometer
- ✅ **Stores samples persistently** in EEPROM
- ✅ **Retrains the model** using gradient descent
- ✅ **Updates brightness control immediately** after retraining

---

## How Live Retraining Works

### 1. **Sample Collection**

When the user adjusts the potentiometer (manual brightness override):

```cpp
void captureTrainingSample(float ml_pred, float user_brightness) {
  TrainingSample sample;
  sample.ambient_light = smoothed_ldr;
  sample.sin_hour = sin(2.0 * PI * now.hour() / 24.0);
  sample.cos_hour = cos(2.0 * PI * now.hour() / 24.0);
  sample.motion_detected = digitalRead(PIR_PIN);
  sample.time_period = getTimePeriod(now.hour());
  sample.day_of_week = (now.dayOfTheWeek() + 6) % 7;
  sample.target_brightness = user_brightness;  // User's desired brightness
  
  saveTrainingSample(sample);  // Store in EEPROM
}
```

**When samples are captured:**
- User adjusts potentiometer beyond deadzone (±200 ADC units from center)
- Potentiometer stays stable for 5 seconds (prevents accidental adjustments)
- Adjustment is significant (>5% brightness change)

**What's stored:**
- Current sensor readings (ambient light, motion)
- Time features (sin/cos hour, time period, day of week)
- **User's desired brightness** (ML prediction + manual offset)

### 2. **Persistent Storage**

Training samples are stored in EEPROM:

```
EEPROM Layout:
├─ 0x0000: Sample count (int)
├─ 0x0064: Sample 1 (32 bytes)
├─ 0x0084: Sample 2 (32 bytes)
├─ ...
├─ 0x0CE4: Sample 100 (32 bytes)
├─ 0x0CE8: Model weights (struct, ~100 bytes)
```

- **Max samples:** 100 (circular buffer - overwrites oldest)
- **Sample size:** 32 bytes each
- **Total EEPROM usage:** ~3.5 KB of 4 KB available

### 3. **Adaptive Model Architecture**

Instead of rebuilding the complex decision tree on-device, we use a **linear adaptive model** that can be updated in real-time:

```cpp
struct ModelWeights {
  float ambient_weight;        // Weight for ambient light
  float motion_weight;         // Weight for motion detection
  float sin_hour_weight;       // Weight for sin(hour)
  float cos_hour_weight;       // Weight for cos(hour)
  float time_period_weights[5]; // Weights for each time period
  float day_of_week_weights[7]; // Weights for each day
  float bias;                  // Baseline brightness
  float learning_rate;         // Update step size
  uint32_t update_count;       // Number of retraining cycles
};
```

**Prediction formula:**
```
brightness = bias 
           + ambient_weight × ambient_light
           + motion_weight × motion_detected
           + sin_hour_weight × sin(hour)
           + cos_hour_weight × cos(hour)
           + time_period_weights[period]
           + day_of_week_weights[day]
```

### 4. **Retraining Algorithm**

Uses **mini-batch gradient descent** to update weights:

```cpp
void performRetraining() {
  for (int epoch = 0; epoch < 10; epoch++) {
    for (int i = 0; i < training_sample_count; i++) {
      // Load sample from EEPROM
      TrainingSample sample = loadTrainingSample(i);
      
      // Forward pass: make prediction
      float prediction = predictAdaptive(sample);
      
      // Calculate error
      float error = sample.target_brightness - prediction;
      
      // Backward pass: update weights
      float lr = adaptive_model.learning_rate;
      adaptive_model.ambient_weight += lr × error × sample.ambient_light;
      adaptive_model.motion_weight += lr × error × sample.motion_detected;
      // ... update all weights
      adaptive_model.bias += lr × error;
    }
  }
  
  // Save updated model to EEPROM
  saveAdaptiveModel();
}
```

**Training process:**
1. Load all samples from EEPROM
2. Run 10 epochs of gradient descent
3. Update each weight proportional to its contribution to error
4. Save updated model to EEPROM
5. **Immediately** start using the new model for predictions

### 5. **Active Brightness Control**

After retraining, the new model is used immediately:

```cpp
// In loop():
if (use_adaptive_model) {
  ml_brightness = predictAdaptive(...);  // Uses updated weights
} else {
  ml_brightness = predict_brightness(...);  // Uses original tree
}

final_brightness = ml_brightness + manual_offset;
analogWrite(LED_PIN, map(final_brightness, 0, 100, 0, 255));
```

**Brightness updates in real-time:**
- Model switches from tree to adaptive after first retraining
- All subsequent predictions use the learned weights
- User sees brightness change immediately after retraining completes

---

## Retraining Triggers

### Automatic Triggers

1. **After 20 manual adjustments:**
   - User adjusts potentiometer 20 times in a day
   - Ensures model learns from accumulated user preferences
   - Flag `retrained_at_20_changes` prevents multiple retrains

2. **Scheduled at midnight (00:00:01):**
   - Daily retraining with all samples from previous day
   - Consolidates learning from entire day
   - Flag `retrained_at_midnight` prevents duplicate runs

### Manual Trigger

Send `retrain` command via Serial:
```
retrain
```

---

## Serial Commands

| Command | Description |
|---------|-------------|
| `stats` | Show prediction stats, sample count, model type |
| `retrain` | Force immediate retraining |
| `samples` | View all stored training samples |
| `clearsamples` | Delete all training samples |
| `resetmodel` | Reset model to default weights |
| `modelinfo` | Show model details (weights, update count) |

---

## Example Output

### Sample Collection

```
📊 Captured: Ambient=235.4 ML=45.2% User=60.0%
✓ Training sample saved. Total: 12
```

### Retraining Process

```
╔════════════════════════════════════════════╗
║      LIVE RETRAINING IN PROGRESS          ║
╚════════════════════════════════════════════╝
📚 Training on 12 samples...

✓ RETRAINING COMPLETE
  Average Error: 3.2%
  Total Updates: 3
  Model: ADAPTIVE (User-Learned)

📊 Updated Model Parameters:
  Ambient Weight: -0.0187
  Motion Weight: 18.34
  Bias: 52.15
╔════════════════════════════════════════════╗
║    MODEL ACTIVE - BRIGHTNESS UPDATED       ║
╚════════════════════════════════════════════╝
```

### Monitoring Table

```
┌──────────┬─────┬──────┬─────────┬────────┬────────┬──────┬────────┬────────┬─────┐
│   Time   │ Day │ Hour │ Ambient │ Motion │ Period │  ML% │ Offset │ Final% │ PWM │
├──────────┼─────┼──────┼─────────┼────────┼────────┼──────┼────────┼────────┼─────┤
│ 22:15:03 │ Tue │ 22 │  320.5  │   1    │   4    │ 48.2 │  +12   │  60.2  │ 154 │
```

---

## Key Differences from Original Code

| Feature | Original Code | Live Retraining Code |
|---------|--------------|----------------------|
| Sample collection | ❌ None | ✅ Captured on pot adjustment |
| Storage | ❌ None | ✅ EEPROM persistent storage |
| Retraining | ❌ Fake (just prints) | ✅ Real gradient descent |
| Model updates | ❌ Static tree | ✅ Dynamic adaptive model |
| Brightness control | ❌ Tree only | ✅ Switches to learned model |
| Learning | ❌ None | ✅ Improves over time |

---

## Memory Usage

- **Program memory:** ~25 KB (fits easily on ESP32)
- **RAM:** ~2 KB (ModelWeights struct + state variables)
- **EEPROM:** ~3.5 KB (100 samples + model weights)

---

## Testing the Implementation

### 1. Initial Setup

```
1. Upload code to ESP32
2. Open Serial Monitor (9600 baud)
3. Wait for system initialization
4. Check training sample count (should be 0 initially)
```

### 2. Collect Training Samples

```
1. Observe ML prediction (ML% column)
2. Adjust potentiometer to desired brightness
3. Hold for 5+ seconds
4. Observe "Training sample saved" message
5. Repeat 5-10 times in different conditions:
   - Different times of day
   - With/without motion
   - Different ambient light levels
```

### 3. Trigger Retraining

**Option A: Manual**
```
Send command: retrain
```

**Option B: Automatic**
```
Make 20 adjustments (or wait until midnight)
```

### 4. Verify Learning

```
1. Check "Model: ADAPTIVE" in retraining output
2. Compare ML predictions before/after retraining
3. Verify brightness responds to learned preferences
4. Use 'stats' command to see update count
```

### 5. Inspect Model

```
Send command: modelinfo
Expected output:
  Type: ADAPTIVE
  Update Count: 1+
  Weights adjusted from defaults
```

---

## Troubleshooting

### Problem: No samples collected

**Solutions:**
- Ensure potentiometer adjustment > 50 ADC units
- Hold potentiometer stable for 5+ seconds
- Check `manual_offset > 5%` requirement

### Problem: Retraining has no effect

**Solutions:**
- Check `training_sample_count >= 5`
- Verify `use_adaptive_model = true` after retraining
- Inspect model weights with `modelinfo` command

### Problem: EEPROM data corrupted

**Solutions:**
- Send `clearsamples` to reset
- Send `resetmodel` to restore defaults
- Re-upload code to reinitialize EEPROM

---

## Customization

### Adjust Learning Rate

```cpp
adaptive_model.learning_rate = 0.01;  // Default
// Increase (0.05) = faster learning, may overshoot
// Decrease (0.001) = slower learning, more stable
```

### Change Sample Capacity

```cpp
#define MAX_TRAINING_SAMPLES 100  // Default
// Increase = more history, more EEPROM usage
// Decrease = less history, faster retraining
```

### Modify Retraining Frequency

```cpp
#define MAX_CHANGES_BEFORE_RETRAIN 20  // Default
// Increase = less frequent retraining
// Decrease = more frequent retraining
```

### Adjust Training Epochs

```cpp
int num_epochs = 10;  // In performRetraining()
// Increase = better convergence, slower retraining
// Decrease = faster retraining, may underfit
```

---

## Performance Characteristics

- **Sample collection:** <10 ms
- **Retraining (100 samples, 10 epochs):** ~200-500 ms
- **Prediction:** <1 ms
- **EEPROM write:** ~20 ms per sample
- **EEPROM read:** <5 ms per sample

---

## Future Enhancements

Possible improvements:

1. **Hybrid model:** Combine tree and adaptive predictions
2. **Confidence scoring:** Weight predictions by sample similarity
3. **Automatic learning rate adjustment:** Adapt based on error
4. **Sample importance weighting:** Prioritize recent samples
5. **Feature normalization:** Improve gradient descent stability
6. **Cross-validation:** Prevent overfitting with held-out samples

---

## Conclusion

This implementation provides **true online learning** that:
- Captures user preferences automatically
- Stores data persistently across power cycles
- Retrains the model with real gradient descent
- Updates brightness control immediately
- Improves over time with continued use

The system learns your lighting preferences and adapts the ML model to match your behavior patterns!
