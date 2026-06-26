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

    // ▼ 修正：バンド数の範囲を 4〜32、デフォルトを 24 程度に変更
    layout.add(std::make_unique<juce::AudioParameterInt>(
        "bands", "Bands", 4, 32, 16
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
    // prepareToPlay内の初期化
    for (int i = 0; i < maxBands; ++i)
    {
        modFilters[i].prepare(spec);
        modFilters[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
        
        carFiltersL[i].prepare(spec);
        carFiltersL[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);

        carFiltersR[i].prepare(spec);
        carFiltersR[i].setType(juce::dsp::StateVariableTPTFilterType::bandpass);
    }

    // 周波数再計算トリガーのリセット
    lastActiveBands = -1; 

    // ※内部オシレーターの初期化処理は削除しました
}

void VocoderAudioProcessor::releaseResources() {}

// ==============================================================================
// 音声処理: サンプルごとのリアルタイムDSP処理
// ==============================================================================
void VocoderAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    // 非正規化数によるCPUスパイク防止
    juce::ScopedNoDenormals noDenormals;

    // ※MIDIノートによる発音管理処理は削除しました

    // --------------------------------------------------------------------------
    // 1. バンド数の動的割り当てと最適Q値の算出
    // --------------------------------------------------------------------------
    int activeBands = (int)apvts.getRawParameterValue("bands")->load();

    // バンド数変更時のみ周波数とQ値を再計算（CPU負荷の最適化）
    if (activeBands != lastActiveBands)
    {
        float minFreq = 100.0f;
        float maxFreq = 8000.0f;
        
        // 隣接バンド間の周波数比率（数式における 2^N に該当）
        float ratio = std::pow(maxFreq / minFreq, 1.0f / (float)(activeBands - 1));

        // 数式に基づく動的Q値の計算: Q = sqrt(R) / (R - 1)
        float dynamicQ = std::sqrt(ratio) / (ratio - 1.0f);

        // ※補足：滑舌をより強調したい場合は、この dynamicQ に 1.2f〜1.5f ほどの係数を掛けて
        // わざとQ値を少し高め（鋭く）設定するのも実践的なテクニックです。

        for (int i = 0; i < activeBands; ++i)
        {
            float freq = minFreq * std::pow(ratio, (float)i);
            
            // 周波数の設定
            modFilters[i].setCutoffFrequency(freq);
            carFiltersL[i].setCutoffFrequency(freq);
            carFiltersR[i].setCutoffFrequency(freq);

            // 動的に算出したQ値の一括適用
            modFilters[i].setResonance(dynamicQ);
            carFiltersL[i].setResonance(dynamicQ);
            carFiltersR[i].setResonance(dynamicQ);
        }
        lastActiveBands = activeBands; // 変更状態の記録
    }

    // --------------------------------------------------------------------------
    // 2. パラメータの取得と係数計算
    // --------------------------------------------------------------------------
    float mixWet = apvts.getRawParameterValue("mix")->load() * 0.01f;
    float mixDry = 1.0f - mixWet;
    float goodizePct = apvts.getRawParameterValue("goodize")->load() * 0.01f;
    float drive = 1.0f + (goodizePct * 4.0f); 

    // ▼ 削除：以前あった float q = 2.0f; や、その下のQ値更新用 forループ はすべて削除！

    float attackMs = apvts.getRawParameterValue("attack")->load();
    float releaseMs = apvts.getRawParameterValue("release")->load();
    float gainLinear = juce::Decibels::decibelsToGain(apvts.getRawParameterValue("gain")->load());

    // サンプル単位のエンベロープ係数
    float attackCoef = std::exp(-1.0f / ((attackMs * 0.001f) * currentSampleRate));
    float releaseCoef = std::exp(-1.0f / ((releaseMs * 0.001f) * currentSampleRate));
    

    // --------------------------------------------------------------------------
    // 3. バスバッファの取得
    // --------------------------------------------------------------------------
    auto voiceBuffer = getBusBuffer(buffer, true, 0); 
    auto synthBuffer = getBusBuffer(buffer, true, 1); 

    auto* channelData = buffer.getWritePointer(0);

    for (int s = 0; s < buffer.getNumSamples(); ++s)
    {
        // 信号未接続時は無音（0.0f）に設定
        // 信号の取得（キャリアはステレオで取得）
        float modInput = voiceBuffer.getNumChannels() > 0 ? voiceBuffer.getSample(0, s) : 0.0f;
        float carInputL = synthBuffer.getNumChannels() > 0 ? synthBuffer.getSample(0, s) : 0.0f;
        float carInputR = synthBuffer.getNumChannels() > 1 ? synthBuffer.getSample(1, s) : carInputL; // キャリアがモノラルならRにはLと同じものを入れる

        float drySignal = modInput; // モジュレーターの原音（必要に応じてLのみ）
        float outputSumL = 0.0f;
        float outputSumR = 0.0f;
        
        for (int b = 0; b < activeBands; ++b)
        {
            // モジュレーターは1回のみ処理
            float m = modFilters[b].processSample(0, modInput);

            // キャリアはLとRを独立して処理
            float cL = carFiltersL[b].processSample(0, carInputL);
            float cR = carFiltersR[b].processSample(0, carInputR);

            // モジュレーターからエンベロープ（声の動き）を抽出
            float rect = std::abs(m);
            if (rect > envelopes[b])
                envelopes[b] = attackCoef * envelopes[b] + (1.0f - attackCoef) * rect;
            else
                envelopes[b] = releaseCoef * envelopes[b] + (1.0f - releaseCoef) * rect;

            // 抽出した1つの声の動きを、LとRのキャリアにそれぞれ掛ける
            outputSumL += cL * envelopes[b];
            outputSumR += cR * envelopes[b];
        }

        // 音量補正
        float wetSignalL = outputSumL * (8.0f / (float)activeBands);
        float wetSignalR = outputSumR * (8.0f / (float)activeBands);

        // ミックス
        float mixedSignalL = (drySignal * mixDry) + (wetSignalL * mixWet);
        float mixedSignalR = (drySignal * mixDry) + (wetSignalR * mixWet); // Dry音はモジュレーターのモノラル音を両耳に流す
        
        // サチュレーションと最終出力
        channelData[s] = (std::tanh(mixedSignalL * drive) / std::tanh(drive)) * gainLinear; // Lチャンネル
        
        if (buffer.getNumChannels() > 1)
        {
            buffer.getWritePointer(1)[s] = (std::tanh(mixedSignalR * drive) / std::tanh(drive)) * gainLinear; // Rチャンネル
        }
    }
}

juce::AudioProcessorEditor* VocoderAudioProcessor::createEditor()
{
    return new VocoderAudioProcessorEditor(*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new VocoderAudioProcessor();
}