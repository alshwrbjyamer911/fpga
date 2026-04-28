# System Pinout Guide

This document outlines the hardware pin connections for all three modules in the FPGA Antenna Tracker project.

## 1. Arduino Transmitter Module
*Assumes standard Arduino Uno/Nano.*

| Component | Arduino Pin | Notes |
| :--- | :--- | :--- |
| **nRF24L01 CE** | `D9` | Chip Enable |
| **nRF24L01 CSN**| `D10` | SPI Chip Select |
| **nRF24L01 SCK**| `D13` | Hardware SPI Clock |
| **nRF24L01 MOSI**| `D11` | Hardware SPI MOSI |
| **nRF24L01 MISO**| `D12` | Hardware SPI MISO |

---

## 2. Arduino Receiver Module (Master)
*Assumes standard Arduino Uno/Nano.*

| Component | Arduino Pin | Notes |
| :--- | :--- | :--- |
| **nRF24L01 CE** | `D7` | Chip Enable |
| **nRF24L01 CSN**| `D9` | SPI Chip Select for Radio |
| **FPGA SPI CS** | `D10` | SPI Chip Select for FPGA |
| **FPGA READY** | `D2` | External Interrupt 0 (Rising Edge) |
| **Stepper PUL+**| `D3` | TB6600 Step/Pulse |
| **Stepper DIR+**| `D4` | TB6600 Direction |
| **Stepper ENA+**| `D5` | TB6600 Enable (Active Low) |
| **SPI SCK** | `D13` | Shared Hardware SPI Clock (Connects to nRF24 & FPGA) |
| **SPI MOSI** | `D11` | Shared Hardware SPI MOSI (Connects to nRF24 & FPGA) |
| **SPI MISO** | `D12` | Shared Hardware SPI MISO (Connects to nRF24 & FPGA) |

---

## 3. FPGA Module (Terasic DE0-Nano)
*Cyclone IV E EP4CE22F17C6*

| FPGA Signal | DE0-Nano Pin | Header / Note |
| :--- | :--- | :--- |
| **CLOCK_50** | `PIN_R8` | 50 MHz Onboard Oscillator |
| **RESET_N** | `PIN_J15` | Onboard Pushbutton `KEY[0]` |
| **SPI_SCLK** | `PIN_A8` | `GPIO_0` Header (Connects to Arduino D13) |
| **SPI_MOSI** | `PIN_B8` | `GPIO_0` Header (Connects to Arduino D11) |
| **SPI_MISO** | `PIN_A2` | `GPIO_0` Header (Connects to Arduino D12) |
| **SPI_CS_N** | `PIN_B3` | `GPIO_0` Header (Connects to Arduino D10) |
| **FPGA_READY**| `PIN_A4` | `GPIO_0` Header (Connects to Arduino D2) |

---

## Shared SPI Bus Wiring Checklist (Receiver ↔ Devices)
Since the Receiver coordinates both the nRF24L01 and the FPGA over the same hardware SPI bus, ensure the following are connected in parallel:
- **Arduino D13** ➔ nRF24 SCK **AND** FPGA `PIN_A8`
- **Arduino D11** ➔ nRF24 MOSI **AND** FPGA `PIN_B8`
- **Arduino D12** ➔ nRF24 MISO **AND** FPGA `PIN_A2`
*(Chip Selects MUST remain separate: D9 for radio, D10 for FPGA).*
