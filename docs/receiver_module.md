# Receiver Module

**Source File**: `arduino/receiver/receiver.ino`

## Overview
The Receiver module acts as the central brain and master controller of the antenna tracking system. It runs on an Arduino and is responsible for managing the stepper motor, sampling the RF signal strength, communicating with the FPGA via SPI, and ultimately steering the antenna to point at the strongest signal.

## Functional Description
1. **Mechanical Sweep**:
   - The module controls a NEMA23 stepper motor through a TB6600 driver (configured for 1/32 microstepping).
   - It performs a 360° scan by making 15 discrete forward moves (16 total positions, 24° apart).
2. **Signal Measurement**:
   - At each angle, the receiver opens a 3-second window to listen for packets from the Transmitter.
   - It calculates the packet loss by comparing the sequence IDs of the first and last received packets against the total packet count received.
   - It converts the success rate into a high-resolution 12-bit signal strength value (0-4095).
3. **FPGA Offloading (SPI Master)**:
   - The receiver compresses the 12-bit signal strength into 4 bits.
   - It acts as an SPI Master (Mode 0, 4 MHz) to send an 8-bit frame (4-bit strength + 4-bit angle index) to the FPGA for hardware processing.
   - It waits for a hardware interrupt (`FPGA_READY`) and requests the best angle from the FPGA by sending a dummy 17th frame.
4. **Dual-Validation & Pointing**:
   - The Arduino maintains its own internal software-based record using the full 12-bit precision.
   - After retrieving the 4-bit precision result from the FPGA, it compares both. The Arduino relies on its higher-resolution 12-bit result as the final truth.
   - Finally, the stepper motor reverses to point exactly at the optimal angle before waiting for the next scan cycle.
