#include "mm77la_opla.h"
#include "mm77la_opla_table.h"

uint16_t opla_ix(uint8_t a) {
    uint16_t raw = kOplaTable[a & 0xF];
    uint16_t out = static_cast<uint16_t>(~raw) & 0x3FF;
    auto bit = [&](int n) -> uint16_t { return (out >> n) & 1; };
    // bitswap<10>(out, 9,7,5,3,1,0,2,4,6,8): dest bit 9..0 <- src bits 9,7,5,3,1,0,2,4,6,8
    uint16_t result = 0;
    const int order[10] = {9,7,5,3,1,0,2,4,6,8};
    for (int i = 0; i < 10; i++) {
        int dest_bit = 9 - i;
        result |= bit(order[i]) << dest_bit;
    }
    return result;
}
