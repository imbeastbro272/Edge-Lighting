"""
Visualization tools for LED Brightness Control Model
Generates plots to understand data and model behavior
"""

import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import seaborn as sns
from sklearn.tree import plot_tree
import joblib
import argparse


def plot_data_distribution(df, save_path='data_distribution.png'):
    """
    Plot data distribution analysis
    
    Args:
        df: Training dataset
        save_path: Path to save figure
    """
    fig, axes = plt.subplots(2, 3, figsize=(15, 10))
    fig.suptitle('Training Data Distribution Analysis', fontsize=16, fontweight='bold')
    
    # 1. LED Brightness distribution
    axes[0, 0].hist(df['led_brightness'], bins=30, color='skyblue', edgecolor='black', alpha=0.7)
    axes[0, 0].set_xlabel('LED Brightness (%)')
    axes[0, 0].set_ylabel('Frequency')
    axes[0, 0].set_title('Target Brightness Distribution')
    axes[0, 0].axvline(df['led_brightness'].mean(), color='red', linestyle='--', 
                       label=f'Mean: {df["led_brightness"].mean():.1f}%')
    axes[0, 0].legend()
    
    # 2. Ambient Light distribution
    axes[0, 1].hist(df['ambient_light'], bins=30, color='orange', edgecolor='black', alpha=0.7)
    axes[0, 1].set_xlabel('Ambient Light (lux)')
    axes[0, 1].set_ylabel('Frequency')
    axes[0, 1].set_title('Ambient Light Distribution')
    axes[0, 1].axvline(df['ambient_light'].mean(), color='red', linestyle='--',
                       label=f'Mean: {df["ambient_light"].mean():.1f}')
    axes[0, 1].legend()
    
    # 3. Time Period distribution
    time_period_names = {0: 'Early Morn', 1: 'Morning', 2: 'Afternoon', 3: 'Evening', 4: 'Night'}
    period_counts = df['time_period'].value_counts().sort_index()
    period_labels = [time_period_names.get(i, str(i)) for i in period_counts.index]
    
    axes[0, 2].bar(range(len(period_counts)), period_counts.values, 
                   color=['#FF6B6B', '#FFA500', '#FFD93D', '#95E1D3', '#4A90E2'])
    axes[0, 2].set_xticks(range(len(period_counts)))
    axes[0, 2].set_xticklabels(period_labels, rotation=45, ha='right')
    axes[0, 2].set_ylabel('Count')
    axes[0, 2].set_title('Time Period Distribution')
    
    # 4. Motion Detection distribution
    motion_counts = df['motion_detected'].value_counts()
    axes[1, 0].bar(['No Motion', 'Motion'], 
                   [motion_counts.get(0, 0), motion_counts.get(1, 0)],
                   color=['#E74C3C', '#2ECC71'])
    axes[1, 0].set_ylabel('Count')
    axes[1, 0].set_title('Motion Detection Distribution')
    for i, v in enumerate([motion_counts.get(0, 0), motion_counts.get(1, 0)]):
        axes[1, 0].text(i, v + 10, str(v), ha='center', fontweight='bold')
    
    # 5. Brightness vs Ambient Light scatter
    scatter = axes[1, 1].scatter(df['ambient_light'], df['led_brightness'], 
                                 c=df['time_period'], cmap='viridis', alpha=0.5, s=20)
    axes[1, 1].set_xlabel('Ambient Light (lux)')
    axes[1, 1].set_ylabel('LED Brightness (%)')
    axes[1, 1].set_title('Brightness vs Ambient Light (colored by time period)')
    plt.colorbar(scatter, ax=axes[1, 1], label='Time Period')
    
    # 6. Hourly pattern
    if 'timestamp' in df.columns:
        df['hour'] = pd.to_datetime(df['timestamp']).dt.hour
    elif 'sin_hour' in df.columns and 'cos_hour' in df.columns:
        # Reconstruct hour from sin/cos
        df['hour'] = (np.arctan2(df['sin_hour'], df['cos_hour']) * 24 / (2 * np.pi)) % 24
        df['hour'] = df['hour'].round().astype(int)
    
    if 'hour' in df.columns:
        hourly_avg = df.groupby('hour')['led_brightness'].mean()
        axes[1, 2].plot(hourly_avg.index, hourly_avg.values, marker='o', linewidth=2, markersize=6)
        axes[1, 2].fill_between(hourly_avg.index, hourly_avg.values, alpha=0.3)
        axes[1, 2].set_xlabel('Hour of Day')
        axes[1, 2].set_ylabel('Average LED Brightness (%)')
        axes[1, 2].set_title('Average Brightness by Hour')
        axes[1, 2].set_xticks(range(0, 24, 3))
        axes[1, 2].grid(True, alpha=0.3)
    else:
        axes[1, 2].text(0.5, 0.5, 'Hour data not available', 
                        ha='center', va='center', transform=axes[1, 2].transAxes)
        axes[1, 2].set_title('Hourly Pattern')
    
    plt.tight_layout()
    plt.savefig(save_path, dpi=300, bbox_inches='tight')
    print(f"✓ Data distribution plot saved to {save_path}")
    plt.close()


def plot_feature_importance(model, feature_names, save_path='feature_importance.png'):
    """
    Plot feature importance
    
    Args:
        model: Trained model
        feature_names: List of feature names
        save_path: Path to save figure
    """
    importances = model.feature_importances_
    indices = np.argsort(importances)[::-1]
    
    plt.figure(figsize=(10, 6))
    plt.title('Feature Importance', fontsize=16, fontweight='bold')
    plt.bar(range(len(importances)), importances[indices], 
            color='steelblue', edgecolor='black', alpha=0.7)
    plt.xticks(range(len(importances)), 
               [feature_names[i] for i in indices], 
               rotation=45, ha='right')
    plt.ylabel('Importance')
    plt.xlabel('Feature')
    
    # Add value labels on bars
    for i, v in enumerate(importances[indices]):
        plt.text(i, v + 0.01, f'{v:.3f}', ha='center', fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(save_path, dpi=300, bbox_inches='tight')
    print(f"✓ Feature importance plot saved to {save_path}")
    plt.close()


def plot_decision_tree(model, feature_names, save_path='decision_tree.png', max_depth=3):
    """
    Plot decision tree structure (limited depth for readability)
    
    Args:
        model: Trained model
        feature_names: List of feature names
        save_path: Path to save figure
        max_depth: Maximum depth to display
    """
    plt.figure(figsize=(20, 10))
    plot_tree(model, 
              feature_names=feature_names,
              filled=True, 
              rounded=True,
              fontsize=10,
              max_depth=max_depth)
    plt.title(f'Decision Tree Structure (max depth={max_depth} shown)', 
              fontsize=16, fontweight='bold', pad=20)
    plt.tight_layout()
    plt.savefig(save_path, dpi=300, bbox_inches='tight')
    print(f"✓ Decision tree plot saved to {save_path}")
    print(f"  Note: Only showing top {max_depth} levels for readability")
    print(f"  Actual tree depth: {model.get_depth()}")
    plt.close()


def plot_prediction_analysis(model, df, feature_columns, save_path='prediction_analysis.png'):
    """
    Plot prediction analysis comparing actual vs predicted
    
    Args:
        model: Trained model
        df: Dataset
        feature_columns: List of feature column names
        save_path: Path to save figure
    """
    X = df[feature_columns]
    y_true = df['led_brightness']
    y_pred = model.predict(X)
    
    residuals = y_true - y_pred
    
    fig, axes = plt.subplots(2, 2, figsize=(14, 12))
    fig.suptitle('Model Prediction Analysis', fontsize=16, fontweight='bold')
    
    # 1. Actual vs Predicted scatter
    axes[0, 0].scatter(y_true, y_pred, alpha=0.5, s=20)
    axes[0, 0].plot([0, 100], [0, 100], 'r--', linewidth=2, label='Perfect prediction')
    axes[0, 0].set_xlabel('Actual Brightness (%)')
    axes[0, 0].set_ylabel('Predicted Brightness (%)')
    axes[0, 0].set_title('Actual vs Predicted')
    axes[0, 0].legend()
    axes[0, 0].grid(True, alpha=0.3)
    
    # 2. Residuals distribution
    axes[0, 1].hist(residuals, bins=50, color='coral', edgecolor='black', alpha=0.7)
    axes[0, 1].axvline(0, color='red', linestyle='--', linewidth=2, label='Zero error')
    axes[0, 1].set_xlabel('Residual (Actual - Predicted) %')
    axes[0, 1].set_ylabel('Frequency')
    axes[0, 1].set_title(f'Residuals Distribution (Mean: {residuals.mean():.2f}%)')
    axes[0, 1].legend()
    
    # 3. Residuals vs Predicted
    axes[1, 0].scatter(y_pred, residuals, alpha=0.5, s=20)
    axes[1, 0].axhline(0, color='red', linestyle='--', linewidth=2)
    axes[1, 0].set_xlabel('Predicted Brightness (%)')
    axes[1, 0].set_ylabel('Residual (%)')
    axes[1, 0].set_title('Residuals vs Predicted')
    axes[1, 0].grid(True, alpha=0.3)
    
    # 4. Error by time period
    df_temp = df.copy()
    df_temp['abs_error'] = np.abs(residuals)
    
    time_period_names = {0: 'Early\nMorn', 1: 'Morning', 2: 'Afternoon', 
                        3: 'Evening', 4: 'Night'}
    period_errors = df_temp.groupby('time_period')['abs_error'].mean().sort_index()
    period_labels = [time_period_names.get(i, str(i)) for i in period_errors.index]
    
    axes[1, 1].bar(range(len(period_errors)), period_errors.values,
                   color=['#FF6B6B', '#FFA500', '#FFD93D', '#95E1D3', '#4A90E2'])
    axes[1, 1].set_xticks(range(len(period_errors)))
    axes[1, 1].set_xticklabels(period_labels)
    axes[1, 1].set_ylabel('Mean Absolute Error (%)')
    axes[1, 1].set_title('Prediction Error by Time Period')
    
    for i, v in enumerate(period_errors.values):
        axes[1, 1].text(i, v + 0.5, f'{v:.2f}', ha='center', fontweight='bold')
    
    plt.tight_layout()
    plt.savefig(save_path, dpi=300, bbox_inches='tight')
    print(f"✓ Prediction analysis plot saved to {save_path}")
    plt.close()


def plot_scenario_heatmap(model, feature_columns, save_path='scenario_heatmap.png'):
    """
    Plot heatmap showing model predictions across different scenarios
    
    Args:
        model: Trained model
        feature_columns: List of feature column names
        save_path: Path to save figure
    """
    # Create grid of scenarios
    ambient_range = np.linspace(0, 1000, 20)
    hours = range(24)
    
    # Initialize results for different scenarios
    results_no_motion = np.zeros((len(ambient_range), len(hours)))
    results_motion = np.zeros((len(ambient_range), len(hours)))
    
    for i, ambient in enumerate(ambient_range):
        for j, hour in enumerate(hours):
            sin_hour = np.sin(2 * np.pi * hour / 24)
            cos_hour = np.cos(2 * np.pi * hour / 24)
            
            # Determine time period
            if 4 <= hour <= 6:
                time_period = 0
            elif 6 < hour <= 12:
                time_period = 1
            elif 12 < hour <= 16:
                time_period = 2
            elif 16 < hour <= 20:
                time_period = 3
            else:
                time_period = 4
            
            # Predict for no motion
            input_data_no = pd.DataFrame([{
                'ambient_light': ambient,
                'motion_detected': 0,
                'sin_hour': sin_hour,
                'cos_hour': cos_hour,
                'time_period': time_period
            }])
            results_no_motion[i, j] = model.predict(input_data_no)[0]
            
            # Predict for motion
            input_data_motion = pd.DataFrame([{
                'ambient_light': ambient,
                'motion_detected': 1,
                'sin_hour': sin_hour,
                'cos_hour': cos_hour,
                'time_period': time_period
            }])
            results_motion[i, j] = model.predict(input_data_motion)[0]
    
    # Plot heatmaps
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(18, 6))
    fig.suptitle('LED Brightness Predictions Across Scenarios', fontsize=16, fontweight='bold')
    
    # No motion
    im1 = ax1.imshow(results_no_motion, aspect='auto', cmap='YlOrRd', origin='lower',
                     extent=[0, 24, 0, 1000])
    ax1.set_xlabel('Hour of Day')
    ax1.set_ylabel('Ambient Light (lux)')
    ax1.set_title('No Motion Detected')
    ax1.set_xticks(range(0, 25, 3))
    plt.colorbar(im1, ax=ax1, label='Predicted Brightness (%)')
    
    # Add time period boundaries
    for hour in [4, 6, 12, 16, 20]:
        ax1.axvline(hour, color='white', linestyle='--', alpha=0.5, linewidth=1)
    
    # With motion
    im2 = ax2.imshow(results_motion, aspect='auto', cmap='YlOrRd', origin='lower',
                     extent=[0, 24, 0, 1000])
    ax2.set_xlabel('Hour of Day')
    ax2.set_ylabel('Ambient Light (lux)')
    ax2.set_title('Motion Detected')
    ax2.set_xticks(range(0, 25, 3))
    plt.colorbar(im2, ax=ax2, label='Predicted Brightness (%)')
    
    # Add time period boundaries
    for hour in [4, 6, 12, 16, 20]:
        ax2.axvline(hour, color='white', linestyle='--', alpha=0.5, linewidth=1)
    
    plt.tight_layout()
    plt.savefig(save_path, dpi=300, bbox_inches='tight')
    print(f"✓ Scenario heatmap saved to {save_path}")
    plt.close()


def main():
    parser = argparse.ArgumentParser(description='Visualize LED Brightness Model')
    parser.add_argument('--data', type=str, required=True, help='Path to dataset CSV')
    parser.add_argument('--model', type=str, required=True, help='Path to trained model (.pkl)')
    parser.add_argument('--output-dir', type=str, default='.', help='Output directory for plots')
    
    args = parser.parse_args()
    
    print("="*70)
    print("LED Brightness Model Visualization")
    print("="*70 + "\n")
    
    # Load data
    print(f"Loading dataset from {args.data}...")
    df = pd.read_csv(args.data)
    print(f"Dataset shape: {df.shape}\n")
    
    # Load model
    print(f"Loading model from {args.model}...")
    model_data = joblib.load(args.model)
    model = model_data['model']
    feature_columns = model_data['feature_columns']
    print(f"Model loaded. Tree depth: {model.get_depth()}\n")
    
    # Generate visualizations
    print("Generating visualizations...\n")
    
    plot_data_distribution(df, f'{args.output_dir}/data_distribution.png')
    plot_feature_importance(model, feature_columns, f'{args.output_dir}/feature_importance.png')
    plot_decision_tree(model, feature_columns, f'{args.output_dir}/decision_tree.png')
    plot_prediction_analysis(model, df, feature_columns, f'{args.output_dir}/prediction_analysis.png')
    plot_scenario_heatmap(model, feature_columns, f'{args.output_dir}/scenario_heatmap.png')
    
    print("\n" + "="*70)
    print("VISUALIZATION COMPLETE!")
    print("="*70)
    print(f"\nGenerated plots in {args.output_dir}/:")
    print("  - data_distribution.png")
    print("  - feature_importance.png")
    print("  - decision_tree.png")
    print("  - prediction_analysis.png")
    print("  - scenario_heatmap.png")


if __name__ == "__main__":
    main()
