#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine/ParallaxEngine.h"
#include "Host/PluginCatalog.h"
#include "Host/PluginWindows.h"

// Parallax: a dry path plus three chains of hosted plug-ins, mixed in parallel, with the
// output loudness locked to the dry input.
class ParallaxProcessor : public juce::AudioProcessor,
                          public juce::ChangeBroadcaster,
                          private juce::Timer
{
public:
    ParallaxProcessor();
    ~ParallaxProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Parallax"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- chain model (message thread)
    struct Slot
    {
        std::unique_ptr<plx::HostedInstance> inst;
        juce::PluginDescription desc;          // also kept for a missing plug-in
        juce::MemoryBlock missingState;        // state of a plug-in that isn't installed here
        bool missing() const noexcept { return inst == nullptr; }
        juce::String name() const { return desc.name + (missing() ? " (missing)" : juce::String()); }
    };
    const std::vector<std::unique_ptr<Slot>>& chain (int c) const { return chains[(size_t) c]; }
    bool addPlugin (int c, const juce::PluginDescription& d, juce::String& error,
                    const juce::MemoryBlock* state = nullptr, int index = -1);
    void removePlugin (int c, int index);
    void movePlugin (int c, int from, int to);
    void openPluginWindow (int c, int index);
    bool isWindowOpen (int c, int index) const;

    juce::AudioProcessorValueTreeState apvts;
    plx::ParallaxEngine engine;
    juce::AudioPluginFormatManager formats;
    plx::PluginCatalog catalog { formats };
    plx::PluginWindowManager windows;

    static juce::String chainName (int c) { return juce::String::charToString ((juce::juce_wchar) ('A' + c)); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout makeLayout();
    void timerCallback() override;
    void syncEngineChain (int c);   // rebuild engine pointers for chain c (takes the lock)
    void clearAll();

    std::array<std::vector<std::unique_ptr<Slot>>, plx::kNumChains> chains;
    std::atomic<float>* pAmount = nullptr;
    std::array<std::atomic<float>*, plx::kNumChains> pLevel {};
    std::atomic<float>* pLock = nullptr;
    std::atomic<float>* pSolo = nullptr;
    double sr = 44100.0;
    int bs = 512;
    int reportedLatency = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParallaxProcessor)
};
