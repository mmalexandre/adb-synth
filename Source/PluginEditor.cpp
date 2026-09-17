#include "PluginEditor.h"

#include <array>

namespace
{
constexpr std::array<float, 5> frequencies { 220.0f, 261.63f, 440.0f, 523.25f, 880.0f };
}

AdbSynthAudioProcessorEditor::AdbSynthAudioProcessorEditor(AdbSynthAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor), processor(audioProcessor)
{
    setSize(280, 120);

    frequencyButton.setButtonText({});
    frequencyButton.onClick = [this] { tuneToNextFrequency(); };
    addAndMakeVisible(frequencyButton);
    updateButtonText();
}

void AdbSynthAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff101418));
}

void AdbSynthAudioProcessorEditor::resized()
{
    frequencyButton.setBounds(getLocalBounds().reduced(24));
}

void AdbSynthAudioProcessorEditor::tuneToNextFrequency()
{
    selectedFrequency = (selectedFrequency + 1) % static_cast<int>(frequencies.size());

    if (auto* parameter = dynamic_cast<juce::RangedAudioParameter*>(processor.parameters.getParameter("frequency")))
        parameter->setValueNotifyingHost(parameter->convertTo0to1(frequencies[static_cast<size_t>(selectedFrequency)]));

    updateButtonText();
}

void AdbSynthAudioProcessorEditor::updateButtonText()
{
    frequencyButton.setButtonText("Tune " + juce::String(frequencies[static_cast<size_t>(selectedFrequency)], 2) + " Hz");
}