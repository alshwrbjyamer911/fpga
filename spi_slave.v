// =============================================================================
// File    : spi_slave.v   (top-level entity must match .qsf: fpga_antenna_spi)
// Board   : Terasic DE0-Nano  –  Cyclone IV E  EP4CE22F17C6
// Clock   : 50 MHz (CLOCK_50)
//
// Purpose : SPI Slave, Mode 0 (CPOL=0, CPHA=0), MSB-first.
//           Receives 16 x 8-bit frames from an Arduino SPI master.
//           Each frame encodes { strength[7:4], angle[3:0] }.
//           Tracks the maximum strength on-the-fly; ties favour first
//           occurrence.  After 16 frames FPGA_READY is asserted HIGH.
//           Frame 17 (dummy 0x00 from Arduino) causes FPGA to shift
//           best_angle out on MISO as { best_angle[3:0], 4'b0000 }, MSB first.
//
// Constraints strictly followed:
//   - ALL logic clocked by CLOCK_50; SCLK is NEVER used as a clock.
//   - 3-stage synchronisers on every async input.
//   - SCLK edges detected from synchroniser output using 50 MHz clock.
//   - MISO driven LOW when CS_N is HIGH.
//   - On-the-fly comparison; no memory array.
//   - Full 17-frame cycle before resetting.
// =============================================================================

module fpga_antenna_spi (
    // ── System ───────────────────────────────────────────────────────────────
    input  wire CLOCK_50,    // 50 MHz board oscillator
    input  wire RESET_N,     // Active-low reset → KEY[0] on DE0-Nano

    // ── SPI (slave side) ─────────────────────────────────────────────────────
    input  wire SPI_SCLK,    // Serial clock from Arduino (asynchronous!)
    input  wire SPI_MOSI,    // Master-Out Slave-In
    output wire SPI_MISO,    // Master-In Slave-Out
    input  wire SPI_CS_N,    // Chip-select, active-low

    // ── Status ───────────────────────────────────────────────────────────────
    output wire FPGA_READY   // HIGH when best_angle is valid; LOW during scan
);

// =============================================================================
// Parameters
// =============================================================================
parameter integer FRAMES_PER_SCAN = 16;  // total data frames per scan cycle
parameter integer BITS_PER_FRAME  = 8;   // SPI word width in bits

// FSM state encoding (one-hot style is fine on small Cyclone IV logic)
localparam [1:0] ST_SCAN  = 2'b00; // receiving data frames 0..15
localparam [1:0] ST_READY = 2'b01; // scan done, FPGA_READY HIGH
localparam [1:0] ST_REPLY = 2'b10; // clocking best_angle out on MISO

// =============================================================================
// 3-stage synchronisers for every async input
// Eliminates metastability before any logic touches these signals.
// Shift direction: [0]=newest raw sample, [2]=oldest (settled) sample.
// =============================================================================
reg [2:0] sclk_sync;  // synchronised SCLK pipeline
reg [2:0] mosi_sync;  // synchronised MOSI pipeline
reg [2:0] cs_n_sync;  // synchronised CS_N pipeline
reg [2:0] rst_n_sync; // synchronised RESET_N pipeline

always @(posedge CLOCK_50) begin
    sclk_sync  <= {sclk_sync[1:0],  SPI_SCLK};
    mosi_sync  <= {mosi_sync[1:0],  SPI_MOSI};
    cs_n_sync  <= {cs_n_sync[1:0],  SPI_CS_N};
    rst_n_sync <= {rst_n_sync[1:0], RESET_N};
end

// Stable, single-bit views of key synchronised inputs
wire cs_n_s  = cs_n_sync[2];   // settled CS_N  (used throughout FSM)
wire rst_n_s = rst_n_sync[2];  // settled RESET_N (async-reset sensitivity list)

// =============================================================================
// SCLK edge detection  (all registered on CLOCK_50)
// We compare the two most-recently-settled synchroniser stages:
//   sclk_sync[2] = older settled value   ("was")
//   sclk_sync[1] = newer settled value   ("is now")
//
// sclk_rising  fires for exactly ONE 50 MHz cycle when SCLK goes 0→1.
// sclk_falling fires for exactly ONE 50 MHz cycle when SCLK goes 1→0.
//
// At 4 MHz SPI and 50 MHz system clock there are ~12 system cycles per
// SPI half-period, so both SCLK and MOSI have fully settled through all
// three synchroniser stages well before the edge pulse is consumed.
// =============================================================================
wire sclk_rising  = ~sclk_sync[2] &  sclk_sync[1]; // SCLK 0→1 pulse
wire sclk_falling =  sclk_sync[2] & ~sclk_sync[1]; // SCLK 1→0 pulse

// =============================================================================
// Module-level register declarations  (Verilog-2001 compatible)
// =============================================================================
reg [1:0] state;            // FSM state

reg [3:0] frame_cnt;        // which scan frame we are on (0..15)
reg [2:0] bit_cnt;          // bit position within current frame (7=MSB .. 0=LSB)

reg [7:0] rx_shift;         // MOSI shift register (fill from bit 0, MSB first)
reg [7:0] miso_shift;       // MISO shift register (MSB driven to pin)

reg [3:0] best_strength;    // running maximum signal strength (4-bit)
reg [3:0] best_angle;       // angle that produced best_strength

reg       fpga_ready_reg;   // register behind FPGA_READY output

// Combinational byte reconstruction.
// When bit_cnt==0 and sclk_rising fires, the last MOSI bit has been
// synchronised to mosi_sync[2] (same pipeline depth as sclk_rising).
// All 8 bits are therefore correctly aligned at the same cycle.
wire [7:0] rx_byte     = {rx_shift[6:0], mosi_sync[2]}; // assemble received byte
wire [3:0] rx_strength = rx_byte[7:4];                   // upper nibble = strength
wire [3:0] rx_angle    = rx_byte[3:0];                   // lower nibble = angle

// =============================================================================
// Output assignments
// =============================================================================
// MISO: drive 0 when CS_N is HIGH; otherwise put MSB of shift register on pin.
assign SPI_MISO   = cs_n_s ? 1'b0 : miso_shift[7];
assign FPGA_READY = fpga_ready_reg;

// =============================================================================
// Main FSM — all synchronous to CLOCK_50; async reset via synchronised rst_n_s
// =============================================================================
always @(posedge CLOCK_50 or negedge rst_n_s) begin

    // ------------------------------------------------------------------
    // Synchronous, active-low reset (rst_n_s already fully synchronised)
    // ------------------------------------------------------------------
    if (!rst_n_s) begin
        state          <= ST_SCAN;
        frame_cnt      <= 4'd0;
        bit_cnt        <= 3'd7;   // ready to receive bit 7 (MSB) of first frame
        rx_shift       <= 8'd0;
        miso_shift     <= 8'd0;
        best_strength  <= 4'd0;
        best_angle     <= 4'd0;
        fpga_ready_reg <= 1'b0;

    end else begin

        // ------------------------------------------------------------------
        // FSM
        // ------------------------------------------------------------------
        case (state)

            // ==============================================================
            // ST_SCAN — receive 16 x 8-bit frames.
            // Sample MOSI on every rising edge of SCLK (Mode 0).
            // After 8 bits, compare strength and advance frame counter.
            // ==============================================================
            ST_SCAN: begin

                if (cs_n_s) begin
                    // CS inactive: keep bit counter reset so the first bit of
                    // a new frame is always treated as MSB.
                    bit_cnt  <= 3'd7;
                    rx_shift <= 8'd0;

                end else if (sclk_rising) begin
                    // ── Shift MOSI into receive register (MSB first) ──────
                    rx_shift <= {rx_shift[6:0], mosi_sync[2]};

                    if (bit_cnt == 3'd0) begin
                        // ── Last bit of this frame just arrived ──────────
                        // rx_byte / rx_strength / rx_angle are combinational,
                        // so they are valid right here.

                        // On-the-fly max: strictly greater keeps first tie.
                        if (rx_strength > best_strength) begin
                            best_strength <= rx_strength;
                            best_angle    <= rx_angle;
                        end

                        // Check if this was the 16th (last) scan frame
                        if (frame_cnt == (FRAMES_PER_SCAN - 1)) begin
                            // Scan complete → go to READY, assert FPGA_READY
                            frame_cnt      <= 4'd0;
                            fpga_ready_reg <= 1'b1;
                            state          <= ST_READY;
                        end else begin
                            frame_cnt <= frame_cnt + 4'd1;
                        end

                        // Reset for the next frame
                        bit_cnt  <= 3'd7;
                        rx_shift <= 8'd0;

                    end else begin
                        // More bits to receive in this frame
                        bit_cnt <= bit_cnt - 3'd1;
                    end
                end
            end // ST_SCAN


            // ==============================================================
            // ST_READY — scan is done, FPGA_READY is HIGH.
            // Pre-load MISO shift register while waiting for CS_N to fall.
            // Transition to ST_REPLY the moment CS_N goes LOW.
            // ==============================================================
            ST_READY: begin
                if (cs_n_s) begin
                    // CS still HIGH: keep MISO register loaded and bit counter
                    // ready so the very first SCLK edge shifts correctly.
                    miso_shift <= {best_angle, 4'b0000}; // upper nibble = angle
                    bit_cnt    <= 3'd7;
                end else begin
                    // CS just went LOW → dummy frame (frame 17) has started.
                    // De-assert FPGA_READY; the Arduino sees the falling edge
                    // and knows the reply frame is in progress.
                    fpga_ready_reg <= 1'b0;
                    state          <= ST_REPLY;
                end
            end // ST_READY


            // ==============================================================
            // ST_REPLY — clock out best_angle on MISO, MSB first.
            // In SPI Mode 0 the slave shifts on the FALLING edge so data is
            // stable before the master samples on the next RISING edge.
            // The first bit (bit 7 of miso_shift) is already on the MISO pin
            // because the shift register was pre-loaded in ST_READY.
            // ==============================================================
            ST_REPLY: begin
                if (cs_n_s) begin
                    // CS de-asserted → reply frame complete (or aborted).
                    // Full reset for the next scan cycle.
                    frame_cnt      <= 4'd0;
                    bit_cnt        <= 3'd7;
                    rx_shift       <= 8'd0;
                    miso_shift     <= 8'd0;
                    best_strength  <= 4'd0;
                    best_angle     <= 4'd0;
                    // fpga_ready_reg already 0 from ST_READY transition
                    state          <= ST_SCAN;

                end else if (sclk_falling) begin
                    // Shift MISO register left; the new MSB appears on pin next cycle.
                    if (bit_cnt != 3'd0) begin
                        miso_shift <= {miso_shift[6:0], 1'b0};
                        bit_cnt    <= bit_cnt - 3'd1;
                    end
                    // When bit_cnt == 0, the last bit is currently on the pin;
                    // we stop shifting and wait for CS_N to rise (handled above).
                end
            end // ST_REPLY


            // ==============================================================
            // Default: safe recovery — should never be reached.
            // ==============================================================
            default: state <= ST_SCAN;

        endcase
    end
end

endmodule
