# Online Learning System - Complete Guide

## 🎯 Overview

This system implements **true online learning** where the LED controller learns from your potentiometer adjustments and automatically retrains the model to predict your preferred brightness levels.

## 🔄 How It Works

### 1. **Data Collection Phase** (Auto Mode)
- System runs in automatic mode, showing live sensor readings
- When you adjust the potentiometer and **keep it stable for 5 seconds**, it records:
  - Ambient light level
  - Motion sensor state
  - Hour, day of week, time period
  - Your final brightness setting (after pot adjustment)
- Each stable pot change = **1 training sample**

### 2. **Retraining Triggers** (Automatic)

The model retrains automatically at **either** of these events:

#### A. **20 Pot Changes** 
- After collecting 20 stable pot adjustments throughout the day
- Triggers immediately when 20th change is recorded
- Message: `>>> 20 POT CHANGES REACHED - RETRAINING NOW <<<`

#### B. **Daily at 00:00:01** (Midnight)
- Triggers every night at exactly 00:00:01
- Processes all samples collected that day
- Resets counters for the new day
- Message: `>>> MIDNIGHT RETRAINING (00:00:01) <<<`

### 3. **Learning Algorithm**

When retraining happens:

```
For each time period (0-4):
   1. Calculate average BASE prediction from collected samples
   2. Calculate average TARGET brightness (what you set)
   3. Learned Offset = Average Target - Average Base
   4. Store offset in learned model
   5. Save to EEPROM for persistence
```

### 4. **Applying Learned Model**

After retraining, every prediction uses:

```
Final Prediction = Base Prediction + Learned Offset[time_period] + Manual Offset
```

The learned offset automatically adjusts the base prediction based on your historical preferences for each time period.

## 📊 Time Periods

The system divides the day into 5 periods:

- **0**: Early Morning (04:01 - 06:00)
- **1**: Morning (06:01 - 12:00)
- **2**: Afternoon (12:01 - 16:00)
- **3**: Evening (16:01 - 20:00)
- **4**: Night (20:01 - 04:00)

Each period learns independently!

## 🎮 Usage Instructions

### Step 1: Auto Mode (Learning)

1. Upload the code to ESP32
2. System starts in **automatic mode**
3. Adjust potentiometer to your preferred brightness
4. **Hold it steady for 5 seconds** - you'll see:
   ```
   >>> TRAINING SAMPLE #1 RECORDED | Period=2 | Target=65.0% <<<
   ```
5. Continue using naturally - system learns from your adjustments
6. After 20 changes OR at midnight, retraining happens automatically

### Step 2: Manual Mode (Testing)

After the model has been retrained, test it:

1. Type `manual` in Serial Monitor
2. Enter predictions with:
   ```
   predict HH MM DAY LUX MOTION POT
   ```
   
   Example:
   ```
   predict 14 30 2 500.5 1 2048
   ```

3. You'll see the **learned** prediction:
   ```
   ===================================
        PREDICTION RESULT (LEARNED)   
   ===================================
   Time Period: 2
   Base Prediction: 40.0%
   Learned Adjustment: +15.5% (from 8 samples)
   Learned Brightness: 55.5%
   Manual Offset: 0%
   Final Brightness: 55.5%
   PWM Value: 141
   ===================================
   ```

4. The "Learned Adjustment" shows how much the system adapted based on your preferences!

## 📝 Serial Commands

| Command | Description |
|---------|-------------|
| `help` | Show all commands |
| `manual` | Enter manual prediction mode (test learned values) |
| `auto` | Return to automatic mode |
| `predict HH MM DAY LUX MOTION POT` | Test prediction with learned model |
| `stats` | Show statistics and learned offsets |
| `retrain` | Force immediate retraining |
| `clear` | Clear learned model and start fresh |

## 📈 Example Workflow

### Day 1: Initial Learning

```
Time: 08:00 - Morning period (1)
Action: User adjusts pot to 70% (base was 40%)
Result: Training sample recorded: target=70%

Time: 14:00 - Afternoon period (2)
Action: User adjusts pot to 55% (base was 35%)
Result: Training sample recorded: target=55%

... (18 more adjustments throughout the day)

Time: 18:45 - Evening period (3)
Action: 20th pot adjustment made
Result: >>> RETRAINING TRIGGERED <<<
```

**Model Update:**
- Morning (1): Learned offset = +25%
- Afternoon (2): Learned offset = +18%
- Evening (3): Learned offset = +12%

### Day 2: Using Learned Model

```
Time: 08:30 - Morning
Base: 40% → Learned: 65% (40 + 25) ✓
User sees preferred brightness automatically!

Time: 14:30 - Afternoon  
Base: 35% → Learned: 53% (35 + 18) ✓
Much closer to user preference!
```

## 🔍 Monitoring Output

### Auto Mode Display:

```
┌──────────┬─────┬──────┬─────────┬────────┬────────┬───────┬────────┬────────┬─────┐
│   Time   │ Day │ Hour │ Ambient │ Motion │ Period │ Base% │ Offset │ Final% │ PWM │
├──────────┼─────┼──────┼─────────┼────────┼────────┬───────┼────────┼────────┼─────┤
│ 08:15:23 │ Mon │  8   │  450.2  │   1    │   1    │ 58.5  │   0   │  58.5  │ 149 │
```

- **Base%**: Prediction with learned adjustments applied
- **Offset**: Current pot manual offset
- **Final%**: Base + Offset

### Training Sample Recording:

```
>>> TRAINING SAMPLE #15 RECORDED | Period=1 | Target=72.3% <<<
```

### Retraining Messages:

```
─────────────────────────────────────────────
        RETRAINING MODEL NOW                 
─────────────────────────────────────────────
Training samples: 20

Learned adjustments by time period:
  Period 1: Offset = +22.5% (8 samples)
  Period 2: Offset = +15.3% (7 samples)
  Period 3: Offset = +18.7% (5 samples)

Saving model to EEPROM... Done.

Retraining complete! Model updated.
─────────────────────────────────────────────
```

## ⚙️ Configuration Options

In the code, you can adjust:

```cpp
#define MAX_CHANGES_BEFORE_RETRAIN 20  // Change to 10, 30, etc.
#define RETRAIN_HOUR 0                 // Change midnight hour
#define RETRAIN_MINUTE 0
#define RETRAIN_SECOND 1
#define POT_STABLE_DURATION 5000       // How long pot must be stable (ms)
#define MAX_LEARNING_SAMPLES 50        // Max samples to store
```

## 💾 Persistence

- **Learned model saved to EEPROM** after each retraining
- Survives power cycles and reboots
- Loads automatically on startup
- Shows loaded adjustments in Serial Monitor

## 🧪 Testing the Learning

1. **Clear existing model**: `clear`
2. **Make 20 pot adjustments** in auto mode
3. **Watch for retraining** message
4. **Switch to manual mode**: `manual`
5. **Test with same conditions** you trained on
6. **Compare** "Base Prediction" vs "Learned Brightness"

You should see the learned brightness much closer to your preferences!

## 🎯 Key Benefits

✅ **Truly adaptive** - learns YOUR preferences  
✅ **Time-aware** - different learning per time period  
✅ **Persistent** - model saved to EEPROM  
✅ **Automatic** - retrains without intervention  
✅ **Verifiable** - manual mode shows learned values  
✅ **Incremental** - keeps learning over time

## 🐛 Troubleshooting

**Q: Not recording samples?**  
A: Hold pot steady for 5 full seconds. Watch for "TRAINING SAMPLE RECORDED" message.

**Q: Model not retraining?**  
A: Check you've made 20 stable changes OR wait for midnight (00:00:01).

**Q: Manual mode shows same values?**  
A: Ensure retraining happened first. Check `stats` to see if model has data.

**Q: Want to start fresh?**  
A: Type `clear` to wipe learned model.

## 📞 Debug Commands

```
stats    - See how many samples collected, learned offsets
clear    - Reset everything
retrain  - Force immediate retraining (if samples available)
```

Enjoy your self-learning LED controller! 🚀
