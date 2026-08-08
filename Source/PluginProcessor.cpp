#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
LoopHarmonizerAudioProcessor::LoopHarmonizerAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
}

LoopHarmonizerAudioProcessor::~LoopHarmonizerAudioProcessor() = default;

//==============================================================================
void LoopHarmonizerAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // Nothing to allocate yet. The pitch detector, key detector and MIDI
    // generators will be sized and reset from here in later phases.
    juce::ignoreUnused (sampleRate, samplesPerBlock);
}

void LoopHarmonizerAudioProcessor::releaseResources()
{
}

bool LoopHarmonizerAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& mainIn  = layouts.getMainInputChannelSet();
    const auto& mainOut = layouts.getMainOutputChannelSet();

    // The analysis path is monophonic, but hosts commonly instantiate an
    // effect as stereo, so accept both and require the layouts to match.
    if (mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo())
        return false;

    return mainIn == mainOut;
}

void LoopHarmonizerAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                                 juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Clear any channels the host handed us beyond what the input bus supplies,
    // so they don't carry whatever garbage was in the buffer.
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels(); ++i)
        buffer.clear (i, 0, buffer.getNumSamples());

    // Phase 1: audio passes through untouched, and we emit no MIDI. Clearing is
    // what makes this an explicit "produce nothing yet" rather than an accident
    // of the host's buffer contents.
    midiMessages.clear();
}

//==============================================================================
const juce::String LoopHarmonizerAudioProcessor::getProgramName (int index)
{
    juce::ignoreUnused (index);
    return {};
}

void LoopHarmonizerAudioProcessor::changeProgramName (int index, const juce::String& newName)
{
    juce::ignoreUnused (index, newName);
}

//==============================================================================
void LoopHarmonizerAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // No parameters to persist yet.
    juce::ignoreUnused (destData);
}

void LoopHarmonizerAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    juce::ignoreUnused (data, sizeInBytes);
}

//==============================================================================
juce::AudioProcessorEditor* LoopHarmonizerAudioProcessor::createEditor()
{
    return new LoopHarmonizerAudioProcessorEditor (*this);
}

//==============================================================================
// This creates new instances of the plugin.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new LoopHarmonizerAudioProcessor();
}
