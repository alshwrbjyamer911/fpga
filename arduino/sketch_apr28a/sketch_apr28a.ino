// =============================================================================
// File    : sketch_apr28a.ino
// Purpose : Standalone stepper motor test — rotates exactly 360° across
//           16 antenna positions (15 moves × 24° each), then stops.
//
// Motor   : NEMA 17  (1.8°/step = 200 full steps per revolution)
// Driver  : TB6600   — set DIP switches for 1/8 microstep
//             → 200 × 8 = 1600 pulses / full revolution
//
// Wiring (TB6600 ↔ Arduino):
//   STEP_PIN (D3) → PUL+   |  GND → PUL-
//   DIR_PIN  (D4) → DIR+   |  GND → DIR-
//   EN_PIN   (D5) → ENA+   |  GND → ENA-
//   Motor coil wires → A+/A-/B+/B- on TB6600
//
// To change microstepping:
//   Adjust the TB6600 DIP switches AND update MICROSTEP below.
//   Common values: 1 | 2 | 4 | 8 | 16 | 32
// =============================================================================

// ── Pin definitions ──────────────────────────────────────────────────────────
#define STEP_PIN    3    // PUL+ on TB6600
#define DIR_PIN     4    // DIR+ on TB6600
#define EN_PIN      5    // ENA+ on TB6600  (LOW = motor enabled, HIGH = free)

// ── Motor & driver parameters ─────────────────────────────────────────────────
#define FULL_STEPS_PER_REV  200     // NEMA17: 1.8° per step
#define MICROSTEP           8       // TB6600 DIP: 1/8 microstep
#define PULSES_PER_REV      (FULL_STEPS_PER_REV * MICROSTEP)   // = 1600

// ── Scan geometry ─────────────────────────────────────────────────────────────
// 16 positions spread over 360°:
//   position 0 = 0°, position 1 = 24°, … position 15 = 360°
//   → 15 moves of 24° each to sweep all 16 positions
#define NUM_POSITIONS       16      // antenna measurement positions
#define NUM_MOVES           (NUM_POSITIONS - 1)   // = 15 moves = 360°
#define DEGREES_PER_MOVE    24      // 360° / 15 = 24°

// Pulses needed for one 24° move:
//   PULSES_PER_REV × (24 / 360) = 1600 × (24/360) = 106.67 → 107
#define PULSES_PER_MOVE     107

// ── Speed & timing ────────────────────────────────────────────────────────────
#define PULSE_HIGH_US       5       // TB6600 minimum HIGH pulse width (2.2 µs min; use 5 µs)
#define PULSE_LOW_US        800     // LOW time between pulses — controls speed
                                    // 800 µs → ~588 pulses/sec → ~22 rpm
                                    // Decrease to go faster; don't go below ~200 µs

#define DIR_SETTLE_US       10      // DIR must be stable before first pulse
#define DWELL_MS            500     // pause at each position (ms)

// ── Direction ─────────────────────────────────────────────────────────────────
#define DIR_FORWARD         HIGH    // clockwise as wired; swap to LOW if reversed

// =============================================================================
//  Helper: pulse the stepper N times in the current DIR direction
// =============================================================================
void stepPulses(uint16_t count) {
    for (uint16_t i = 0; i < count; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(PULSE_HIGH_US);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(PULSE_LOW_US);
    }
}

// =============================================================================
//  Helper: enable / disable driver
// =============================================================================
void motorEnable()  { digitalWrite(EN_PIN, LOW);  }  // energise coils
void motorDisable() { digitalWrite(EN_PIN, HIGH); }  // release coils (saves heat)

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
    Serial.begin(115200);

    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN,  OUTPUT);
    pinMode(EN_PIN,   OUTPUT);

    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN,  DIR_FORWARD);

    motorDisable();   // start with motor free until we're ready to move

    Serial.println(F("=== Stepper 360° Sweep Test ==="));
    Serial.print(F("Motor  : NEMA17, 200 steps/rev, 1/"));
    Serial.print(MICROSTEP);
    Serial.println(F(" microstep"));
    Serial.print(F("Pulses : "));
    Serial.print(PULSES_PER_REV);
    Serial.print(F(" /rev   →   "));
    Serial.print(PULSES_PER_MOVE);
    Serial.println(F(" pulses per 24° move"));
    Serial.println(F("Sweep  : 16 positions × 24° = 360° (15 moves)"));
    Serial.println(F("Starting in 2 seconds...\n"));
    delay(2000);
}

// =============================================================================
//  LOOP — runs once, then halts
// =============================================================================
void loop() {
    Serial.println(F("--- Begin 360° sweep ---"));

    motorEnable();
    digitalWrite(DIR_PIN, DIR_FORWARD);
    delayMicroseconds(DIR_SETTLE_US);

    // Sweep: visit each of the 16 positions, pausing at each one
    for (uint8_t pos = 0; pos < NUM_POSITIONS; pos++) {

        Serial.print(F("  Position "));
        if (pos < 10) Serial.print('0');
        Serial.print(pos);
        Serial.print(F("  →  "));
        Serial.print(pos * DEGREES_PER_MOVE);
        Serial.println(F("°"));

        // Dwell at this position (measurement time in real system)
        delay(DWELL_MS);

        // Move to the next position (skip after the last one)
        if (pos < NUM_POSITIONS - 1) {
            stepPulses(PULSES_PER_MOVE);
        }
    }

    // All 16 positions visited — motor is now at 360° (= home)
    motorDisable();

    Serial.println(F("\n--- Sweep complete. Motor disabled. ---"));
    Serial.println(F("Reset the Arduino to run again.\n"));

    // Halt — do not repeat
    while (true) { /* done */ }
}