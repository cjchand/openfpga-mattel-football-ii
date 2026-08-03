#include "mm77la_opla.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main() {
    CHECK(opla_ix(0x0) == 0x03F); // standard 7-seg "0"
    CHECK(opla_ix(0xF) == 0x000); // blank
    CHECK(opla_ix(0x1) == 0x006);
    CHECK(opla_ix(0xC) == 0x200);
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
