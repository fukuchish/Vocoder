#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class VocoderAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    VocoderAudioProcessorEditor(VocoderAudioProcessor&);
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderAudioProcessorEditor)
};