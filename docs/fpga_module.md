# FPGA SPI Slave Module

**Source File**: `spi_slave.v`

## Overview
The FPGA module is designed for the Terasic DE0-Nano (Cyclone IV E) development board. It acts as a hardware co-processor, interfacing as an SPI Slave (Mode 0) to the Arduino Receiver.

## Functional Description
1. **SPI Communication**: The module receives 16 consecutive 8-bit frames via SPI. Each frame packs a compressed 4-bit signal strength (MSB) and a 4-bit angle index (LSB).
2. **On-the-fly Comparison**: Rather than storing all frames in memory, the FPGA updates the maximum signal strength and its corresponding angle index on the fly as each frame arrives. In the event of a tie, the first maximum value is retained.
3. **Hardware Interrupt**: After processing all 16 frames, the FPGA asserts the `FPGA_READY` signal HIGH to notify the Arduino that the scan processing is complete.
4. **Data Retrieval**: Upon receiving a dummy 17th frame from the Arduino, the FPGA shifts the best angle index out on the MISO line.

## Design Constraints & Best Practices
- **Strict Synchronous Design**: All logic is clocked by the 50 MHz board oscillator (`CLOCK_50`). The incoming asynchronous SPI clock (`SPI_SCLK`) is NEVER used as a direct clock source.
- **Metastability Mitigation**: All asynchronous inputs (SCLK, MOSI, CS_N, RESET_N) pass through 3-stage synchronizers before interacting with the FPGA logic.
- **Edge Detection**: SPI clock edges are detected digitally within the 50 MHz clock domain by comparing the states of consecutive synchronizer stages.
- **Finite State Machine (FSM)**: The core logic is managed by a 3-state FSM (`ST_SCAN`, `ST_READY`, `ST_REPLY`) to ensure reliable operation and straightforward recovery.
