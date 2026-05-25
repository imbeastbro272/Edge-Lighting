# Quick Start Guide

Get your ML-powered LED controller running in 5 steps!

## Step 1: Install Dependencies (2 minutes)

```bash
pip install -r requirements.txt
```

## Step 2: Try the Demo (3 minutes)

Run the demo to see how everything works:

```bash
python demo_example.py
```

This will:
- Generate sample training data
- Train a Decision Tree model
- Show predictions with manual override
- Save `demo_led_model.pkl` and `sample_training_data.csv`

## Step 3: Train with Your Data (5 minutes)

Prepare your CSV file with these columns:
- `ambient_light` (lux from LDR)
- `motion_detected` (0 or 1 from PIR)
- `sin_hour` (sin(2π × hour / 24))
- `cos_hour` (cos(2π × hour / 24))
- `time_period` (0-4, see time mapping below)
- `led_brightness` (0-100%, your target)

**Time Period Mapping:**
- 0: Early Morning (4:01 - 6:00)
- 1: Morning (6:01 - 12:00)
- 2: Afternoon (12:01 - 16:00)
- 3: Evening (16:01 - 20:00)
- 4: Night (20:01 - 4:00)

Train your model:
```bash
python train_model.py --data your_data.csv --output led_model.pkl
```

**Don't have sin_hour/cos_hour/time_period columns?** See helper functions at the bottom.

## Step 4: Export for Arduino (2 minutes)

Convert your trained model to C code:

```bash
python export_tree_rules.py --model led_model.pkl --output tree_rules.h
```

This creates:
- `tree_rules.h` - Your ML model in C
- `test_cases.h` - Validation tests

## Step 5: Deploy to Hardware (10 minutes)

### Hardware Setup
Wire up your components (see README.md for detailed pinout):
- RTC Module (DS3231) → I2C pins (A4/A5)
- PIR Sensor → D2
- LDR → A0 (with voltage divider)
- Potentiometer → A1 (for manual override)
- LED → D9 (PWM pin)

### Arduino IDE
1. Install library: **RTClib** (by Adafruit)
2. Copy `tree_rules.h` and `test_cases.h` to sketch folder
3. Open `led_controller.ino`
4. Upload to your Arduino/ESP32

### Monitor Serial Output
Open Serial Monitor (9600 baud) to see:
```
=== Model Validation ===
Test 1: Hour=22 Expected=25.34% Predicted=25.34% PASS
✓ ALL TESTS PASSED

Time        Hour  Ambient  Motion  Period  ML%   Offset  Final%  PWM
--------    ----  -------  ------  ------  ---   ------  ------  ---
14:23:45    14    750.2    1       2       42.3  0       42.3    107
```

## Bonus: Visualize Your Model (Optional)

```bash
python visualize_model.py --data your_data.csv --model led_model.pkl --output-dir plots/
```

Creates beautiful plots showing:
- Data distribution
- Feature importance
- Decision tree structure
- Prediction accuracy
- Scenario heatmaps

---

## Helper Functions for Data Preparation

If your dataset only has `hour` (0-23), add these columns:

```python
import pandas as pd
import numpy as np

# Load your data
df = pd.read_csv('your_raw_data.csv')

# Extract hour from timestamp (if needed)
df['hour'] = pd.to_datetime(df['timestamp']).dt.hour

# Calculate sin/cos features
df['sin_hour'] = np.sin(2 * np.pi * df['hour'] / 24)
df['cos_hour'] = np.cos(2 * np.pi * df['hour'] / 24)

# Calculate time period
def get_time_period(hour):
    if 4 <= hour <= 6:
        return 0  # Early Morning
    elif 6 < hour <= 12:
        return 1  # Morning
    elif 12 < hour <= 16:
        return 2  # Afternoon
    elif 16 < hour <= 20:
        return 3  # Evening
    else:
        return 4  # Night

df['time_period'] = df['hour'].apply(get_time_period)

# Save processed data
df.to_csv('processed_data.csv', index=False)
```

---

## Manual Override Usage

The potentiometer controls real-time brightness adjustment:

| Potentiometer Position | Effect |
|------------------------|--------|
| **Left** | Dimmer (-100% to 0%) |
| **Center** | No change (0%) |
| **Right** | Brighter (0% to +100%) |

**Example:**
- ML predicts 60% brightness
- You turn pot 20% right
- Final brightness = 80%

---

## Troubleshooting Common Issues

**"RTC not found"**
→ Check I2C wiring (SDA/SCL) and connections

**"Brightness changes too fast"**
→ Increase `SMOOTHING_FACTOR` in Arduino sketch (0.7 → 0.85)

**"Test MAE too high (>10%)"**
→ Collect more training data or adjust tree depth

**"Night + high light not working"**
→ Data augmentation is automatic - check if `led_brightness` target matches expectations

---

## What's Next?

✅ **Working?** Great! Now you can:
- Collect real-world data and retrain
- Add WiFi control (ESP32)
- Connect to home automation (MQTT)
- Train separate models for different rooms

📚 **Need more details?** See:
- `README.md` - Complete documentation
- `arduino_deployment_guide.md` - Hardware details
- Code comments in `.py` and `.ino` files

🎉 **Enjoy your smart LED system!**
