#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

// ==============================================================================
// GUI画面を担当するクラス
// ==============================================================================
class VocoderAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    VocoderAudioProcessorEditor(VocoderAudioProcessor&);
    ~VocoderAudioProcessorEditor() override;

    void paint(juce::Graphics&) override; // 背景や文字を描く処理
    void resized() override;              // スライダーの位置を決める処理

private:
    VocoderAudioProcessor& audioProcessor;

    // スライダー本体
    juce::Slider bandsSlider;
    juce::Slider attackSlider;
    juce::Slider releaseSlider;
    juce::Slider mixSlider;
    juce::Slider goodizeSlider;
    juce::Slider gainSlider;

    // スライダーとパラメータを紐づけるアタッチメント
    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    std::unique_ptr<SliderAttachment> bandsAttachment;
    std::unique_ptr<SliderAttachment> attackAttachment;
    std::unique_ptr<SliderAttachment> releaseAttachment;
    std::unique_ptr<SliderAttachment> mixAttachment;
    std::unique_ptr<SliderAttachment> goodizeAttachment;
    std::unique_ptr<SliderAttachment> gainAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VocoderAudioProcessorEditor)
};