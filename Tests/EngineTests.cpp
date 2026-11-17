// Offline tests: the real Parallax processor hosting real VST3 test plug-ins, fed a kick-like signal.
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "UI/PluginEditor.h"

using namespace juce;
using namespace plx;

static int failures = 0;
static void check (bool ok, const String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}

static File buildDir;
static PluginDescription findPlugin (AudioPluginFormatManager& fm, const String& name)
{
    for (auto f : buildDir.findChildFiles (File::findDirectories, true, name + ".vst3"))
        for (auto* fmt : fm.getFormats())
        {
            OwnedArray<PluginDescription> found;
            fmt->findAllTypesForFile (found, f.getFullPathName());
            if (! found.isEmpty()) return *found[0];
        }
    std::cout << "  (could not find " << name << ".vst3 under " << buildDir.getFullPathName() << ")" << std::endl;
    return {};
}

// kick: 55 Hz sine with a fast decay and a click, every half second, plus a little noise
struct Kick
{
    double fs = 48000.0;
    int64 t = 0;
    Random rnd { 1 };
    float level = 0.6f;
    void fill (AudioBuffer<float>& b)
    {
        for (int i = 0; i < b.getNumSamples(); ++i, ++t)
        {
            const double tt = std::fmod ((double) t / fs, 0.5);
            const double env = std::exp (-tt * 9.0);
            double x = std::sin (MathConstants<double>::twoPi * (55.0 + 80.0 * std::exp (-tt * 40.0)) * tt) * env * level;
            x += (tt < 0.003 ? (rnd.nextFloat() * 2.0f - 1.0f) * 0.4 : 0.0) + (rnd.nextFloat() * 2.0f - 1.0f) * 0.005;
            b.setSample (0, i, (float) x);
            b.setSample (1, i, (float) (x * 0.97));
        }
    }
};

static double lufs (const std::vector<float>& l, const std::vector<float>& r, double fs, size_t from)
{
    KWeighting k; k.prepare (fs);
    const size_t n = l.size() - from;
    const double ms = k.process (l.data() + from, r.data() + from, (int) n) / (2.0 * (double) n);
    return 10.0 * std::log10 (jmax (ms, 1e-20)) - 0.691;
}

struct Rig
{
    ParallaxProcessor p;
    double fs = 48000.0;
    int bs = 512;
    Kick kick;
    std::vector<float> inL, inR, outL, outR;
    std::vector<float> compTrace;

    Rig() { p.setPlayConfigDetails (2, 2, fs, bs); p.prepareToPlay (fs, bs); }

    void set (const String& id, float v)
    {
        auto* prm = p.apvts.getParameter (id);
        prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }

    void run (double seconds)
    {
        inL.clear(); inR.clear(); outL.clear(); outR.clear(); compTrace.clear();
        AudioBuffer<float> b (2, bs);
        MidiBuffer m;
        const int blocks = (int) std::ceil (seconds * fs / bs);
        for (int k = 0; k < blocks; ++k)
        {
            kick.fill (b);
            for (int i = 0; i < bs; ++i) { inL.push_back (b.getSample (0, i)); inR.push_back (b.getSample (1, i)); }
            p.processBlock (b, m);
            for (int i = 0; i < bs; ++i) { outL.push_back (b.getSample (0, i)); outR.push_back (b.getSample (1, i)); }
            compTrace.push_back (p.engine.compDb.load());
        }
    }
    // loudness of output minus loudness of input over the last `secs`
    double delta (double secs) const
    {
        const size_t from = outL.size() - (size_t) (secs * fs);
        return lufs (outL, outR, fs, from) - lufs (inL, inR, fs, from);
    }
    LockState state() const { return (LockState) p.engine.state.load(); }
    bool finite() const
    {
        for (auto v : outL) if (! std::isfinite (v)) return false;
        for (auto v : outR) if (! std::isfinite (v)) return false;
        return true;
    }
};

static void setPluginParam (ParallaxProcessor& p, int chain, int slot, int param, float plainValue, float maxValue)
{
    auto& inst = *p.chain (chain)[(size_t) slot]->inst;
    inst.plugin().getParameters()[param]->setValueNotifyingHost (plainValue / maxValue);
}

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    buildDir = File (argc > 1 ? String (argv[1]) : File::getCurrentWorkingDirectory().getFullPathName());

    if (argc > 3 && String (argv[2]) == "--snapshot")
    {
        ParallaxProcessor p;
        p.setPlayConfigDetails (2, 2, 48000.0, 512);
        p.prepareToPlay (48000.0, 512);
        String err;
        p.addPlugin (0, findPlugin (p.formats, "PLXTestDrive"), err);
        p.addPlugin (0, findPlugin (p.formats, "PLXTestComp"), err);
        p.addPlugin (1, findPlugin (p.formats, "PLXTestLatency"), err);
        Kick k; AudioBuffer<float> b (2, 512); MidiBuffer m;
        for (int i = 0; i < 400; ++i) { k.fill (b); p.processBlock (b, m); }
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        for (int i = 0; i < 5; ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        File out (argv[3]); out.deleteFile();
        FileOutputStream os (out); PNGImageFormat().writeImageToStream (img, os);
        std::cout << "snapshot written" << std::endl;
        if (argc > 4)
        {
            dynamic_cast<ParallaxEditor*> (ed.get())->openBrowser (2);
            for (int i = 0; i < 5; ++i) MessageManager::getInstance()->runDispatchLoopUntil (50);
            auto img2 = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
            File out2 (argv[4]); out2.deleteFile();
            FileOutputStream os2 (out2); PNGImageFormat().writeImageToStream (img2, os2);
        }
        ed.reset();
        return 0;
    }

    MemoryBlock savedState;
    float savedComp = 0.0f;

    std::cout << "== 1. Empty chains: output matches dry" << std::endl;
    {
        Rig r;
        r.set ("amount", 60.0f);
        r.run (3.0);
        check (r.finite(), "no NaN / inf");
        check (r.state() == LockState::locked, "locked after 3 s");
        check (std::abs (r.delta (1.0)) < 0.3, "loudness equal to dry (" + String (r.delta (1.0), 2) + " dB)");
    }

    std::cout << "== 2. Drive in chain A: louder plug-in, same output loudness" << std::endl;
    {
        Rig r;
        auto d = findPlugin (r.p.formats, "PLXTestDrive");
        String err;
        check (d.name.isNotEmpty() && r.p.addPlugin (0, d, err), "load PLXTestDrive into A " + err);
        if (r.p.chain (0).size() == 1)
        {
            setPluginParam (r.p, 0, 0, 0, 6.0f, 36.0f);
            r.set ("amount", 60.0f); r.set ("levelA", 80.0f);
            r.run (4.0);
            check (r.state() == LockState::locked, "locked");
            check (std::abs (r.delta (1.5)) < 0.5, "matched: " + String (r.delta (1.5), 2) + " dB from dry");

            // steady state: the gain must not wander with the kick hits
            r.run (6.0);
            auto mm = std::minmax_element (r.compTrace.begin(), r.compTrace.end());
            check (*mm.second - *mm.first < 0.05f, "gain stays still while locked (moved "
                                                     + String (*mm.second - *mm.first, 3) + " dB over 6 s)");
            check (std::abs (r.delta (2.0)) < 0.5, "still matched: " + String (r.delta (2.0), 2) + " dB");

            // raise Drive inside the hosted plug-in: Parallax must notice and re-match by itself
            const float before = r.p.engine.compDb.load();
            setPluginParam (r.p, 0, 0, 0, 30.0f, 36.0f);
            r.run (1.0);
            const double jump = r.delta (0.5);
            r.run (5.0);
            check (std::abs (jump) > 1.0, "Drive change was audible before re-match (" + String (jump, 2) + " dB)");
            check (r.state() == LockState::locked, "re-locked by itself after Drive change");
            check (std::abs (r.delta (1.5)) < 0.5, "re-matched: " + String (r.delta (1.5), 2) + " dB (gain "
                                                    + String (before, 1) + " -> " + String (r.p.engine.compDb.load(), 1) + ")");

            // a knob of Parallax itself
            r.set ("amount", 90.0f);
            r.run (4.0);
            check (std::abs (r.delta (1.5)) < 0.5, "Amount 90%: matched " + String (r.delta (1.5), 2) + " dB");

            // chain level: must change the sound (balance) but not the loudness
            r.set ("levelA", 30.0f);
            r.run (4.0);
            // almost pure distortion against a perfectly periodic test kick: the window edge matters most here
            check (std::abs (r.delta (1.5)) < 1.0, "Level A 30%: matched " + String (r.delta (1.5), 2) + " dB");

            // lock off: compensation goes away
            r.set ("lock", 0.0f);
            r.run (1.0);
            check (std::abs (r.p.engine.compDb.load()) < 0.01f, "lock off: no compensation");
            check (r.state() == LockState::off, "lock off: state off");
            r.set ("lock", 1.0f);
            r.run (4.0);
            check (r.state() == LockState::locked && std::abs (r.delta (1.5)) < 0.5, "lock on again: matched " + String (r.delta (1.5), 2) + " dB");

            savedComp = r.p.engine.compDb.load();
            r.p.getStateInformation (savedState);
            check (r.finite(), "no NaN / inf");
        }
    }

    std::cout << "== 3. Saved and reopened" << std::endl;
    {
        Rig r;
        r.p.setStateInformation (savedState.getData(), (int) savedState.getSize());
        check (r.p.chain (0).size() == 1 && r.p.chain (0)[0]->desc.name == "PLXTestDrive", "chain A restored");
        if (r.p.chain (0).size() == 1)
        {
            auto v = r.p.chain (0)[0]->inst->plugin().getParameters()[0]->getValue() * 36.0f;
            check (std::abs (v - 30.0f) < 0.1f, "plug-in state restored (Drive " + String (v, 1) + ")");
        }
        check (std::abs (r.p.apvts.getRawParameterValue ("levelA")->load() - 30.0f) < 0.01f, "Parallax knobs restored");
        r.run (0.5);
        check (r.state() == LockState::locked, "locked immediately, no re-measuring on open");
        check (std::abs (r.p.engine.compDb.load() - savedComp) < 0.05f, "same compensation as saved ("
                                                                          + String (r.p.engine.compDb.load(), 2) + " vs " + String (savedComp, 2) + ")");
    }

    std::cout << "== 4. Latency: chains line up with dry" << std::endl;
    {
        Rig r;
        auto d = findPlugin (r.p.formats, "PLXTestLatency");
        String err;
        check (d.name.isNotEmpty() && r.p.addPlugin (1, d, err), "load PLXTestLatency into B " + err);
        check (r.p.engine.latency() == 100 && r.p.getLatencySamples() == 100, "reports 100 samples to the host");
        r.set ("amount", 50.0f); r.set ("levelB", 100.0f); r.set ("lock", 0.0f);
        r.run (2.0);
        // with a unity-gain delayed chain, the output must be the input delayed by 100, scaled (no comb filtering)
        double xy = 0, xx = 0, yy = 0;
        for (size_t i = 48000; i < r.outL.size(); ++i)
        {
            const double x = r.inL[i - 100], y = r.outL[i];
            xy += x * y; xx += x * x; yy += y * y;
        }
        const double corr = xy / std::sqrt (xx * yy + 1e-30);
        check (corr > 0.9999, "aligned (correlation " + String (corr, 6) + ")");

        std::cout << "== 5. Solo at dry loudness" << std::endl;
        r.set ("lock", 1.0f);
        r.set ("solo", 2.0f);   // B
        r.run (4.0);
        check (std::abs (r.delta (1.5)) < 0.5, "solo B plays at dry loudness (" + String (r.delta (1.5), 2) + " dB)");
        r.set ("solo", 0.0f);
        r.run (1.0);
        check (r.finite(), "no NaN / inf");

        std::cout << "== 6. Remove while playing" << std::endl;
        r.p.removePlugin (1, 0);
        r.run (1.0);
        check (r.p.engine.latency() == 0 && r.p.getLatencySamples() == 0, "latency back to 0");
        check (r.finite(), "no NaN / inf");
    }

    std::cout << std::endl << (failures == 0 ? "ALL TESTS PASSED" : String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
