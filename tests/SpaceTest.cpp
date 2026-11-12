#include <catch2/catch_test_macros.hpp>
#include "core/RenderStats.h"
#include "core/engine/Space.h"

namespace {

constexpr double kRate = 48000.0, kBpm = 120.0;

// Runs Space over a mono input fed to both sides; returns {left, right}.
std::pair<std::vector<float>, std::vector<float>> run(mensis::Space& space, const mensis::SpaceParams& p,
                                                      const std::function<float(int)>& in, int n, int from = 0) {
    std::vector<float> l(static_cast<size_t>(n)), r(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) l[static_cast<size_t>(i)] = r[static_cast<size_t>(i)] = in(from + i);
    for (int off = 0; off < n; off += 512) {
        const int m = std::min(512, n - off);
        space.process(l.data() + off, r.data() + off, m, p, kBpm);
    }
    return {l, r};
}

double energy(const std::vector<float>& x, size_t a, size_t b) {
    double e = 0.0;
    for (size_t i = a; i < std::min(b, x.size()); ++i) e += x[i] * x[i];
    return e;
}

} // namespace

TEST_CASE("space with nothing sent stays silent") {
    mensis::Space space;
    space.prepare(kRate);
    const auto [l, r] = run(space, {}, [](int) { return 0.0f; }, 48000);
    for (float v : l) REQUIRE(v == 0.0f);
}

TEST_CASE("delay echoes land on the beat and ping-pong") {
    mensis::Space space;
    space.prepare(kRate);
    mensis::SpaceParams p;
    p.blend = 0.0f;      // delay only
    p.timeBeats = 1.0;   // 1 beat at 120 BPM = 0.5 s = 24000 samples
    p.feedback = 0.5f;
    std::vector<float> l(72000, 0.0f), r(72000, 0.0f);
    l[0] = 1.0f; // impulse on the left only
    for (int off = 0; off < 72000; off += 512) space.process(l.data() + off, r.data() + off, 512, p, kBpm);
    auto peakAt = [](const std::vector<float>& x) {
        size_t best = 0;
        for (size_t i = 1; i < x.size(); ++i) if (std::abs(x[i]) > std::abs(x[best])) best = i;
        return best;
    };
    // First echo: left at 0.5 s. Second: bounced to the right at 1.0 s.
    CHECK(std::abs(static_cast<int>(peakAt({l.begin(), l.begin() + 36000})) - 24000) <= 2);
    CHECK(std::abs(static_cast<int>(peakAt({r.begin() + 36000, r.end()})) + 36000 - 48000) <= 2);
}

TEST_CASE("reverb rings longer with a bigger size, and is wide") {
    auto tailAfter = [](float size, double seconds) {
        mensis::Space space;
        space.prepare(kRate);
        mensis::SpaceParams p;
        p.blend = 1.0f;     // reverb only
        p.feedback = 0.0f;  // and no echoes feeding it
        p.size = size;
        const auto [l, r] = run(space, p, [](int i) { return i < 48 ? 1.0f : 0.0f; }, static_cast<int>(6.0 * kRate));
        const auto first = energy(l, 0, 24000), late = energy(l, static_cast<size_t>(seconds * kRate), static_cast<size_t>((seconds + 0.5) * kRate));
        return 10.0 * std::log10(late / first + 1e-30);
    };
    const double smallRoom = tailAfter(0.2f, 1.5), bigRoom = tailAfter(0.8f, 1.5);
    CHECK(smallRoom < -40.0); // ~1 s decay: long gone after 1.5 s
    CHECK(bigRoom > -20.0);   // ~10 s decay: still ringing
    // Left and right differ (stereo), so it sounds wide.
    mensis::Space space;
    space.prepare(kRate);
    mensis::SpaceParams p;
    p.blend = 1.0f;
    const auto [l, r] = run(space, p, [](int i) { return i < 48 ? 1.0f : 0.0f; }, 48000);
    double lr = 0.0, ll = 0.0, rr = 0.0;
    for (size_t i = 4800; i < l.size(); ++i) { lr += l[i] * r[i]; ll += l[i] * l[i]; rr += r[i] * r[i]; }
    CHECK(std::abs(lr / std::sqrt(ll * rr)) < 0.5);
}

TEST_CASE("freeze holds the tail, unfreeze lets it go") {
    mensis::Space space;
    space.prepare(kRate);
    mensis::SpaceParams p;
    p.blend = 0.5f;
    p.size = 0.3f;
    juce::Random rng(4);
    run(space, p, [&](int) { return 0.3f * (rng.nextFloat() * 2.0f - 1.0f); }, 48000); // fill it up
    p.freeze = true;
    const auto [held, unusedR] = run(space, p, [&](int) { return 0.5f * (rng.nextFloat() * 2.0f - 1.0f); }, static_cast<int>(10.0 * kRate));
    const double early = energy(held, 0, 48000), late = energy(held, static_cast<size_t>(9.0 * kRate), static_cast<size_t>(10.0 * kRate));
    CHECK(early > 0.0);
    CHECK(10.0 * std::log10(late / early) > -6.0); // still there after 10 s (input ignored while frozen)
    CHECK(mensis::measure(held).finite);
    p.freeze = false;
    const auto [gone, unusedR2] = run(space, p, [](int) { return 0.0f; }, static_cast<int>(6.0 * kRate));
    CHECK(energy(gone, static_cast<size_t>(5.0 * kRate), static_cast<size_t>(6.0 * kRate)) < 1e-6 * early);
}

TEST_CASE("space at the extremes stays finite and bounded") {
    mensis::Space space;
    space.prepare(kRate);
    mensis::SpaceParams p{4.0, 0.95f, 0.5f, 1.0f, 1.0f, 2.0f, false};
    juce::Random rng(9);
    const auto [l, r] = run(space, p, [&](int) { return rng.nextFloat() * 2.0f - 1.0f; }, static_cast<int>(10.0 * kRate));
    const auto stats = mensis::measure(l);
    CHECK(stats.finite);
    CHECK(stats.peak < 20.0f);
}

TEST_CASE("changing the delay time glides instead of clicking") {
    mensis::Space space;
    space.prepare(kRate);
    mensis::SpaceParams p;
    p.blend = 0.0f;
    auto tone = [](int i) { return 0.3f * std::sin(2.0f * juce::MathConstants<float>::pi * 220.0f * i / 48000.0f); };
    run(space, p, tone, 48000);
    p.timeBeats = 0.25;
    const auto [l, r] = run(space, p, tone, 48000, 48000);
    CHECK(mensis::measure(l).maxStep < 0.1f); // 0.3 * 220 Hz steps ~0.009; a jump in the read point would be far bigger
}
