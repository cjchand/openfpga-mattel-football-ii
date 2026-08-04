#pragma once
#include <cstdint>

struct IoState {
    uint16_t r_output = 0x3FF;
    uint16_t d_output = 0;
    // External drivers on the (bidirectional) D bus. SKISL reads
    // d_output | d_input, matching MAME's (m_d_output | m_read_d()). On
    // this board the only external driver is the PRO 1 / PRO 2 difficulty
    // switch on DIO10 -- see test_skisl_reads_the_d_input_pins_too.
    uint16_t d_input = 0;
    uint8_t p_input = 0x00;
};

void io_reset(IoState& io);

void io_sos(IoState& io, uint8_t ram_addr);
void io_ros(IoState& io, uint8_t ram_addr);
bool io_skisl(const IoState& io, uint8_t ram_addr);

uint8_t io_i2c(const IoState& io);

void io_ioa(IoState& io, uint8_t& a, uint8_t c_in);
void io_ox(IoState& io, uint8_t a, uint8_t c_in);

bool io_i1sk(const IoState& io, uint8_t& a);
