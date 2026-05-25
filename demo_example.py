"""
Demo script showing how to use the LED Brightness Controller
with sample data generation
"""

import pandas as pd
import numpy as np
from led_brightness_model import LEDBrightnessController, calculate_time_features


def generate_sample_dataset(n_samples=1000):
    """
    Generate a sample dataset for demonstration
    
    Args:
        n_samples (int): Number of samples to generate
        
    Returns:
        pd.DataFrame: Sample dataset
    """
    print(f"Generating {n_samples} sample data points...\n")
    
    np.random.seed(42)
    data = []
    
    for _ in range(n_samples):
        # Random hour (0-23)
        hour = np.random.randint(0, 24)
        
        # Calculate time features
        sin_hour, cos_hour, time_period = calculate_time_features(hour)
        
        # Generate realistic ambient light based on time
        if time_period == 0:  # Early Morning (4-6)
            ambient_light = np.random.uniform(10, 100)
        elif time_period == 1:  # Morning (6-12)
            ambient_light = np.random.uniform(200, 600)
        elif time_period == 2:  # Afternoon (12-16)
            ambient_light = np.random.uniform(500, 1000)
        elif time_period == 3:  # Evening (16-20)
            ambient_light = np.random.uniform(100, 400)
        else:  # Night (20-4)
            ambient_light = np.random.uniform(0, 100)
        
        # Random motion detection
        motion_detected = np.random.choice([0, 1], p=[0.3, 0.7])
        
        # Calculate target LED brightness based on logical rules
        # Base brightness inverse to ambient light
        base_brightness = 100 - (ambient_light / 1000 * 100)
        
        # Boost if motion detected
        if motion_detected:
            base_brightness += 20
        
        # Time-based adjustments
        if time_period == 4:  # Night
            base_brightness += 10
        elif time_period == 2:  # Afternoon
            base_brightness -= 20
        
        # Clamp and add noise
        led_brightness = np.clip(base_brightness, 0, 100)
        led_brightness += np.random.uniform(-5, 5)  # Add noise
        led_brightness = np.clip(led_brightness, 0, 100)
        
        data.append({
            'timestamp': pd.Timestamp('2024-01-01') + pd.Timedelta(hours=hour, minutes=np.random.randint(0, 60)),
            'ambient_light': ambient_light,
            'motion_detected': motion_detected,
            'sin_hour': sin_hour,
            'cos_hour': cos_hour,
            'time_period': time_period,
            'led_brightness': led_brightness
        })
    
    df = pd.DataFrame(data)
    
    print("Sample data statistics:")
    print(df.describe())
    print("\nTime period distribution:")
    time_period_names = {0: 'Early Morning', 1: 'Morning', 2: 'Afternoon', 3: 'Evening', 4: 'Night'}
    for period in sorted(df['time_period'].unique()):
        count = (df['time_period'] == period).sum()
        print(f"  {time_period_names[period]}: {count} samples")
    
    return df


def demo_training():
    """Demonstrate model training"""
    print("="*70)
    print("DEMO: Training LED Brightness Control Model")
    print("="*70 + "\n")
    
    # Generate sample data
    df = generate_sample_dataset(n_samples=1000)
    
    # Save sample data for reference
    df.to_csv('sample_training_data.csv', index=False)
    print(f"\nSample data saved to: sample_training_data.csv\n")
    
    # Initialize and train controller
    controller = LEDBrightnessController()
    
    print("\n" + "="*70)
    print("Training Model...")
    print("="*70 + "\n")
    
    metrics = controller.train(df, max_depth=8, min_samples_split=15)
    
    # Save model
    controller.save_model('demo_led_model.pkl')
    
    return controller


def demo_prediction(controller):
    """Demonstrate predictions with manual override"""
    print("\n" + "="*70)
    print("DEMO: Predictions with Manual Override")
    print("="*70 + "\n")
    
    # Test scenario: Night time with high ambient light (synthetic scenario)
    print("Scenario 1: Night + High Ambient Light (Unusual case)")
    print("-" * 50)
    hour = 22  # 10 PM
    ambient_light = 800  # High lux (unusual for night)
    motion_detected = 1
    
    sin_hour, cos_hour, time_period = calculate_time_features(hour)
    
    # Prediction without manual override
    result = controller.predict(ambient_light, motion_detected, sin_hour, cos_hour, time_period)
    print(f"Input: Hour={hour}, Ambient={ambient_light} lux, Motion={motion_detected}")
    print(f"ML Prediction: {result['ml_predicted']}%")
    print(f"Manual Offset: {result['manual_offset']}%")
    print(f"Final Brightness: {result['final_brightness']}%")
    
    # Now with manual override (user wants dimmer)
    print("\n→ User adjusts: -20% override (wants dimmer)")
    controller.set_manual_offset(-20)
    result = controller.predict(ambient_light, motion_detected, sin_hour, cos_hour, time_period)
    print(f"ML Prediction: {result['ml_predicted']}%")
    print(f"Manual Offset: {result['manual_offset']}%")
    print(f"Final Brightness: {result['final_brightness']}%")
    
    # Reset override
    controller.reset_manual_offset()
    
    # Test scenario 2: Morning with medium light
    print("\n\nScenario 2: Morning + Medium Ambient Light")
    print("-" * 50)
    hour = 8
    ambient_light = 400
    motion_detected = 1
    
    sin_hour, cos_hour, time_period = calculate_time_features(hour)
    
    result = controller.predict(ambient_light, motion_detected, sin_hour, cos_hour, time_period)
    print(f"Input: Hour={hour}, Ambient={ambient_light} lux, Motion={motion_detected}")
    print(f"ML Prediction: {result['ml_predicted']}%")
    print(f"Final Brightness: {result['final_brightness']}%")
    
    # User wants brighter
    print("\n→ User adjusts: +30% override (wants brighter)")
    controller.set_manual_offset(30)
    result = controller.predict(ambient_light, motion_detected, sin_hour, cos_hour, time_period)
    print(f"Final Brightness: {result['final_brightness']}%")
    
    controller.reset_manual_offset()
    
    # Test scenario 3: Night with low light
    print("\n\nScenario 3: Night + Low Ambient Light + No Motion")
    print("-" * 50)
    hour = 2  # 2 AM
    ambient_light = 10
    motion_detected = 0
    
    sin_hour, cos_hour, time_period = calculate_time_features(hour)
    
    result = controller.predict(ambient_light, motion_detected, sin_hour, cos_hour, time_period)
    print(f"Input: Hour={hour} (2 AM), Ambient={ambient_light} lux, Motion={motion_detected}")
    print(f"ML Prediction: {result['ml_predicted']}%")
    print(f"Final Brightness: {result['final_brightness']}%")


def demo_load_and_predict():
    """Demonstrate loading a saved model and making predictions"""
    print("\n" + "="*70)
    print("DEMO: Loading Saved Model")
    print("="*70 + "\n")
    
    # Load the saved model
    controller = LEDBrightnessController(model_path='demo_led_model.pkl')
    
    # Make a prediction
    hour = 19  # 7 PM
    ambient_light = 150
    motion_detected = 1
    
    sin_hour, cos_hour, time_period = calculate_time_features(hour)
    
    result = controller.predict(ambient_light, motion_detected, sin_hour, cos_hour, time_period)
    
    print(f"Loaded model and made prediction:")
    print(f"Input: Hour={hour}, Ambient={ambient_light} lux, Motion={motion_detected}")
    print(f"Predicted Brightness: {result['final_brightness']}%")


def main():
    """Run all demos"""
    print("\n" + "="*70)
    print("LED BRIGHTNESS CONTROLLER - FULL DEMONSTRATION")
    print("="*70 + "\n")
    
    # Demo 1: Training
    controller = demo_training()
    
    # Demo 2: Predictions with manual override
    demo_prediction(controller)
    
    # Demo 3: Load and predict
    demo_load_and_predict()
    
    print("\n" + "="*70)
    print("DEMO COMPLETE!")
    print("="*70)
    print("\nFiles created:")
    print("  - sample_training_data.csv (sample dataset)")
    print("  - demo_led_model.pkl (trained model)")
    print("\nNext steps:")
    print("  1. Replace sample data with your actual dataset")
    print("  2. Run: python train_model.py --data your_data.csv --output led_model.pkl")
    print("  3. Deploy model to your microcontroller")
    print("  4. Use manual override via potentiometer for real-time adjustments")


if __name__ == "__main__":
    main()
