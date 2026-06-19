#include "PluginEditor.h"

VocoderAudioProcessorEditor::VocoderAudioProcessorEditor(VocoderAudioProcessor& p)
    : AudioProcessorEditor(&p)
{
    setSize(400, 300);
}

void VocoderAudioProcessorEditor::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::darkgrey);
    g.setColour(juce::Colours::white);
    g.setFont(20.0f);
    g.drawText("Vocoder Plugin", getLocalBounds(), juce::Justification::centred);
}

void VocoderAudioProcessorEditor::resized() {}