#pragma once
#include <JuceHeader.h>
#include <array> // 配列を使うため追加

class VocoderAudioProcessor : public juce::AudioProcessor
{
public:
    VocoderAudioProcessor();
    ~VocoderAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    // ▼ 新規追加：DAWに「このプラグインはどんな入出力をサポートしているか」を伝える関数
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "My Vocoder 1"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameters();
    
    // ▼ 修正：16から一気に100バンドへ引き上げます！
    static constexpr int numBands = 100;

    std::array<juce::dsp::StateVariableTPTFilter<float>, numBands> modFilters;
    std::array<juce::dsp::StateVariableTPTFilter<float>, numBands> carFilters;
    std::array<float, numBands> envelopes;

    juce::dsp::Oscillator<float> carrierOsc;
    double currentSampleRate = 44100.0;

    
    // ▼ 新規追加：8音同時発音（ポリフォニック）のための変数群
    static constexpr int numVoices = 8;
    std::array<juce::dsp::Oscillator<float>, numVoices> carrierOscs;
    std::array<float, numVoices> carrierGates;
    std::array<int, numVoices> activeNotes; // どのMIDIノート番号を鳴らしているか記憶

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderAudioProcessor)
};