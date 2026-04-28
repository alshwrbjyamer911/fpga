# FPGA SPI Antenna Tracker — DE0-Nano

SPI slave implemented on a Terasic DE0-Nano (Cyclone IV E EP4CE22F17C6) that receives 16 frames of antenna signal-strength data from an Arduino SPI master, finds the strongest signal on-the-fly, and returns the corresponding angle on the 17th dummy frame.

---

## Wiring Table

| Signal       | Arduino Pin | DE0-Nano Header | DE0-Nano FPGA Pin |
|:------------ |:-----------:|:---------------:|:-----------------:|
| SPI_SCLK     | D13 (SCK)   | GPIO_0 [0]      | PIN_A15           |
| SPI_MOSI     | D11 (MOSI)  | GPIO_0 [2]      | PIN_B15           |
| SPI_MISO     | D12 (MISO)  | GPIO_0 [4]      | PIN_A14           |
| SPI_CS_N     | D10 (SS)    | GPIO_0 [6]      | PIN_B14           |
| FPGA_READY   | D9          | GPIO_0 [8]      | PIN_A13           |
| GND          | GND         | GPIO_0 GND pins | GND               |

> [!WARNING]
> **Voltage mismatch!** The DE0-Nano GPIO operates at **3.3 V**. A standard Arduino Uno/Nano outputs 5 V logic.
> Use a **3.3 V Arduino** (Pro Mini 3.3 V, Arduino Due, Zero) **or** add a bidirectional level-shifter (e.g. TXB0104, BSS138-based) on **all five signal lines** before connecting. Applying 5 V to DE0-Nano GPIO will damage the FPGA.

### GPIO_0 Header Pin-out (partial)

```
GPIO_0 connector (JP1) — top view, pin 1 at top-left
  Pin  1 = GPIO_0[0]  → SPI_SCLK  (PIN_A15)
  Pin  2 = 3.3 V supply
  Pin  3 = GPIO_0[1]
  Pin  4 = GPIO_0[2]  → SPI_MOSI  (PIN_B15)
  Pin  5 = GPIO_0[3]
  Pin  6 = GPIO_0[4]  → SPI_MISO  (PIN_A14)
  Pin  7 = GPIO_0[5]
  Pin  8 = GPIO_0[6]  → SPI_CS_N  (PIN_B14)
  Pin  9 = GPIO_0[7]
  Pin 10 = GPIO_0[8]  → FPGA_READY (PIN_A13)
  ...
  Pin 12 = GND
  Pin 30 = GND
```

---

## Protocol Summary

```
CS_N  ──┐                                                           ┌──
         └─────────────────────────────────────────────────────────┘
SCLK      ┌┐ ┌┐ ┌┐ ... (8 clocks) ... ┌┐      ...      ┌┐ ... (8 clocks)
MOSI   [Frame 0: {str,ang}] ... [Frame 15: {str,ang}]   [0x00 dummy]
MISO   (don't care)                                      [best_angle<<4]

         ←────────── 16 scan frames ──────────→← READY →← reply frame →
FPGA_READY                                     HIGH      (falls with CS_N)
```

- **Frame format (frames 0-15):** `byte = (strength[3:0] << 4) | angle[3:0]`
- **Reply byte:** `(best_angle[3:0] << 4) | 0x00` — read upper nibble only.
- Arduino holds **CS_N LOW for the entire 17-frame burst** per cycle.

---

## Compiling in Quartus Prime Lite

### Prerequisites
- Quartus Prime Lite Edition (tested with 25.1 std)
- USB-Blaster driver installed (see Altera/Intel driver package)

### Steps

1. **Open the project**
   ```
   File → Open Project → select  fpga_antenna_spi.qpf
   ```

2. **Add the source file** *(if not already listed)*
   ```
   Project → Add/Remove Files in Project → Add  spi_slave.v
   ```
   The `.qsf` already contains `set_global_assignment -name VERILOG_FILE spi_slave.v`,
   so Quartus will pick it up automatically on project open.

3. **Compile**
   ```
   Processing → Start Compilation   (Ctrl+L)
   ```
   Expected result: **0 errors, 0 critical warnings**.
   The Timing Analyzer should report timing closed at 50 MHz on `CLOCK_50`.

4. **Review pin assignments** *(optional sanity check)*
   ```
   Assignments → Pin Planner
   ```
   Verify each signal matches the wiring table above.

5. **Program the FPGA**
   - Connect the DE0-Nano via USB-Blaster (mini-USB cable).
   - Open the programmer:
     ```
     Tools → Programmer
     ```
   - Click **Hardware Setup** and select `USB-Blaster [USB-0]`.
   - The `.sof` file (`output_files/fpga_antenna_spi.sof`) should be auto-populated.
   - Click **Start** to program.
   - The FPGA is programmed (volatile SRAM); repeat every power cycle unless you
     burn the `.jic` to the configuration flash.

---

## Running the Arduino Sketch

1. Open `arduino/arduino_master.ino` in the Arduino IDE.
2. Select your board and COM port.
3. Set baud rate to **115200** in Tools → Serial Monitor.
4. Upload the sketch.
5. Open the Serial Monitor; you should see output like:

```
=== FPGA SPI Antenna Tracker — Arduino Master ===
Initialised. Starting scan in 1 second...

--- Starting new 16-frame scan ---
  Frame 0: TX=0x00  strength=0  angle=0
  Frame 1: TX=0x11  strength=1  angle=1
  Frame 2: TX=0x22  strength=2  angle=2
  ...
  Frame 15: TX=0xFF  strength=15  angle=15
Scan frames sent. Waiting for FPGA_READY...
FPGA_READY asserted. Sending dummy frame to read result...
Reply byte = 0xF0  →  best_angle = 15
(Expected: 15 for the default test pattern)
Cycle complete. Next cycle in 3 seconds...
```

> [!NOTE]
> The default test pattern uses monotonically increasing strength (0-15), so frame 15 always wins with `best_angle = 15` and `reply byte = 0xF0`. Edit the `testData[]` array in the sketch to test other patterns (e.g., put the peak strength in the middle).

---

## File Structure

```
fpga_project/
├── spi_slave.v              ← Verilog RTL (top-level entity: fpga_antenna_spi)
├── fpga_antenna_spi.qpf     ← Quartus project file
├── fpga_antenna_spi.qsf     ← Settings + pin assignments
├── README.md                ← This file
└── arduino/
    └── arduino_master.ino   ← Arduino SPI master sketch
```

---

## Design Notes

| Aspect | Decision |
|:-------|:---------|
| System clock | 50 MHz (`CLOCK_50`) — the **only** clock in the design |
| SPI clock handling | SCLK is **never** used as a clock; edges detected via 3-stage synchroniser on 50 MHz clock |
| Metastability protection | 3-stage FF synchroniser on `SPI_SCLK`, `SPI_MOSI`, `SPI_CS_N`, and `RESET_N` |
| SPI mode | Mode 0: CPOL=0, CPHA=0; data captured on rising SCLK edge, shifted on falling edge |
| Bit order | MSB first |
| MISO idle | Driven LOW when CS_N is HIGH |
| Comparison rule | Strictly greater-than; first occurrence wins on tie |
| Memory usage | Zero — purely register-based on-the-fly comparison |
| Reset | Active-low, synchronised (`KEY[0]` on DE0-Nano); assert before starting SPI |
