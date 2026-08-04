#include "mm77la_io.h"

void io_reset(IoState& io) { io = IoState{}; }

void io_sos(IoState& io, uint8_t ram_addr) {
    if (ram_addr & 0x40) return;
    uint8_t bl = ram_addr & 0xF;
    if (bl < 12) io.d_output = static_cast<uint16_t>((io.d_output | (1u << bl)) & 0xFFF);
}

void io_ros(IoState& io, uint8_t ram_addr) {
    if (ram_addr & 0x40) return;
    uint8_t bl = ram_addr & 0xF;
    if (bl < 12) io.d_output = static_cast<uint16_t>(io.d_output & ~(1u << bl) & 0xFFF);
}

bool io_skisl(const IoState& io, uint8_t ram_addr) {
    if (ram_addr & 0x40) return false;
    uint8_t bl = ram_addr & 0xF;
    if (bl < 12) return (((io.d_output | io.d_input) >> bl) & 1) == 0;
    return false;
}

uint8_t io_i2c(const IoState& io) {
    return static_cast<uint8_t>((~io.p_input >> 4) & 0xF);
}

void io_ioa(IoState& io, uint8_t& a, uint8_t c_in) {
    uint16_t mask = 0x1F;
    uint8_t tmp = static_cast<uint8_t>(a);
    io.r_output = static_cast<uint16_t>((io.r_output & ~mask) | ((c_in << 4) | a));
    a = tmp;
}

void io_ox(IoState& io, uint8_t a, uint8_t c_in) {
    uint16_t mask = 0x1F;
    io.r_output = static_cast<uint16_t>((io.r_output & mask) | (((c_in << 4) | a) << 5));
}

bool io_i1sk(const IoState& io, uint8_t& a) {
    uint8_t sum = static_cast<uint8_t>(a + (io.p_input & 0xF));
    a = sum & 0xF;
    return (sum & 0x10) == 0;
}
