#include "PluginProcessor.h"
#include "UI/PluginEditor.h"

using namespace juce;
using namespace plx;

AudioProcessorValueTreeState::ParameterLayout ParallaxProcessor::makeLayout()
{
    AudioProcessorValueTreeState::ParameterLayout l;
    auto pct = AudioParameterFloatAttributes().withLabel ("%");
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "amount", 1 }, "Amount", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 60.0f, pct));
    for (int c = 0; c < kNumChains; ++c)
        l.add (std::make_unique<AudioParameterFloat> (ParameterID { "level" + chainName (c), 1 }, "Chain " + chainName (c),
                                                      NormalisableRange<float> (0.0f, 100.0f, 0.1f), 70.0f, pct));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "lock", 1 }, "Loudness Lock", true));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "solo", 1 }, "Solo", StringArray { "Off", "A", "B", "C" }, 0));
    return l;
}

ParallaxProcessor::ParallaxProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", makeLayout())
{
    addDefaultFormatsToManager (formats);
    pAmount = apvts.getRawParameterValue ("amount");
    for (int c = 0; c < kNumChains; ++c) pLevel[(size_t) c] = apvts.getRawParameterValue ("level" + chainName (c));
    pLock = apvts.getRawParameterValue ("lock");
    pSolo = apvts.getRawParameterValue ("solo");
    engine.prepare (sr, bs);
    startTimerHz (10);
}

ParallaxProcessor::~ParallaxProcessor()
{
    stopTimer();
    windows.closeAll();
    clearAll();
}

bool ParallaxProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
    if (out != AudioChannelSet::stereo() && out != AudioChannelSet::mono()) return false;
    return in == out;
}

void ParallaxProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate;
    bs = jmax (1, samplesPerBlock);
    const SpinLock::ScopedLockType sl (engine.chainLock);
    engine.prepare (sr, bs);
    for (auto& ch : chains)
        for (auto& s : ch)
            if (s->inst != nullptr) s->inst->prepare (sr, bs, isNonRealtime());
    engine.updateLatency();
    setLatencySamples (reportedLatency = engine.latency());
}

void ParallaxProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer&)
{
    ScopedNoDenormals noDenormals;
    EngineParams p;
    p.amount = pAmount->load();
    for (int c = 0; c < kNumChains; ++c) p.level[(size_t) c] = pLevel[(size_t) c]->load();
    p.lock = pLock->load() > 0.5f;
    p.solo = roundToInt (pSolo->load()) - 1;

    // hosts may hand over a bigger block than announced: process it in pieces
    const int total = buffer.getNumSamples();
    for (int pos = 0; pos < total; pos += bs)
    {
        const int n = jmin (bs, total - pos);
        AudioBuffer<float> view (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), pos, n);
        engine.process (view, p);
    }
}

// ------------------------------------------------------------------ chain model
void ParallaxProcessor::syncEngineChain (int c)
{
    std::vector<HostedInstance*> v;
    v.reserve (kMaxSlots);
    for (auto& s : chains[(size_t) c]) if (s->inst != nullptr) v.push_back (s->inst.get());
    {
        const SpinLock::ScopedLockType sl (engine.chainLock);
        engine.chains[(size_t) c].swap (v);
        engine.updateLatency();
    }
    engine.structureChanged();
    if (engine.latency() != reportedLatency) setLatencySamples (reportedLatency = engine.latency());
    sendChangeMessage();
}

bool ParallaxProcessor::addPlugin (int c, const PluginDescription& requested, String& error, const MemoryBlock* state, int index)
{
    if (c < 0 || c >= kNumChains) return false;
    if ((int) chains[(size_t) c].size() >= kMaxSlots) { error = "A chain holds up to " + String (kMaxSlots) + " plug-ins."; return false; }

    auto found = catalog.resolveForLoad (requested, error);
    if (found.isEmpty()) return false;
    auto d = found.getFirst();
    if (PluginCatalog::isSelf (d)) { error = "Parallax can't be loaded inside itself."; return false; }
    if (d.isInstrument) { error = d.name + " is an instrument, not an effect."; return false; }
    for (auto* fmt : formats.getFormats())
        if (fmt->getName() == d.pluginFormatName && fmt->requiresUnblockedMessageThreadDuringCreation (d))
        { error = d.name + " can't be hosted yet (it needs asynchronous loading)."; return false; }

    String err;
    auto p = formats.createPluginInstance (d, sr, bs, err);
    if (p == nullptr) { error = "Couldn't load " + d.name + (err.isNotEmpty() ? ": " + err : String()); return false; }

    auto slot = std::make_unique<Slot>();
    slot->desc = d;
    slot->inst = std::make_unique<HostedInstance> (std::move (p), d, 0);
    slot->inst->configureLayout();
    if (state != nullptr && state->getSize() > 0)
        slot->inst->plugin().setStateInformation (state->getData(), (int) state->getSize());
    slot->inst->prepare (sr, bs, isNonRealtime());

    auto& ch = chains[(size_t) c];
    if (index < 0 || index > (int) ch.size()) index = (int) ch.size();
    ch.insert (ch.begin() + index, std::move (slot));
    syncEngineChain (c);
    return true;
}

void ParallaxProcessor::removePlugin (int c, int index)
{
    auto& ch = chains[(size_t) c];
    if (index < 0 || index >= (int) ch.size()) return;
    if (ch[(size_t) index]->inst != nullptr) windows.closeFor (ch[(size_t) index]->inst.get());
    auto victim = std::move (ch[(size_t) index]);
    ch.erase (ch.begin() + index);
    syncEngineChain (c);   // engine no longer references it once the lock is released
    victim.reset();
}

void ParallaxProcessor::movePlugin (int c, int from, int to)
{
    auto& ch = chains[(size_t) c];
    if (from < 0 || from >= (int) ch.size() || to < 0 || to >= (int) ch.size() || from == to) return;
    auto s = std::move (ch[(size_t) from]);
    ch.erase (ch.begin() + from);
    ch.insert (ch.begin() + to, std::move (s));
    syncEngineChain (c);
}

void ParallaxProcessor::openPluginWindow (int c, int index)
{
    auto& ch = chains[(size_t) c];
    if (index < 0 || index >= (int) ch.size() || ch[(size_t) index]->inst == nullptr) return;
    windows.toggle (ch[(size_t) index]->inst.get(), "Parallax " + chainName (c) + " - " + ch[(size_t) index]->desc.name);
}

bool ParallaxProcessor::isWindowOpen (int c, int index) const
{
    auto& ch = chains[(size_t) c];
    return index >= 0 && index < (int) ch.size() && ch[(size_t) index]->inst != nullptr
        && windows.isOpen (ch[(size_t) index]->inst.get());
}

void ParallaxProcessor::clearAll()
{
    {
        const SpinLock::ScopedLockType sl (engine.chainLock);
        for (auto& v : engine.chains) v.clear();
        engine.updateLatency();
    }
    windows.closeAll();
    for (auto& ch : chains) ch.clear();
}

void ParallaxProcessor::timerCallback()
{
    // hosted plug-ins may change their latency (e.g. a lookahead switch)
    int before = engine.latency();
    {
        const SpinLock::ScopedLockType sl (engine.chainLock);
        engine.updateLatency();
    }
    if (engine.latency() != before || engine.latency() != reportedLatency)
        setLatencySamples (reportedLatency = engine.latency());
}

// ------------------------------------------------------------------ state
void ParallaxProcessor::getStateInformation (MemoryBlock& dest)
{
    XmlElement root ("PARALLAX");
    root.setAttribute ("version", 1);
    if (auto params = apvts.copyState().createXml()) root.addChildElement (params.release());
    if (engine.hasLockedValue()) root.setAttribute ("compensation", (double) engine.savedCompensation());

    auto* chainsXml = root.createNewChildElement ("CHAINS");
    for (int c = 0; c < kNumChains; ++c)
    {
        auto* cx = chainsXml->createNewChildElement ("CHAIN");
        cx->setAttribute ("index", c);
        for (auto& s : chains[(size_t) c])
        {
            auto* px = cx->createNewChildElement ("PLUGIN");
            if (auto dx = s->desc.createXml()) px->addChildElement (dx.release());
            MemoryBlock st = s->inst != nullptr ? s->inst->getState() : s->missingState;
            px->setAttribute ("state", st.toBase64Encoding());
        }
    }
    copyXmlToBinary (root, dest);
}

void ParallaxProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr || ! xml->hasTagName ("PARALLAX")) return;

    clearAll();
    if (auto* params = xml->getChildByName (apvts.state.getType()))
        apvts.replaceState (ValueTree::fromXml (*params));

    if (auto* chainsXml = xml->getChildByName ("CHAINS"))
        for (auto* cx : chainsXml->getChildWithTagNameIterator ("CHAIN"))
        {
            const int c = cx->getIntAttribute ("index", -1);
            if (c < 0 || c >= kNumChains) continue;
            for (auto* px : cx->getChildWithTagNameIterator ("PLUGIN"))
            {
                PluginDescription d;
                if (auto* dx = px->getChildByName ("PLUGIN")) d.loadFromXml (*dx);
                else if (auto* first = px->getFirstChildElement()) d.loadFromXml (*first);
                MemoryBlock st;
                st.fromBase64Encoding (px->getStringAttribute ("state"));
                String err;
                if (! addPlugin (c, d, err, &st))
                {
                    auto s = std::make_unique<Slot>();   // keep it, so saving again doesn't lose it
                    s->desc = d;
                    s->missingState = st;
                    chains[(size_t) c].push_back (std::move (s));
                }
            }
        }
    for (int c = 0; c < kNumChains; ++c) syncEngineChain (c);
    if (xml->hasAttribute ("compensation"))
        engine.restoreCompensation ((float) xml->getDoubleAttribute ("compensation"));
    sendChangeMessage();
}

AudioProcessorEditor* ParallaxProcessor::createEditor() { return new ParallaxEditor (*this); }

#ifndef PLX_TESTS
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ParallaxProcessor(); }
#endif
