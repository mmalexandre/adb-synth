#pragma once

#include "PluginProcessor.h"

class AdbSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::Timer
{
public:
    explicit AdbSynthAudioProcessorEditor(AdbSynthAudioProcessor&);
    ~AdbSynthAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void chooseFile();
    void guessParameters();
    void updateGuess(const juce::String& output, const juce::String& error);
    void drawWaveform(juce::Graphics&, juce::Rectangle<int>) const;

    AdbSynthAudioProcessor& processor;
    juce::Slider frequencyKnob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment;
    juce::TextButton chooseButton { "Load audio" };
    juce::TextButton guessButton { "Guess" };
    juce::TextButton playFileButton { "Play file" };
    juce::TextButton playSynthButton { "Hold synth" };
    juce::Label fileLabel;
    juce::Label guessLabel;
    std::thread guessThread;
    std::atomic<bool> guessRunning { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdbSynthAudioProcessorEditor)
};