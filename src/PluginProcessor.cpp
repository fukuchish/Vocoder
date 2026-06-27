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

    // ▼ 新規追加：プリエンファシス・フィルターの初期化
    preEmphasisFilter.prepare(spec);
    // 3000Hz以上を +8dB ブースト（Q値は標準的な 0.707）
    preEmphasisFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighShelf(
        sampleRate, 3000.0, 0.707, juce::Decibels::decibelsToGain(8.0f)
    );


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

    // バンド数変更時のみ周波数とQ値を再計算
    if (activeBands != lastActiveBands)
    {
        float minFreq = 100.0f;
        float maxFreq = 8000.0f;
        
        // 隣接する1バンド間の周波数比率
        float ratio = std::pow(maxFreq / minFreq, 1.0f / (float)(activeBands - 1));
        
        // フィルターの帯域幅を「隣接する2バンド分」に拡張するための数学的算出
        // 帯域幅比率を ratio^2 とした場合のQ値の厳密な定義式
        float dynamicQ = ratio / (std::pow(ratio, 2.0f) - 1.0f);

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
        // ▼ 修正：まず原音（Dry）をピュアな状態で取得
        float drySignal = voiceBuffer.getNumChannels() > 0 ? voiceBuffer.getSample(0, s) : 0.0f;
        
        // ▼ 追加：ボコーダー処理用のモジュレーター信号にのみ、強烈な高域ブーストを掛ける
        float modInput = preEmphasisFilter.processSample(drySignal);

        // キャリアはステレオで取得
        float carInputL = synthBuffer.getNumChannels() > 0 ? synthBuffer.getSample(0, s) : 0.0f;
        float carInputR = synthBuffer.getNumChannels() > 1 ? synthBuffer.getSample(1, s) : carInputL;

        float outputSumL = 0.0f;
        float outputSumR = 0.0f;

        
        for (int b = 0; b < activeBands; ++b)
        {
            float m = modFilters[b].processSample(0, modInput);
            float cL = carFiltersL[b].processSample(0, carInputL);
            float cR = carFiltersR[b].processSample(0, carInputR);

            float rect = std::abs(m);
            if (rect > envelopes[b])
                envelopes[b] = attackCoef * envelopes[b] + (1.0f - attackCoef) * rect;
            else
                envelopes[b] = releaseCoef * envelopes[b] + (1.0f - releaseCoef) * rect;

            outputSumL += cL * envelopes[b];
            outputSumR += cR * envelopes[b];
        }

        // ----------------------------------------------------------------------
        // 5. 音量補正とサチュレーション
        // ----------------------------------------------------------------------
        // 複数の帯域を合成する際、エネルギー保存則（RMS加算則）に基づき 1 / √N で正規化
        float scalingFactor = 1.0f / std::sqrt((float)activeBands);
        
        float wetSignalL = outputSumL * scalingFactor;
        float wetSignalR = outputSumR * scalingFactor;

        // ミックス
        float mixedSignalL = (drySignal * mixDry) + (wetSignalL * mixWet);
        float mixedSignalR = (drySignal * mixDry) + (wetSignalR * mixWet);
        
        // tanhによるソフトクリッピング（出力が数学的に必ず -1.0 〜 +1.0 の間に漸近する）
        channelData[s] = std::tanh(mixedSignalL * drive) * gainLinear;
        
        if (buffer.getNumChannels() > 1)
        {
            buffer.getWritePointer(1)[s] = std::tanh(mixedSignalR * drive) * gainLinear;
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