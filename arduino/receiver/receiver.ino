#include <SPI.h>
#include <RF24.h>

// ═══════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ═══════════════════════════════════════════════════════════

// nRF24L01
#define NRF_CE      7
#define NRF_CSN     9

// FPGA SPI (shares hardware SPI bus, separate CS)
#define FPGA_CS     10
#define FPGA_READY  2       // interrupt pin (FPGA_READY rising edge)

// TB6600 Stepper Driver
#define STEP_PIN    3       // PUL+  → TB6600 PUL+
#define DIR_PIN     4       // DIR+  → TB6600 DIR+
#define EN_PIN      5       // ENA+  → TB6600 ENA+  (LOW = enabled, HIGH = free)

// ═══════════════════════════════════════════════════════════
//  CONSTANTS
// ═══════════════════════════════════════════════════════════

// Scan
#define NUM_ANGLES          16
#define DEGREES_PER_STEP    24          // 360° / 15 gaps = 24° per position

// Stepper — NEMA23, TB6600 driver
#define FULL_STEPS_PER_REV  200         // NEMA23: 1.8° per step = 200 full steps/rev
#define MICROSTEP           32          // TB6600 DIP switch: set to 1/32
                                        // → 200 × 32 = 6400 pulses per full revolution
// Pulses for one 24° move:
//   6400 × (24/360) = 426.67 → 427
#define STEPS_PER_MOVE      427         // pulses per 24° position step
#define PULSE_WIDTH_US      5           // HIGH pulse width (TB6600 min: 2.2 µs)
#define STEP_DELAY_US       800         // LOW time between pulses (controls speed)
                                        // 800 µs ≈ 22 RPM; decrease to go faster
#define DIR_SETTLE_US       10          // DIR must be stable before first pulse

// nRF24L01
#define NRF_PAYLOAD_SIZE    8           // bytes per packet (only 4-byte ID needed; 8 = safe margin)
#define NRF_WINDOW_MS       3000        // measurement window in milliseconds (3 seconds)

// SPI
#define SPI_CLOCK_FPGA      4000000     // 4 MHz to FPGA
#define FRAME_GAP_US        500         // gap between SPI frames
#define FPGA_READY_TIMEOUT  500         // ms before giving up on FPGA reply

// Timing
#define SCAN_INTERVAL_MS    3000        // delay between full scans

// Error sentinel
#define ERROR_NO_REPLY      0xFF

// ═══════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════

RF24 radio(NRF_CE, NRF_CSN);
const byte nrfAddress[6] = "ANT01";     // 6 bytes in RAM (RF24 needs RAM pointer)

volatile bool fpgaReady = false;

// ═══════════════════════════════════════════════════════════
//  ISR — FPGA_READY rising edge
// ═══════════════════════════════════════════════════════════

void fpgaReadyISR() {
    fpgaReady = true;
}

// ═══════════════════════════════════════════════════════════
//  FUNCTION 1
//  Measure signal strength via nRF24L01  — 12-bit precision
//
//  Opens a 3-second receive window, counts received vs expected
//  packets, then maps success rate to 12-bit range (0–4095).
//
//  12-bit gives ~0.024% resolution per step — far better than
//  the 6.67% resolution of the 0-100 scale previously used.
//
//  Returns: uint16_t  0 (no signal) – 4095 (perfect link)
// ═══════════════════════════════════════════════════════════

uint16_t measureSignalStrength(uint32_t *outLostPackets) {
    uint8_t  buf[NRF_PAYLOAD_SIZE];
    uint32_t receivedCount = 0;
    uint32_t firstPacketId = 0;
    uint32_t lastPacketId  = 0;
    bool firstPacketReceived = false;

    uint32_t deadline = millis() + NRF_WINDOW_MS;
    radio.flush_rx();

    while (millis() < deadline) {
        if (radio.available()) {
            radio.read(buf, NRF_PAYLOAD_SIZE);

            // Reconstruct 32-bit packet sequence counter (MSB first)
            uint32_t pktId = ((uint32_t)buf[0] << 24) |
                             ((uint32_t)buf[1] << 16) |
                             ((uint32_t)buf[2] <<  8) |
                              (uint32_t)buf[3];

            if (!firstPacketReceived) {
                firstPacketId = pktId;
                firstPacketReceived = true;
            }
            lastPacketId = pktId;
            receivedCount++;
        }
    }

    uint32_t lostPackets = 0;
    uint16_t strength12  = 0;       // 12-bit result (0–4095)

    if (firstPacketReceived) {
        uint32_t expectedPackets = (lastPacketId - firstPacketId) + 1;
        lostPackets = (expectedPackets >= receivedCount)
                      ? (expectedPackets - receivedCount) : 0;

        // Map success rate (0.0–1.0) to 12-bit range
        float successRate = (float)receivedCount / (float)expectedPackets;
        strength12 = (uint16_t)(successRate * 4095.0f);
    } else {
        // No packets at all — assume full loss
        lostPackets = 15000;
        strength12  = 0;
    }

    if (outLostPackets) *outLostPackets = lostPackets;

    return (uint16_t)constrain((int32_t)strength12, 0, 4095);
}

// ═══════════════════════════════════════════════════════════
//  FUNCTION 2
//  Send one angle + strength frame to FPGA over SPI
//
//  strength12  0–4095 (12-bit) → top 4 bits sent as 4-bit strength
//  Mapping: strength4 = strength12 >> 8  (divide by 256)
//    4095 >> 8 = 15,  2048 >> 8 = 8,  0 >> 8 = 0
//  frame byte = [strength4(7:4) | angleIndex(3:0)]
// ═══════════════════════════════════════════════════════════

void sendToFPGA(uint8_t angleIndex, uint16_t strength12) {
    // Extract top 4 bits of 12-bit value for FPGA protocol
    uint8_t strength4 = (uint8_t)(strength12 >> 8);   // 0–15
    strength4 = constrain(strength4, 0, 15);

    uint8_t frame = (strength4 << 4) | (angleIndex & 0x0F);

    SPI.beginTransaction(SPISettings(SPI_CLOCK_FPGA, MSBFIRST, SPI_MODE0));
    digitalWrite(FPGA_CS, LOW);
    SPI.transfer(frame);
    digitalWrite(FPGA_CS, HIGH);
    SPI.endTransaction();

    delayMicroseconds(FRAME_GAP_US);
}

// ═══════════════════════════════════════════════════════════
//  FUNCTION 3
//  Read best angle back from FPGA over SPI
//
//  Waits for FPGA_READY interrupt then sends dummy byte
//  to clock out MISO reply.
//  FPGA returns: {4'h0, best_angle[3:0]}
//
//  Returns: best angle index 0–15
//           ERROR_NO_REPLY (0xFF) on timeout
// ═══════════════════════════════════════════════════════════

uint8_t readBestAngleFromFPGA() {
    uint32_t t = millis();

    while (!fpgaReady) {
        if (millis() - t > FPGA_READY_TIMEOUT) {
            Serial.println(F("[WARN] FPGA_READY timeout — no reply"));
            return ERROR_NO_REPLY;
        }
    }
    fpgaReady = false;

    SPI.beginTransaction(SPISettings(SPI_CLOCK_FPGA, MSBFIRST, SPI_MODE0));
    digitalWrite(FPGA_CS, LOW);
    uint8_t reply = SPI.transfer(0x00);     // dummy write → clocks out MISO
    digitalWrite(FPGA_CS, HIGH);
    SPI.endTransaction();

    return reply & 0x0F;                    // lower nibble = best angle index
}

// ═══════════════════════════════════════════════════════════
//  FUNCTION 4
//  Move stepper one position (24°)
//
//  forward = true  → clockwise
//  forward = false → counter-clockwise
// ═══════════════════════════════════════════════════════════

// Enable motor coils (call before any movement)
void stepperEnable() {
    digitalWrite(EN_PIN, LOW);              // LOW = coils energised
}

// Disable motor coils (call when done moving — saves power and heat)
void stepperDisable() {
    digitalWrite(EN_PIN, HIGH);             // HIGH = coils de-energised
}

// Move one antenna position (24°) forward or backward.
// stepperEnable() must be called before the first move.
void stepperMove(bool forward) {
    digitalWrite(DIR_PIN, forward ? HIGH : LOW);
    delayMicroseconds(DIR_SETTLE_US);       // DIR must settle before pulses

    for (int i = 0; i < STEPS_PER_MOVE; i++) {
        digitalWrite(STEP_PIN, HIGH);
        delayMicroseconds(PULSE_WIDTH_US);
        digitalWrite(STEP_PIN, LOW);
        delayMicroseconds(STEP_DELAY_US);
    }
}

// Move n positions in the given direction.
void stepperMoveN(uint8_t n, bool forward) {
    for (uint8_t i = 0; i < n; i++) {
        stepperMove(forward);
    }
}

// ═══════════════════════════════════════════════════════════
//  FUNCTION 5
//  Full 360° scan across all 16 antenna positions
//
//  For each position:
//    1. Measure signal strength via nRF24
//    2. Send strength + angle to FPGA over SPI
//    3. Step to next position
//
//  After all 16 frames:
//    4. Read best angle from FPGA
//    5. Return home (reverse 15 steps)
//
//  Returns: best angle index 0–15
//           ERROR_NO_REPLY on FPGA timeout
// ═══════════════════════════════════════════════════════════

uint8_t fullScan() {
    Serial.println(F("\n╔══════════════════════════════╗"));
    Serial.println(F("║      Starting Full Scan      ║"));
    Serial.println(F("╚══════════════════════════════╝"));

    // ── Arduino-side best-angle tracker (12-bit, ties favour first) ──────
    uint16_t arduinoBestStrength = 0;
    uint8_t  arduinoBestAngle    = 0;

    // ── Phase 1: Sweep all 16 positions (15 forward moves) ───────────────
    stepperEnable();
    digitalWrite(DIR_PIN, HIGH);
    delayMicroseconds(DIR_SETTLE_US);

    for (uint8_t i = 0; i < NUM_ANGLES; i++) {

        // 1. Measure — 12-bit precision
        uint32_t lostPackets = 0;
        uint16_t strength12  = measureSignalStrength(&lostPackets);

        // 2. Arduino on-the-fly comparison (strictly greater — first tie wins)
        if (strength12 > arduinoBestStrength) {
            arduinoBestStrength = strength12;
            arduinoBestAngle    = i;
        }

        // 3. Send 4-bit compressed frame to FPGA
        sendToFPGA(i, strength12);

        // 4. Serial log
        Serial.print(F("  ["));
        if (i < 10) Serial.print('0');
        Serial.print(i);
        Serial.print(F("] "));
        Serial.print(i * DEGREES_PER_STEP);
        Serial.print((char)0xC2); Serial.print((char)0xB0); // UTF-8 degree °
        Serial.print(F("  str12="));
        Serial.print(strength12);
        Serial.print(F("/4095  4bit="));
        Serial.print(strength12 >> 8);
        Serial.print(F("  lost="));
        Serial.println(lostPackets);

        // 5. Step to next position (skip after last)
        if (i < NUM_ANGLES - 1) stepperMove(true);
    }
    // Motor is now at position 15

    // ── Phase 2: Try to get FPGA result ──────────────────────────────────
    Serial.println(F("\n  Waiting for FPGA result..."));
    uint8_t fpgaAngle = readBestAngleFromFPGA();   // ERROR_NO_REPLY if timeout

    // ── Phase 3: Print comparison ─────────────────────────────────────────
    Serial.println();
    Serial.println(F("  ╔════════════════════════════════════════╗"));
    Serial.println(F("  ║           RESULT COMPARISON            ║"));
    Serial.println(F("  ╠══════════════╦════════╦════════════════╣"));
    Serial.println(F("  ║ Source       ║  Idx   ║  Direction     ║"));
    Serial.println(F("  ╠══════════════╬════════╬════════════════╣"));

    // Arduino result (always valid)
    Serial.print(F("  ║ Arduino      ║  "));
    if (arduinoBestAngle < 10) Serial.print(' ');
    Serial.print(arduinoBestAngle);
    Serial.print(F("      ║  "));
    Serial.print(arduinoBestAngle * DEGREES_PER_STEP);
    Serial.println(F("           ║"));

    // FPGA result
    Serial.print(F("  ║ FPGA         ║  "));
    if (fpgaAngle == ERROR_NO_REPLY) {
        Serial.println(F("--      ║  TIMEOUT       ║"));
    } else {
        if (fpgaAngle < 10) Serial.print(' ');
        Serial.print(fpgaAngle);
        Serial.print(F("      ║  "));
        Serial.print(fpgaAngle * DEGREES_PER_STEP);
        Serial.println(F("           ║"));
    }
    Serial.println(F("  ╚══════════════╩════════╩════════════════╝"));

    if (fpgaAngle != ERROR_NO_REPLY && fpgaAngle != arduinoBestAngle) {
        Serial.println(F("  [NOTE] FPGA and Arduino disagree — using Arduino (12-bit)."));
    }

    // ── Phase 4: Motor points to ARDUINO best angle (always) ─────────────
    uint8_t stepsBack = (NUM_ANGLES - 1) - arduinoBestAngle;
    if (stepsBack > 0) {
        Serial.print(F("\n  Moving to Arduino best: pos "));
        Serial.print(arduinoBestAngle);
        Serial.print(F(" ("));
        Serial.print(arduinoBestAngle * DEGREES_PER_STEP);
        Serial.println(F(")"));
        stepperMoveN(stepsBack, false);
    } else {
        Serial.println(F("\n  Motor already at best position."));
    }

    delay(1000);
    stepperDisable();

    return arduinoBestAngle;   // always return Arduino result
}

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    while (!Serial);                        // wait for serial on Leonardo/Due

    // ── Stepper ──
    pinMode(STEP_PIN, OUTPUT);
    pinMode(DIR_PIN,  OUTPUT);
    pinMode(EN_PIN,   OUTPUT);
    digitalWrite(STEP_PIN, LOW);
    digitalWrite(DIR_PIN,  LOW);
    stepperDisable();

    // ── FPGA SPI ──
    pinMode(FPGA_CS,    OUTPUT);
    pinMode(FPGA_READY, INPUT);
    digitalWrite(FPGA_CS, HIGH);           // CS idle HIGH

    // ── FPGA_READY interrupt ──
    attachInterrupt(
        digitalPinToInterrupt(FPGA_READY),
        fpgaReadyISR,
        RISING
    );

    // ── Hardware SPI bus ──
    SPI.begin();

    // ── nRF24L01 ──
    if (!radio.begin()) {
        Serial.println(F("[ERROR] nRF24L01 not detected! Check wiring."));
        while (1);
    }

    radio.openReadingPipe(1, nrfAddress);
    radio.setDataRate(RF24_250KBPS);
    radio.setPayloadSize(NRF_PAYLOAD_SIZE);
    radio.setPALevel(RF24_PA_MIN);
    radio.startListening();

    Serial.println(F("╔══════════════════════════════════╗"));
    Serial.println(F("║   Antenna Tracker — System Ready ║"));
    Serial.println(F("╠══════════════════════════════════╣"));
    Serial.println(F("║  nRF24L01  : 2 Mbps, 100B packet ║"));
    Serial.println(F("║  FPGA SPI  : 4 MHz, Mode 0        ║"));
    Serial.println(F("║  Stepper   : TB6600, 1/32 microstep║"));
    Serial.println(F("║  Angles    : 16 × 24°             ║"));
    Serial.println(F("╚══════════════════════════════════╝\n"));
}

// ═══════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════

void loop() {
    uint8_t bestAngle = fullScan();

    Serial.println(F("\n╔══════════════════════════════╗"));

    // bestAngle is always the Arduino result (never ERROR_NO_REPLY)
    Serial.println(F("║      FINAL RESULT (Arduino)  ║"));
    Serial.print(F("║  Best angle index : "));
    if (bestAngle < 10) Serial.print(' ');
    Serial.print(bestAngle);
    Serial.println(F("         ║"));
    Serial.print(F("║  Best direction   : "));
    Serial.print(bestAngle * DEGREES_PER_STEP);
    Serial.println(F("           ║"));

    Serial.println(F("╚══════════════════════════════╝"));
    Serial.print(F("\nNext scan in "));
    Serial.print(SCAN_INTERVAL_MS / 1000);
    Serial.println(F(" seconds...\n"));

    delay(SCAN_INTERVAL_MS);
}