# LED Brightness Control with ML Decision Tree

An intelligent LED brightness control system using Machine Learning (Decision Tree) with manual override capability. The system learns from patterns and autonomously adjusts LED brightness based on time of day, ambient light, and motion detection, while allowing users to manually fine-tune via a potentiometer.

## 🎯 Features

- **ML-Based Brightness Control**: Decision Tree model predicts optimal LED brightness
- **Manual Override**: Potentiometer-based additive/subtractive offset (-100% to +100%)
- **Synthetic Data Augmentation**: Handles missing scenarios (e.g., night + high ambient light)
- **Multiple Inputs**: RTC, PIR motion sensor, LDR light sensor, potentiometer
- **Time-Aware Features**: Normalized sin/cos hour representation + time period classification
- **Arduino/ESP32 Deployment**: Converts trained model to embedded C code
- **Real-Time Control**: Updates every second with smooth transitions

## 📋 System Requirements

### Python Environment (Training)
- Python 3.7+
- Dependencies: See `requirements.txt`

### Hardware (Deployment)
- **Microcontroller**: Arduino Uno/Mega, ESP32, or ESP8266
- **RTC Module**: DS3231 or compatible
- **Motion Sensor**: HC-SR501 PIR sensor
- **Light Sensor**: LDR (Light Dependent Resistor) + 10kΩ resistor
- **Manual Control**: 10kΩ potentiometer
- **LED**: PWM-capable LED or LED strip
- **Miscellaneous**: Breadboard, jumper wires, power supply

## 🚀 Quick Start

### 1. Install Python Dependencies

```bash
pip install -r requirements.txt
```

### 2. Prepare Your Dataset

Your CSV file should contain the following columns:
- `timestamp`: Date/time from RTC
- `ambient_light`: LDR reading (lux)
- `motion_detected`: PIR sensor (0 or 1)
- `sin_hour`: sin(2π × hour / 24)
- `cos_hour`: cos(2π × hour / 24)
- `time_period`: Time classification (0-4)
  - 0: Early Morning (4:01 - 6:00)
  - 1: Morning (6:01 - 12:00)
  - 2: Afternoon (12:01 - 16:00)
  - 3: Evening (16:01 - 20:00)
  - 4: Night (20:01 - 4:00)
- `led_brightness`: Target brightness (0-100%)

**Note**: If you don't have sin_hour, cos_hour, or time_period columns, see the helper functions in `led_brightness_model.py` to calculate them from hour.

### 3. Train the Model

#### Option A: Use the demo (with sample data)
```bash
python demo_example.py
```

This generates sample data and trains a demo model.

#### Option B: Train with your actual dataset
```bash
python train_model.py --data your_dataset.csv --output led_model.pkl
```

**Optional parameters**:
- `--max-depth`: Maximum tree depth (default: 10)
- `--min-samples-split`: Minimum samples to split (default: 10)

### 4. Export Model to Arduino

```bash
python export_tree_rules.py --model led_model.pkl --output tree_rules.h
```

This generates:
- `tree_rules.h`: Decision tree implementation in C
- `test_cases.h`: Validation test cases

### 5. Deploy to Arduino

1. Copy `tree_rules.h` and `test_cases.h` to your Arduino sketch folder
2. Open `led_controller.ino` in Arduino IDE
3. Install required libraries:
   - RTClib (by Adafruit)
4. Wire up the hardware (see pin configuration below)
5. Upload to your Arduino/ESP32

## 🔌 Hardware Wiring

### Arduino Uno/Mega

```
Component          Arduino Pin
─────────────────────────────
RTC DS3231
  SDA    ────────→    A4
  SCL    ────────→    A5
  VCC    ────────→    5V
  GND    ────────→    GND

PIR HC-SR501
  OUT    ────────→    D2
  VCC    ────────→    5V
  GND    ────────→    GND

LDR Circuit (Voltage Divider)
  LDR    ────────→    5V
  LDR    ────┬───→    A0
  10kΩ   ────┴───→    GND

Potentiometer (Manual Override)
  VCC    ────────→    5V
  Wiper  ────────→    A1
  GND    ────────→    GND

LED (PWM)
  Anode  ────────→    D9 (through resistor/driver)
  Cathode ───────→    GND
```

### ESP32 (Adjust pins as needed)

```
Component          ESP32 Pin
─────────────────────────────
RTC DS3231
  SDA    ────────→    GPIO21
  SCL    ────────→    GPIO22
  VCC    ────────→    3.3V
  GND    ────────→    GND

PIR HC-SR501
  OUT    ────────→    GPIO4
  VCC    ────────→    5V
  GND    ────────→    GND

LDR Circuit
  LDR + 10kΩ ────→    GPIO34 (ADC1_CH6)

Potentiometer
  Wiper  ────────→    GPIO35 (ADC1_CH7)

LED (PWM)
  Anode  ────────→    GPIO25 (LEDC channel)
```

## 📊 Data Augmentation for Missing Scenarios

The model automatically augments your training data to handle the scenario where:
- **Time**: Night (20:01 - 4:00)
- **Ambient Light**: High (unusual for night, e.g., artificial lights on)
- **Expected Behavior**: Low LED brightness (since ambient is already bright)

This ensures the model handles edge cases not present in your original dataset.

You can adjust the augmentation parameters in `led_brightness_model.py`:
```python
num_synthetic = int(len(df) * 0.1)  # 10% synthetic data
high_ambient = np.random.uniform(500, 1000)  # High lux range
led_brightness = np.random.uniform(0, 30)  # Low brightness target
```

## 🎛️ Manual Override Usage

The potentiometer provides real-time brightness adjustment:

- **Center Position**: No offset (pure ML prediction)
- **Turn Left**: Negative offset (dimmer, -100% max)
- **Turn Right**: Positive offset (brighter, +100% max)

**Final Brightness** = ML Prediction + Manual Offset (clamped to 0-100%)

Example:
- ML predicts 60%
- User sets +20% offset via pot
- Final brightness = 80%

## 📈 Model Performance

After training, you'll see metrics like:
```
=== Training Metrics ===
Test MAE: 5.23%
Test RMSE: 7.45%
Test R²: 0.9234
Cross-Val MAE: 5.67% (+/- 1.23)

=== Feature Importance ===
ambient_light    0.4523
time_period      0.2891
sin_hour         0.1234
motion_detected  0.0987
cos_hour         0.0365
```

**Interpretation**:
- MAE (Mean Absolute Error): Average prediction error in %
- R² (R-squared): Model fit quality (higher is better, max 1.0)
- Feature Importance: Which inputs matter most

## 🧪 Testing & Validation

### Python Testing
```python
from led_brightness_model import LEDBrightnessController

# Load trained model
controller = LEDBrightnessController(model_path='led_model.pkl')

# Test prediction
result = controller.predict(
    ambient_light=800,    # High ambient light
    motion_detected=1,
    sin_hour=-0.5,       # ~22:00 (10 PM)
    cos_hour=-0.866,
    time_period=4        # Night
)

print(f"Predicted brightness: {result['final_brightness']}%")
```

### Arduino Validation

The Arduino sketch includes automatic validation on startup:
```
=== Model Validation ===
Test 1: Hour=22 Expected=25.34% Predicted=25.34% Error=0.0000 PASS
Test 2: Hour=8 Expected=62.18% Predicted=62.18% Error=0.0000 PASS
...
✓ ALL TESTS PASSED
```

## 🔧 Calibration

### LDR Calibration
1. Measure actual lux with a light meter
2. Read ADC values from LDR
3. Adjust conversion formula in `readLDR()` function:
   ```cpp
   float lux = voltage * CALIBRATION_FACTOR;
   ```

### Potentiometer Dead Zone
Adjust center dead zone to reduce jitter:
```cpp
#define POT_DEADZONE 20  // Increase if pot is too sensitive
```

### PWM Gamma Correction (Optional)
For perceptual brightness linearity:
```cpp
float gamma = 2.2;
int pwm_value = pow(brightness / 100.0, gamma) * 255;
```

## 📁 Project Structure

```
Edge-Lighting/
├── README.md                       # This file
├── requirements.txt                # Python dependencies
├── led_brightness_model.py         # Main ML controller class
├── train_model.py                  # Training script
├── demo_example.py                 # Demo with sample data
├── export_tree_rules.py            # Model to C converter
├── arduino_deployment_guide.md     # Detailed Arduino guide
├── led_controller.ino              # Arduino sketch
├── tree_rules.h                    # Generated (after export)
└── test_cases.h                    # Generated (after export)
```

## 🔍 Troubleshooting

### Issue: Model predictions differ between Python and Arduino
**Solution**: Check floating-point precision and sensor calibration. Use validation tests.

### Issue: Erratic brightness changes
**Solution**: Increase `SMOOTHING_FACTOR` (0.7 → 0.85) for more smoothing.

### Issue: RTC not found
**Solution**: Check I2C wiring (SDA/SCL) and pull-up resistors.

### Issue: Potentiometer too sensitive
**Solution**: Increase `POT_DEADZONE` value.

### Issue: Night + high ambient scenario not working
**Solution**: Check if data augmentation is enabled. Verify `night_period = 4` matches your encoding.

## 🎓 Advanced Usage

### Custom Time Periods
Modify the time period definitions in both Python and Arduino:
```python
# Python
def calculate_time_features(hour):
    if 5 <= hour < 9:
        time_period = 0  # Custom: Dawn
    # ... your definitions
```

```cpp
// Arduino
int getTimePeriod(int hour) {
    if (hour >= 5 && hour < 9) {
        return 0;  // Custom: Dawn
    }
    // ... your definitions
}
```

### Multiple LED Zones
Train separate models for different areas:
```bash
python train_model.py --data bedroom_data.csv --output bedroom_model.pkl
python train_model.py --data kitchen_data.csv --output kitchen_model.pkl
```

### WiFi Control (ESP32)
Extend the sketch with web server or MQTT for remote override:
```cpp
#include <WiFi.h>
#include <WebServer.h>

// Expose manual_offset via HTTP endpoint
server.on("/brightness", HTTP_POST, handleBrightnessChange);
```

## 📝 License

This project is open source. Feel free to use and modify for your projects.

## 🤝 Contributing

Contributions welcome! Areas for improvement:
- Additional sensor support (temperature, humidity)
- LSTM/RNN for temporal patterns
- Mobile app for remote control
- Energy consumption optimization

## 📧 Support

For issues or questions:
1. Check the troubleshooting section
2. Review `arduino_deployment_guide.md`
3. Open an issue with:
   - Hardware setup details
   - Training metrics
   - Error messages/logs

## 🎉 Acknowledgments

- scikit-learn for the Decision Tree implementation
- Adafruit for the RTClib library
- Arduino/ESP32 community for hardware support

---

**Happy building! 💡**
