# Quick Start: Manual Testing & Verification

## 🎯 You Asked For

**Can I manually input ALL parameters (time, day, ambient light, motion, pot) to test predictions and verify trained values?**

## ✅ Yes! Here's How

---

## 5-Minute Quick Start

### 1. Upload Code

```
1. Download led_controller_live_retraining.ino
2. Open in Arduino IDE
3. Select Board → ESP32 Dev Module
4. Upload
```

### 2. Open Serial Monitor

- Port: Your ESP32 COM port
- Baud: 9600
- Line ending: Newline

### 3. Test Manual Prediction

**Type this command:**
```
test 22 1 300 1 2048
```

**What it means:**
- 22 = 10 PM
- 1 = Tuesday
- 300 = 300 lux (medium light)
- 1 = Motion detected
- 2048 = Pot at center (no offset)

**You'll see:**
```
TREE (Original): 45%
ADAPTIVE (Learned): 45%  (same - not trained yet)
Difference: 0%
```

### 4. Make Adjustments & Collect Samples

**Physical ESP32:**
- Adjust potentiometer 5-10 times
- Watch for "Training sample saved" messages

**OR via Serial:**
- Use `test` with different pot values
- Each manual adjustment is a learning opportunity

### 5. Force Retraining

**Type:**
```
retrain
```

**Watch for:**
```
RETRAINING IN PROGRESS
Training on N samples...
✓ RETRAINING COMPLETE
Average Error: X%
Model: ADAPTIVE (User-Learned)
```

### 6. Test Same Scenario Again

**Type same command:**
```
test 22 1 300 1 2048
```

**You'll now see:**
```
TREE (Original): 45%
ADAPTIVE (Learned): 52%  (different! - model learned!)
Difference: +7%
```

---

## Testing Command Syntax

```
test HH DD AMBIENT MOTION POT

HH       = Hour (0-23)
DD       = Day (0=Mon, 1=Tue, ..., 6=Sun)
AMBIENT  = Light in lux (0-100000)
MOTION   = 0 or 1
POT      = Pot value (0-4095, 2048=center)
```

### Examples

```
test 22 1 300 1 2048
(10 PM, Tuesday, 300 lux, motion, center pot)

test 8 2 900 1 2500
(8 AM, Wednesday, 900 lux, motion, +20% offset)

test 0 0 50 0 1500
(Midnight, Monday, 50 lux, no motion, -15% offset)

test 14 4 750 0 2048
(2 PM, Friday, 750 lux, no motion, center pot)
```

---

## Key Commands

```
test HH DD AMBIENT MOTION POT   → Test with custom inputs
modelinfo                        → Show model type & updates
weights                          → Show all learned parameters
samples                          → View all training samples
stats                            → Overall statistics
retrain                          → Force retraining
help                             → Show all commands
```

---

## What You Can Verify

### ✅ Before Retraining
```
test 22 1 300 1 2048
Output: TREE=45%, ADAPTIVE=45%
(Models identical - no training yet)
```

### ✅ After Retraining
```
test 22 1 300 1 2048
Output: TREE=45%, ADAPTIVE=52%
(Models different - learning confirmed!)
```

### ✅ What Model Learned
```
weights
Shows:
  - Motion weight: 15.0 → 18.5 (increased)
  - Night weight: 40.0 → 48.9 (increased)
  - Bias: 50.0 → 52.1 (increased)
```

### ✅ Training Data Used
```
samples
Shows all samples with ambient, motion, target brightness
```

---

## Real Testing Scenario

### Day 1: Initial State

```
> test 22 1 300 1 2048
TREE: 45%
ADAPTIVE: 45%
(Identical - no training)

> stats
Training Samples: 0
Model Type: TREE
```

### Day 1: Make Adjustments

```
Adjust potentiometer on ESP32:
- 22:00, turn to 65%
- 22:15, turn to 62%
- 22:30, turn to 68%

> samples
Shows 3 samples captured
```

### Day 1: Verify Samples

```
> samples
Sample 1: Ambient=300 lux, Motion=1, Period=Night, Target=65%
Sample 2: Ambient=320 lux, Motion=1, Period=Night, Target=62%
Sample 3: Ambient=280 lux, Motion=1, Period=Night, Target=68%
```

### Day 1: Trigger Learning

```
> retrain
RETRAINING IN PROGRESS
Training on 3 samples...
✓ RETRAINING COMPLETE
Average Error: 2.1%
Model: ADAPTIVE (User-Learned)
```

### Day 2: Verify Learning

```
> test 22 1 300 1 2048
TREE: 45% (unchanged)
ADAPTIVE: 58% (learned +13%!)

> weights
Motion: 15.0 → 18.5
Night: 40.0 → 48.9
Bias: 50.0 → 52.1

✓ Model successfully learned night+motion preference!
```

---

## Understanding Test Output

```
╔════════════════════════════════════════════╗
║     MANUAL PREDICTION TEST RESULTS         ║
╚════════════════════════════════════════════╝

📍 Input Parameters:
   Time: 22:00 (Night)
   Day: Tuesday
   Ambient Light: 300 lux
   Motion: YES
   Pot Value: 2048 (Offset: 0%)

📊 Model Predictions:
┌─────────────────┬──────────┬──────────┬──────────┐
│ Model           │ Raw (%)  │ Offset   │ Final(%) │
├─────────────────┼──────────┼──────────┼──────────┤
│ TREE (Original) │   45.23 │     0   │   45.23 │
│ ADAPTIVE(Learn) │   52.15 │     0   │   52.15 │
└─────────────────┴──────────┴──────────┴──────────┘

📈 Model Comparison:
   Difference: +6.92% (+15.3% change)
   Active Model: ADAPTIVE (Learned)
   ✓ Using learned model for real control
```

**Breaking it down:**
- **Raw (%)** = Model prediction before pot offset
- **Offset** = Potentiometer adjustment (-100 to +100)
- **Final (%)** = What gets sent to LED (Raw + Offset, clamped 0-100)
- **Difference** = How much ADAPTIVE differs from TREE
- **Active Model** = Which one is controlling your LED

---

## Verification Checklist

✅ **Sample Collection**
```
test 22 1 300 1 2200
(Make pot adjustment)
samples
(Verify sample appears)
```

✅ **Before Retraining**
```
test 22 1 300 1 2048
(Note TREE and ADAPTIVE are equal)
modelinfo
(Update Count: 0)
```

✅ **Trigger Retraining**
```
(Make 20 adjustments or send "retrain")
(Watch for "RETRAINING COMPLETE")
```

✅ **After Retraining**
```
test 22 1 300 1 2048
(Note TREE and ADAPTIVE differ - learning worked!)
modelinfo
(Update Count: 1+)
weights
(See weight changes from defaults)
```

---

## Common Test Cases

### Night + Motion (Should Prefer Bright)
```
test 22 1 300 1 2048
Expected: ADAPTIVE > TREE (learned to be brighter)
```

### Day + No Motion (Should Prefer Dim)
```
test 14 2 900 0 2048
Expected: ADAPTIVE < TREE (learned to be dimmer)
```

### Early Morning (Depends on Your Preference)
```
test 5 0 100 1 2048
Expected: Shows your specific preference
```

### Evening Transition
```
test 18 5 400 1 2048
Expected: Smooth transition to evening preferences
```

---

## Example Commands for Testing

```
# Test night scenarios
test 22 1 300 1 2048
test 23 1 200 1 2200
test 0 2 100 0 1800

# Test day scenarios
test 8 2 800 1 2048
test 14 2 900 0 2048
test 10 1 700 1 2300

# Check learning
weights
modelinfo
samples

# Verify predictions
stats
```

---

## Troubleshooting Quick Answers

**Q: Both models show same percentage?**
A: Normal before retraining. After retraining, they should differ.

**Q: Samples not being collected?**
A: Adjustment must be >5%, and held stable 5+ seconds.

**Q: Retraining says "insufficient samples"?**
A: Need at least 5 samples. Make 5 pot adjustments first.

**Q: Want to restart?**
A: Send `clearsamples` then `resetmodel`

**Q: Want to see all parameters**
A: Send `weights` command

---

## Summary

You can now:

1. ✅ **Input any parameters** → `test HH DD AMBIENT MOTION POT`
2. ✅ **Get predictions** → Both TREE and ADAPTIVE models
3. ✅ **Verify learning** → Compare TREE vs ADAPTIVE after retraining
4. ✅ **See what learned** → `weights` shows all parameters
5. ✅ **Audit training** → `samples` shows all training data
6. ✅ **Test any scenario** → All combinations supported

**Your LED system is now fully testable and transparent!** 🎉

---

## Next Steps

1. **Download** `led_controller_live_retraining.ino`
2. **Upload** to ESP32
3. **Open** Serial Monitor (9600 baud)
4. **Type:** `test 22 1 300 1 2048`
5. **Make adjustments** on your ESP32
6. **Send:** `retrain`
7. **Type:** `test 22 1 300 1 2048` again
8. **Watch** ADAPTIVE prediction change! 🎯

**That's it! Full manual testing enabled!**
