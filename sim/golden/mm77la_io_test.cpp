#include "mm77la_io.h"
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

static void test_reset_values() {
    IoState io; io.r_output = 0; io.d_output = 0xFFF; io.p_input = 0x42;
    io_reset(io);
    CHECK(io.r_output == 0x3FF);
    CHECK(io.d_output == 0);
    CHECK(io.p_input == 0x00);
}

static void test_sos_sets_d_pin() {
    IoState io; io_reset(io);
    io_sos(io, 0x05);
    CHECK(io.d_output == (1u << 5));
}

static void test_ros_clears_d_pin() {
    IoState io; io_reset(io);
    io.d_output = 0xFFF;
    io_ros(io, 0x0B);
    CHECK(io.d_output == static_cast<uint16_t>(0xFFF & ~(1u << 11)));
}

static void test_sos_invalid_b7_high_is_noop() {
    IoState io; io_reset(io);
    io_sos(io, 0x40 | 0x05);
    CHECK(io.d_output == 0);
}

static void test_sos_bl_12_to_15_is_noop_not_interrupt_flag() {
    for (uint8_t bl = 12; bl <= 15; bl++) {
        IoState io2; io_reset(io2);
        io_sos(io2, bl);
        CHECK(io2.d_output == 0);
    }
}

static void test_skisl_reads_the_d_input_pins_too() {
    // SKISL tests (d_output | d_input), per MAME mm78op.cpp::op_skisl():
    //   m_skip = !BIT((m_d_output | m_read_d()) & m_d_mask, bl)
    // The D bus is bidirectional: the ROM releases a pin with ROS and then
    // reads whatever is driving it externally. On this board that external
    // driver is the PRO 1 / PRO 2 difficulty switch on DIO10.
    //
    // The real ROM does exactly that at 0x35E-0x371:
    //   LB 10; EOB 2; L 0; AISK 15; (T skipped); ROS; SKISL; LAI 3 / LAI 4
    // i.e. it selects pin 10, releases it, tests it, and loads a different
    // constant for each switch position. Reading only d_output -- which is
    // what this model did before -- pins the game to PRO 1 forever.
    IoState io;
    io_reset(io);
    // Pin 10 released (not driven by the CPU) and the switch open: skip.
    io.d_input = 0x000;
    CHECK(io_skisl(io, 10) == true);
    // Switch closed pulls DIO10 high: no skip, so the ROM takes the other
    // branch and loads the other difficulty constant.
    io.d_input = 0x400;
    CHECK(io_skisl(io, 10) == false);
    // d_output still ORs in: a pin the CPU is actively driving high reads
    // high regardless of the external input.
    io.d_input = 0x000;
    io_sos(io, 10);
    CHECK(io_skisl(io, 10) == false);
    // An unrelated input bit must not disturb other pins.
    io_reset(io);
    io.d_input = 0x400;
    CHECK(io_skisl(io, 3) == true);
}

static void test_skisl_skips_when_pin_clear() {
    IoState io; io_reset(io);
    CHECK(io_skisl(io, 0x03) == true);
    io.d_output = (1u << 3);
    CHECK(io_skisl(io, 0x03) == false);
}

static void test_i2c_reads_upper_p_nibble_inverted() {
    IoState io; io_reset(io);
    io.p_input = 0xA5;
    CHECK(io_i2c(io) == static_cast<uint8_t>((~0xA5 >> 4) & 0xF));
}

static void test_ioa_writes_lower_half_with_delayed_carry() {
    IoState io; io_reset(io);
    uint8_t a = 0x7;
    io_ioa(io, a, 1);
    CHECK((io.r_output & 0x1F) == ((1 << 4) | 0x7));
    CHECK((io.r_output & ~0x1Fu) == (0x3FF & ~0x1Fu));
}

static void test_ox_writes_upper_half_with_delayed_carry() {
    IoState io; io_reset(io);
    io_ox(io, 0xA, 0);
    CHECK(((io.r_output >> 5) & 0x1F) == ((0 << 4) | 0xA));
    CHECK((io.r_output & 0x1F) == (0x3FF & 0x1F));
}

static void test_i1sk_adds_p_input_and_skips_on_no_overflow() {
    IoState io; io_reset(io);
    io.p_input = 0x03;
    uint8_t a = 0x02;
    bool skip = io_i1sk(io, a);
    CHECK(a == 0x5);
    CHECK(skip == true);
}

static void test_i1sk_no_skip_on_overflow() {
    IoState io; io_reset(io);
    io.p_input = 0x0F;
    uint8_t a = 0x0E;
    bool skip = io_i1sk(io, a);
    CHECK(a == ((0x0E + 0x0F) & 0xF));
    CHECK(skip == false);
}

int main() {
    test_reset_values();
    test_sos_sets_d_pin();
    test_ros_clears_d_pin();
    test_sos_invalid_b7_high_is_noop();
    test_sos_bl_12_to_15_is_noop_not_interrupt_flag();
    test_skisl_reads_the_d_input_pins_too();
    test_skisl_skips_when_pin_clear();
    test_i2c_reads_upper_p_nibble_inverted();
    test_ioa_writes_lower_half_with_delayed_carry();
    test_ox_writes_upper_half_with_delayed_carry();
    test_i1sk_adds_p_input_and_skips_on_no_overflow();
    test_i1sk_no_skip_on_overflow();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
