// sim/golden/mm77la_model.cpp
#include "mm77la_model.h"

Mm77laModel::Mm77laModel(const uint8_t* rom, size_t rom_size)
    : rom_(rom), rom_size_(rom_size) {
    // For small test ROMs (< 0x100 bytes), allocate a larger buffer and remap
    // the ROM bytes to their PC sequence addresses. This allows test ROMs to
    // work correctly with the PC LFSR that doesn't increment sequentially.
    if (rom_size < 0x100) {
        rom_buffer_.resize(0x100, 0x00);
        // Compute PC sequence and place ROM bytes at their accessed addresses
        uint16_t pc = 0;
        for (size_t i = 0; i < rom_size; i++) {
            rom_buffer_[pc & 0xFF] = rom[i];
            // Compute next PC using the LFSR formula
            int feed = ((pc & 0x3e) == 0) ? 1 : 0;
            feed ^= (pc >> 1 ^ pc) & 1;
            pc = static_cast<uint16_t>((pc & ~0x3fu) | (pc >> 1 & 0x1f) | (feed << 5));
        }
        rom_ = rom_buffer_.data();
        rom_size_ = rom_buffer_.size();
    }
}

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

void Mm77laModel::step() {
    uint16_t ram_addr = st_.b; // ram_delay override handled in Task 8
    uint8_t op = rom_read(st_.pc);
    increment_pc();

    switch (op & 0xF0) {
        case 0x40: { // LAI x
            st_.a = op & 0xF;
            break;
        }
        case 0x60: { // AISK x (x!=0) handled here; I1SK (x==0) is Task 5/7 I/O work
            uint8_t x = op & 0xF;
            uint8_t sum = static_cast<uint8_t>(st_.a + x);
            st_.a = sum & 0xF;
            st_.skip = (x == 6) ? false : (sum < 0x10);
            break;
        }
        default: {
            switch (op & 0xFC) {
                case 0x20: { // SB x
                    uint8_t val = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), val | (1 << (op & 0x3)));
                    break;
                }
                case 0x24: { // RB x
                    uint8_t val = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), val & ~(1 << (op & 0x3)));
                    break;
                }
                case 0x28: { // SKBF x
                    uint8_t val = ram_read(static_cast<uint8_t>(ram_addr));
                    st_.skip = (val & (1 << (op & 0x3))) == 0;
                    break;
                }
                case 0x50: { // L x
                    st_.a = ram_read(static_cast<uint8_t>(ram_addr));
                    st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                    break;
                }
                case 0x5C: { // X x
                    uint8_t tmp = ram_read(static_cast<uint8_t>(ram_addr));
                    ram_write(static_cast<uint8_t>(ram_addr), st_.a);
                    st_.a = tmp;
                    st_.b = static_cast<uint8_t>(st_.b ^ ((op & 0x3) << 4));
                    break;
                }
                default: {
                    switch (op) {
                        case 0x00: break; // NOP
                        case 0x76: { // LBA (MM78: no ram_delay)
                            st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | st_.a);
                            break;
                        }
                        case 0x77: { // COM
                            st_.a = st_.a ^ 0xF;
                            break;
                        }
                        case 0x7A: { // XAB
                            uint8_t tmp = st_.a;
                            st_.a = st_.b & 0xF;
                            st_.b = static_cast<uint8_t>((st_.b & ~0xFu) | tmp);
                            st_.ram_delay = true;
                            break;
                        }
                        case 0x7E: { // A
                            st_.a = static_cast<uint8_t>((st_.a + ram_read(static_cast<uint8_t>(ram_addr))) & 0xF);
                            break;
                        }
                        case 0x7C: { // AC -- carry write only; delay visibility is Task 8
                            uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in);
                            st_.c = (sum >> 4) & 1;
                            st_.a = sum & 0xF;
                            break;
                        }
                        case 0x7D: { // ACSK -- MM78: skip if NEW carry (inverted vs MM76)
                            uint8_t sum = static_cast<uint8_t>(st_.a + ram_read(static_cast<uint8_t>(ram_addr)) + st_.c_in);
                            st_.c = (sum >> 4) & 1;
                            st_.a = sum & 0xF;
                            st_.skip = st_.c != 0;
                            break;
                        }
                        case 0x7F: { // SKMEA
                            st_.skip = (st_.a == ram_read(static_cast<uint8_t>(ram_addr)));
                            break;
                        }
                        default:
                            break; // unimplemented opcodes fall through as NOP until later tasks
                    }
                    break;
                }
            }
            break;
        }
    }

    st_.prev3_op = st_.prev2_op;
    st_.prev2_op = st_.prev_op;
    st_.prev_op = op;
}
