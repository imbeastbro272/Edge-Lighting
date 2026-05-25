/*
 * Test cases for validating Decision Tree implementation
 * Compare Arduino predictions with these expected values
 */

#ifndef TEST_CASES_H
#define TEST_CASES_H

struct TestCase {
  float ambient_light;
  int motion_detected;
  float sin_hour;
  float cos_hour;
  int time_period;
  int hour;
  float expected_brightness;
};

const int NUM_TEST_CASES = 10;

const TestCase TEST_CASES[] = {
  {50.00f, 1, -0.500000f, 0.866025f, 4, 22, 85.000000f},
  {800.00f, 1, -0.500000f, 0.866025f, 4, 22, 25.000000f},
  {400.00f, 1, 0.965926f, 0.258819f, 1, 8, 62.000000f},
  {900.00f, 0, -0.258819f, -0.965926f, 2, 14, 20.000000f},
  {200.00f, 1, -0.965926f, -0.258819f, 3, 18, 65.000000f},
  {30.00f, 0, 0.707107f, 0.707107f, 0, 5, 80.000000f},
  {600.00f, 1, 0.866025f, 0.500000f, 1, 10, 45.000000f},
  {100.00f, 0, 0.130526f, 0.991445f, 4, 23, 75.000000f},
  {500.00f, 1, -0.500000f, -0.866025f, 2, 16, 50.000000f},
  {250.00f, 1, -0.866025f, -0.500000f, 3, 19, 68.000000f},
};

/*
 * Validation function for Arduino
 * Call this in setup() to verify the model works correctly
 */
void validate_model() {
  Serial.println(F("\n=== Model Validation ==="));
  float max_error = 0.0f;
  int failures = 0;
  
  for (int i = 0; i < NUM_TEST_CASES; i++) {
    TestCase tc = TEST_CASES[i];
    float predicted = predict_brightness(
      tc.ambient_light,
      tc.motion_detected,
      tc.sin_hour,
      tc.cos_hour,
      tc.time_period
    );
    
    float error = abs(predicted - tc.expected_brightness);
    if (error > max_error) max_error = error;
    if (error > 0.1) failures++;  // Allow small floating-point differences
    
    Serial.print(F("Test "));
    Serial.print(i + 1);
    Serial.print(F(": Hour="));
    Serial.print(tc.hour);
    Serial.print(F(" Expected="));
    Serial.print(tc.expected_brightness, 2);
    Serial.print(F("% Predicted="));
    Serial.print(predicted, 2);
    Serial.print(F("% Error="));
    Serial.print(error, 4);
    Serial.println(error < 0.1 ? F(" PASS") : F(" FAIL"));
  }
  
  Serial.println(F("\n=== Validation Summary ==="));
  Serial.print(F("Tests: "));
  Serial.println(NUM_TEST_CASES);
  Serial.print(F("Failures: "));
  Serial.println(failures);
  Serial.print(F("Max Error: "));
  Serial.print(max_error, 4);
  Serial.println(F("%"));
  Serial.println(failures == 0 ? F("✓ ALL TESTS PASSED") : F("✗ SOME TESTS FAILED"));
  Serial.println();
}

#endif // TEST_CASES_H
