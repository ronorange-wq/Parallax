// Test "drive": input gain (dB) into tanh saturation. Raising Drive makes it much louder.
#include <juce_audio_utils/juce_audio_utils.h>
using namespace juce;
class TestDrive : public AudioProcessor
{
public:
    TestDrive() : AudioProcessor (BusesProperties().withInput ("In", AudioChannelSet::stereo()).withOutput ("Out", AudioChannelSet::stereo()))
    { addParameter (drive = new AudioParameterFloat (ParameterID { "drive", 1 }, "Drive", 0.0f, 36.0f, 0.0f)); }
    const String getName() const override { return "PLXTestDrive"; }
    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    { return l.getMainInputChannelSet() == l.getMainOutputChannelSet() && ! l.getMainInputChannelSet().isDisabled() && l.getMainInputChannelSet().size() <= 2; }
    void processBlock (AudioBuffer<float>& b, MidiBuffer&) override
    {
        const float g = Decibels::decibelsToGain (drive->get());
        for (int c = 0; c < b.getNumChannels(); ++c)
        {
            auto* d = b.getWritePointer (c);
            for (int i = 0; i < b.getNumSamples(); ++i) d[i] = std::tanh (d[i] * g);
        }
    }
    AudioProcessorEditor* createEditor() override { return new GenericAudioProcessorEditor (*this); }
    bool hasEditor() const override { return true; }
    double getTailLengthSeconds() const override { return 0; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}
    void getStateInformation (MemoryBlock& m) override { MemoryOutputStream s (m, false); s.writeFloat (*drive); }
    void setStateInformation (const void* d, int n) override { MemoryInputStream s (d, (size_t) n, false); *drive = s.readFloat(); }
    AudioParameterFloat* drive;
};
AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new TestDrive(); }
