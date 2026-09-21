#pragma once

#include "PluginProcessor.h"

class AdbSynthAudioProcessorEditor : public juce::AudioProcessorEditor,
                                     private juce::ListBoxModel,
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
    int getNumRows() override;
    void paintListBoxItem(int rowNumber, juce::Graphics&, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent&) override;
    void updateFolderFiles(const juce::File&);
    void loadSelectedFile(juce::File);
    void guessParameters();
    void updateSourceOfTruth(const juce::File&);
    void updateGuess(const juce::String& output, const juce::String& error);
    void drawWaveform(juce::Graphics&, juce::Rectangle<int>) const;

    AdbSynthAudioProcessor& processor;
    juce::Slider frequencyKnob;
    juce::Slider frequency2Knob;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequencyAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> frequency2Attachment;
    juce::TextButton chooseButton { "Load audio" };
    juce::TextButton guessButton { "Guess" };
    juce::TextButton playFileButton { "Play file" };
    juce::TextButton playSynthButton { "Hold synth" };
    juce::Label fileLabel;
    juce::Label guessLabel;
    juce::ListBox folderFilesList { "Folder files", this };
    juce::TextEditor sourceOfTruthEditor;
    juce::File sourceOfTruthForFile;
    juce::File lastAudioDirectory;
    juce::Array<juce::File> folderFiles;
    std::thread guessThread;
    std::atomic<bool> guessRunning { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdbSynthAudioProcessorEditor)
};