// sim/golden/trace_main.cpp
#include "mm77la_model.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <rom-file> <cycle-count>\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) { std::fprintf(stderr, "cannot open %s\n", argv[1]); return 2; }
    std::fseek(f, 0, SEEK_END);
    long size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> rom(static_cast<size_t>(size));
    if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) {
        std::fprintf(stderr, "short read on %s\n", argv[1]);
        std::fclose(f);
        return 2;
    }
    std::fclose(f);

    long cycles = std::strtol(argv[2], nullptr, 10);
    Mm77laModel m(rom.data(), rom.size());
    m.reset();

    std::printf("cycle,pc,a,b,x,c,c_in,stack0,stack1,skip,skip_count\n");
    for (long i = 0; i < cycles; i++) {
        m.step();
        const auto& s = m.state();
        std::printf("%ld,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u\n",
            i, s.pc, s.a, s.b, s.x, s.c, s.c_in, s.stack[0], s.stack[1],
            s.skip ? 1 : 0, s.skip_count);
    }
    return 0;
}
