#pragma once
#include <cstdint>

// Transcribed from MAME's mfootb2_state::update_display():
//   m_display->matrix(m_d, (m_r << 1 & 0x700) | (m_d >> 4 & 0x80) | (m_r & 0x7f));
// rowsel: 10-bit bitmask (D bits 0-9) -- which of the 10 matrix rows are
//   currently strobed. Not necessarily one-hot; model the general case.
// rowdata: 11-bit column value. R[9:7] -> bits 10:8, D[11] -> bit 7 (the
//   ONLY source of column-bit-7 -- R's 10 bits never reach it), R[6:0] ->
//   bits 6:0. D[10] (the pin the driver labels "4th digit DP") is NOT
//   consumed here at all -- D[11] carries the real DP data bit.
void display_mux(uint16_t d, uint16_t r, uint16_t& rowsel, uint16_t& rowdata);
