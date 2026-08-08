#pragma once

#include <JuceHeader.h>

//==============================================================================
/**
    Phase 1 scaffold.

    LoopHarmonizer takes a monophonic guitar/synth loop on its audio input and
    will eventually emit MIDI (chords, basslines, counter-melodies) for the
    producer to route to their own instruments. Right now it does neither: the
    audio is passed through untouched and no MIDI is produced.

    The analysis pipeline (pitch detection -> key/scale detection -> root
    tracking -> MIDI generation) will be developed as standalone modules and
    hooked into processBlock in later phases.
*/
class LoopHarmonizerAudioProcessor final : public juce::AudioProcessor
{
public:
    //==============================================================================
    LoopHarmonizerAudioProcessor();
    ~LoopHarmonizerAudioProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                       { return true; }

    //==============================================================================
    const juce::String getName() const override           { return JucePlugin_Name; }

    bool acceptsMidi() const override                     { return false; }
    bool producesMidi() const override                    { return true; }
    bool isMidiEffect() const override                    { return false; }
    double getTailLengthSeconds() const override          { return 0.0; }

    //==============================================================================
    int getNumPrograms() override                         { return 1; }
    int getCurrentProgram() override                      { return 0; }
    void setCurrentProgram (int index) override           { juce::ignoreUnused (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int index, const juce::String& newName) override;

    //==============================================================================
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

private:
    //==============================================================================
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopHarmonizerAudioProcessor)
};
