#include "PluginEditor.h"

AdbSynthAudioProcessorEditor::AdbSynthAudioProcessorEditor(AdbSynthAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor), processor(audioProcessor)
{
    setSize(180, 180);

    frequencyKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    frequencyKnob.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    frequencyKnob.setRange(0.0, 4.0, 1.0);
    frequencyKnob.setNumDecimalPlacesToDisplay(0);
    frequencyKnob.setTooltip("Frequency");
    addAndMakeVisible(frequencyKnob);

    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters,
        "frequency",
        frequencyKnob);
}

void AdbSynthAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff101418));
}

void AdbSynthAudioProcessorEditor::resized()
{
    frequencyKnob.setBounds(getLocalBounds().reduced(24));
}