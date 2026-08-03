#include "mm77la_display_mux.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_rowsel_is_d_low_10_bits() {
    uint16_t rowsel, rowdata;
    display_mux(0xFFF, 0x000, rowsel, rowdata);
    CHECK(rowsel == 0x3FF); // D bits 10/11 excluded from rowsel
}

static void test_rowdata_r_low_7_bits_direct() {
    uint16_t rowsel, rowdata;
    display_mux(0x000, 0x07F, rowsel, rowdata);
    CHECK(rowdata == 0x07F);
}

static void test_rowdata_r_high_3_bits_shift_to_8_10() {
    uint16_t rowsel, rowdata;
    display_mux(0x000, 0x380, rowsel, rowdata); // R bits 7,8,9 set
    CHECK(rowdata == 0x700); // land at rowdata bits 8,9,10
}

static void test_d11_is_sole_source_of_column_bit_7() {
    uint16_t rowsel, rowdata;
    // D[11] set, R fully clear -- column bit 7 must still be set
    display_mux(0x800, 0x000, rowsel, rowdata);
    CHECK((rowdata & 0x80) == 0x80);
    // R's bits (0-9) can NEVER set column bit 7 on their own
    display_mux(0x000, 0x3FF, rowsel, rowdata);
    CHECK((rowdata & 0x80) == 0x00);
}

static void test_d10_dp_pin_not_consumed() {
    uint16_t rowsel, rowdata;
    display_mux(0x400, 0x000, rowsel, rowdata); // only D[10] set
    CHECK(rowsel == 0x000); // excluded from rowsel (bit 10 is above the mask)
    CHECK(rowdata == 0x000); // not folded into rowdata either
}

int main() {
    test_rowsel_is_d_low_10_bits();
    test_rowdata_r_low_7_bits_direct();
    test_rowdata_r_high_3_bits_shift_to_8_10();
    test_d11_is_sole_source_of_column_bit_7();
    test_d10_dp_pin_not_consumed();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
