#include "PluginProcessor.h"
#include "PluginEditor.h"

VocoderAudioProcessorEditor::VocoderAudioProcessorEditor(VocoderAudioProcessor& p)
    : AudioProcessorEditor(&p), audioProcessor(p)
{
    // 画面サイズ設定（横600px、縦400px）
    setSize(600, 400);

    // スライダー初期化・配置用の関数（色指定と単位指定を追加）
    auto setupSlider = [this](juce::Slider& slider, std::unique_ptr<SliderAttachment>& attachment, const juce::String& paramID, juce::Colour fillColour, const juce::String& suffix)
    {
        // 円形ノブスタイルの適用
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        // 数値ボックスをノブ下部に配置
        slider.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
        
        // ノブの色設定
        slider.setColour(juce::Slider::rotarySliderFillColourId, fillColour);               // 塗りつぶし色
        slider.setColour(juce::Slider::thumbColourId, juce::Colours::lightgrey);            // つまみの色
        slider.setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour::fromRGB(50, 50, 60)); // 枠線の色
        
        // 単位（サフィックス）の設定
        slider.setTextValueSuffix(suffix);
        
        // 画面への追加
        addAndMakeVisible(slider);
        
        // プロセッサ側パラメータとの紐づけ
        attachment = std::make_unique<SliderAttachment>(audioProcessor.apvts, paramID, slider);
    };

    // 各ノブのセットアップ（個別の色と単位を指定）
    setupSlider(bandsSlider, bandsAttachment, "bands", juce::Colours::cyan, "");
    setupSlider(attackSlider, attackAttachment, "attack", juce::Colours::orange, " ms");
    setupSlider(releaseSlider, releaseAttachment, "release", juce::Colours::yellow, " ms");
    setupSlider(mixSlider, mixAttachment, "mix", juce::Colours::lightgreen, " %");
    setupSlider(goodizeSlider, goodizeAttachment, "goodize", juce::Colours::hotpink, " %");
    setupSlider(gainSlider, gainAttachment, "gain", juce::Colours::dodgerblue, " dB");
}

VocoderAudioProcessorEditor::~VocoderAudioProcessorEditor() {}

void VocoderAudioProcessorEditor::paint(juce::Graphics& g)
{
    // 背景の描画（ダークグレー）
    g.fillAll(juce::Colour::fromRGB(30, 30, 40));

    // タイトル文字の描画
    g.setColour(juce::Colours::white);
    g.setFont(30.0f);
    g.drawFittedText("MY VOCODER", getLocalBounds().removeFromTop(60), juce::Justification::centred, 1);

    // ノブラベルの描画設定
    g.setFont(14.0f);
    int knobSize = 100;
    int startY = 80;
    int spacing = getWidth() / 3;

    // 上段ラベル
    g.drawText("Bands",    0 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Attack",   1 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Release",  2 * spacing, startY + knobSize, spacing, 20, juce::Justification::centred);

    // 下段ラベル
    int startY2 = 230;
    g.drawText("Mix",      0 * spacing, startY2 + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Goodize",  1 * spacing, startY2 + knobSize, spacing, 20, juce::Justification::centred);
    g.drawText("Gain",     2 * spacing, startY2 + knobSize, spacing, 20, juce::Justification::centred);
}

void VocoderAudioProcessorEditor::resized()
{
    // ノブ配置用の基準値算出
    int knobSize = 100;
    int startY = 80;
    int spacing = getWidth() / 3;
    int offsetX = (spacing - knobSize) / 2;

    // 上段ノブの配置
    bandsSlider.setBounds  (0 * spacing + offsetX, startY, knobSize, knobSize);
    attackSlider.setBounds (1 * spacing + offsetX, startY, knobSize, knobSize);
    releaseSlider.setBounds(2 * spacing + offsetX, startY, knobSize, knobSize);

    // 下段ノブの配置
    int startY2 = 230;
    mixSlider.setBounds    (0 * spacing + offsetX, startY2, knobSize, knobSize);
    goodizeSlider.setBounds(1 * spacing + offsetX, startY2, knobSize, knobSize);
    gainSlider.setBounds   (2 * spacing + offsetX, startY2, knobSize, knobSize);
}