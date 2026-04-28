#include <SPI.h>
#include <RF24.h>

// ═══════════════════════════════════════════════════════════
//  PIN DEFINITIONS
// ═══════════════════════════════════════════════════════════

#define NRF_CE    9
#define NRF_CSN  10

// ═══════════════════════════════════════════════════════════
//  CONSTANTS — must match receiver exactly
// ═══════════════════════════════════════════════════════════

// !! Keep this in sync with receiver's NRF_PAYLOAD_SIZE !!
#define NRF_PAYLOAD_SIZE   8        // bytes per packet (receiver reads bytes [0..3] only)
                                    // 4 bytes = packet counter  |  4 bytes = spare

#define TX_INTERVAL_US   200        // gap between packets (microseconds)
                                    // 200 µs → ~5000 packets/sec theoretical
                                    // realistic ~1000–2000/sec at 250 kbps with 8 B

// ═══════════════════════════════════════════════════════════
//  GLOBALS
// ═══════════════════════════════════════════════════════════

RF24 radio(NRF_CE, NRF_CSN);
const byte nrfAddress[6] = "ANT01"; // must match receiver address exactly

// Only 8 bytes — packet counter occupies bytes [0..3], rest spare
uint8_t  payload[NRF_PAYLOAD_SIZE];
uint32_t packetCount = 0;

// ═══════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    while (!Serial);

    // Initialise payload to zero
    memset(payload, 0, sizeof(payload));

    if (!radio.begin()) {
        Serial.println(F("[ERROR] nRF24L01 not detected! Check wiring."));
        while (1);
    }

    // ── RF settings — must match receiver exactly ──────────────
    radio.openWritingPipe(nrfAddress);
    radio.setDataRate(RF24_250KBPS);          // 250 kbps — matches receiver
    radio.setPayloadSize(NRF_PAYLOAD_SIZE);   // 8 bytes  — matches receiver
    radio.setPALevel(RF24_PA_MAX);            // MAX power for antenna range
    radio.setAutoAck(false);                  // no ACK — max throughput
    radio.setRetries(0, 0);                   // no retries
    radio.stopListening();                    // TX mode

    Serial.println(F("+=================================+"));
    Serial.println(F("|  nRF24L01 Transmitter Ready     |"));
    Serial.println(F("+=================================+"));
    Serial.println(F("|  Address   : ANT01              |"));
    Serial.println(F("|  Data rate : 250 kbps           |"));
    Serial.println(F("|  Payload   : 8 bytes            |"));
    Serial.println(F("|  PA level  : MAX                |"));
    Serial.println(F("|  Auto-ACK  : OFF                |"));
    Serial.println(F("+=================================+"));
    Serial.println();
}

// ═══════════════════════════════════════════════════════════
//  LOOP — transmit packets as fast as possible
//
//  Packet layout (8 bytes):
//    [0] = packetCount bits 31..24  (MSB)
//    [1] = packetCount bits 23..16
//    [2] = packetCount bits 15.. 8
//    [3] = packetCount bits  7.. 0  (LSB)
//    [4..7] = 0x00 spare
//
//  The receiver reconstructs packetCount from bytes [0..3]
//  to compute expected vs received packets → signal strength.
// ═══════════════════════════════════════════════════════════

void loop() {
    // Write counter into payload BEFORE transmitting
    packetCount++;
    payload[0] = (uint8_t)(packetCount >> 24);
    payload[1] = (uint8_t)(packetCount >> 16);
    payload[2] = (uint8_t)(packetCount >>  8);
    payload[3] = (uint8_t)(packetCount      );

    radio.write(payload, NRF_PAYLOAD_SIZE);

    delayMicroseconds(TX_INTERVAL_US);

    // Print stats every 2000 packets (~every 0.4 s at 5000 pkt/s)
    if (packetCount % 2000 == 0) {
        Serial.print(F("Packets sent: "));
        Serial.println(packetCount);
    }
}
