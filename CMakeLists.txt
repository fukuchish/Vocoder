cmake_minimum_required(VERSION 3.22)
project(Vocoder VERSION 0.0.1)

set(JUCE_DIR "C:/JUCE")
add_subdirectory(${JUCE_DIR} JUCE)

juce_add_plugin(Vocoder
    PLUGIN_MANUFACTURER_CODE Manu
    PLUGIN_CODE ShVo
    FORMATS VST3
    PRODUCT_NAME "My Vocoder 1"
    NEEDS_MIDI_INPUT TRUE  # ← これを追加！
)

# ★追加1: JuceHeader.h を明示的に生成する
juce_generate_juce_header(Vocoder)

target_sources(Vocoder PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_compile_features(Vocoder PRIVATE cxx_std_17)

# ★追加2: 文字コードの警告（C4819）を消すためのコンパイルオプション
if(MSVC)
    target_compile_options(Vocoder PRIVATE /utf-8)
endif()

target_link_libraries(Vocoder PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
)

# VST2との互換性チェックを無効化（警告エラーを防ぐ）
target_compile_definitions(Vocoder PUBLIC
    JUCE_VST3_CAN_REPLACE_VST2=0
)