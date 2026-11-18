// SPDX-License-Identifier: AGPL-3.0-only
//
// "MXJ Test Gain" as a VST3 plugin, built with JUCE only for the tests of our
// VST3 hosting. One parameter, the gain in dB; its state is that parameter.

#include <juce_audio_processors/juce_audio_processors.h>

namespace
{
    class TestGain final : public juce::AudioProcessor
    {
    public:
        TestGain()
            : AudioProcessor (BusesProperties().withInput ("In", juce::AudioChannelSet::stereo())
                                               .withOutput ("Out", juce::AudioChannelSet::stereo()))
        {
            addParameter (gain = new juce::AudioParameterFloat (juce::ParameterID { "gain", 1 }, "Gain",
                                                                juce::NormalisableRange<float> (-60.0f, 24.0f), -6.0f));
        }

        const juce::String getName() const override { return "MXJ Test Gain"; }
        void prepareToPlay (double, int) override {}
        void releaseResources() override {}

        bool isBusesLayoutSupported (const BusesLayout& layout) const override
        {
            const auto in = layout.getMainInputChannelSet();
            return in == layout.getMainOutputChannelSet() && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
        }

        void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
        {
            buffer.applyGain (juce::Decibels::decibelsToGain (gain->get()));
        }

        double getTailLengthSeconds() const override { return 0.0; }
        bool acceptsMidi() const override { return false; }
        bool producesMidi() const override { return false; }
        juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
        bool hasEditor() const override { return true; }
        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& data) override
        {
            juce::MemoryOutputStream (data, false).writeFloat (gain->get());
        }

        void setStateInformation (const void* data, int size) override
        {
            if (size >= 4)
                *gain = juce::MemoryInputStream (data, (size_t) size, false).readFloat();
        }

    private:
        juce::AudioParameterFloat* gain = nullptr;
    };
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TestGain();
}
