# Complete Command Reference

All available commands for the LED Brightness Controller with Live Retraining.

---

## 📋 Command Categories

- [Basic Commands](#basic-commands)
- [Sample Management](#sample-management)
- [Model Inspection](#model-inspection)
- [Manual Testing](#manual-testing)

---

## Basic Commands

### 1. `help`

**Description:** Show all available commands with descriptions

**Usage:**
```
help
```

**Output:**
```
=== BASIC COMMANDS ===
help          - Show all commands
stats         - Show statistics
retrain       - Force retraining

=== SAMPLE MANAGEMENT ===
samples       - View training samples
clearsamples  - Clear all samples

=== MODEL INSPECTION ===
modelinfo     - Show model summary
weights       - Show detailed weights

=== MANUAL TESTING ===
test HH DD AMBIENT MOTION POT
  HH: Hour (0-23)
  DD: Day (0=Mon, 1=Tue, ..., 6=Sun)
  AMBIENT: Light level in lux (0-10000)
  MOTION: 0=No, 1=Yes
  POT: Pot value (0-4095, 2048=center)

Example: test 22 1 300 1 2200
         (10 PM, Tuesday, 300 lux, motion, pot=2200)
```

---

### 2. `stats`

**Description:** Display overall system statistics and current state

**Usage:**
```
stats
```

**Output:**
```
=== STATISTICS ===
Predictions: 12543
Average Brightness: 52.3
Pot Changes Today: 8
Training Samples: 15
Model Type: ADAPTIVE
```

**What it shows:**
- **Predictions:** Total number of predictions made since startup
- **Average Brightness:** Mean brightness across all predictions
- **Pot Changes Today:** Number of potentiometer adjustments today
- **Training Samples:** Number of samples collected in EEPROM
- **Model Type:** Current model in use (TREE or ADAPTIVE)

---

### 3. `retrain`

**Description:** Force immediate retraining of the model

**Usage:**
```
retrain
```

**Requirements:**
- Minimum 5 training samples collected

**Output:**
```
╔════════════════════════════════════════════╗
║      LIVE RETRAINING IN PROGRESS          ║
╚════════════════════════════════════════════╝

📚 Training on 15 samples...

✓ RETRAINING COMPLETE
  Average Error: 2.1%
  Total Updates: 1
  Model: ADAPTIVE (User-Learned)

📊 Updated Model Parameters:
  Ambient Weight: -0.0187
  Motion Weight: 18.34
  Bias: 52.15
╔════════════════════════════════════════════╗
║    MODEL ACTIVE - BRIGHTNESS UPDATED       ║
╚════════════════════════════════════════════╝
```

**When to use:**
- After collecting enough samples (5+)
- To force learning instead of waiting for automatic trigger
- To test learning immediately

**Automatic triggers:**
- After 20 potentiometer adjustments in a day
- Daily at midnight (00:00:01)

---

## Sample Management

### 4. `samples`

**Description:** View all collected training samples

**Usage:**
```
samples
```

**Output:**
```
=== TRAINING SAMPLES ===
Total Samples: 15

┌────┬─────────┬────────┬──────────┬─────────┬──────────┬─────────────┐
│ # │Ambient  │Motion  │ Period   │ Day    │ Target%  │  Timestamp  │
├────┼─────────┼────────┼──────────┼─────────┼──────────┼─────────────┤
│ 1 │  235.4  │   1    │ Night    │  Tue  │   65.2  │ 1716518200 │
│ 2 │  310.2  │   1    │ Night    │  Tue  │   62.8  │ 1716518400 │
│ 3 │  280.5  │   1    │ Night    │  Tue  │   68.1  │ 1716518600 │
...
└────┴─────────┴────────┴──────────┴─────────┴──────────┴─────────────┘
```

**What it shows:**
- Sample number
- Ambient light level (lux)
- Motion detection (0=No, 1=Yes)
- Time period (Early Morning, Morning, Afternoon, Evening, Night)
- Day of week (Mon-Sun)
- Target brightness (your desired brightness)
- Unix timestamp when sample was collected

---

### 5. `clearsamples`

**Description:** Delete all training samples from EEPROM

**Usage:**
```
clearsamples
```

**Output:**
```
✓ All samples cleared
```

**What it does:**
- Removes all training samples
- Keeps model weights intact
- Prepares for fresh data collection

**When to use:**
- Before collecting new training data
- To remove old/incorrect samples
- To start fresh learning

---

## Model Inspection

### 6. `modelinfo`

**Description:** Show current model status and key information

**Usage:**
```
modelinfo
```

**Output:**
```
=== MODEL INFO ===
Type: ADAPTIVE (Learned)
Update Count: 3
Learning Rate: 0.010000
Bias: 52.15
```

**What it shows:**
- **Type:** Current model (TREE=Original, ADAPTIVE=Learned)
- **Update Count:** Number of times model has been retrained
- **Learning Rate:** Step size for gradient descent (default 0.01)
- **Bias:** Baseline brightness prediction

**Interpretation:**
- Update Count > 0 = Model has learned
- Type = ADAPTIVE = Learning is active
- Bias changed from 50.0 = Model adjusted

---

### 7. `weights`

**Description:** Display all detailed model parameters and weights

**Usage:**
```
weights
```

**Output:**
```
╔════════════════════════════════════════════╗
║    DETAILED MODEL WEIGHTS (LEARNED)        ║
╚════════════════════════════════════════════╝

Bias: 52.150

Feature Weights:
  Ambient Light: -0.018700
  Motion: 18.340000
  Sin(Hour): 5.230000
  Cos(Hour): 4.870000

Time Period Weights:
  Early Morning: 30.450
  Morning: 20.120
  Afternoon: 15.330
  Evening: 25.670
  Night: 48.900

Day of Week Weights:
  Mon: 0.450
  Tue: -0.120
  Wed: 0.330
  Thu: 0.550
  Fri: 0.220
  Sat: -0.330
  Sun: 0.120

Learning Rate: 0.010000
Total Updates: 3
```

**What each parameter means:**

| Parameter | Meaning | Impact |
|-----------|---------|--------|
| Bias | Baseline brightness | Higher = brighter by default |
| Ambient Light | Light level effect | More negative = less LED as light increases |
| Motion | Motion importance | Higher = brighter with motion |
| Sin/Cos Hour | Time oscillation | Captures daily cycles |
| Time Periods | Period-specific bias | Each time of day has own preference |
| Day of Week | Day-specific bias | Each day has own adjustment |

**Interpretation Example:**
```
Motion: 18.34 (was 15.0 default)
→ Model learned motion increases brightness

Night: 48.90 (was 40.0 default)
→ Model learned night time prefers brighter

Ambient: -0.0187 (was -0.02 default)
→ Model learned higher light reduces LED
```

---

## Manual Testing

### 8. `test` 

**Description:** Test brightness prediction with any input combination

**Usage:**
```
test HH DD AMBIENT MOTION POT
```

**Parameters:**

| Parameter | Range | Description |
|-----------|-------|-------------|
| HH | 0-23 | Hour in 24-hour format |
| DD | 0-6 | Day of week (0=Monday, 6=Sunday) |
| AMBIENT | 0-100000 | Ambient light in lux |
| MOTION | 0-1 | Motion detection (0=No, 1=Yes) |
| POT | 0-4095 | Potentiometer value (2048=center) |

**Examples:**

```
test 22 1 300 1 2048
(10 PM, Tuesday, 300 lux, motion, center pot)

test 8 2 900 1 2500
(8 AM, Wednesday, 900 lux, motion, pot at +20%)

test 0 0 50 0 1500
(Midnight, Monday, 50 lux, no motion, pot at -15%)

test 14 4 750 0 2048
(2 PM, Friday, 750 lux, no motion, center pot)
```

**Output:**

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

**What it shows:**
- **Raw (%):** Model's base prediction
- **Offset:** Potentiometer adjustment (-100 to +100)
- **Final (%):** What gets sent to LED (Raw + Offset, clamped 0-100)
- **Difference:** How much ADAPTIVE differs from TREE (learning amount)
- **Active Model:** Which model controls the LED

**Use cases:**
- Test prediction with specific parameters
- Compare TREE vs ADAPTIVE to see learning
- Verify model behavior in different scenarios
- Validate brightness predictions

---

## Reset Commands

### 9. `resetmodel`

**Description:** Reset model weights to default values

**Usage:**
```
resetmodel
```

**Output:**
```
✓ Model reset to defaults
  Switched back to TREE model
```

**What it does:**
- Resets all weights to initial defaults
- Sets Update Count to 0
- Switches to TREE model
- Forgets all learning

**When to use:**
- Undo bad learning
- Start fresh learning
- Test system again

---

## Summary Table

| Command | Category | Purpose | Parameters |
|---------|----------|---------|-----------|
| `help` | Basic | Show all commands | None |
| `stats` | Basic | Show statistics | None |
| `retrain` | Basic | Force retraining | None |
| `samples` | Samples | View training data | None |
| `clearsamples` | Samples | Delete training data | None |
| `modelinfo` | Model | Show model status | None |
| `weights` | Model | Show all parameters | None |
| `test` | Testing | Test prediction | HH DD AMBIENT MOTION POT |
| `resetmodel` | Reset | Reset to defaults | None |

---

## Quick Reference Cheat Sheet

**To get started:**
```
help
stats
```

**To test a prediction:**
```
test 22 1 300 1 2048
```

**To see learning:**
```
weights
modelinfo
test 22 1 300 1 2048
```

**To collect & train:**
```
(Adjust potentiometer on ESP32)
retrain
```

**To reset everything:**
```
clearsamples
resetmodel
```

---

## Error Messages & Solutions

| Error | Cause | Solution |
|-------|-------|----------|
| `❌ Error: Hour must be 0-23` | Invalid hour | Use 0-23 format |
| `❌ Error: Day must be 0-6` | Invalid day | Use 0=Mon to 6=Sun |
| `❌ Error: Ambient must be 0-100000` | Invalid light level | Use 0-100000 |
| `❌ Error: Motion must be 0 or 1` | Invalid motion | Use 0 or 1 only |
| `❌ Error: Pot must be 0-4095` | Invalid pot value | Use 0-4095 |
| `⚠ Insufficient samples (need ≥5)` | Not enough data | Collect 5+ samples first |
| `Unknown command: xyz` | Not recognized | Type `help` to see commands |

---

## Tips & Tricks

✅ **Copy-paste ready:** All commands work when copied exactly  
✅ **Case sensitive:** Use lowercase (e.g., `help` not `Help`)  
✅ **Line ending:** Select "Newline" in Serial Monitor dropdown  
✅ **Baud rate:** Must be exactly 9600  
✅ **Fast feedback:** Most commands respond in <100ms  

---

## Command Frequency Guide

**Most often used:**
1. `test HH DD AMBIENT MOTION POT` - Testing
2. `weights` - Checking learning
3. `stats` - Quick status

**Regular use:**
- `retrain` - Force training when needed
- `samples` - Audit data

**Maintenance:**
- `clearsamples` - Clean up data
- `resetmodel` - Start fresh

**Documentation:**
- `help` - Show commands
- `modelinfo` - Show status

---

That's all 9 available commands! 🎯

**Most useful combo:**
```
test 22 1 300 1 2048
weights
stats
```

This shows prediction, what was learned, and overall status.
