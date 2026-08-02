// sim/golden/mm77la_model.h
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

struct Mm77laState {
    uint16_t pc = 0;      // 11-bit program counter
    uint8_t a = 0;         // 4-bit accumulator
    uint8_t b = 0;          // 7-bit RAM address reg (Bu = bits 4-6, Bl = bits 0-3)
    uint8_t x = 0;            // 4-bit secondary register
    uint8_t c = 0;              // 1-bit immediate carry
    uint8_t c_in = 0;            // 1-bit delayed carry, what SKNC actually reads
    uint8_t s = 0;                 // 4-bit serial shift register (unused by FBII, modeled anyway)
    std::array<uint16_t, 2> stack{}; // 2-level return address stack, stack[0] = top
    bool skip = false;
    uint8_t skip_count = 0;
    bool ram_delay = false;
    bool sag = false;
    bool c_delay = false;
    uint8_t prev_op = 0, prev2_op = 0, prev3_op = 0;
    bool tab_pending = false;   // TAB's effect fires on the opcode AFTER next
    bool int1l_hit = false;      // flagged for the testbench, does not affect execution
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

private:
    uint8_t rom_read(uint16_t addr) const;
    uint8_t ram_phys_index(uint8_t addr) const;
    uint8_t ram_read(uint8_t addr) const;
    void ram_write(uint8_t addr, uint8_t val);
    void increment_pc();

    const uint8_t* rom_;
    size_t rom_size_;
    std::vector<uint8_t> rom_buffer_; // internal buffer for small ROMs remapped by PC sequence
    std::array<uint8_t, 96> ram_{};
    Mm77laState st_;
};
