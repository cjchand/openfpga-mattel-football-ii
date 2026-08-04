#include "Vpps41_io.h"
#include "verilated.h"
#include <cstdio>

static void tick(Vpps41_io* dut) {
    dut->clk = 0; dut->eval();
    dut->clk = 1; dut->eval();
}

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); failures++; } \
} while (0)

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    Vpps41_io* dut = new Vpps41_io;
    dut->rst_n = 0; dut->sos_fire = 0; dut->ros_fire = 0; dut->ioa_fire = 0; dut->ox_fire = 0;
    dut->d_input = 0; dut->ram_addr = 0; dut->a_in = 0; dut->c_in = 0; dut->a_out_for_ioa = 0;
    dut->dbg_p_set = 0; dut->p_set_en = 0;
    tick(dut);
    dut->rst_n = 1;

    CHECK(dut->r_output == 0x3FF);
    CHECK(dut->d_output == 0);

    dut->sos_fire = 1; dut->ram_addr = 0x05;
    tick(dut);
    dut->sos_fire = 0;
    CHECK(dut->d_output == (1u << 5));

    dut->ram_addr = 0x05; dut->eval();
    CHECK(dut->skisl_skip == 0);
    dut->ram_addr = 0x06; dut->eval();
    CHECK(dut->skisl_skip == 1);

    dut->ram_addr = 0x40 | 0x06; dut->eval();
    CHECK(dut->skisl_skip == 0);

    dut->ioa_fire = 1; dut->a_in = 0x7; dut->c_in = 1;
    tick(dut);
    dut->ioa_fire = 0;
    CHECK((dut->r_output & 0x1F) == ((1 << 4) | 0x7));

    dut->ox_fire = 1; dut->a_in = 0xA; dut->c_in = 0;
    tick(dut);
    dut->ox_fire = 0;
    CHECK(((dut->r_output >> 5) & 0x1F) == 0xA);

    dut->dbg_p_set = 0xA5; dut->p_set_en = 1;
    tick(dut);
    dut->p_set_en = 0;
    dut->eval();
    CHECK(dut->i2c_a == ((~0xA5 >> 4) & 0xF));

    // SKISL must read the pin, i.e. d_output OR the external d_input --
    // this is how the ROM reads the PRO 1 / PRO 2 switch on DIO10 after
    // releasing that pin with ROS. Mirrors the golden model's
    // test_skisl_reads_the_d_input_pins_too.
    dut->ram_addr = 10;          // select DIO10
    dut->ros_fire = 1; tick(dut); dut->ros_fire = 0;  // release the pin
    dut->d_input = 0;
    dut->eval();
    CHECK(dut->skisl_skip == 1); // switch open -> pin low -> skip
    dut->d_input = 1 << 10;
    dut->eval();
    CHECK(dut->skisl_skip == 0); // switch closed -> pin high -> no skip
    // A CPU-driven high still reads high regardless of the external input.
    dut->d_input = 0;
    dut->sos_fire = 1; tick(dut); dut->sos_fire = 0;
    dut->eval();
    CHECK(dut->skisl_skip == 0);
    // An input on DIO10 must not disturb an unrelated pin.
    dut->ram_addr = 3;
    dut->ros_fire = 1; tick(dut); dut->ros_fire = 0;
    dut->d_input = 1 << 10;
    dut->eval();
    CHECK(dut->skisl_skip == 1);

    delete dut;
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
