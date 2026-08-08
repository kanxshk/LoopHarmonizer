#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
    Phase 1 placeholder UI. Later phases will surface the detected key/scale,
    the tracked root note per bar, and the MIDI output mode selection here.
*/
class LoopHarmonizerAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit LoopHarmonizerAudioProcessorEditor (LoopHarmonizerAudioProcessor&);
    ~LoopHarmonizerAudioProcessorEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    LoopHarmonizerAudioProcessor& processorRef;

    juce::Label titleLabel;
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopHarmonizerAudioProcessorEditor)
};
