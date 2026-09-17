#include "PluginEditor.h"

#include <cmath>

AdbSynthAudioProcessorEditor::AdbSynthAudioProcessorEditor(AdbSynthAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor), processor(audioProcessor)
{
    setSize(180, 200);

    const auto& descriptor = adbsynth::parameterSchema[0];

    frequencyKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    frequencyKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 20);
    frequencyKnob.setRange(descriptor.minValue, descriptor.maxValue);
    frequencyKnob.setSkewFactorFromMidPoint(std::sqrt(descriptor.minValue * descriptor.maxValue));
    frequencyKnob.setNumDecimalPlacesToDisplay(2);
    frequencyKnob.setTextValueSuffix(" Hz");
    frequencyKnob.setTooltip(descriptor.label);
    addAndMakeVisible(frequencyKnob);

    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters,
        descriptor.id,
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