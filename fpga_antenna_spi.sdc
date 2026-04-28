# =============================================================================
# Synopsys Design Constraints (.sdc) for fpga_antenna_spi
# Target: DE0-Nano  EP4CE22F17C6  @ 50 MHz
# =============================================================================

# ── Primary clock ─────────────────────────────────────────────────────────────
# 50 MHz = 20 ns period.  Waveform: low for first 10 ns, high for next 10 ns.
create_clock -name {CLOCK_50} -period 20.000 -waveform {0.000 10.000} [get_ports {CLOCK_50}]

# Let Quartus calculate clock uncertainty automatically (removes Critical Warning 332168)
derive_clock_uncertainty

# ── Asynchronous inputs — cut from timing analysis ────────────────────────────
# SPI_SCLK, SPI_MOSI, SPI_CS_N and RESET_N all enter through 3-stage
# synchronisers.  Tell TimeQuest they are false paths so it does not try
# to time them against CLOCK_50.
set_false_path -from [get_ports {SPI_SCLK}]
set_false_path -from [get_ports {SPI_MOSI}]
set_false_path -from [get_ports {SPI_CS_N}]
set_false_path -from [get_ports {RESET_N}]

# ── Output: FPGA_READY and SPI_MISO are driven by CLOCK_50 domain registers ──
# Relax output constraints (no external timing reference for these signals).
set_false_path -to [get_ports {SPI_MISO}]
set_false_path -to [get_ports {FPGA_READY}]
