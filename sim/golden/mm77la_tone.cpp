#include "mm77la_tone.h"

void tone_reset(ToneState& t) { t = ToneState{}; }

static void reset_tone_count(ToneState& t) { t.tone_count = 1; }

static void toggle_speaker(ToneState& t) { t.spk_output = static_cast<uint8_t>(t.spk_output ^ 3); }

void tone_ios(ToneState& t, uint8_t a) {
    t.tone_freq = static_cast<uint8_t>((t.tone_freq >> 4) | (a << 4));

    if (t.ios_state == 1) {
        t.tone_on = true;
        reset_tone_count(t);
    } else {
        t.tone_on = false;
    }

    t.ios_state = static_cast<uint8_t>((t.ios_state + 1) % 3);
}

void tone_int0h(ToneState& t) { toggle_speaker(t); }

void tone_cycle(ToneState& t) {
    t.tone_count++;
    if (t.tone_on && t.tone_count == t.tone_freq) {
        toggle_speaker(t);
        reset_tone_count(t);
    }
}
