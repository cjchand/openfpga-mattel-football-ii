// sim/stimulus.h
//
// Shared parser for testbench stimulus files: whitespace-separated
// "<cycle-decimal> <p_input-hex>" pairs, one event per line.
//
// This lives in a header rather than being copy-pasted into each testbench
// because it silently produced wrong results in both copies: the original
// `f >> cycle >> std::hex >> val` leaves the stream in hex mode, so every
// cycle number after the first was parsed as hex too ("134500" -> 1263360).
// Every event but the first landed at the wrong time, and nothing failed --
// the runs just quietly tested a different scenario than they claimed to.
// See stimulus_test.cpp.
#pragma once
#include <cstdint>
#include <fstream>
#include <map>

inline std::map<long, uint8_t> load_stimulus(const char* path) {
    std::map<long, uint8_t> events;
    if (!path) return events;
    std::ifstream f(path);
    long cycle; unsigned val;
    // std::dec is required: std::hex below is sticky on the stream.
    while (f >> std::dec >> cycle >> std::hex >> val)
        events[cycle] = static_cast<uint8_t>(val);
    return events;
}
