#include "mm77la_display_mux.h"

void display_mux(uint16_t d, uint16_t r, uint16_t& rowsel, uint16_t& rowdata) {
    rowsel = d & 0x3FF;
    rowdata = static_cast<uint16_t>(((r << 1) & 0x700) | ((d >> 4) & 0x80) | (r & 0x7F));
}
