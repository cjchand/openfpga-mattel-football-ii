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
    Loader() { d.clk = 0; d.rd_clk = 0; d.eval(); }  // Initialize clocks to ensure proper posedge detection
    void tick() { d.clk = 1; d.eval(); d.clk = 0; d.eval(); }
    void rd_tick() { d.rd_clk = 1; d.eval(); d.rd_clk = 0; d.eval(); }

    void write_word(int word_index, uint32_t data) {
        d.bridge_addr = SLOT_BASE + word_index * 4;
        d.bridge_wr_data = data;
        d.bridge_wr = 1;
        tick();
        d.bridge_wr = 0;
    }

    // load a 1536-byte image via 384 word writes. Big-endian within each
    // word (file byte 0 -> bits[31:24]) -- this is how APF actually packs
    // bridge writes on real hardware (confirmed in the sibling FB1
    // project via a debug readback); rom_loader.v's byte_sel extraction
    // is reversed to match.
    void load(const uint8_t* rom) {
        for (int w = 0; w < 384; w++) {
            uint32_t word = (rom[w*4] << 24) | (rom[w*4+1] << 16) | (rom[w*4+2] << 8) | rom[w*4+3];
            write_word(w, word);
        }
    }

    // The read is registered on rd_clk (see rom_loader.v): present the
    // address, take one read clock, then the data is valid. In the core this
    // latency is absorbed by the ~129 core clocks between ce pulses.
    uint8_t read(uint16_t rom_addr) {
        d.rom_addr = rom_addr;
        d.eval();
        rd_tick();
        return d.rom_data;
    }

    // Reads WITHOUT clocking, i.e. what the output holds while the new
    // address is still propagating. Used to pin that the latency is exactly
    // one cycle rather than zero or two.
    uint8_t read_unclocked(uint16_t rom_addr) {
        d.rom_addr = rom_addr;
        d.eval();
        return d.rom_data;
    }
};

static void run_test(const char* name, void (*fn)(void)) { g_current = name; fn(); }

// Pins the read pipeline depth. Registering the word read is what lets
// Quartus infer an M10K for `mem`, but it costs a cycle, and the exact depth
// matters: two cycles would feed the CPU the PREVIOUS instruction's byte on
// every fetch, which is a working-looking core running a shifted program.
static void test_read_latency_is_exactly_one_cycle() {
    Loader l;
    uint8_t rom[1536];
    for (int i = 0; i < 1536; i++) rom[i] = (uint8_t)(i ^ 0x5A);
    l.load(rom);
    l.read(0x000);                       // settle on a known address
    uint8_t before = l.read_unclocked(0x123);
    CHECK(before == rom[0x000], "output still holds the old byte before the read clock");
    l.rd_tick();
    CHECK(l.d.rom_data == rom[0x123], "one read clock after the address, the new byte is out");
    l.rd_tick();
    CHECK(l.d.rom_data == rom[0x123], "and it stays put -- the latency is one cycle, not two");
}

// Walks consecutive addresses the way a CPU fetch does, including crossing a
// 32-bit word boundary (0x003 -> 0x004), which is where a byte-select that
// was not registered alongside its word would come apart.
static void test_consecutive_reads_track_the_address() {
    Loader l;
    uint8_t rom[1536];
    for (int i = 0; i < 1536; i++) rom[i] = (uint8_t)(i * 7 + 3);
    l.load(rom);
    for (uint16_t a = 0x000; a < 0x010; a++)
        CHECK(l.read(a) == rom[a], "consecutive read matches the file, across word boundaries");
}

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
    run_test("read_latency_is_exactly_one_cycle", test_read_latency_is_exactly_one_cycle);
    run_test("consecutive_reads_track_the_address", test_consecutive_reads_track_the_address);
    run_test("load_and_direct_read", test_load_and_direct_read);
    run_test("mirror_fold_0x600_to_0x7ff", test_mirror_fold_0x600_to_0x7ff);
    run_test("bridge_writes_outside_slot_ignored", test_bridge_writes_outside_slot_ignored);
    if (g_failures) { std::printf("FAILED: %d check(s)\n", g_failures); return 1; }
    std::printf("PASS: rom_loader_tb\n");
    return 0;
}
