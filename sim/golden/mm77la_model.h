// sim/golden/mm77la_model.h
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "mm77la_tone.h"
#include "mm77la_io.h"
#include "mm77la_display_mux.h"
#include "mm77la_display_pwm.h"

struct Mm77laState {
    uint16_t pc = 0;      // 11-bit program counter
    uint8_t a = 0;         // 4-bit accumulator
    uint8_t b = 0;          // 7-bit RAM address reg (Bu = bits 4-6, Bl = bits 0-3)
    uint8_t x = 0;            // 4-bit secondary register, written by LXA/XAX
    uint8_t c = 0;              // 1-bit immediate carry
    uint8_t c_in = 0;            // 1-bit delayed carry, what SKNC actually reads
    uint8_t prev_c = 0;           // c as of the START of the previous instruction; this is
                                   // what a pending c_delay republishes into c_in (MAME's
                                   // m_prev_c). See step()'s carry-commit block.
    uint8_t s = 0;                 // 4-bit serial shift register, written by XAS (serial-out pin not modeled -- unused by this game, see mm77la_model.cpp's XAS case)
    std::array<uint16_t, 2> stack{}; // 2-level return address stack, stack[0] = top
    bool skip = false;
    uint8_t skip_count = 0;
    bool ram_delay = false;
    uint8_t ram_addr_reg = 0; // delayed-address latch; see step()'s ram_delay handling
    bool sag = false;
    bool c_delay = false;
    uint8_t prev_op = 0, prev2_op = 0, prev3_op = 0;
    bool tab_pending = false;   // TAB's effect fires on the opcode AFTER next
    bool int1l_hit = false;      // flagged for the testbench, does not affect execution
    bool ix_executed = false;    // true only when op 0x72 (IX) was genuinely dispatched/executed
                                  // this step -- NOT when 0x72 merely passed through as a skipped
                                  // byte or a TR-prefixed operand byte. See the testbench's
                                  // Important #11 fix: post-hoc `prev_op == 0x72` inspection can
                                  // false-positive on those other cases, this flag cannot.
    ToneState tone;
    IoState io;
    DisplayPwmState display;
    bool unimpl_hit = false;
};

class Mm77laModel {
public:
    // Constructor: Note that ROMs smaller than 0x100 bytes are internally remapped
    // to account for the PC LFSR's non-sequential addressing pattern. This allows
    // small test ROMs to work correctly even though step() jumps through PC values
    // like 0 -> 0x20 -> 0x10 instead of 0 -> 1 -> 2. See implementation for details.
    // Large ROMs (>= 0x100) use literal indexing without remapping.
    Mm77laModel(const uint8_t* rom, size_t rom_size);
    void reset();
    void step();
    const Mm77laState& state() const { return st_; }

    // Test-only direct memory accessors (bypass ram_addr/delay logic).
    uint8_t debug_ram_read(uint8_t addr) const;
    void debug_ram_write(uint8_t addr, uint8_t val);
    uint8_t debug_rom_read(uint16_t addr) const;
    void debug_set_pc(uint16_t pc) { st_.pc = pc & 0x7FF; }
    void debug_step_pc_only() { increment_pc(); }
    void debug_set_a(uint8_t a) { st_.a = a & 0xF; }
    void debug_set_b(uint8_t b) { st_.b = b & 0x7F; }
    void debug_set_p(uint8_t p) { st_.io.p_input = p; }
    void debug_set_stack0(uint16_t addr) { st_.stack[0] = addr & 0x7FF; }
    // Test-only: place `value` at ROM address `addr` (after the same
    // 0x600-0x7FF mirror reduction rom_read() applies), independent of the
    // small-ROM sequential remap in the constructor. Used by tests that set
    // PC to a specific address via debug_set_pc() and need a real opcode
    // fetchable there, rather than relying on sequential-from-reset layout.
    void debug_poke_rom(uint16_t addr, uint8_t value);

private:
    uint8_t rom_read(uint16_t addr) const;
    uint8_t ram_phys_index(uint8_t addr) const;
    uint8_t ram_read(uint8_t addr) const;
    void ram_write(uint8_t addr, uint8_t val);
    void increment_pc();
    // Samples the CURRENT st_.io.d_output/r_output (whatever this cycle's
    // dispatch left them as -- freshly written by SOS/ROS/IOA/OX/IX if one
    // of those just ran, otherwise unchanged from the previous cycle) into
    // the display matrix mux + PWM accumulator. Must run on EVERY step()
    // call, including the consumed_by_skip early-return path -- see the two
    // call sites in step() and the comment there for why.
    void update_display();

    const uint8_t* rom_;
    size_t rom_size_;
    std::vector<uint8_t> rom_buffer_; // internal buffer for small ROMs remapped by PC sequence
    std::array<uint8_t, 96> ram_{};
    Mm77laState st_;
};
