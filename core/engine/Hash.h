#pragma once
#include <cstdint>

namespace mensis {

inline uint64_t mix(uint64_t x) { // splitmix64: turns any number into a well-scrambled one
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}

// Seeded random numbers: same state = same sequence.
struct Rng {
    uint64_t state;
    double next() { state = mix(state); return static_cast<double>(state >> 11) * 0x1.0p-53; } // 0..1
};

} // namespace mensis
