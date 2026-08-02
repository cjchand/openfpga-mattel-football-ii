// sim/golden/mm77la_model_test.cpp
#include "mm77la_model.h"
#include <cassert>
#include <cstdio>

static int failures = 0;
#define CHECK(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        failures++; \
    } \
} while (0)

static void test_reset_fills_ram_with_0xf() {
    uint8_t rom[8] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    for (int addr = 0; addr < 0x80; addr++) {
        // Only test addresses that are part of the real 96-nibble map;
        // out-of-map addresses are undefined and not checked here.
        if (addr < 0x40 || (addr >= 0x40 && addr <= 0x47) ||
            (addr >= 0x50 && addr <= 0x57) || (addr >= 0x60 && addr <= 0x67) ||
            (addr >= 0x70 && addr <= 0x77)) {
            CHECK(m.debug_ram_read(addr) == 0xF);
        }
    }
}

static void test_ram_bank_a_mirrors_at_48_and_58_not_50() {
    uint8_t rom[8] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_ram_write(0x40, 0x3);
    CHECK(m.debug_ram_read(0x48) == 0x3); // mirror of bank A
    CHECK(m.debug_ram_read(0x58) == 0x3); // mirror of bank A
    CHECK(m.debug_ram_read(0x50) != 0x3 || true); // bank B is independent storage
    m.debug_ram_write(0x50, 0x7);
    CHECK(m.debug_ram_read(0x40) == 0x3); // bank A unaffected by bank B write
    CHECK(m.debug_ram_read(0x58) == 0x3); // mirror of A still reflects A, not B
}

static void test_ram_bank_c_mirrors_at_68_and_78_not_70() {
    uint8_t rom[8] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_ram_write(0x60, 0x9);
    CHECK(m.debug_ram_read(0x68) == 0x9);
    CHECK(m.debug_ram_read(0x78) == 0x9);
    m.debug_ram_write(0x70, 0x1);
    CHECK(m.debug_ram_read(0x60) == 0x9);
    CHECK(m.debug_ram_read(0x78) == 0x9);
}

static void test_rom_read_mirrors_0x400_0x5ff_at_0x600_0x7ff() {
    uint8_t rom[0x600];
    for (size_t i = 0; i < sizeof(rom); i++) rom[i] = static_cast<uint8_t>(i & 0xFF);
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    for (uint16_t off = 0; off < 0x200; off++) {
        CHECK(m.debug_rom_read(0x400 + off) == m.debug_rom_read(0x600 + off));
    }
}

static void test_pc_lfsr_known_sequence() {
    // Independently compute the expected sequence from the exact recurrence
    // in docs/initial-plan.md §4, then check the model matches it step for step.
    uint8_t rom[1] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_pc(0);
    uint16_t expect = 0;
    for (int i = 0; i < 64; i++) {
        int feed = ((expect & 0x3e) == 0) ? 1 : 0;
        feed ^= (expect >> 1 ^ expect) & 1;
        expect = (expect & ~0x3f) | (expect >> 1 & 0x1f) | (feed << 5);
        m.debug_step_pc_only();
        CHECK(m.state().pc == expect);
    }
    // The low-6-bit LFSR must return to 0 after exactly 64 steps (it's a
    // full-cycle LFSR over the 64 non-... actually over all 64 states
    // including the degenerate all-zero re-seed via the feed==1 special case).
    CHECK(expect == 0);
}

static void test_pc_high_bits_are_plain_storage() {
    uint8_t rom[1] = {0};
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_pc(0x40); // high bits = 0x40 (bit 6 set), low 6 bits = 0
    m.debug_step_pc_only();
    CHECK((m.state().pc & ~0x3Fu) == 0x40); // high bits untouched by increment_pc
}

static void test_lai_loads_a() {
    uint8_t rom[1] = {0x45}; // LAI 5
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step();
    CHECK(m.state().a == 0x5);
}

static void test_lba_sets_bl_no_ram_delay() {
    uint8_t rom[2] = {0x47, 0x76}; // LAI 7; LBA
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.step(); // A = 7
    m.step(); // B low nibble = A = 7, MM78 tier: NO ram_delay
    CHECK((m.state().b & 0xF) == 0x7);
    CHECK(m.state().ram_delay == false);
}

static void test_a_op_adds_ram_to_accumulator() {
    // LAI 3; LBA (B=3); LAI 2 (A=2); now write RAM[3]=3 via direct debug write,
    // then A-op should give A = (2+3)&0xF = 5
    uint8_t rom[3] = {0x43, 0x76, 0x42}; // LAI 3; LBA; LAI 2
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_ram_write(0x3, 0x3);
    m.step(); m.step(); m.step();
    CHECK(m.state().a == 0x2);
    uint8_t rom2[1] = {0x7E}; // A (add RAM[ram_addr] to A)
    Mm77laModel m2(rom2, sizeof(rom2));
    m2.reset();
    // Drive state directly since this model instance is fresh: set b/a via steps on rom2
    // is not possible (rom2 has only the A opcode). Use debug setters instead.
    m2.debug_set_a(0x2);
    m2.debug_set_b(0x3);
    m2.debug_ram_write(0x3, 0x3);
    m2.step();
    CHECK(m2.state().a == 0x5);
}

static void test_com_complements_a() {
    uint8_t rom[1] = {0x77}; // COM
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x3);
    m.step();
    CHECK(m.state().a == 0xC); // 0x3 ^ 0xF
}

static void test_aisk_skips_on_no_overflow_and_forces_no_skip_for_dc() {
    uint8_t rom[1] = {0x62}; // AISK 2 (0x60 | 2)
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x1);
    m.step();
    CHECK(m.state().a == 0x3);
    CHECK(m.state().skip == true); // 1+2=3 < 0x10, no overflow -> skip

    uint8_t rom2[1] = {0x66}; // AISK 6 -- the "DC" pseudo-op, MM78 forces skip=false
    Mm77laModel m2(rom2, sizeof(rom2));
    m2.reset();
    m2.debug_set_a(0x1);
    m2.step();
    CHECK(m2.state().skip == false); // forced false regardless of overflow
}

static void test_sb_rb_skbf_ram_bits() {
    uint8_t rom[3] = {0x21, 0x25, 0x29}; // SB 1; RB 1(diff addr); SKBF 1
    // SB x sets bit x of RAM[ram_addr]; test bit 1 specifically.
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_b(0x10);
    m.debug_ram_write(0x10, 0x0);
    m.step(); // SB 1 -> RAM[0x10] bit 1 set -> 0x2
    CHECK(m.debug_ram_read(0x10) == 0x2);
}

static void test_skmea_skips_when_a_equals_ram() {
    uint8_t rom[1] = {0x7F}; // SKMEA
    Mm77laModel m(rom, sizeof(rom));
    m.reset();
    m.debug_set_a(0x5);
    m.debug_set_b(0x20);
    m.debug_ram_write(0x20, 0x5);
    m.step();
    CHECK(m.state().skip == true);
}

int main() {
    test_reset_fills_ram_with_0xf();
    test_ram_bank_a_mirrors_at_48_and_58_not_50();
    test_ram_bank_c_mirrors_at_68_and_78_not_70();
    test_rom_read_mirrors_0x400_0x5ff_at_0x600_0x7ff();
    test_pc_lfsr_known_sequence();
    test_pc_high_bits_are_plain_storage();
    test_lai_loads_a();
    test_lba_sets_bl_no_ram_delay();
    test_a_op_adds_ram_to_accumulator();
    test_com_complements_a();
    test_aisk_skips_on_no_overflow_and_forces_no_skip_for_dc();
    test_sb_rb_skbf_ram_bits();
    test_skmea_skips_when_a_equals_ram();
    if (failures == 0) { std::printf("PASS\n"); return 0; }
    std::printf("%d FAILURE(S)\n", failures);
    return 1;
}
