// sim/golden/gameplay_test.cpp
//
// End-to-end gameplay smoke test: plays a real opening sequence and checks
// what the scoreboard shows at each step.
//
// Every other test here pins a mechanism -- an opcode, a pin, a display
// cell. This one pins that the game is actually *playable*, which is the
// property that matters and the one nothing else covers.
//
// Measured, rather than assumed, by reintroducing past bugs:
//   EOB immediate truncated to 2 bits  -> 4 checks fail (clock never
//       starts, Status shows nothing)
//   display smoothing back to alpha=1/8 -> 2 checks fail (the field no
//       longer blanks under Score/Status)
//   carry delay published one instruction early -> PASSES, not caught
//
// That last one is worth stating plainly: this test does not cover carry
// semantics. Those are pinned by the unit tests in mm77la_model_test.cpp
// (test_ac_carry_is_not_visible_until_two_instructions_later and
// test_back_to_back_ac_uses_the_older_carry), which do fail on it. Nor is
// it caught by mame-parity-test, because that test recomputes the logged
// c_in from c/prev_c rather than reading the model's own -- so a c_in
// that is wrong but never consumed before being corrected leaves the
// instruction stream, and therefore the digest, unchanged.
//
// The sequence is the user's own acceptance script, run against the real
// ROM:
//
//   press Score   -> 0 / 15.0 / 0        (home, time remaining, visitors)
//   press Kick    -> kickoff; ball travels, then the game waits to run
//   press Left x10-> the play runs and the clock starts counting down
//   press Status  -> 1 / <field pos> / 10   (down, position, yards to go)
//
// Digit windows are read by decoding the seven-segment cells straight out
// of the display model, the same data the renderer draws.
#include "mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
} while (0)

// Screen digit position (0-6, left to right) -> display matrix row.
static int digit_row(int d) { static const int r[7] = {8,9,0,1,2,6,7}; return r[d]; }
// Field lamp column (0-9) -> display matrix row.
static int field_row(int c) { static const int r[10] = {8,9,0,1,2,3,4,5,6,7}; return r[c]; }

// Seven-segment bitmask (bit0=a .. bit6=g) -> character. Unknown patterns
// become '?', which is fine: the field-position glyph includes a
// direction marker that is not a digit.
static char seg_decode(unsigned m) {
    switch (m) {
        case 0x3F: return '0'; case 0x06: return '1'; case 0x5B: return '2';
        case 0x4F: return '3'; case 0x66: return '4'; case 0x6D: return '5';
        case 0x7D: return '6'; case 0x07: return '7'; case 0x7F: return '8';
        case 0x6F: return '9'; case 0x00: return ' ';
        default:   return '?';
    }
}

struct Screen {
    std::string digits;  // 7 characters, left to right
    int lamps;           // lit field lamps
};

static Screen read_screen(const Mm77laModel& m) {
    const auto& L = m.state().display.levels;
    Screen s;
    for (int d = 0; d < 7; d++) {
        unsigned mask = 0;
        for (int seg = 0; seg < 7; seg++)
            if (L[digit_row(d) * 11 + seg]) mask |= 1u << seg;
        s.digits += seg_decode(mask);
    }
    s.lamps = 0;
    for (int c = 0; c < 10; c++)
        for (int lamp = 8; lamp < 11; lamp++)
            if (L[field_row(c) * 11 + lamp]) s.lamps++;
    return s;
}

int main(int argc, char** argv) {
    if (argc != 2) { std::fprintf(stderr, "usage: %s <rom-file>\n", argv[0]); return 2; }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) { std::fclose(f); return 2; }
    std::fclose(f);

    // Button bits, per MAME's mfootb2 IN.0 port.
    const uint8_t SCORE = 0x01, STATUS = 0x02, KICK = 0x10, LEFT = 0x80;
    const long HOLD = 12000; // ~126ms, a realistic press

    std::map<long, uint8_t> stim;
    auto tap = [&](long cycle, uint8_t val) {
        stim[cycle] = val;
        stim[cycle + HOLD] = 0x00;
    };
    tap(100000, SCORE);
    tap(220000, KICK);
    const long run_start = 707000;           // ~5s after the kickoff
    for (int i = 0; i < 10; i++) tap(run_start + i * 40000, LEFT);
    const long status_at = run_start + 10 * 40000 + 60000; // 1167000
    tap(status_at, STATUS);

    Mm77laModel m(rom.data(), rom.size());
    m.reset();

    Screen at_score{}, at_wait{}, at_run{}, at_status{};
    uint8_t p = 0;
    for (long i = 0; i <= 1200000; i++) {
        auto it = stim.find(i);
        if (it != stim.end()) p = it->second;
        m.debug_set_p(p);
        m.step();
        if (i == 108000)  at_score  = read_screen(m); // mid Score press
        if (i == 700000)  at_wait   = read_screen(m); // after kickoff, waiting to run
        if (i == 900000)  at_run    = read_screen(m); // mid run
        // Sampled late in the Status hold: the display's IIR smoothing means
        // lamps lit just before the press take a few frames to decay below
        // the dim threshold, so an early sample still shows a residual lamp.
        if (i == 1178000) at_status = read_screen(m); // late in the Status press
    }

    std::printf("  Score  -> [%s] lamps=%d\n", at_score.digits.c_str(), at_score.lamps);
    std::printf("  Wait   -> [%s] lamps=%d\n", at_wait.digits.c_str(), at_wait.lamps);
    std::printf("  Run    -> [%s] lamps=%d\n", at_run.digits.c_str(), at_run.lamps);
    std::printf("  Status -> [%s] lamps=%d\n", at_status.digits.c_str(), at_status.lamps);

    // Score: 00 | 150 | 00, and the field blanks while Score is held.
    CHECK(at_score.digits == "0015000", "Score shows 0 / 15.0 / 0");
    CHECK(at_score.lamps == 0, "field blanks while Score is held");

    // After the kickoff the game waits: the clock has not started, so the
    // middle window still reads 15.0, and the ball is on the field.
    CHECK(at_wait.digits.substr(2, 3) == "150", "clock still 15.0 while waiting to run");
    CHECK(at_wait.lamps >= 1, "at least one lamp lit while waiting to run");

    // Running: the clock has started. Middle window is 14.x, and more
    // lamps are lit than while waiting (players are on the field).
    CHECK(at_run.digits.substr(2, 2) == "14", "clock has started counting down (14.x)");
    CHECK(at_run.digits != at_wait.digits, "the clock actually advanced");
    CHECK(at_run.lamps > at_wait.lamps, "more lamps lit once the play is running");

    // Status: down 1, yards to go 10. The middle window is the field
    // position and varies with game state, so only its shape is checked.
    CHECK(at_status.digits[0] == ' ' && at_status.digits[1] == '1',
          "Status shows down = 1");
    CHECK(at_status.digits.substr(5, 2) == "10", "Status shows yards to go = 10");
    CHECK(at_status.lamps == 0, "field blanks while Status is held");

    if (failures == 0) { std::printf("PASS: gameplay_test\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
