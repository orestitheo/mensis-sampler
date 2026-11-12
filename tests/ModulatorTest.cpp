#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "core/engine/Macros.h"
#include "core/engine/Modulator.h"

using Catch::Matchers::WithinAbs;

TEST_CASE("LFO is locked to the song position and its shapes are right") {
    mensis::Modulator m;
    mensis::ModParams p; // 1 bar (4 beats) sine
    CHECK_THAT(m.next(p, 0.0, 0, 0, 120), WithinAbs(0.0, 1e-6));
    CHECK_THAT(m.next(p, 1.0, 0, 0, 120), WithinAbs(1.0, 1e-6));   // quarter way: top
    CHECK_THAT(m.next(p, 3.0, 0, 0, 120), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(m.next(p, 5.0, 0, 0, 120), WithinAbs(1.0, 1e-6));   // next bar, same place
    CHECK_THAT(mensis::Modulator::lfo(1.0f, 0.25), WithinAbs(1.0, 1e-6));  // square: high first half
    CHECK_THAT(mensis::Modulator::lfo(1.0f, 0.75), WithinAbs(-1.0, 1e-6));
    CHECK_THAT(mensis::Modulator::lfo(2.0f / 3.0f, 0.4), WithinAbs(0.8, 1e-6)); // saw ramps up
    for (float shape : {0.0f, 0.2f, 0.5f, 0.9f, 1.0f})
        for (double ph = 0.0; ph < 1.0; ph += 0.01) {
            const float v = mensis::Modulator::lfo(shape, ph);
            REQUIRE(v >= -1.0f);
            REQUIRE(v <= 1.0f);
        }
}

TEST_CASE("Random: same seed = same values, steps or glides, in range") {
    mensis::ModParams p;
    p.type = mensis::ModType::Random;
    p.rateBeats = 1.0;
    p.seed = 3;
    // Stepped: constant within a step.
    CHECK(mensis::Modulator::random(p, 2.1) == mensis::Modulator::random(p, 2.9));
    CHECK(mensis::Modulator::random(p, 2.5) != mensis::Modulator::random(p, 3.5));
    // Smooth: reaches the next step's value right at the step.
    p.shape = 1.0f;
    const float nextStep = mensis::Modulator::random(p, 4.0);
    CHECK_THAT(mensis::Modulator::random(p, 3.9999), WithinAbs(nextStep, 1e-3));
    auto q = p;
    q.slot = 1; // another slot: different values
    CHECK(mensis::Modulator::random(p, 7.2) != mensis::Modulator::random(q, 7.2));
    for (double b = 0; b < 64; b += 0.05) {
        const float v = mensis::Modulator::random(p, b);
        REQUIRE(v >= -1.0f);
        REQUIRE(v <= 1.0f);
    }
}

TEST_CASE("Env rises with a hit and falls over RATE") {
    mensis::Modulator m;
    mensis::ModParams p;
    p.type = mensis::ModType::Env;
    p.rateBeats = 1.0; // 0.5 s at 120 BPM
    const double dt = 128.0 / 48000.0;
    float v = 0.0f;
    for (int i = 0; i < 20; ++i) v = m.next(p, 0, 1.0f, dt, 120); // 53 ms of a loud hit
    CHECK(v > 0.95f);
    for (int i = 0; i < 187; ++i) v = m.next(p, 0, 0.0f, dt, 120); // 0.5 s of silence
    CHECK(v < 0.01f);  // -60 dB after RATE
    CHECK(v > 0.0f);
}

TEST_CASE("macros at zero change nothing; at full they move the right knobs, within range") {
    mensis::LoopParams lp;
    float send = 0.2f;
    mensis::SpaceParams sp;
    const auto before = lp;
    mensis::applyMacros(mensis::Macros{}, lp, send);
    mensis::applyMacros(mensis::Macros{}, sp);
    CHECK(lp.color.drive == before.color.drive);
    CHECK(lp.grains.mix == before.grains.mix);
    CHECK(send == 0.2f);

    const mensis::Macros all{1.0f, 1.0f, 1.0f, 1.0f};
    mensis::applyMacros(all, lp, send);
    mensis::applyMacros(all, sp);
    CHECK(lp.color.drive > 0.7f);
    CHECK(lp.color.bits <= 4.0f);
    CHECK(lp.grains.mix > 0.7f);
    CHECK(lp.grains.detune <= 12.0f);
    CHECK(lp.res.mix > 0.7f);
    CHECK(lp.res.decay <= 10.0f);
    CHECK(send <= 1.0f);
    CHECK(send > 0.7f);
    CHECK(sp.feedback <= 0.95f);
    CHECK(sp.size <= 1.0f);
}
