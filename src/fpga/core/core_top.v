//
// User core top-level
//
// Instantiated by the real top-level: apf_top
//

`default_nettype none

module core_top #(
    parameter [25:0] ROM_WAIT_MAX_P = 26'd55_000_000
) (

//
// physical connections
//

///////////////////////////////////////////////////
// clock inputs 74.25mhz. not phase aligned, so treat these domains as asynchronous

input   wire            clk_74a, // mainclk1
input   wire            clk_74b, // mainclk1 

///////////////////////////////////////////////////
// cartridge interface
// switches between 3.3v and 5v mechanically
// output enable for multibit translators controlled by pic32

// GBA AD[15:8]
inout   wire    [7:0]   cart_tran_bank2,
output  wire            cart_tran_bank2_dir,

// GBA AD[7:0]
inout   wire    [7:0]   cart_tran_bank3,
output  wire            cart_tran_bank3_dir,

// GBA A[23:16]
inout   wire    [7:0]   cart_tran_bank1,
output  wire            cart_tran_bank1_dir,

// GBA [7] PHI#
// GBA [6] WR#
// GBA [5] RD#
// GBA [4] CS1#/CS#
//     [3:0] unwired
inout   wire    [7:4]   cart_tran_bank0,
output  wire            cart_tran_bank0_dir,

// GBA CS2#/RES#
inout   wire            cart_tran_pin30,
output  wire            cart_tran_pin30_dir,
// when GBC cart is inserted, this signal when low or weak will pull GBC /RES low with a special circuit
// the goal is that when unconfigured, the FPGA weak pullups won't interfere.
// thus, if GBC cart is inserted, FPGA must drive this high in order to let the level translators
// and general IO drive this pin.
output  wire            cart_pin30_pwroff_reset,

// GBA IRQ/DRQ
inout   wire            cart_tran_pin31,
output  wire            cart_tran_pin31_dir,

// infrared
input   wire            port_ir_rx,
output  wire            port_ir_tx,
output  wire            port_ir_rx_disable, 

// GBA link port
inout   wire            port_tran_si,
output  wire            port_tran_si_dir,
inout   wire            port_tran_so,
output  wire            port_tran_so_dir,
inout   wire            port_tran_sck,
output  wire            port_tran_sck_dir,
inout   wire            port_tran_sd,
output  wire            port_tran_sd_dir,
 
///////////////////////////////////////////////////
// cellular psram 0 and 1, two chips (64mbit x2 dual die per chip)

output  wire    [21:16] cram0_a,
inout   wire    [15:0]  cram0_dq,
input   wire            cram0_wait,
output  wire            cram0_clk,
output  wire            cram0_adv_n,
output  wire            cram0_cre,
output  wire            cram0_ce0_n,
output  wire            cram0_ce1_n,
output  wire            cram0_oe_n,
output  wire            cram0_we_n,
output  wire            cram0_ub_n,
output  wire            cram0_lb_n,

output  wire    [21:16] cram1_a,
inout   wire    [15:0]  cram1_dq,
input   wire            cram1_wait,
output  wire            cram1_clk,
output  wire            cram1_adv_n,
output  wire            cram1_cre,
output  wire            cram1_ce0_n,
output  wire            cram1_ce1_n,
output  wire            cram1_oe_n,
output  wire            cram1_we_n,
output  wire            cram1_ub_n,
output  wire            cram1_lb_n,

///////////////////////////////////////////////////
// sdram, 512mbit 16bit

output  wire    [12:0]  dram_a,
output  wire    [1:0]   dram_ba,
inout   wire    [15:0]  dram_dq,
output  wire    [1:0]   dram_dqm,
output  wire            dram_clk,
output  wire            dram_cke,
output  wire            dram_ras_n,
output  wire            dram_cas_n,
output  wire            dram_we_n,

///////////////////////////////////////////////////
// sram, 1mbit 16bit

output  wire    [16:0]  sram_a,
inout   wire    [15:0]  sram_dq,
output  wire            sram_oe_n,
output  wire            sram_we_n,
output  wire            sram_ub_n,
output  wire            sram_lb_n,

///////////////////////////////////////////////////
// vblank driven by dock for sync in a certain mode

input   wire            vblank,

///////////////////////////////////////////////////
// i/o to 6515D breakout usb uart

output  wire            dbg_tx,
input   wire            dbg_rx,

///////////////////////////////////////////////////
// i/o pads near jtag connector user can solder to

output  wire            user1,
input   wire            user2,

///////////////////////////////////////////////////
// RFU internal i2c bus 

inout   wire            aux_sda,
output  wire            aux_scl,

///////////////////////////////////////////////////
// RFU, do not use
output  wire            vpll_feed,


//
// logical connections
//

///////////////////////////////////////////////////
// video, audio output to scaler
output  wire    [23:0]  video_rgb,
output  wire            video_rgb_clock,
output  wire            video_rgb_clock_90,
output  wire            video_de,
output  wire            video_skip,
output  wire            video_vs,
output  wire            video_hs,
    
output  wire            audio_mclk,
input   wire            audio_adc,
output  wire            audio_dac,
output  wire            audio_lrck,

///////////////////////////////////////////////////
// bridge bus connection
// synchronous to clk_74a
output  wire            bridge_endian_little,
input   wire    [31:0]  bridge_addr,
input   wire            bridge_rd,
output  reg     [31:0]  bridge_rd_data,
input   wire            bridge_wr,
input   wire    [31:0]  bridge_wr_data,

///////////////////////////////////////////////////
// controller data
// 
// key bitmap:
//   [0]    dpad_up
//   [1]    dpad_down
//   [2]    dpad_left
//   [3]    dpad_right
//   [4]    face_a
//   [5]    face_b
//   [6]    face_x
//   [7]    face_y
//   [8]    trig_l1
//   [9]    trig_r1
//   [10]   trig_l2
//   [11]   trig_r2
//   [12]   trig_l3
//   [13]   trig_r3
//   [14]   face_select
//   [15]   face_start
//   [31:28] type
// joy values - unsigned
//   [ 7: 0] lstick_x
//   [15: 8] lstick_y
//   [23:16] rstick_x
//   [31:24] rstick_y
// trigger values - unsigned
//   [ 7: 0] ltrig
//   [15: 8] rtrig
//
input   wire    [31:0]  cont1_key,
input   wire    [31:0]  cont2_key,
input   wire    [31:0]  cont3_key,
input   wire    [31:0]  cont4_key,
input   wire    [31:0]  cont1_joy,
input   wire    [31:0]  cont2_joy,
input   wire    [31:0]  cont3_joy,
input   wire    [31:0]  cont4_joy,
input   wire    [15:0]  cont1_trig,
input   wire    [15:0]  cont2_trig,
input   wire    [15:0]  cont3_trig,
input   wire    [15:0]  cont4_trig
    
);

// not using the IR port, so turn off both the LED, and
// disable the receive circuit to save power
assign port_ir_tx = 0;
assign port_ir_rx_disable = 1;

// bridge endianness
assign bridge_endian_little = 0;

// cart is unused, so set all level translators accordingly
// directions are 0:IN, 1:OUT
assign cart_tran_bank3 = 8'hzz;
assign cart_tran_bank3_dir = 1'b0;
assign cart_tran_bank2 = 8'hzz;
assign cart_tran_bank2_dir = 1'b0;
assign cart_tran_bank1 = 8'hzz;
assign cart_tran_bank1_dir = 1'b0;
assign cart_tran_bank0 = 4'hf;
assign cart_tran_bank0_dir = 1'b1;
assign cart_tran_pin30 = 1'b0;      // reset or cs2, we let the hw control it by itself
assign cart_tran_pin30_dir = 1'bz;
assign cart_pin30_pwroff_reset = 1'b0;  // hardware can control this
assign cart_tran_pin31 = 1'bz;      // input
assign cart_tran_pin31_dir = 1'b0;  // input

// link port is unused, set to input only to be safe
// each bit may be bidirectional in some applications
assign port_tran_so = 1'bz;
assign port_tran_so_dir = 1'b0;     // SO is output only
assign port_tran_si = 1'bz;
assign port_tran_si_dir = 1'b0;     // SI is input only
assign port_tran_sck = 1'bz;
assign port_tran_sck_dir = 1'b0;    // clock direction can change
assign port_tran_sd = 1'bz;
assign port_tran_sd_dir = 1'b0;     // SD is input and not used

// tie off the rest of the pins we are not using
assign cram0_a = 'h0;
assign cram0_dq = {16{1'bZ}};
assign cram0_clk = 0;
assign cram0_adv_n = 1;
assign cram0_cre = 0;
assign cram0_ce0_n = 1;
assign cram0_ce1_n = 1;
assign cram0_oe_n = 1;
assign cram0_we_n = 1;
assign cram0_ub_n = 1;
assign cram0_lb_n = 1;

assign cram1_a = 'h0;
assign cram1_dq = {16{1'bZ}};
assign cram1_clk = 0;
assign cram1_adv_n = 1;
assign cram1_cre = 0;
assign cram1_ce0_n = 1;
assign cram1_ce1_n = 1;
assign cram1_oe_n = 1;
assign cram1_we_n = 1;
assign cram1_ub_n = 1;
assign cram1_lb_n = 1;

assign dram_a = 'h0;
assign dram_ba = 'h0;
assign dram_dq = {16{1'bZ}};
assign dram_dqm = 'h0;
assign dram_clk = 'h0;
assign dram_cke = 'h0;
assign dram_ras_n = 'h1;
assign dram_cas_n = 'h1;
assign dram_we_n = 'h1;

assign sram_a = 'h0;
assign sram_dq = {16{1'bZ}};
assign sram_oe_n  = 1;
assign sram_we_n  = 1;
assign sram_ub_n  = 1;
assign sram_lb_n  = 1;

assign dbg_tx = 1'bZ;
assign user1 = 1'bZ;
assign aux_scl = 1'bZ;
assign vpll_feed = 1'bZ;


// for bridge write data, we just broadcast it to all bus devices
// for bridge read data, we have to mux it
// add your own devices here
always @(*) begin
    casex(bridge_addr)
    default: begin
        bridge_rd_data <= 0;
    end
    32'h10xxxxxx: begin
        // example
        // bridge_rd_data <= example_device_data;
        bridge_rd_data <= 0;
    end
    32'hF8xxxxxx: begin
        bridge_rd_data <= cmd_bridge_rd_data;
    end
    endcase
end


//
// host/target command handler
//
    wire            reset_n;                // driven by host commands, can be used as core-wide reset
    wire    [31:0]  cmd_bridge_rd_data;
    
// bridge host commands
// synchronous to clk_74a
    wire            status_boot_done = pll_core_locked_s; 
    wire            status_setup_done = pll_core_locked_s; // rising edge triggers a target command
    wire            status_running = reset_n; // we are running as soon as reset_n goes high

    wire            dataslot_requestread;
    wire    [15:0]  dataslot_requestread_id;
    wire            dataslot_requestread_ack = 1;
    wire            dataslot_requestread_ok = 1;

    wire            dataslot_requestwrite;
    wire    [15:0]  dataslot_requestwrite_id;
    wire    [31:0]  dataslot_requestwrite_size;
    wire            dataslot_requestwrite_ack = 1;
    wire            dataslot_requestwrite_ok = 1;

    wire            dataslot_update;
    wire    [15:0]  dataslot_update_id;
    wire    [31:0]  dataslot_update_size;
    
    wire            dataslot_allcomplete;

    wire     [31:0] rtc_epoch_seconds;
    wire     [31:0] rtc_date_bcd;
    wire     [31:0] rtc_time_bcd;
    wire            rtc_valid;

    wire            savestate_supported;
    wire    [31:0]  savestate_addr;
    wire    [31:0]  savestate_size;
    wire    [31:0]  savestate_maxloadsize;

    wire            savestate_start;
    wire            savestate_start_ack;
    wire            savestate_start_busy;
    wire            savestate_start_ok;
    wire            savestate_start_err;

    wire            savestate_load;
    wire            savestate_load_ack;
    wire            savestate_load_busy;
    wire            savestate_load_ok;
    wire            savestate_load_err;
    
    wire            osnotify_inmenu;

// bridge target commands
// synchronous to clk_74a

    reg             target_dataslot_read;       
    reg             target_dataslot_write;
    reg             target_dataslot_getfile;    // require additional param/resp structs to be mapped
    reg             target_dataslot_openfile;   // require additional param/resp structs to be mapped
    
    wire            target_dataslot_ack;        
    wire            target_dataslot_done;
    wire    [2:0]   target_dataslot_err;

    reg     [15:0]  target_dataslot_id;
    reg     [31:0]  target_dataslot_slotoffset;
    reg     [31:0]  target_dataslot_bridgeaddr;
    reg     [31:0]  target_dataslot_length;
    
    wire    [31:0]  target_buffer_param_struct; // to be mapped/implemented when using some Target commands
    wire    [31:0]  target_buffer_resp_struct;  // to be mapped/implemented when using some Target commands
    
// bridge data slot access
// synchronous to clk_74a

    wire    [9:0]   datatable_addr;
    wire            datatable_wren;
    wire    [31:0]  datatable_data;
    wire    [31:0]  datatable_q;

core_bridge_cmd icb (

    .clk                ( clk_74a ),
    .reset_n            ( reset_n ),

    .bridge_endian_little   ( bridge_endian_little ),
    .bridge_addr            ( bridge_addr ),
    .bridge_rd              ( bridge_rd ),
    .bridge_rd_data         ( cmd_bridge_rd_data ),
    .bridge_wr              ( bridge_wr ),
    .bridge_wr_data         ( bridge_wr_data ),
    
    .status_boot_done       ( status_boot_done ),
    .status_setup_done      ( status_setup_done ),
    .status_running         ( status_running ),

    .dataslot_requestread       ( dataslot_requestread ),
    .dataslot_requestread_id    ( dataslot_requestread_id ),
    .dataslot_requestread_ack   ( dataslot_requestread_ack ),
    .dataslot_requestread_ok    ( dataslot_requestread_ok ),

    .dataslot_requestwrite      ( dataslot_requestwrite ),
    .dataslot_requestwrite_id   ( dataslot_requestwrite_id ),
    .dataslot_requestwrite_size ( dataslot_requestwrite_size ),
    .dataslot_requestwrite_ack  ( dataslot_requestwrite_ack ),
    .dataslot_requestwrite_ok   ( dataslot_requestwrite_ok ),

    .dataslot_update            ( dataslot_update ),
    .dataslot_update_id         ( dataslot_update_id ),
    .dataslot_update_size       ( dataslot_update_size ),
    
    .dataslot_allcomplete   ( dataslot_allcomplete ),

    .rtc_epoch_seconds      ( rtc_epoch_seconds ),
    .rtc_date_bcd           ( rtc_date_bcd ),
    .rtc_time_bcd           ( rtc_time_bcd ),
    .rtc_valid              ( rtc_valid ),
    
    .savestate_supported    ( savestate_supported ),
    .savestate_addr         ( savestate_addr ),
    .savestate_size         ( savestate_size ),
    .savestate_maxloadsize  ( savestate_maxloadsize ),

    .savestate_start        ( savestate_start ),
    .savestate_start_ack    ( savestate_start_ack ),
    .savestate_start_busy   ( savestate_start_busy ),
    .savestate_start_ok     ( savestate_start_ok ),
    .savestate_start_err    ( savestate_start_err ),

    .savestate_load         ( savestate_load ),
    .savestate_load_ack     ( savestate_load_ack ),
    .savestate_load_busy    ( savestate_load_busy ),
    .savestate_load_ok      ( savestate_load_ok ),
    .savestate_load_err     ( savestate_load_err ),

    .osnotify_inmenu        ( osnotify_inmenu ),
    
    .target_dataslot_read       ( target_dataslot_read ),
    .target_dataslot_write      ( target_dataslot_write ),
    .target_dataslot_getfile    ( target_dataslot_getfile ),
    .target_dataslot_openfile   ( target_dataslot_openfile ),
    
    .target_dataslot_ack        ( target_dataslot_ack ),
    .target_dataslot_done       ( target_dataslot_done ),
    .target_dataslot_err        ( target_dataslot_err ),

    .target_dataslot_id         ( target_dataslot_id ),
    .target_dataslot_slotoffset ( target_dataslot_slotoffset ),
    .target_dataslot_bridgeaddr ( target_dataslot_bridgeaddr ),
    .target_dataslot_length     ( target_dataslot_length ),

    .target_buffer_param_struct ( target_buffer_param_struct ),
    .target_buffer_resp_struct  ( target_buffer_resp_struct ),
    
    .datatable_addr         ( datatable_addr ),
    .datatable_wren         ( datatable_wren ),
    .datatable_data         ( datatable_data ),
    .datatable_q            ( datatable_q )

);



////////////////////////////////////////////////////////////////////////////////////////

//
// Football II CPU / display / audio core
//
    // "Presentation" settings toggle (interact.json), read through the
    // core-template's existing but previously-unused datatable mechanism
    // (core_bridge_cmd.v's datatable, port A -- port B is driven by the
    // host in response to bridge reads/writes at 0xF8002000). Only one
    // flag needed (unlike FB1's 3-flag round-robin scan), so the address
    // stays fixed at word 0.
    // Two interact.json variables now live in the datatable (word 0 =
    // Presentation, word 1 = PRO 2), so the read address has to alternate
    // rather than sit at word 0. Each address is held for 8 clk_74a cycles
    // and the result latched at the end of its slot, which is far more
    // settling time than the datatable's registered read needs, and these
    // are user settings that change at human speed.
    reg [3:0]  dt_phase;
    reg [9:0]  dt_addr_r;
    reg [31:0] dt_word0, dt_word1;
    always @(posedge clk_74a) begin
        dt_phase  <= dt_phase + 1'b1;
        dt_addr_r <= dt_phase[3] ? 10'd1 : 10'd0;
        if (dt_phase == 4'd7)  dt_word0 <= datatable_q;
        if (dt_phase == 4'd15) dt_word1 <= datatable_q;
    end
    assign datatable_addr = dt_addr_r;
    assign datatable_wren = 1'b0;
    assign datatable_data = 32'd0;
    // Expected: this reads 0 (bezel off) from power-on until the APF host
    // performs its first datatable write reflecting interact.json's
    // defaultval:1 -- the datatable powers up zeroed and only the host
    // knows the setting. Not a bug; the sibling FB1 project's equivalent
    // "Overlay" toggle uses the same mechanism and works on real hardware.
    wire   bezel_enable_74a = dt_word0[0];

    wire   bezel_enable;
    synch_2 bezel_enable_sync ( bezel_enable_74a, bezel_enable, clk_core_12288, , );

    // PRO 1 / PRO 2 difficulty switch. On the real board this is simply a
    // switch wired to a CPU pin: the ROM releases DIO10 with ROS and then
    // tests it with SKISL (at 0x371, reached via LB 10 / EOB 2 / ... / ROS),
    // branching to LAI 3 or LAI 4 for the two positions. So it is an input
    // on the D bus, not a core-side game option -- see pps41_io.v.
    // Switch closed (PRO 2) pulls DIO10 high; open reads low (PRO 1).
    wire        pro2_enable_74a = dt_word1[0];
    wire        pro2_enable;
    synch_2 pro2_enable_sync ( pro2_enable_74a, pro2_enable, clk_core_12288, , );
    wire [11:0] d_input_w = {1'b0, pro2_enable, 10'b0}; // bit 10 = DIO10

    // The host releases reset_n as soon as the core is "running", which is
    // BEFORE the ROM data slot has been transferred. Left ungated, the CPU
    // starts executing at its 0x3C0 reset vector while rom_loader's memory
    // is still all zeroes -- it runs opcode 0x00 (NOP) through the LFSR PC
    // sequence, then executes a half-loaded ROM as the bridge writes stream
    // in, and by the time the image is complete it is at an arbitrary PC
    // with arbitrary A/B/RAM/stack. Crucially it has already run past the
    // ROM's own boot sequence at 0x3C0 and never returns to it, so the
    // RAM-clearing init loop never runs: RAM keeps whatever the reset value
    // left in locations the game later relies on. Most of the game still
    // works (the main loop rebuilds most state), which is exactly why this
    // survived so long -- it surfaces as a hang deep into a play, when the
    // code finally reads a location boot should have cleared.
    //
    // So: hold the CPU and its peripherals in reset until the transfer is
    // done. target_dataslot_done is the host's completion signal for the
    // read requested above. The timeout is a safety net only -- if that
    // signal never arrives the core must still start rather than sit dead
    // forever, and by ~0.75s a 1536-byte slot transfer has certainly
    // finished. rom_loader itself is deliberately NOT gated: it has to stay
    // live to receive the very writes being waited on.
    // Overridable so sim/core_top_tb.cpp can reach the timeout without
    // simulating 55 million clocks (Verilator -pvalue+). Not a synthesis
    // knob -- the default is the real value.
    localparam [25:0] ROM_WAIT_MAX = ROM_WAIT_MAX_P[25:0]; // ~0.75s at 74.25MHz
    reg [25:0] rom_wait_count;
    reg        rom_loaded_74a;
    always @(posedge clk_74a) begin
        if (!reset_n) begin
            rom_wait_count <= 26'd0;
            rom_loaded_74a <= 1'b0;
        end else if (!rom_loaded_74a) begin
            rom_wait_count <= rom_wait_count + 26'd1;
            if (target_dataslot_done || rom_wait_count == ROM_WAIT_MAX)
                rom_loaded_74a <= 1'b1;
        end
    end

    wire rom_loaded;
    synch_2 rom_loaded_sync ( rom_loaded_74a, rom_loaded, clk_core_12288, , );

    // Core-domain reset for everything that consumes the ROM.
    wire core_rst_n = reset_n & rom_loaded;

    wire        core_ce;
    ce_gen u_ce_gen (
        .clk   ( clk_core_12288 ),
        .rst_n ( core_rst_n ),
        .ce    ( core_ce )
    );

    wire [10:0] rom_addr_w;
    wire [7:0]  rom_data_w;
    rom_loader u_rom_loader (
        .clk            ( clk_74a ),
        .bridge_wr      ( bridge_wr ),
        .bridge_addr    ( bridge_addr ),
        .bridge_wr_data ( bridge_wr_data ),
        .rd_clk         ( clk_core_12288 ),
        .rom_addr       ( rom_addr_w ),
        .rom_data       ( rom_data_w )
    );

    // Data slot id 0 (b8000-12.bin, data.json) is never auto-transferred by
    // APF: core_bridge_cmd.v's target_dataslot_read is host<-target, so the
    // core itself must request the read. Without this, bridge_wr never
    // fires for rom_loader and its BRAM stays at power-on zero -- the CPU
    // then executes opcode 0x00 (NOP) forever, which is silent and never
    // lights any LED. Fire the request once, on the rising edge of
    // status_setup_done (per core_bridge_cmd.v's own comment: "rising edge
    // triggers a target command"). Same pattern as the sibling FB1 project,
    // which hit this exact bug on real hardware first.
    reg         status_setup_done_r;
    reg         rom_load_pending;
always @(posedge clk_74a) begin
    status_setup_done_r <= status_setup_done;
    target_dataslot_read <= 1'b0;
    if (status_setup_done && !status_setup_done_r) begin
        rom_load_pending <= 1'b1;
    end
    if (rom_load_pending) begin
        target_dataslot_read      <= 1'b1;
        target_dataslot_id        <= 16'd0;
        target_dataslot_slotoffset <= 32'd0;
        target_dataslot_bridgeaddr <= 32'h10000000;
        target_dataslot_length    <= 32'd1536; // b8000-12.bin: 384 words x 4 bytes
        rom_load_pending          <= 1'b0;
    end
end

    // p_input bit mapping, per docs/initial-plan.md §7's IN.0 table.
    // Face buttons match the real device's layout (Phase 5 remap):
    // top=Score, bottom=Kick, left=Status, right=Pass. Select/Start are
    // kept as redundant alternates for Status/Score.
    wire [7:0] p_input_74a = {
        cont1_key[2],                  // bit7: Left     = dpad_left
        cont1_key[1],                  // bit6: Down     = dpad_down
        cont1_key[4],                  // bit5: Pass     = face_a (right)
        cont1_key[5],                  // bit4: Kick     = face_b (bottom)
        cont1_key[3],                  // bit3: Right    = dpad_right
        cont1_key[0],                  // bit2: Up       = dpad_up
        cont1_key[7] | cont1_key[14],  // bit1: Status   = face_y (left) | Select
        cont1_key[6] | cont1_key[15]   // bit0: Score    = face_x (top)  | Start
    };

    // cont1_key is in the clk_74a domain; the CPU samples p_input in the
    // clk_core_12288 domain. Feeding it across unsynchronised let the CPU
    // latch a metastable or half-updated value on any button edge -- and the
    // ROM reads this port with I1SK/I2C, which branch on it, so a single bad
    // sample can divert control flow. Same 2-FF treatment bezel_enable
    // already gets. Synchronising the 8 bits independently means an edge can
    // still be seen a cycle apart across bits, but each bit is now stable and
    // resolved; a real button press lasts millions of core cycles, so a
    // one-cycle skew between bits is harmless, whereas metastability is not.
    wire [7:0] p_input_w;
    synch_2 #(.WIDTH(8)) p_input_sync (p_input_74a, p_input_w, clk_core_12288, , );

    wire [10:0] dbg_pc_w;
    wire        dbg_tone_on_w, dbg_unimpl_w, dbg_int1l_w;
    wire [9:0]  r_output_w;
    wire [11:0] d_output_w;
    wire [1:0]  spk_level_w;
    pps41_core u_pps41_core (
        .clk               ( clk_core_12288 ),
        .rst_n             ( core_rst_n ),
        .ce                ( core_ce ),
        .rom_addr          ( rom_addr_w ),
        .pc                ( dbg_pc_w ),
        .rom_data          ( rom_data_w ),
        .p_input           ( p_input_w ),
        .d_input           ( d_input_w ),
        .dbg_b_set         ( 1'b0 ),
        .dbg_b_val         ( 7'h0 ),
        .dbg_sag_set       ( 1'b0 ),
        .dbg_ram_wr        ( 1'b0 ),
        .dbg_ram_wdata     ( 4'h0 ),
        .ram_addr          (  ),
        .ram_rdata         (  ),
        .a_out             (  ),
        .b_out             (  ),
        .skip_out          (  ),
        .c_out             (  ),
        .stack0_out        (  ),
        .stack1_out        (  ),
        .skip_count_out    (  ),
        .int1l_hit_out     ( dbg_int1l_w ),
        .r_output_out      ( r_output_w ),
        .d_output_out      ( d_output_w ),
        .tone_on_result    ( dbg_tone_on_w ),
        .tone_freq_result  (  ),
        .spk_output_result ( spk_level_w ),
        .ios_state_result  (  ),
        .skisl_skip_out    (  ),
        .unimpl_hit_out    ( dbg_unimpl_w ),
        .x_out             (  ),
        .s_out             (  )
    );

    // On-screen flight recorder -- inert until a fault fires. See
    // src/debug_probe.v for why this exists and what arms it.
    wire        dbg_trig_w, dbg_cause_tone_w, dbg_cause_pc_w;
    wire        dbg_unimpl_seen_w, dbg_int1l_seen_w;
    wire [10:0] dbg_pc_latched_w;
    debug_probe u_debug_probe (
        .clk         ( clk_core_12288 ),
        .rst_n       ( core_rst_n ),
        .ce          ( core_ce ),
        .pc          ( dbg_pc_w ),
        .tone_on     ( dbg_tone_on_w ),
        .unimpl_hit  ( dbg_unimpl_w ),
        .int1l_hit   ( dbg_int1l_w ),
        .trig        ( dbg_trig_w ),
        .pc_latched  ( dbg_pc_latched_w ),
        .cause_tone  ( dbg_cause_tone_w ),
        .cause_pc    ( dbg_cause_pc_w ),
        .unimpl_seen ( dbg_unimpl_seen_w ),
        .int1l_seen  ( dbg_int1l_seen_w )
    );

    // 16 slots of 20px, drawn as a strip across the top of the screen when
    // armed. Slot 0 is a marker, 1 = tone cause, 2 = pc cause,
    // 3 = unimpl seen, 4 = int1l seen, 5..15 = the latched PC, MSB first.
    wire [3:0] dbg_slot   = visible_x[7:4];
    wire       dbg_in_bar = dbg_trig_w && (visible_y < 10'd16) && (visible_x < 10'd256);
    reg        dbg_bit;
    always @(*) begin
        case (dbg_slot)
            4'd0:    dbg_bit = 1'b1;
            4'd1:    dbg_bit = dbg_cause_tone_w;
            4'd2:    dbg_bit = dbg_cause_pc_w;
            4'd3:    dbg_bit = dbg_unimpl_seen_w;
            4'd4:    dbg_bit = dbg_int1l_seen_w;
            default: dbg_bit = dbg_pc_latched_w[4'd15 - dbg_slot];
        endcase
    end
    // White = 1, dark grey = 0, with a 2px black gutter between slots so the
    // bit boundaries stay countable in a photograph.
    wire [23:0] dbg_rgb = (visible_x[3:0] < 4'd2) ? 24'h000000
                        : dbg_bit               ? 24'hFFFFFF
                                                : 24'h202020;

    wire [9:0]   rowsel_w;
    wire [10:0]  rowdata_w;
    pps41_display_mux u_display_mux (
        .d       ( d_output_w ),
        .r       ( r_output_w ),
        .rowsel  ( rowsel_w ),
        .rowdata ( rowdata_w )
    );

    wire [219:0] levels_w;
    pps41_display_pwm u_display_pwm (
        .clk         ( clk_core_12288 ),
        .rst_n       ( core_rst_n ),
        .ce          ( core_ce ),
        .rowsel      ( rowsel_w ),
        .rowdata     ( rowdata_w ),
        .levels      ( levels_w ),
        .window_tick (  )
    );

    wire [23:0] render_rgb_w;
    video_renderer u_video_renderer (
        .levels       ( levels_w ),
        .x            ( visible_x ),
        .y            ( visible_y ),
        .bezel_enable ( bezel_enable ),
        .rgb          ( render_rgb_w )
    );

    audio_gen u_audio_gen (
        .clk_74a    ( clk_74a ),
        .level      ( spk_level_w ),
        .audio_mclk ( audio_mclk ),
        .audio_sclk (  ),
        .audio_lrck ( audio_lrck ),
        .audio_dac  ( audio_dac )
    );



// video generation
// ~12,288,000 hz pixel clock
//
// we want our video mode of 320x240 @ 60hz, this results in 204800 clocks per frame
// we need to add hblank and vblank times to this, so there will be a nondisplay area. 
// it can be thought of as a border around the visible area.
// to make numbers simple, we can have 400 total clocks per line, and 320 visible.
// dividing 204800 by 400 results in 512 total lines per frame, and 240 visible.
// this pixel clock is fairly high for the relatively low resolution, but that's fine.
// PLL output has a minimum output frequency anyway.


assign video_rgb_clock = clk_core_12288;
assign video_rgb_clock_90 = clk_core_12288_90deg;
assign video_rgb = vidout_rgb;
assign video_de = vidout_de;
assign video_skip = vidout_skip;
assign video_vs = vidout_vs;
assign video_hs = vidout_hs;

    localparam  VID_V_BPORCH = 'd10;
    localparam  VID_V_ACTIVE = 'd360;
    localparam  VID_V_TOTAL = 'd400;
    localparam  VID_H_BPORCH = 'd10;
    localparam  VID_H_ACTIVE = 'd400;
    localparam  VID_H_TOTAL = 'd512;

    reg [15:0]  frame_count;
    
    reg [9:0]   x_count;
    reg [9:0]   y_count;
    
    wire [9:0]  visible_x = x_count - VID_H_BPORCH;
    wire [9:0]  visible_y = y_count - VID_V_BPORCH;

    reg [23:0]  vidout_rgb;
    reg         vidout_de, vidout_de_1;
    reg         vidout_skip;
    reg         vidout_vs;
    reg         vidout_hs, vidout_hs_1;
    
    reg [9:0]   square_x = 'd135;
    reg [9:0]   square_y = 'd95;

always @(posedge clk_core_12288 or negedge reset_n) begin

    if(~reset_n) begin
    
        x_count <= 0;
        y_count <= 0;
        
    end else begin
        vidout_de <= 0;
        vidout_skip <= 0;
        vidout_vs <= 0;
        vidout_hs <= 0;
        
        vidout_hs_1 <= vidout_hs;
        vidout_de_1 <= vidout_de;
        
        // x and y counters
        x_count <= x_count + 1'b1;
        if(x_count == VID_H_TOTAL-1) begin
            x_count <= 0;
            
            y_count <= y_count + 1'b1;
            if(y_count == VID_V_TOTAL-1) begin
                y_count <= 0;
            end
        end
        
        // generate sync 
        if(x_count == 0 && y_count == 0) begin
            // sync signal in back porch
            // new frame
            vidout_vs <= 1;
            frame_count <= frame_count + 1'b1;
        end
        
        // we want HS to occur a bit after VS, not on the same cycle
        if(x_count == 3) begin
            // sync signal in back porch
            // new line
            vidout_hs <= 1;
        end

        // inactive screen areas are black
        vidout_rgb <= 24'h0;
        // generate active video
        if(x_count >= VID_H_BPORCH && x_count < VID_H_ACTIVE+VID_H_BPORCH) begin

            if(y_count >= VID_V_BPORCH && y_count < VID_V_ACTIVE+VID_V_BPORCH) begin
                // data enable. this is the active region of the line
                vidout_de <= 1;
                
                vidout_rgb <= dbg_in_bar ? dbg_rgb : render_rgb_w;
                
            end 
        end
    end
end




// audio_mclk/audio_dac/audio_lrck are now driven directly by u_audio_gen
// (see near the top of the core-logic section, next to u_pps41_core).


///////////////////////////////////////////////


    wire    clk_core_12288;
    wire    clk_core_12288_90deg;
    
    wire    pll_core_locked;
    wire    pll_core_locked_s;
synch_3 s01(pll_core_locked, pll_core_locked_s, clk_74a);

mf_pllbase mp1 (
    .refclk         ( clk_74a ),
    .rst            ( 0 ),
    
    .outclk_0       ( clk_core_12288 ),
    .outclk_1       ( clk_core_12288_90deg ),
    
    .locked         ( pll_core_locked )
);


    
endmodule
