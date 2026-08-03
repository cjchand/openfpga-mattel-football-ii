#pragma once
#include <cstdint>

// Returns the final MM77LA IX transform for accumulator value `a` (4-bit):
// table lookup, invert, then the exact bitswap<10> pattern from
// docs/initial-plan.md section 5.2 (MM77LA tier). Transcribe the swap
// pattern exactly -- do not "clean up" the reused bit-8 position.
uint16_t opla_ix(uint8_t a);
