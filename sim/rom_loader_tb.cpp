// Verifies bridge-write loading (dense 1536-byte file) and CPU-side
// 0x600-0x7FF -> 0x400-0x5FF mirror-fold translation.
#include "Vrom_loader.h"
#include "verilated.h"
#include <cstdio>
#include <cstdint>

static int g_failures = 0;
static const char* g_current = "";
#define CHECK(cond, msg) do { \
    if (!(cond)) { std::printf("FAIL [%s]: %s (line %d)\n", g_current, msg, __LINE__); \
                   g_failures++; return; } } while (0)

static const uint32_t SLOT_BASE = 0x10000000;

struct Loader {
    Vrom_loader d;
    Loader() { d.clk = 0; d.eval(); }  // Initialize clk to ensure proper posedge detection
    void tick() { d.clk = 1; d.eval(); d.clk = 0; d.eval(); }

    void write_word(int word_index, uint32_t data) {
        d.bridge_addr = SLOT_BASE + word_index * 4;
        d.bridge_wr_data = data;
        d.bridge_wr = 1;
        tick();
        d.bridge_wr = 0;
    }

    // load a 1536-byte image via 384 word writes, little-endian within each word
    void load(const uint8_t* rom) {
        for (int w = 0; w < 384; w++) {
            uint32_t word = rom[w*4] | (rom[w*4+1] << 8) | (rom[w*4+2] << 16) | (rom[w*4+3] << 24);
            write_word(w, word);
        }
    }

    uint8_t read(uint16_t rom_addr) {
        d.rom_addr = rom_addr;
        d.eval();
        return d.rom_data;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

static void test_load_and_direct_read() {
    Loader l;
    uint8_t rom[1536];
    for (int i = 0; i < 1536; i++) rom[i] = (uint8_t)(i ^ 0x5A);
    l.load(rom);
    CHECK(l.read(0x000) == rom[0x000], "addr 0x000 = file offset 0x000");
    CHECK(l.read(0x5FF) == rom[0x5FF], "addr 0x5FF = file offset 0x5FF (last real byte)");
}

static void test_mirror_fold_0x600_to_0x7ff() {
    Loader l;
    uint8_t rom[1536];
    for (int i = 0; i < 1536; i++) rom[i] = (uint8_t)(i ^ 0x5A);
    l.load(rom);
    CHECK(l.read(0x600) == rom[0x400], "addr 0x600 mirrors file offset 0x400");
    CHECK(l.read(0x7FF) == rom[0x5FF], "addr 0x7FF mirrors file offset 0x5FF (last mirrored byte)");
    CHECK(l.read(0x700) == rom[0x500], "addr 0x700 mirrors file offset 0x500 (middle of mirror range)");
}

static void test_bridge_writes_outside_slot_ignored() {
    Loader l;
    uint8_t rom[1536] = {0};
    l.load(rom);
    l.d.bridge_addr = 0x20000000; l.d.bridge_wr_data = 0xFFFFFFFF; l.d.bridge_wr = 1;
    l.tick();
    l.d.bridge_wr = 0;
    CHECK(l.read(0x000) == 0, "write outside SLOT_BASE range does not touch ROM");
}

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    run_test("load_and_direct_read", test_load_and_direct_read);
    run_test("mirror_fold_0x600_to_0x7ff", test_mirror_fold_0x600_to_0x7ff);
    run_test("bridge_writes_outside_slot_ignored", test_bridge_writes_outside_slot_ignored);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: rom_loader_tb\n");
    return 0;
}
