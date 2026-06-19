#include "PluginProcessor.h"
#include "PluginEditor.h"

// ▼ 修正：BusesProperties() を使って、メイン入力とサイドチェイン入力の2つを定義します
VocoderAudioProcessor::VocoderAudioProcessor()
    : AudioProcessor(BusesProperties()
          .withInput("Voice (Modulator)", juce::AudioChannelSet::stereo(), true)
          .withInput("Synth (Carrier)",   juce::AudioChannelSet::stereo(), true) // ここがサイドチェイン
          .withOutput("Output",           juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameters())
{
    envelopes.fill(0.0f);
}

VocoderAudioProcessor::~VocoderAudioProcessor() {}

juce::AudioProcessorValueTreeState::ParameterLayout
VocoderAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // ▼ 修正：最大値を100.0f、デフォルトを40.0fなどに引き上げます
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "q", "Bandwidth (Q)", 1.0f, 100.0f, 40.0f
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack (ms)", 1.0f, 100.0f, 5.0f
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release (ms)", 5.0f, 500.0f, 50.0f
    ));

    // ▼ 新規追加：MIX比（0% = 原音のみ、100% = ボコーダーのみ）
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix (%)", 0.0f, 100.0f, 50.0f
    ));

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain", -60.0f, 12.0f, 0.0f
    ));

    return layout;
}

// ▼ 新規追加：入出力のチャンネル設定ルールを定義
bool VocoderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // メイン出力がモノラルかステレオならOK
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // メイン入力（声）がモノラルかステレオならOK
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void VocoderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();

    // 16バンド分の周波数を対数スケールで計算して割り当て（100Hz 〜 8000Hz）
    float minFreq = 100.0f;
    float maxFreq = 8000.0f;

    for (int i = 0; i < numBands; ++i)
    {
        modFilters[i].prepare(spec);
        modFilters[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        
        carFilters[i].prepare(spec);
        carFilters[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);

        // 対数的な周波数間隔の計算
        float freq = minFreq * std::pow(maxFreq / minFreq, (float)i / (numBands - 1));
        modFilters[i].setCutoffFrequency(freq);
        carFilters[i].setCutoffFrequency(freq);
    }

    // ▼ 修正：8つのオシレーターすべてを初期化する
    for (int i = 0; i < numVoices; ++i)
    {
        carrierOscs[i].prepare(spec);
        carrierOscs[i].initialise([](float x) { return x / juce::MathConstants<float>::pi; });
        carrierGates[i] = 0.0f;
        activeNotes[i] = -1; // -1 は「何も鳴っていない」状態
    }
}

void VocoderAudioProcessor::releaseResources() {}

void VocoderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // ▼ 修正：和音に対応したMIDIの振り分け処理
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();
        int note = message.getNoteNumber();

        if (message.isNoteOn())
        {
            // 空いているボイス（gateが0のもの）を探して音を割り当てる
            for (int i = 0; i < numVoices; ++i)
            {
                if (carrierGates[i] == 0.0f)
                {
                    carrierOscs[i].setFrequency((float)message.getMidiNoteInHertz(note));
                    carrierGates[i] = 1.0f;
                    activeNotes[i] = note;
                    break; // 1つ見つけたら終了
                }
            }
        }
        else if (message.isNoteOff())
        {
            // 離された鍵盤と同じノート番号を持つボイスを探して止める
            for (int i = 0; i < numVoices; ++i)
            {
                if (activeNotes[i] == note)
                {
                    carrierGates[i] = 0.0f;
                    activeNotes[i] = -1;
                }
            }
        }
    }

    // ▼ 修正：MIXパラメータを取得し、0.0 〜 1.0 の係数に変換
    float mixWet = apvts.getRawParameterValue("mix")->load() * 0.01f;
    float mixDry = 1.0f - mixWet;

    float q = apvts.getRawParameterValue("q")->load();
    float attackMs = apvts.getRawParameterValue("attack")->load();
    float releaseMs = apvts.getRawParameterValue("release")->load();
    float gainLinear = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("gain")->load());

    for (int i = 0; i < numBands; ++i) {
        modFilters[i].setResonance(q);
        carFilters[i].setResonance(q);
    }

    float attackCoef = std::exp(-1.0f / ((attackMs * 0.001f) * currentSampleRate));
    float releaseCoef = std::exp(-1.0f / ((releaseMs * 0.001f) * currentSampleRate));

    // ▼ 新規追加：バス（入力口）ごとにバッファを切り分ける
    auto voiceBuffer = getBusBuffer(buffer, true, 0); // メイン入力（声）
    auto synthBuffer = getBusBuffer(buffer, true, 1); // サイドチェイン入力（シンセ）

    auto* channelData = buffer.getWritePointer(0);

    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        // ▼ 修正：バッファから音を取得。繋がっていない場合は無音(0.0f)にする
        float modInput = voiceBuffer.getNumChannels() > 0 ? voiceBuffer.getSample(0, s) : 0.0f;
        
        // ▼ 修正：内部オシレーターではなく、外部シンセ（サイドチェイン）の音を直接使う！
        float carInput = synthBuffer.getNumChannels() > 0 ? synthBuffer.getSample(0, s) : 0.0f;

        float drySignal = modInput;
        float outputSum = 0.0f;
        
        // Bands
        for (int b = 0; b < numBands; ++b)
        {
            float m = modFilters[b].processSample(0, modInput);
            float c = carFilters[b].processSample(0, carInput);

            float rect = std::abs(m);
            if (rect > envelopes[b])
                envelopes[b] = attackCoef * envelopes[b] + (1.0f - attackCoef) * rect;
            else
                envelopes[b] = releaseCoef * envelopes[b] + (1.0f - releaseCoef) * rect;

            outputSum += c * envelopes[b];
        }

        // ▼ 修正：バンド数に応じて音量を自動で下げるように計算式を変更
        // （16バンドの時と同じくらいの音量感になるように調整しています）
        float wetSignal = outputSum * (8.0f / numBands);

        // ▼ 修正：Dry音とWet音をMIX比でブレンドしてからゲインを掛ける
        channelData[s] = ((drySignal * mixDry) + (wetSignal * mixWet)) * gainLinear;
        
        if (buffer.getNumChannels() > 1)
        {
            buffer.getWritePointer(1)[s] = channelData[s];
        }
    }
}

juce::AudioProcessorEditor* VocoderAudioProcessor::createEditor()
{
    return new juce::GenericAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocoderAudioProcessor();
}