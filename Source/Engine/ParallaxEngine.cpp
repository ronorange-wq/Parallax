#include "ParallaxEngine.h"

namespace plx
{
using namespace juce;

// ------------------------------------------------------------------ K-weighting
void KWeighting::prepare (double rate)
{
    {   // stage 1: high shelf
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (MathConstants<double>::pi * f0 / rate);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {   // stage 2: high pass
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (MathConstants<double>::pi * f0 / rate);
        const double a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
    reset();
}

void KWeighting::reset()
{
    for (auto* b : { &shelf, &hp })
        for (int c = 0; c < 2; ++c) b->z1[c] = b->z2[c] = 0.0;
}

double KWeighting::process (const float* l, const float* r, int n) noexcept
{
    double sum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double yl = hp.run (0, shelf.run (0, (double) l[i]));
        const double yr = hp.run (1, shelf.run (1, (double) r[i]));
        sum += yl * yl + yr * yr;
    }
    if (! std::isfinite (sum)) { reset(); return 0.0; }
    return sum;
}

// ------------------------------------------------------------------ engine
void ParallaxEngine::prepare (double sampleRate, int block)
{
    fs = sampleRate > 0 ? sampleRate : 44100.0;
    maxBlock = jmax (1, block);
    dryBuf.setSize (2, maxBlock, false, true, false);
    mixBuf.setSize (2, maxBlock, false, true, false);
    for (auto& b : chainBuf) b.setSize (2, maxBlock, false, true, false);
    dryRing.allocate (65536 + maxBlock);
    for (auto& r : chainRing) r.allocate (65536 + maxBlock);
    kDry.prepare (fs); kMix.prepare (fs);
    for (auto& k : kChain) k.prepare (fs);
    firstBlock = true;
    if (! everLocked.load()) startMatching();
}

void ParallaxEngine::updateLatency()
{
    int mx = 0;
    for (int c = 0; c < kNumChains; ++c)
    {
        int l = 0;
        for (auto* h : chains[(size_t) c]) l += jmax (0, h->latency());
        chainLat[(size_t) c] = l;
        mx = jmax (mx, l);
    }
    maxLatency = mx;
}

void ParallaxEngine::restoreCompensation (float db) noexcept
{
    compTargetDb = jlimit (-24.0f, 24.0f, db);
    compNowDb = compTargetDb.load();
    everLocked = true;
    restoredFlag = true;
    skipChangeOnce = true;
    structureFlag = false;
    state = (int) LockState::locked;
}

void ParallaxEngine::startMatching() noexcept
{
    accD = accR = accN = accT = 0.0;
    avgD = avgR = 0.0;          // restart all running averages together, so their ratios stay honest
    for (auto& a : avgC) a = 0.0;
    winD = winR = winN = winT = 0.0;
    state = (int) LockState::matching;
    matchProgress = 0.0f;
}

static inline double toDb (double p) { return 10.0 * std::log10 (jmax (p, 1.0e-20)); }

void ParallaxEngine::process (AudioBuffer<float>& io, const EngineParams& p) noexcept
{
    const int n = io.getNumSamples();
    if (n <= 0 || n > maxBlock || io.getNumChannels() < 1) return;
    float* ioL = io.getWritePointer (0);
    float* ioR = io.getNumChannels() > 1 ? io.getWritePointer (1) : ioL;

    const SpinLock::ScopedTryLockType tl (chainLock);
    if (! tl.isLocked()) return;   // a chain is being edited right now: let the input through

    const int L = maxLatency.load();
    const double blockSec = n / fs;

    // ---- dry path, delayed to line up with the slowest chain
    dryRing.write (ioL, ioR, n);
    float* dL = dryBuf.getWritePointer (0);
    float* dR = dryBuf.getWritePointer (1);
    dryRing.readTo (dL, dR, n, L);

    // ---- chains
    for (int c = 0; c < kNumChains; ++c)
    {
        auto& v = chains[(size_t) c];
        float* cL = chainBuf[(size_t) c].getWritePointer (0);
        float* cR = chainBuf[(size_t) c].getWritePointer (1);
        chainActive[(size_t) c] = false;
        for (auto* h : v) if (h != nullptr && h->isPrepared()) { chainActive[(size_t) c] = true; break; }
        if (! chainActive[(size_t) c])
        {
            FloatVectorOperations::clear (cL, n);
            FloatVectorOperations::clear (cR, n);
            continue;
        }
        FloatVectorOperations::copy (cL, ioL, n);
        FloatVectorOperations::copy (cR, ioR, n);
        for (auto* h : v)
            if (h != nullptr && h->isPrepared())
                h->process (cL, cR, cL, cR, n, 0);
        for (int i = 0; i < n; ++i)   // a broken plug-in must not take the whole track down
        {
            if (! std::isfinite (cL[i])) cL[i] = 0.0f;
            if (! std::isfinite (cR[i])) cR[i] = 0.0f;
        }
        auto& ring = chainRing[(size_t) c];
        ring.write (cL, cR, n);
        ring.readTo (cL, cR, n, L - chainLat[(size_t) c].load());
    }

    // ---- gains: Amount is an equal-power dry/parallel crossfade; A, B, C set each chain's
    // share independently (squared taper: 50% is about -12 dB). Empty chains are silent.
    {
        const double th = jlimit (0.0, 100.0, (double) p.amount) / 100.0 * MathConstants<double>::halfPi;
        const float wet = (float) std::sin (th);
        std::array<float, kNumChains + 1> t {};
        t[0] = (float) std::cos (th);
        for (int c = 0; c < kNumChains; ++c)
        {
            const float x = jlimit (0.0f, 1.0f, p.level[(size_t) c] / 100.0f);
            t[(size_t) c + 1] = chainActive[(size_t) c] ? wet * x * x : 0.0f;
        }
        bool changed = false;
        for (size_t i = 0; i < t.size(); ++i)
            if (std::abs (t[i] - gainTarget[i]) > 1.0e-4f) changed = true;
        gainTarget = t;
        if (firstBlock) gainNow = t;
        if (structureFlag.exchange (false)) changed = true;
        if (skipChangeOnce.exchange (false)) changed = false;
        if (changed && ! firstBlock && p.lock) startMatching();
    }

    // ---- mix (gains ramp over the block)
    float* mL = mixBuf.getWritePointer (0);
    float* mR = mixBuf.getWritePointer (1);
    FloatVectorOperations::clear (mL, n);
    FloatVectorOperations::clear (mR, n);
    auto addRamp = [n] (float* dst, const float* src, float g0, float g1)
    {
        if (g0 == 0.0f && g1 == 0.0f) return;
        const float step = (g1 - g0) / (float) n;
        float g = g0;
        for (int i = 0; i < n; ++i) { g += step; dst[i] += src[i] * g; }
    };
    addRamp (mL, dL, gainNow[0], gainTarget[0]);
    addRamp (mR, dR, gainNow[0], gainTarget[0]);
    for (int c = 0; c < kNumChains; ++c)
    {
        addRamp (mL, chainBuf[(size_t) c].getReadPointer (0), gainNow[(size_t) c + 1], gainTarget[(size_t) c + 1]);
        addRamp (mR, chainBuf[(size_t) c].getReadPointer (1), gainNow[(size_t) c + 1], gainTarget[(size_t) c + 1]);
    }
    gainNow = gainTarget;

    // ---- measure (K-weighted mean square per block)
    const double pd = kDry.process (dL, dR, n) / (2.0 * n);
    const double pr = kMix.process (mL, mR, n) / (2.0 * n);
    std::array<double, kNumChains> pc {};
    for (int c = 0; c < kNumChains; ++c)
        pc[(size_t) c] = chainActive[(size_t) c] ? kChain[(size_t) c].process (chainBuf[(size_t) c].getReadPointer (0),
                                                                             chainBuf[(size_t) c].getReadPointer (1), n) / (2.0 * n)
                                                 : 0.0;

    const double gateP = std::pow (10.0, gateDb / 10.0);
    const bool gated = pd > gateP && pr > gateP * 1.0e-3;
    const double a = 1.0 - std::exp (-blockSec / averageSeconds);
    if (gated)
    {
        const bool fresh = avgD <= 0.0;
        if (fresh) { avgD = pd; avgR = pr; } else { avgD += (pd - avgD) * a; avgR += (pr - avgR) * a; }
        for (int c = 0; c < kNumChains; ++c)
            avgC[(size_t) c] = fresh ? pc[(size_t) c] : avgC[(size_t) c] + (pc[(size_t) c] - avgC[(size_t) c]) * a;
    }

    // ---- lock state machine
    if (relearnFlag.exchange (false) && p.lock) startMatching();
    if (p.lock && ! lockPrev) startMatching();
    lockPrev = p.lock;

    if (! p.lock)
    {
        state = (int) LockState::off;
    }
    else if (state.load() == (int) LockState::matching)
    {
        // listen for a fixed span of real time (silence inside it is skipped, but doesn't stretch it),
        // so a repeating pattern such as a kick is always measured over the same number of hits
        if (gated) { accD += pd * n; accR += pr * n; accN += n; }
        accT += blockSec;
        matchProgress = (float) jlimit (0.0, 1.0, accT / measureSeconds);
        if (accT >= measureSeconds && accN < 0.25 * measureSeconds * fs)
        {
            accD = accR = accN = accT = 0.0;   // mostly silence: keep listening
        }
        else if (accT >= measureSeconds && accR > 0.0)
        {
            compTargetDb = (float) jlimit (-24.0, 24.0, toDb (accD / accR));
            winD = winR = winN = winT = 0.0;
            lastWindowDb = compTargetDb.load();
            everLocked = true;
            state = (int) LockState::locked;
        }
    }
    else
    {
        state = (int) LockState::locked;
        // keep checking in windows of the same exact length as the measurement
        if (gated) { winD += pd * n; winR += pr * n; winN += n; }
        winT += blockSec;
        if (winT >= measureSeconds)
        {
            if (winN >= 0.25 * measureSeconds * fs && winR > 0.0)
            {
                lastWindowDb = toDb (winD / winR);
                if (std::abs (lastWindowDb - compTargetDb.load()) > driftDb) startMatching();
            }
            winD = winR = winN = winT = 0.0;
        }
    }

    // ---- compensation: one smooth move to the target, then still
    const float target = p.lock ? compTargetDb.load() : 0.0f;
    const float c0 = compNowDb;
    if (restoredFlag.exchange (false)) compNowDb = target;
    else compNowDb += (target - compNowDb) * (float) (1.0 - std::exp (-blockSec / jumpSeconds));
    if (std::abs (compNowDb - target) < 0.01f) compNowDb = target;

    // ---- solo: one chain alone, at the dry loudness
    const int solo = (p.solo >= 0 && p.solo < kNumChains && chainActive[(size_t) p.solo]) ? p.solo : -1;
    if (solo >= 0)
    {
        const double pcs = avgC[(size_t) solo];
        const float want = (pcs > 0.0 && avgD > 0.0) ? (float) jlimit (0.0, 16.0, std::sqrt (avgD / pcs)) : 1.0f;
        const float s0 = soloPrev == solo ? soloNow : want;
        soloNow = s0 + (want - s0) * (float) (1.0 - std::exp (-blockSec / 0.2));
        soloGain = soloNow;
        const float* sL = chainBuf[(size_t) solo].getReadPointer (0);
        const float* sR = chainBuf[(size_t) solo].getReadPointer (1);
        const float step = (soloNow - s0) / (float) n;
        float g = s0;
        for (int i = 0; i < n; ++i) { g += step; ioL[i] = sL[i] * g; if (ioR != ioL) ioR[i] = sR[i] * g; }
    }
    else
    {
        const float g0 = Decibels::decibelsToGain (c0), g1 = Decibels::decibelsToGain (compNowDb);
        const float step = (g1 - g0) / (float) n;
        float g = g0;
        if (ioR == ioL)
            for (int i = 0; i < n; ++i) { g += step; ioL[i] = 0.5f * (mL[i] + mR[i]) * g; }
        else
            for (int i = 0; i < n; ++i) { g += step; ioL[i] = mL[i] * g; ioR[i] = mR[i] * g; }
    }
    soloPrev = solo;
    firstBlock = false;

    // ---- readouts
    compDb = compNowDb;
    if (avgD > 0.0 && avgR > 0.0)
    {
        dryLufs = (float) (toDb (avgD) - 0.691);
        outLufs = (float) (toDb (avgR) - 0.691 + compNowDb);
        deltaDb = (float) ((p.lock ? compNowDb - lastWindowDb : toDb (avgR / avgD)));
    }
}
}
