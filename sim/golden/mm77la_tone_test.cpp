#include "mm77la_tone.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_reset_defaults() {
    ToneState t; t.tone_on = true; t.ios_state = 2;
    tone_reset(t);
    CHECK(!t.tone_on);
    CHECK(t.tone_count == 1);
    CHECK(t.spk_output == 2);
    CHECK(t.ios_state == 0);
}

static void test_single_ios_does_not_arm() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0x5);
    CHECK(!t.tone_on);
    CHECK(t.ios_state == 1);
}

static void test_second_ios_arms() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0x5);
    tone_ios(t, 0x3);
    CHECK(t.tone_on);
    CHECK(t.ios_state == 2);
    CHECK(t.tone_count == 1);
    CHECK(t.tone_freq == 0x35);
}

static void test_third_ios_disarms() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0x1);
    tone_ios(t, 0x2);
    CHECK(t.tone_on);
    tone_ios(t, 0x3);
    CHECK(!t.tone_on);
    CHECK(t.ios_state == 0);
}

static void test_fourth_ios_rearms_cycle_repeats() {
    ToneState t; tone_reset(t);
    tone_ios(t, 0); tone_ios(t, 0); tone_ios(t, 0);
    CHECK(!t.tone_on);
    tone_ios(t, 0);
    CHECK(!t.tone_on);
    tone_ios(t, 0);
    CHECK(t.tone_on);
}

static void test_cycle_free_runs_even_when_off() {
    ToneState t; tone_reset(t);
    t.tone_freq = 3;
    uint8_t before = t.tone_count;
    tone_cycle(t);
    CHECK(t.tone_count == static_cast<uint8_t>(before + 1));
    CHECK(t.spk_output == 2);
}

static void test_cycle_toggles_on_match_and_resets_counter() {
    ToneState t; tone_reset(t);
    t.tone_on = true;
    t.tone_freq = 3;
    t.tone_count = 1;
    tone_cycle(t);
    CHECK(t.tone_count == 2);
    CHECK(t.spk_output == 2);
    tone_cycle(t);
    CHECK(t.tone_count == 1);
    CHECK(t.spk_output == 1);
}

static void test_int0h_toggles_directly_without_arming() {
    ToneState t; tone_reset(t);
    tone_int0h(t);
    CHECK(t.spk_output == 1);
    tone_int0h(t);
    CHECK(t.spk_output == 2);
}

int main() {
    test_reset_defaults();
    test_single_ios_does_not_arm();
    test_second_ios_arms();
    test_third_ios_disarms();
    test_fourth_ios_rearms_cycle_repeats();
    test_cycle_free_runs_even_when_off();
    test_cycle_toggles_on_match_and_resets_counter();
    test_int0h_toggles_directly_without_arming();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
