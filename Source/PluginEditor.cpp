#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
LoopHarmonizerAudioProcessorEditor::LoopHarmonizerAudioProcessorEditor (LoopHarmonizerAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    titleLabel.setText ("LoopHarmonizer", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (26.0f, juce::Font::bold));
    titleLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (titleLabel);

    statusLabel.setText ("Phase 1 - passthrough scaffold, no analysis yet",
                         juce::dontSendNotification);
    statusLabel.setFont (juce::FontOptions (14.0f));
    statusLabel.setJustificationType (juce::Justification::centred);
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    addAndMakeVisible (statusLabel);

    setSize (420, 220);
}

LoopHarmonizerAudioProcessorEditor::~LoopHarmonizerAudioProcessorEditor() = default;

//==============================================================================
void LoopHarmonizerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (getLookAndFeel().findColour (juce::ResizableWindow::backgroundColourId));
}

void LoopHarmonizerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (20);

    titleLabel.setBounds (area.removeFromTop (44));
    statusLabel.setBounds (area.removeFromTop (24));
}
