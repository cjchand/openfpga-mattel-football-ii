#pragma once
#include <cstdint>

struct IoState {
    uint16_t r_output = 0x3FF;
    uint16_t d_output = 0;
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
