#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>

/**
    Deterministic test instrument. While the host transport plays it sends a short tone burst on every beat, to output
    (beat mod 4), so a 16-bar pattern is fully determined by the host's position. Parameters and a waveform choice
    make the saved state change.
*/
class TestPluginProcessor : public juce::AudioProcessor
{
public:
    TestPluginProcessor();

    void prepareToPlay(double sampleRate, int maxBlockSize) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Sampler Test Plugin"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    juce::AudioParameterFloat* tune = nullptr;  // frequency multiplier
    juce::AudioParameterFloat* level = nullptr;
    juce::AudioParameterInt* waveform = nullptr; // 0 sine, 1 triangle, 2 square, 3 saw

    static constexpr int numOutputs = 4;
    static constexpr double burstSeconds = 0.1;

private:
    double sampleRate = 44100.0;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TestPluginProcessor)
};
