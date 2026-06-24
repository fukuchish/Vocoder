#include "PluginProcessor.h"
#include "PluginEditor.h"

// ==============================================================================
// コンストラクタ: プラグイン生成時の初期化処理
// ==============================================================================
VocoderAudioProcessor::VocoderAudioProcessor()
    // BusesProperties()でDAWへプラグインの入出力を定義
    : AudioProcessor(BusesProperties()
          // 入力1: メインの声（モジュレーター）
          .withInput("Voice (Modulator)", juce::AudioChannelSet::stereo(), true)
          // 入力2: サイドチェイン用外部シンセ（キャリア）
          .withInput("Synth (Carrier)",   juce::AudioChannelSet::stereo(), true)
          // 出力: 最終ミックス音
          .withOutput("Output",           juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "Parameters", createParameters())
{
    // エンベロープ配列のゼロ初期化
    envelopes.fill(0.0f);
}

VocoderAudioProcessor::~VocoderAudioProcessor() {}

// ==============================================================================
// パラメータ定義: DAWから操作可能な変数の設定
// ==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
VocoderAudioProcessor::createParameters()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    // バンド数: 音の分割数（4〜100、デフォルト100）
    // 少ないほどレトロ、多いほどクリアな音質に変化
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "bands", "Bands", 4, 100, 100
    ));

    // Q値（レゾナンス）: フィルターの鋭さ
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "attack", "Attack (ms)", 1.0f, 100.0f, 5.0f
    ));

    // Release: 音の減衰時間
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "release", "Release (ms)", 5.0f, 500.0f, 50.0f
    ));

    // Mix: 原音とエフェクト音のブレンド割合（%）
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "mix", "Mix (%)", 0.0f, 100.0f, 50.0f
    ));

    // Goodize: サチュレーションによるアナログ感と音圧の付加
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "goodize", "Goodize (%)", 0.0f, 100.0f, 0.0f
    ));

    // Gain: 最終出力音量
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        "gain", "Gain", -60.0f, 12.0f, 0.0f
    ));

    return layout;
}

// ==============================================================================
// バス設定: 入出力チャンネルの構成ルール
// ==============================================================================
bool VocoderAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    // メイン出力のモノラル/ステレオ判定
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    // メイン入力（声）のモノラル/ステレオ判定
    if (layouts.getMainInputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainInputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

// ==============================================================================
// 再生準備: 再生開始時の初期化処理
// ==============================================================================
void VocoderAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = samplesPerBlock;
    spec.numChannels = getTotalNumOutputChannels();

    // 最大数（100）のフィルターを事前準備
    for (int i = 0; i < maxBands; ++i)
    {
        modFilters[i].prepare(spec);
        modFilters[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        
        carFilters[i].prepare(spec);
        carFilters[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
    }

    // 周波数再計算トリガーのリセット
    lastActiveBands = -1; 

    // 内部オシレーターの初期化
    for (int i = 0; i < numVoices; ++i)
    {
        carrierOscs[i].prepare(spec);
        carrierOscs[i].initialise([](float x) { return x / juce::MathConstants<float>::pi; });
        carrierGates[i] = 0.0f;
        activeNotes[i] = -1;
    }
}

void VocoderAudioProcessor::releaseResources() {}

// ==============================================================================
// 音声処理: サンプルごとのリアルタイムDSP処理
// ==============================================================================
void VocoderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // 非正規化数によるCPUスパイク防止
    juce::ScopedNoDenormals noDenormals;

    // --------------------------------------------------------------------------
    // 1. 和音対応のMIDIノート振り分け処理
    // --------------------------------------------------------------------------
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();
        int note = message.getNoteNumber();

        if (message.isNoteOn())
        {
            // 空きボイスにノートを割り当て
            for (int i = 0; i < numVoices; ++i)
            {
                if (carrierGates[i] == 0.0f)
                {
                    carrierOscs[i].setFrequency((float)message.getMidiNoteInHertz(note));
                    carrierGates[i] = 1.0f;
                    activeNotes[i] = note;
                    break;
                }
            }
        }
        else if (message.isNoteOff())
        {
            // 該当するノートのボイスを停止
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

    // --------------------------------------------------------------------------
    // 2. バンド数の動的割り当て
    // --------------------------------------------------------------------------
    int activeBands = (int)apvts.getRawParameterValue("bands")->load();

    // バンド数変更時のみ周波数を再計算
    if (activeBands != lastActiveBands)
    {
        float minFreq = 100.0f;
        float maxFreq = 8000.0f;
        
        // 対数スケールによる周波数の等分割
        for (int i = 0; i < activeBands; ++i)
        {
            float freq = minFreq * std::pow(maxFreq / minFreq, (float)i / (activeBands - 1));
            modFilters[i].setCutoffFrequency(freq);
            carFilters[i].setCutoffFrequency(freq);
        }
        lastActiveBands = activeBands; // 変更状態の記録
    }

    // --------------------------------------------------------------------------
    // 3. パラメータの取得と係数計算
    // --------------------------------------------------------------------------
    float mixWet = apvts.getRawParameterValue("mix")->load() * 0.01f;
    float mixDry = 1.0f - mixWet;
    float goodizePct = apvts.getRawParameterValue("goodize")->load() * 0.01f;
    float drive = 1.0f + (goodizePct * 4.0f); 

    float q = apvts.getRawParameterValue("q")->load();
    float attackMs = apvts.getRawParameterValue("attack")->load();
    float releaseMs = apvts.getRawParameterValue("release")->load();
    float gainLinear = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("gain")->load());

    // 使用バンド分のQ値を更新
    for (int i = 0; i < activeBands; ++i) {
        modFilters[i].setResonance(q);
        carFilters[i].setResonance(q);
    }

    // サンプル単位のエンベロープ係数
    float attackCoef = std::exp(-1.0f / ((attackMs * 0.001f) * currentSampleRate));
    float releaseCoef = std::exp(-1.0f / ((releaseMs * 0.001f) * currentSampleRate));

    // --------------------------------------------------------------------------
    // 4. バスバッファの取得
    // --------------------------------------------------------------------------
    auto voiceBuffer = getBusBuffer(buffer, true, 0); 
    auto synthBuffer = getBusBuffer(buffer, true, 1); 

    auto* channelData = buffer.getWritePointer(0);

    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        // 信号未接続時は無音（0.0f）に設定
        float modInput = voiceBuffer.getNumChannels() > 0 ? voiceBuffer.getSample(0, s) : 0.0f;
        float carInput = synthBuffer.getNumChannels() > 0 ? synthBuffer.getSample(0, s) : 0.0f;

        float drySignal = modInput;
        float outputSum = 0.0f;
        
        // ----------------------------------------------------------------------
        // 5. フィルターとエンベロープ処理
        // ----------------------------------------------------------------------
        // 指定されたバンド数だけ処理を実行
        for (int b = 0; b < activeBands; ++b)
        {
            float m = modFilters[b].processSample(0, modInput);
            float c = carFilters[b].processSample(0, carInput);

            // 絶対値による振幅抽出
            float rect = std::abs(m);
            
            // アタック/リリース特性を持つエンベロープ生成
            if (rect > envelopes[b])
                envelopes[b] = attackCoef * envelopes[b] + (1.0f - attackCoef) * rect;
            else
                envelopes[b] = releaseCoef * envelopes[b] + (1.0f - releaseCoef) * rect;

            // キャリア信号へのエンベロープ適用と加算
            outputSum += c * envelopes[b];
        }

        // バンド数に応じた音量補正
        float wetSignal = outputSum * (8.0f / (float)activeBands);

        // ----------------------------------------------------------------------
        // 6. 出力合成とサチュレーション
        // ----------------------------------------------------------------------
        float mixedSignal = (drySignal * mixDry) + (wetSignal * mixWet);
        
        // tanhカーブによるソフトクリッピング（アナログ感の付加）
        float goodizedSignal = std::tanh(mixedSignal * drive) / std::tanh(drive);

        // 最終ゲイン適用
        channelData[s] = goodizedSignal * gainLinear;
        
        // ステレオ時の右チャンネルコピー
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