# Transmitter Module

**Source File**: `arduino/transmitter/transmitter.ino`

## Overview
The Transmitter module acts as the beacon for the antenna tracker system. Running on an Arduino, it continuously broadcasts RF packets using an nRF24L01 transceiver module. 

## Functional Description
1. **Continuous Broadcasting**: The transmitter operates in an endless loop, broadcasting packets with a very short interval (200 µs delay). This enables the receiver to accurately sample the signal strength by calculating the percentage of successfully received packets.
2. **Sequence Tracking**: To allow the receiver to calculate packet loss, the transmitter embeds a 32-bit sequence counter (packet count) into the first 4 bytes of every 8-byte packet.
3. **RF Settings**: 
   - Uses a strict 250 kbps data rate to maximize range.
   - Auto-ACK (Auto-Acknowledge) and retries are explicitly disabled to ensure maximum unhindered packet throughput.
   - Operates at maximum Power Amplifier (PA) level (`RF24_PA_MAX`) to ensure the beacon can be detected over a wide area.
