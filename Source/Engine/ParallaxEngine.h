#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../Host/HostedInstance.h"
#include "../Host/DelayRing.h"

namespace plx
{
constexpr int kNumChains = 3;
constexpr int kMaxSlots = 8;

// ITU-R BS.1770 K-weighting (high-shelf + high-pass), stereo. Loudness that follows the ear:
// distortion that adds highs counts as louder, like it sounds.
class KWeighting
{
public:
    void prepare (double fs);
    void reset();
    // returns the sum of squared K-weighted samples over both channels
    double process (const float* l, const float* r, int n) noexcept;

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        double z1[2] {}, z2[2] {};
        inline double run (int ch, double x) noexcept
        {
            const double y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
    };
    Biquad shelf, hp;
};

enum class LockState : int { off = 0, matching = 1, locked = 2 };

struct EngineParams
{
    float amount = 60.0f;                 // 0..100: dry <-> parallel (equal power)
    std::array<float, kNumChains> level { 70.0f, 70.0f, 70.0f };   // 0..100 per chain
    bool lock = true;
    int solo = -1;                        // -1 none, else chain index (played at dry loudness)
};

// Audio engine. Chains are edited on the message thread under chainLock; the audio thread only
// try-locks it, so loading a plug-in never blocks audio for longer than a pointer update.
class ParallaxEngine
{
public:
    // ---- tuning (public for tests)
    double measureSeconds = 1.5;    // matching: listen this long (after the last change), then move once
    double jumpSeconds = 0.05;      // time constant of the single compensation move (~150 ms to settle)
    double driftDb = 1.5;           // locked: each window is checked; off by more than this -> re-match
                                    // (e.g. Drive raised inside a hosted plug-in)
    double averageSeconds = 3.0;    // running averages used for drift and readouts
    double gateDb = -70.0;          // ignore silence

    void prepare (double sampleRate, int maxBlock);
    void process (juce::AudioBuffer<float>& io, const EngineParams& p) noexcept;

    // ---- message thread (hold chainLock while changing `chains`, then call updateLatency)
    juce::SpinLock chainLock;
    std::array<std::vector<HostedInstance*>, kNumChains> chains;
    void updateLatency();           // call with chainLock held
    int latency() const noexcept { return maxLatency.load(); }
    int chainLatency (int c) const noexcept { return chainLat[(size_t) c].load(); }

    void requestRelearn() noexcept { relearnFlag = true; }
    void structureChanged() noexcept { structureFlag = true; }

    // saved with the project so a reopened set sounds the same right away
    float savedCompensation() const noexcept { return compTargetDb.load(); }
    bool hasLockedValue() const noexcept { return everLocked.load(); }
    void restoreCompensation (float db) noexcept;

    // ---- readouts (any thread)
    std::atomic<int> state { (int) LockState::matching };
    std::atomic<float> compDb { 0.0f }, deltaDb { 0.0f }, dryLufs { -100.0f }, outLufs { -100.0f };
    std::atomic<float> matchProgress { 0.0f };
    std::atomic<float> soloGain { 1.0f };

private:
    void startMatching() noexcept;

    double fs = 44100.0;
    int maxBlock = 512;
    juce::AudioBuffer<float> dryBuf, mixBuf;
    std::array<juce::AudioBuffer<float>, kNumChains> chainBuf;
    DelayRing dryRing;
    std::array<DelayRing, kNumChains> chainRing;
    std::array<std::atomic<int>, kNumChains> chainLat {};
    std::atomic<int> maxLatency { 0 };
    std::array<bool, kNumChains> chainActive {};

    KWeighting kDry, kMix;
    std::array<KWeighting, kNumChains> kChain;

    std::array<float, kNumChains + 1> gainNow {}, gainTarget {};   // [0] = dry
    bool firstBlock = true;
    float compNowDb = 0.0f, soloNow = 1.0f;
    int soloPrev = -1;
    bool lockPrev = true;

    // loudness bookkeeping (powers are mean squares of K-weighted audio)
    double accD = 0, accR = 0, accN = 0, accT = 0;   // accT: real time listened (s)
    double avgD = 0, avgR = 0;
    std::array<double, kNumChains> avgC {};
    double winD = 0, winR = 0, winN = 0, winT = 0;   // locked: consecutive check windows
    double lastWindowDb = 0;                         // dry/mix ratio of the last full window
    std::atomic<float> compTargetDb { 0.0f };
    std::atomic<bool> everLocked { false };
    std::atomic<bool> relearnFlag { false }, structureFlag { false };
    std::atomic<bool> restoredFlag { false }, skipChangeOnce { false };
};
}
