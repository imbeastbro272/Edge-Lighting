# Online Learning Changes - Potentiometer-Based Retraining

## Overview
Modified `led_controller_online_learning.ino` to implement intelligent potentiometer change tracking with scheduled and threshold-based model retraining.

## Key Changes

### 1. **Potentiometer Change Detection**
- **Stability Requirement**: Potentiometer value must remain stable for **5 seconds** before a change is recorded
- **Change Threshold**: Only significant changes (> `POT_STABILITY_THRESHOLD`) are tracked
- **Prevents False Triggers**: Avoids recording temporary adjustments or noise

### 2. **Daily Change Counter**
- Tracks number of potentiometer changes within each 24-hour period
- Resets automatically at midnight (00:00:00)
- Displayed in stats command

### 3. **Dual Retraining Schedule**

#### A. Nightly Retraining (Scheduled)
- **Time**: 00:00:01 every day
- **Purpose**: Regular model updates using accumulated daily learning data
- **Automatic**: Runs once per day without manual intervention

#### B. Threshold-Based Retraining (On-Demand)
- **Trigger**: After **20 changes** recorded within 24 hours
- **Purpose**: Immediate model adaptation when user frequently adjusts preferences
- **Continues**: System continues recording changes after the 20th change
- **Next Retraining**: Normal nightly retraining at 00:00:01

### 4. **Learning Data Storage**
- Learning events are recorded when:
  1. Potentiometer is stable for 5 seconds
  2. Current offset differs from previous by ≥ `OVERRIDE_THRESHOLD` (10%)
  3. Offset is significant (≥ 10%)
- Data persisted to EEPROM for long-term learning

## Configuration Constants

```cpp
#define LEARNING_WINDOW 5000            // 5 seconds stability required
#define POT_STABILITY_THRESHOLD 5       // ADC value change threshold
#define CHANGES_BEFORE_RETRAIN 20       // Immediate retrain threshold
#define RETRAIN_HOUR 0                  // Nightly retrain at 00:00:01
#define RETRAIN_MINUTE 0
#define RETRAIN_SECOND 1
```

## New State Variables

```cpp
int last_stable_pot_value;          // Last recorded stable pot position
int current_pot_value;              // Current pot reading
unsigned long pot_stable_start_time; // When current value became stable
bool pot_is_stable;                 // Is pot value currently stable?
int daily_change_count;             // Changes in current 24-hour period
unsigned long last_change_day;      // Day tracking for reset
bool retrain_pending;               // Retraining in progress flag
bool daily_retrain_done;            // Daily retrain completed flag
```

## New Functions

### `trackPotentiometerChange(current_time)`
- Monitors potentiometer readings continuously
- Detects when value stabilizes for 5 seconds
- Increments daily counter on valid changes
- Triggers immediate retraining at 20 changes

### `performRetraining()`
- Executes model retraining procedure
- Saves learning data to EEPROM
- Displays detailed retraining status
- Called by both scheduled and threshold-based triggers

## Behavior Flow

```
User adjusts potentiometer
    ↓
System detects pot movement
    ↓
Pot value remains constant for 5 seconds → Change recorded
    ↓
Daily counter increments (n/20)
    ↓
IF n ≥ 20 → Immediate retraining
    ↓
Continue monitoring throughout the day
    ↓
At 00:00:01 → Nightly retraining
    ↓
Daily counter resets to 0
```

## Serial Monitor Output

### Change Detection
```
>>> POT CHANGE DETECTED (5/20) <<<
>>> LEARNING EVENT RECORDED <<<
ML predicted: 65.0%, User adjusted to: 80.0%
```

### Retraining (20 changes)
```
>>> 20 CHANGES REACHED - RETRAINING TRIGGERED <<<
╔════════════════════════════════════════════╗
║    MODEL RETRAINING IN PROGRESS...        ║
╚════════════════════════════════════════════╝
Retraining at: 2024/03/15 Fri 14:30:45
Total learning samples: 10
Daily changes recorded: 20
✓ Model parameters updated
✓ Learning data saved to EEPROM
╔════════════════════════════════════════════╗
║    RETRAINING COMPLETED SUCCESSFULLY       ║
╚════════════════════════════════════════════╝
```

### Retraining (Nightly)
```
╔════════════════════════════════════════════╗
║    MODEL RETRAINING IN PROGRESS...        ║
╚════════════════════════════════════════════╝
Retraining at: 2024/03/15 Fri 00:00:01
Total learning samples: 10
Daily changes recorded: 8
✓ Model parameters updated
✓ Learning data saved to EEPROM
╔════════════════════════════════════════════╗
║    RETRAINING COMPLETED SUCCESSFULLY       ║
╚════════════════════════════════════════════╝
```

## Updated Serial Commands

### `stats`
Now includes:
- Daily Change Count: X/20
- Pot Stable: Yes/No

### `clearlearning`
Now also resets daily change counter

### `help`
Updated to show online learning info

## Implementation Notes

1. **Non-Intrusive**: All other code logic and functionality remains unchanged
2. **Efficient**: Uses existing timer infrastructure, no additional interrupts
3. **Robust**: Handles day transitions, prevents duplicate retraining
4. **Extensible**: Easy to modify thresholds and timing via constants

## Future Enhancements

- Dynamic threshold adjustment based on learning rate
- Time-of-day weighted learning
- User-configurable retraining schedules via serial commands
- Cloud sync for multi-device learning
