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
    bool acceptsMidi() const override { return false; }
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
    static constexpr int maxBands = 100;

    // ▼ 修正：配列のサイズは常に「最大値(100)」で確保しておきます
    std::array<juce::dsp::StateVariableTPTFilter<float>, maxBands> modFilters;
    std::array<juce::dsp::StateVariableTPTFilter<float>, maxBands> carFiltersL; // 左チャンネル用
    std::array<juce::dsp::StateVariableTPTFilter<float>, maxBands> carFiltersR; // 右チャンネル用
    std::array<float, maxBands> envelopes;


    double currentSampleRate = 44100.0;

    juce::Random random;

    // ▼ 新規追加：バンド数が変更されたかを検知するための変数
    int lastActiveBands = -1; 

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderAudioProcessor)
};