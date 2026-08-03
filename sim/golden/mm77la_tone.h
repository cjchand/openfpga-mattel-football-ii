#pragma once
#include <cstdint>

struct ToneState {
    bool tone_on = false;
    uint8_t tone_freq = 0;
    uint8_t tone_count = 1; // NOT 0 -- reset_tone_count() sets 1, see design spec section 4
    uint8_t spk_output = 2; // 2-bit register; toggle_speaker() XORs with 3,
                             // perpetually alternating indices 1/2 of the
                             // {0.0,1.0,-1.0,0.0} speaker_levels table
    uint8_t ios_state = 0;  // 3-state arming FSM: 0 -> 1 -> 2 -> 0
};

void tone_reset(ToneState& t);

// IOS: builds tone_freq across repeated calls, and arms/disarms tone_on via
// the 3-state FSM. The arming check reads ios_state BEFORE incrementing it
// (arms on the SECOND call after a reset/disarm, not the first) -- see
// docs/superpowers/specs/2026-08-02-io-peripherals-phase2-design.md section 4.
void tone_ios(ToneState& t, uint8_t a);

// INT0H (MM78LA repurposing): toggles the speaker output directly.
void tone_int0h(ToneState& t);

// Called once per CPU step(), unconditionally (even when tone_on is false):
// free-running counter increment, with a toggle+reset when tone_on and the
// counter matches tone_freq.
void tone_cycle(ToneState& t);
