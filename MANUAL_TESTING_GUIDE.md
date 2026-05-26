# Manual Testing & Model Verification Guide

## Overview

The enhanced `led_controller_live_retraining.ino` now includes **comprehensive manual testing** capabilities that allow you to:

✅ **Test with ANY input combination** (hour, day, ambient light, motion, pot)  
✅ **Compare TREE vs ADAPTIVE model outputs** side-by-side  
✅ **Verify learned values** after retraining happens  
✅ **Inspect detailed model weights** and feature importance  
✅ **Validate model learning** with specific test scenarios  

---

## Quick Start

### 1. Upload Code to ESP32

1. Download `led_controller_live_retraining.ino`
2. Open in Arduino IDE
3. Select Board: **ESP32 Dev Module**
4. Upload to your ESP32

### 2. Open Serial Monitor

- **Baud Rate:** 9600
- **Line ending:** Newline

### 3. Check Setup

```
Type: help
(You'll see all available commands)
```

---

## Manual Testing Command

### Syntax

```
test HH DD AMBIENT MOTION POT
```

### Parameters

| Parameter | Range | Description | Example |
|-----------|-------|-------------|---------|
| **HH** | 0-23 | Hour in 24-hour format | 22 (for 10 PM) |
| **DD** | 0-6 | Day of week (0=Mon, 6=Sun) | 1 (for Tuesday) |
| **AMBIENT** | 0-100000 | Ambient light in lux | 300 (medium light) |
| **MOTION** | 0 or 1 | Motion detection (0=No, 1=Yes) | 1 (motion detected) |
| **POT** | 0-4095 | Potentiometer value (2048=center) | 2200 (slight positive offset) |

### Example Commands

```
test 22 1 300 1 2048
(10 PM, Tuesday, 300 lux, motion detected, pot at center)

test 8 2 800 1 2500
(8 AM, Wednesday, 800 lux, motion, pot offset to +20%)

test 0 0 50 0 1500
(Midnight, Monday, 50 lux, no motion, pot offset to -15%)
```

---

## Test Output Explained

### Sample Output

```
╔════════════════════════════════════════════╗
║     MANUAL PREDICTION TEST RESULTS         ║
╚════════════════════════════════════════════╝

📍 Input Parameters:
   Time: 22:00 (Night)
   Day: Tuesday
   Ambient Light: 300 lux
   Motion: YES
   Pot Value: 2200 (Offset: +5%)

📊 Model Predictions:
┌─────────────────┬──────────┬──────────┬──────────┐
│ Model           │ Raw (%)  │ Offset   │ Final(%) │
├─────────────────┼──────────┼──────────┼──────────┤
│ TREE (Original) │   45.23 │    +5   │   50.23 │
│ ADAPTIVE(Learn) │   52.15 │    +5   │   57.15 │
└─────────────────┴──────────┴──────────┴──────────┘

📈 Model Comparison:
   Difference: +6.92% (+15.3% change)
   Active Model: ADAPTIVE (Learned)
   ✓ Using learned model for real control
```

### Understanding the Output

**Raw (%):** Model's prediction before user offset
- **TREE:** Original decision tree model (static)
- **ADAPTIVE:** Learned model (trained on your data)

**Offset:** Potentiometer adjustment
- Negative = Darker
- Positive = Brighter

**Final (%):** Actual brightness after applying offset
- This is what would be sent to the LED

**Difference:** How much the adaptive model differs from tree
- Shows learning effect
- Positive = adaptive predicts brighter
- Negative = adaptive predicts darker

**Active Model:** Which model is actually controlling your LED
- **TREE:** Before any retraining
- **ADAPTIVE:** After first successful retraining

---

## Testing Workflow

### Phase 1: Before Retraining (Initial State)

```
Step 1: Send "stats"
Expected: Training Samples: 0, Model Type: TREE

Step 2: Send "test 22 1 300 1 2048"
Expected: Both models show same prediction (not trained yet)

Step 3: Manually adjust potentiometer on ESP32
OR use multiple "test" commands with different pots

Step 4: Send "samples"
Expected: See captured training samples
```

### Phase 2: Trigger Retraining

**Option A: Wait for automatic retraining**
```
- Wait for 20 pot adjustments
  OR
- Wait until midnight (00:00:01)
```

**Option B: Manual trigger**
```
Send: retrain
Expected: 
  "RETRAINING IN PROGRESS"
  "✓ RETRAINING COMPLETE"
  "Model: ADAPTIVE (User-Learned)"
```

### Phase 3: After Retraining (Verify Learning)

```
Step 1: Send "modelinfo"
Expected: Model Type: ADAPTIVE, Update Count: 1+

Step 2: Send "weights"
Expected: See updated weights (different from defaults)

Step 3: Send "test 22 1 300 1 2048"
Expected: ADAPTIVE prediction differs from TREE
          This shows learning worked!

Step 4: Send "samples"
Expected: See all training samples used in retraining
```

---

## Verification Scenarios

### Scenario 1: Night + Motion Learning

**Initial behavior:**
```
test 22 0 300 1 2048
TREE: 45% (too dark for motion at night)
ADAPTIVE: 45% (no training yet)
```

**Collect samples:** Adjust pot to 65% several times at night with motion

**After retraining:**
```
test 22 0 300 1 2048
TREE: 45% (unchanged - static)
ADAPTIVE: 58% (learned: user prefers brighter at night with motion)
```

**What happened:** Model learned motion_weight increased

---

### Scenario 2: Bright Day with Motion

**Initial:**
```
test 10 2 900 1 2048
TREE: 30% (bright day)
ADAPTIVE: 30% (no training yet)
```

**Collect samples:** Adjust pot to 20% several times during bright morning

**After retraining:**
```
test 10 2 900 1 2048
TREE: 30% (unchanged)
ADAPTIVE: 22% (learned: user prefers darker despite brightness)
```

**What happened:** Model learned to reduce brightness in bright conditions

---

### Scenario 3: Check Learned Day/Period Preferences

**Monday morning (before training):**
```
test 8 0 600 0 2048
TREE: 40%
ADAPTIVE: 40%
```

**Collect multiple samples on Monday morning**

**After retraining:**
```
test 8 0 600 0 2048
TREE: 40%
ADAPTIVE: 44% (Monday morning weight increased)
```

**What happened:** Model learned Monday preference

---

## Advanced Testing Commands

### View All Training Samples

```
samples
```

**Output shows:**
- Sample number and count
- Ambient light, motion, time period
- Day of week
- User's target brightness
- Timestamp

**Use to verify:**
- ✓ Correct samples were captured
- ✓ Diverse scenarios represented
- ✓ Realistic brightness ranges

---

### Display Detailed Model Weights

```
weights
```

**Shows:**
- Bias (baseline brightness)
- Feature weights (ambient, motion, sin/cos hour)
- Time period weights (early morning, morning, etc.)
- Day of week weights (Mon-Sun)
- Learning rate
- Total updates

**What to look for:**
- **Motion weight > 15:** Model prefers brightness with motion
- **Night period weight > 40:** Model likes brighter at night
- **Ambient weight < -0.02:** More light = less LED brightness
- **Updates > 0:** Retraining has occurred

---

### Compare Models Side-by-Side

Use the `test` command with different scenarios:

**Test 1: Night + Motion**
```
test 22 2 300 1 2048
```

**Test 2: Day + No Motion**
```
test 14 2 800 0 2048
```

**Test 3: Early Morning + Motion**
```
test 5 0 100 1 2048
```

**Test 4: Evening Transition**
```
test 18 5 400 1 2048
```

**Compare outputs** to see which scenarios the model learned best

---

## Real-World Verification Process

### Step 1: Collect Initial Data (Day 1)

```
1. Upload code
2. Use ESP32 normally for 1-2 hours
3. Adjust potentiometer 5-10 times as needed
4. Send "stats" to verify samples captured
```

**Expected output:**
```
Training Samples: 5-10
Model Type: TREE
```

### Step 2: Test Before Retraining

```
1. Send "test 22 1 300 1 2048"
2. Record TREE and ADAPTIVE values (should be equal)
3. Send "weights" to see initial model parameters
```

**Expected:**
```
TREE: 45%
ADAPTIVE: 45% (identical - not trained yet)
Bias: 50.0
Motion Weight: 15.0 (defaults)
```

### Step 3: Trigger Retraining

```
1. Make 10+ more adjustments (reach 20+ total)
   OR send "retrain" command
2. Watch for retraining messages
3. Verify "Model: ADAPTIVE" appears
```

**Expected output:**
```
RETRAINING IN PROGRESS
Training on 15 samples...
✓ RETRAINING COMPLETE
Average Error: 3.2%
Total Updates: 1
Model: ADAPTIVE (User-Learned)
```

### Step 4: Verify Learning

```
1. Send "test 22 1 300 1 2048" (same input as Step 2)
2. Compare TREE vs ADAPTIVE values
3. Send "weights" to see updated parameters
```

**Expected:**
```
TREE: 45% (unchanged)
ADAPTIVE: 52% (different - model learned!)
Bias: 52.15 (increased from 50.0)
Motion Weight: 18.5 (increased from 15.0)
```

### Step 5: Validate Samples

```
1. Send "samples" to view all training data
2. Check for diverse scenarios:
   - Different times of day
   - With/without motion
   - Different ambient light levels
3. Verify target brightness makes sense
```

**Expected:**
```
Sample 1: 235 lux, Motion=1, Night, Target=65%
Sample 2: 800 lux, Motion=0, Afternoon, Target=25%
Sample 3: 150 lux, Motion=1, Evening, Target=55%
...
```

---

## Troubleshooting Tests

### Problem: TREE and ADAPTIVE show same value

**Cause:** Model hasn't been trained yet (use_adaptive_model = false)

**Solution:**
```
1. Send "stats"
   Check: "Update Count: 0" (no retraining)
2. Make 20+ pot adjustments
   OR send "retrain" command
3. After retraining, values should differ
```

---

### Problem: Samples not being collected

**Cause:** Pot adjustments not significant enough

**Solution:**
```
1. Check your adjustment > 5% brightness change
2. Hold potentiometer stable for 5+ seconds
3. Verify by sending "samples" after adjustment
4. Check "Pot Changes Today" in stats
```

---

### Problem: Retraining shows "Insufficient samples"

**Cause:** Need at least 5 samples to train

**Solution:**
```
1. Manually adjust potentiometer on ESP32 5+ times
   OR
2. Use "test" command with different pots
3. Use "samples" to verify capture
4. Then send "retrain"
```

---

### Problem: Model weights look wrong

**Cause:** Could be learning artifact or noise

**Solution:**
```
1. Collect more samples (10+)
2. Run retraining again
3. Check if weights stabilize
4. If erratic, send "resetmodel" and restart
```

---

## Performance Verification

### Test Prediction Speed

```
Send "test 22 1 300 1 2048"
Measure: How fast does output appear?
Expected: <100ms response time
```

### Test Memory Stability

```
Send "samples" repeatedly
Expected: Consistent output, no crashes
Indicates: EEPROM operations working
```

### Test Retraining Speed

```
Send "retrain"
Measure: How long until "COMPLETE"?
Expected: 0.5-2 seconds
Indicates: Gradient descent efficiency
```

---

## CSV Export for Analysis

You can manually copy test results:

### Template

```
Hour, Day, Ambient, Motion, Pot, TreePred%, AdaptivePred%, Difference%
22,   Tue,  300,    1,      2048, 45.23,    52.15,        +6.92
8,    Wed,  800,    1,      2500, 35.12,    38.45,        +3.33
0,    Mon,  50,     0,      1500, 60.45,    62.10,        +1.65
```

Copy this to Excel/Google Sheets to track:
- Learning patterns
- Model improvement over time
- Feature importance

---

## Summary

With these testing tools you can now:

✅ **Manually test any condition** → Predict brightness  
✅ **Compare before/after retraining** → Verify learning  
✅ **Inspect learned weights** → Understand what model learned  
✅ **View all samples** → Audit training data  
✅ **Validate predictions** → Ensure model makes sense  

**The system is now fully transparent and verifiable!** 🔍
