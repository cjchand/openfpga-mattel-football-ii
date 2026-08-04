// Pins the stimulus-file parser. The bug this guards against produced no
// error of any kind -- it just shifted every event after the first to a
// wrong cycle, so runs silently tested a different scenario than intended.
#include "stimulus.h"
#include <cstdio>
#include <cstdlib>

static int failures = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL: %s (line %d)\n", msg, __LINE__); failures++; } \
} while (0)

int main() {
    const char* path = "/tmp/stimulus_test_input.txt";
    { FILE* f = std::fopen(path, "w");
      // Cycles are decimal; values are hex. The cycles here are chosen so
      // that misparsing them as hex gives a very different number:
      // 134500 hex = 1263360, 707000 hex = 7368704.
      std::fprintf(f, "75200 10\n134500 00\n707000 80\n719000 00\n");
      std::fclose(f); }

    auto ev = load_stimulus(path);
    CHECK(ev.size() == 4, "all four events parsed");
    CHECK(ev.count(75200) && ev[75200] == 0x10, "first event: decimal cycle, hex value");
    CHECK(ev.count(134500) && ev[134500] == 0x00,
          "second cycle is decimal, not re-parsed as hex (would be 1263360)");
    CHECK(ev.count(707000) && ev[707000] == 0x80, "third cycle is decimal");
    CHECK(ev.count(719000) && ev[719000] == 0x00, "fourth cycle is decimal");
    CHECK(!ev.count(1263360), "no event at the hex-misparse of 134500");
    CHECK(!ev.count(7368704), "no event at the hex-misparse of 707000");

    // Hex values with letters must still parse as hex.
    { FILE* f = std::fopen(path, "w");
      std::fprintf(f, "1000 ff\n2000 a5\n"); std::fclose(f); }
    ev = load_stimulus(path);
    CHECK(ev.count(1000) && ev[1000] == 0xFF, "hex value ff");
    CHECK(ev.count(2000) && ev[2000] == 0xA5, "hex value a5 at a decimal cycle");

    CHECK(load_stimulus(nullptr).empty(), "null path yields no events");

    if (failures == 0) { std::printf("PASS: stimulus_test\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
