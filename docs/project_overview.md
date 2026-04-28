# FPGA-Based Antenna Tracker Project

## Overview
This project implements an automated directional antenna tracking system. It consists of three main modules that work together to find the strongest signal source.

## System Architecture
The system is divided into three key components:

1. **[Transmitter Module](./transmitter_module.md)**: An Arduino-based beacon that continuously transmits sequence-numbered packets over an nRF24L01 radio link.
2. **[Receiver Module](./receiver_module.md)**: An Arduino-based master controller that sweeps a directional antenna using a stepper motor, measures signal strength at 16 different angles (covering 360 degrees), and delegates data offloading to the FPGA.
3. **[FPGA Module](./fpga_module.md)**: A hardware accelerator running on a Cyclone IV FPGA that acts as an SPI slave. It receives signal strength data frame-by-frame and calculates the optimal angle in real-time.

## Operation Flow
1. The **Transmitter** continuously broadcasts packets at 250 kbps.
2. The **Receiver** moves the antenna 24° at a time using the stepper motor.
3. At each angle, the **Receiver** samples the airwaves for 3 seconds, measuring the packet success rate to determine signal strength.
4. The **Receiver** sends this signal strength and angle to the **FPGA** via SPI.
5. The **FPGA** updates its running maximum on the fly.
6. After 16 positions, the **Receiver** asks the **FPGA** for the best angle.
7. Finally, the **Receiver** points the antenna directly at the optimal direction.
