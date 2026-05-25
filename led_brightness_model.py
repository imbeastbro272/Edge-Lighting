"""
LED Brightness Control using Decision Tree with Manual Override
Features: RTC timestamp, PIR motion, LDR ambient light, time patterns (sin/cos hour)
"""

import pandas as pd
import numpy as np
from sklearn.tree import DecisionTreeRegressor
from sklearn.model_selection import train_test_split, cross_val_score
from sklearn.metrics import mean_absolute_error, mean_squared_error, r2_score
import joblib
import warnings
warnings.filterwarnings('ignore')


class LEDBrightnessController:
    """
    LED Brightness Controller with ML-based prediction and manual override
    """
    
    def __init__(self, model_path=None):
        """
        Initialize the controller
        
        Args:
            model_path (str): Path to pre-trained model file
        """
        self.model = None
        self.feature_columns = ['ambient_light', 'motion_detected', 'sin_hour', 'cos_hour', 'time_period']
        self.manual_offset = 0  # Range: -100 to +100
        
        if model_path:
            self.load_model(model_path)
    
    def augment_data(self, df):
        """
        Augment dataset with synthetic data for missing scenarios
        Specifically: Night time + High ambient light -> Low LED brightness
        
        Args:
            df (pd.DataFrame): Original dataset
            
        Returns:
            pd.DataFrame: Augmented dataset
        """
        print("Augmenting data for missing scenarios...")
        
        # Create synthetic data for night + high ambient light scenario
        synthetic_samples = []
        
        # Night time period code (you'll need to adjust based on your encoding)
        # Assuming time_period is encoded: 0=Early Morning, 1=Morning, 2=Afternoon, 3=Evening, 4=Night
        night_period = 4
        
        # Generate synthetic samples
        num_synthetic = int(len(df) * 0.1)  # 10% synthetic data
        
        for _ in range(num_synthetic):
            # Night hours: 20:01 - 4:00 (in 24h: 20-23 and 0-4)
            night_hour = np.random.choice(list(range(21, 24)) + list(range(0, 5)))
            
            # High ambient light (assuming your LDR values range, adjust as needed)
            high_ambient = np.random.uniform(500, 1000)  # Adjust range based on your LDR sensor
            
            # Motion can be random
            motion = np.random.choice([0, 1], p=[0.3, 0.7])  # More likely to have motion
            
            # Calculate sin/cos for hour
            sin_hour = np.sin(2 * np.pi * night_hour / 24)
            cos_hour = np.cos(2 * np.pi * night_hour / 24)
            
            # Low brightness for night + high ambient light
            # Logic: If ambient light is already high at night, LED should be dim
            led_brightness = np.random.uniform(0, 30)  # 0-30% brightness
            
            synthetic_samples.append({
                'ambient_light': high_ambient,
                'motion_detected': motion,
                'sin_hour': sin_hour,
                'cos_hour': cos_hour,
                'time_period': night_period,
                'led_brightness': led_brightness,
                'synthetic': True
            })
        
        # Create DataFrame from synthetic samples
        synthetic_df = pd.DataFrame(synthetic_samples)
        
        # Mark original data
        df['synthetic'] = False
        
        # Combine original and synthetic data
        augmented_df = pd.concat([df, synthetic_df], ignore_index=True)
        
        print(f"Added {num_synthetic} synthetic samples")
        print(f"Total samples: {len(augmented_df)}")
        
        return augmented_df
    
    def prepare_features(self, df):
        """
        Prepare features for training/prediction
        
        Args:
            df (pd.DataFrame): Input dataframe
            
        Returns:
            X (pd.DataFrame): Features
            y (pd.Series): Target (if present)
        """
        X = df[self.feature_columns].copy()
        y = df['led_brightness'] if 'led_brightness' in df.columns else None
        
        return X, y
    
    def train(self, df, test_size=0.2, random_state=42, max_depth=10, min_samples_split=10):
        """
        Train the Decision Tree model
        
        Args:
            df (pd.DataFrame): Training dataset
            test_size (float): Proportion of test set
            random_state (int): Random seed
            max_depth (int): Maximum depth of tree
            min_samples_split (int): Minimum samples to split a node
            
        Returns:
            dict: Training metrics
        """
        print("Training Decision Tree model...")
        
        # Augment data
        df_augmented = self.augment_data(df)
        
        # Prepare features
        X, y = self.prepare_features(df_augmented)
        
        # Split data
        X_train, X_test, y_train, y_test = train_test_split(
            X, y, test_size=test_size, random_state=random_state
        )
        
        # Train Decision Tree
        self.model = DecisionTreeRegressor(
            max_depth=max_depth,
            min_samples_split=min_samples_split,
            min_samples_leaf=5,
            random_state=random_state
        )
        
        self.model.fit(X_train, y_train)
        
        # Evaluate
        y_pred_train = self.model.predict(X_train)
        y_pred_test = self.model.predict(X_test)
        
        metrics = {
            'train_mae': mean_absolute_error(y_train, y_pred_train),
            'test_mae': mean_absolute_error(y_test, y_pred_test),
            'train_rmse': np.sqrt(mean_squared_error(y_train, y_pred_train)),
            'test_rmse': np.sqrt(mean_squared_error(y_test, y_pred_test)),
            'train_r2': r2_score(y_train, y_pred_train),
            'test_r2': r2_score(y_test, y_pred_test),
        }
        
        # Cross-validation
        cv_scores = cross_val_score(self.model, X, y, cv=5, scoring='neg_mean_absolute_error')
        metrics['cv_mae'] = -cv_scores.mean()
        metrics['cv_mae_std'] = cv_scores.std()
        
        print("\n=== Training Metrics ===")
        print(f"Train MAE: {metrics['train_mae']:.2f}%")
        print(f"Test MAE: {metrics['test_mae']:.2f}%")
        print(f"Train RMSE: {metrics['train_rmse']:.2f}%")
        print(f"Test RMSE: {metrics['test_rmse']:.2f}%")
        print(f"Train R²: {metrics['train_r2']:.4f}")
        print(f"Test R²: {metrics['test_r2']:.4f}")
        print(f"Cross-Val MAE: {metrics['cv_mae']:.2f}% (+/- {metrics['cv_mae_std']:.2f})")
        
        # Feature importance
        feature_importance = pd.DataFrame({
            'feature': self.feature_columns,
            'importance': self.model.feature_importances_
        }).sort_values('importance', ascending=False)
        
        print("\n=== Feature Importance ===")
        print(feature_importance.to_string(index=False))
        
        return metrics
    
    def predict(self, ambient_light, motion_detected, sin_hour, cos_hour, time_period):
        """
        Predict LED brightness with manual override
        
        Args:
            ambient_light (float): LDR reading (lux)
            motion_detected (int): 0 or 1
            sin_hour (float): Sin component of hour
            cos_hour (float): Cos component of hour
            time_period (int): Time period code (0-4)
            
        Returns:
            dict: Prediction results with override
        """
        if self.model is None:
            raise ValueError("Model not trained or loaded. Train or load a model first.")
        
        # Prepare input
        input_data = pd.DataFrame([{
            'ambient_light': ambient_light,
            'motion_detected': motion_detected,
            'sin_hour': sin_hour,
            'cos_hour': cos_hour,
            'time_period': time_period
        }])
        
        # ML prediction
        ml_brightness = self.model.predict(input_data)[0]
        
        # Apply manual override (additive offset)
        final_brightness = ml_brightness + self.manual_offset
        
        # Clamp to valid range [0, 100]
        final_brightness = np.clip(final_brightness, 0, 100)
        
        return {
            'ml_predicted': round(ml_brightness, 2),
            'manual_offset': self.manual_offset,
            'final_brightness': round(final_brightness, 2)
        }
    
    def set_manual_offset(self, offset):
        """
        Set manual override offset
        
        Args:
            offset (float): Offset value (-100 to +100)
        """
        self.manual_offset = np.clip(offset, -100, 100)
        print(f"Manual offset set to: {self.manual_offset}%")
    
    def reset_manual_offset(self):
        """Reset manual override to 0"""
        self.manual_offset = 0
        print("Manual offset reset to 0%")
    
    def save_model(self, filepath='led_model.pkl'):
        """
        Save trained model to file
        
        Args:
            filepath (str): Path to save model
        """
        if self.model is None:
            raise ValueError("No model to save")
        
        model_data = {
            'model': self.model,
            'feature_columns': self.feature_columns
        }
        
        joblib.dump(model_data, filepath)
        print(f"Model saved to {filepath}")
    
    def load_model(self, filepath):
        """
        Load trained model from file
        
        Args:
            filepath (str): Path to model file
        """
        model_data = joblib.load(filepath)
        self.model = model_data['model']
        self.feature_columns = model_data['feature_columns']
        print(f"Model loaded from {filepath}")


def calculate_time_features(hour):
    """
    Calculate sin/cos hour features and time period from hour
    
    Args:
        hour (int): Hour in 24h format (0-23)
        
    Returns:
        tuple: (sin_hour, cos_hour, time_period)
    """
    sin_hour = np.sin(2 * np.pi * hour / 24)
    cos_hour = np.cos(2 * np.pi * hour / 24)
    
    # Determine time period
    if 4 <= hour <= 6:
        time_period = 0  # Early Morning
    elif 6 < hour <= 12:
        time_period = 1  # Morning
    elif 12 < hour <= 16:
        time_period = 2  # Afternoon
    elif 16 < hour <= 20:
        time_period = 3  # Evening
    else:  # 20 < hour or hour < 4
        time_period = 4  # Night
    
    return sin_hour, cos_hour, time_period


if __name__ == "__main__":
    # Example usage
    print("LED Brightness Controller - Example Usage\n")
    
    # Note: Replace with your actual dataset
    # Expected columns: timestamp, ambient_light, motion_detected, sin_hour, cos_hour, time_period, led_brightness
    
    print("To use this controller:")
    print("1. Load your dataset with required columns")
    print("2. Initialize controller: controller = LEDBrightnessController()")
    print("3. Train model: controller.train(df)")
    print("4. Make predictions with manual override")
    print("5. Save/load model for deployment")
