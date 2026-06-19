#include "PluginProcessor.h"
#include "PluginEditor.h"

VocoderAudioProcessor::VocoderAudioProcessor()
    : apvts(*this, nullptr, "Parameters", createParameters())
{
}

VocoderAudioProcessor::~VocoderAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
VocoderAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "cutoff", "Cutoff",
        juce::NormalisableRange<float>(20.0f, 20000.0f, 1.0f, 0.3f),
        1000.0f
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "q", "Q", 0.1f, 10.0f, 0.707f
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain", -60.0f, 12.0f, 0.0f
    ));

    return layout;
}

void VocoderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();

    filter.prepare(spec);
    filter.setType(juce::dsp::StateVariableTPTFilterType::bandpass);
}

void VocoderAudioProcessor::releaseResources() {}

void VocoderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer,
                                         juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    float cutoff = apvts.getRawParameterValue("cutoff")->load();
    float q = apvts.getRawParameterValue("q")->load();
    float gainDb = apvts.getRawParameterValue("gain")->load();
    
    filter.setCutoffFrequency(cutoff);
    filter.setResonance(q);

    // フィルタ処理
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    filter.process(context);

    // ゲイン適用
    float gainLinear = juce::Decibels::decibelsToGain(gainDb);
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.applyGain(ch, 0, buffer.getNumSamples(), gainLinear);
}

// 簡単のため、JUCEの自動生成UIを使用します
juce::AudioProcessorEditor* VocoderAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocoderAudioProcessor();
}