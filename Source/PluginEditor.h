#pragma once

#include "PluginProcessor.h"

class AdbSynthAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AdbSynthAudioProcessorEditor(AdbSynthAudioProcessor&);
    ~AdbSynthAudioProcessorEditor() override = default;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void tuneToNextFrequency();
    void updateButtonText();

    AdbSynthAudioProcessor& processor;
    juce::TextButton frequencyButton;
    int selectedFrequency = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdbSynthAudioProcessorEditor)
};