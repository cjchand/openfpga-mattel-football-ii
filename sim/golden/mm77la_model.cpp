// sim/golden/mm77la_model.cpp
#include "mm77la_model.h"

Mm77laModel::Mm77laModel(const uint8_t* rom, size_t rom_size)
    : rom_(rom), rom_size_(rom_size) {}

void Mm77laModel::reset() {
    st_ = Mm77laState{};
    ram_.fill(0xF);
}

uint8_t Mm77laModel::rom_read(uint16_t addr) const {
    addr &= 0x7FF;
    if (addr >= 0x600) addr -= 0x200; // 0x600-0x7FF mirrors 0x400-0x5FF
    return (addr < rom_size_) ? rom_[addr] : 0x00;
}

// Maps the 7-bit RAM address space onto the 96 physically-real nibbles.
// See docs/initial-plan.md §3 and the design spec's RAM-map derivation:
// 0x00-0x3F: 64 real cells, indices 0-63
// 0x40-0x47: bank A, indices 64-71; mirrored at 0x48-0x4F and 0x58-0x5F
// 0x50-0x57: bank B, indices 72-79 (NOT a mirror of bank A)
// 0x60-0x67: bank C, indices 80-87; mirrored at 0x68-0x6F and 0x78-0x7F
// 0x70-0x77: bank D, indices 88-95 (NOT a mirror of bank C)
uint8_t Mm77laModel::ram_phys_index(uint8_t addr) const {
    addr &= 0x7F;
    if (addr < 0x40) return addr;
    if (addr <= 0x4F || (addr >= 0x58 && addr <= 0x5F)) return 64 + (addr & 0x07);
    if (addr <= 0x57) return 72 + (addr & 0x07);
    if (addr <= 0x6F || (addr >= 0x78 && addr <= 0x7F)) return 80 + (addr & 0x07);
    return 88 + (addr & 0x07);
}

uint8_t Mm77laModel::ram_read(uint8_t addr) const {
    return ram_[ram_phys_index(addr)] & 0xF;
}

void Mm77laModel::ram_write(uint8_t addr, uint8_t val) {
    ram_[ram_phys_index(addr)] = val & 0xF;
}

uint8_t Mm77laModel::debug_ram_read(uint8_t addr) const { return ram_read(addr); }
void Mm77laModel::debug_ram_write(uint8_t addr, uint8_t val) { ram_write(addr, val); }
uint8_t Mm77laModel::debug_rom_read(uint16_t addr) const { return rom_read(addr); }

void Mm77laModel::increment_pc() {
    int feed = ((st_.pc & 0x3e) == 0) ? 1 : 0;
    feed ^= (st_.pc >> 1 ^ st_.pc) & 1;
    st_.pc = static_cast<uint16_t>((st_.pc & ~0x3fu) | (st_.pc >> 1 & 0x1f) | (feed << 5));
}
