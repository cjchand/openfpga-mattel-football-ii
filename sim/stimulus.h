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
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <map>

inline std::map<long, uint8_t> load_stimulus(const char* path) {
    std::map<long, uint8_t> events;
    if (!path) return events;
    std::ifstream f(path);
    // A path that was asked for but cannot be opened must be loud. Returning
    // an empty event map instead silently converts the run into the
    // no-input scenario, which still "passes" -- the same silent-failure
    // shape as the sticky-std::hex bug this file was created for.
    if (!f) {
        std::fprintf(stderr, "load_stimulus: cannot open '%s'\n", path);
        std::exit(2);
    }
    // Line-oriented so that '#' comments are supported. Parsing token-wise
    // instead meant a leading comment line aborted the whole parse on its
    // first token, yielding zero events with no complaint -- the run then
    // silently became the no-input scenario.
    std::string line;
    while (std::getline(f, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        std::istringstream ls(line);
        long cycle; unsigned val;
        // std::dec is required: std::hex below is sticky on the stream.
        if (ls >> std::dec >> cycle >> std::hex >> val)
            events[cycle] = static_cast<uint8_t>(val);
    }
    // A stimulus file that parses to nothing is a mistake every time --
    // wrong format, wrong file. Failing here beats running the no-input
    // scenario under the name of a scripted one.
    if (events.empty()) {
        std::fprintf(stderr, "load_stimulus: '%s' contained no usable events\n", path);
        std::exit(2);
    }
    return events;
}
