#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <array>
#include <cmath>

namespace
{
constexpr double twoPi = juce::MathConstants<double>::twoPi;
constexpr std::array<double, 5> frequencies { 220.0, 261.63, 440.0, 523.25, 880.0 };
}

AdbSynthAudioProcessor::AdbSynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AdbSynthAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterChoice>(
        "frequency",
        "Frequency",
        juce::StringArray { "220 Hz", "261.63 Hz", "440 Hz", "523.25 Hz", "880 Hz" },
        2));

    return layout;
}

const juce::String AdbSynthAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AdbSynthAudioProcessor::acceptsMidi() const
{
    return false;
}

bool AdbSynthAudioProcessor::producesMidi() const
{
    return false;
}

bool AdbSynthAudioProcessor::isMidiEffect() const
{
    return false;
}

double AdbSynthAudioProcessor::getTailLengthSeconds() const
{
    return 0.0;
}

int AdbSynthAudioProcessor::getNumPrograms()
{
    return 1;
}

int AdbSynthAudioProcessor::getCurrentProgram()
{
    return 0;
}

void AdbSynthAudioProcessor::setCurrentProgram(int)
{
}

const juce::String AdbSynthAudioProcessor::getProgramName(int)
{
    return {};
}

void AdbSynthAudioProcessor::changeProgramName(int, const juce::String&)
{
}

void AdbSynthAudioProcessor::prepareToPlay(double sampleRate, int)
{
    currentSampleRate = sampleRate;
    phase = 0.0;
}

void AdbSynthAudioProcessor::releaseResources()
{
}

bool AdbSynthAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto output = layouts.getMainOutputChannelSet();
    return output == juce::AudioChannelSet::mono() || output == juce::AudioChannelSet::stereo();
}

void AdbSynthAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    const auto frequencyIndex = static_cast<int>(*parameters.getRawParameterValue("frequency"));
    const auto frequency = frequencies[static_cast<size_t>(juce::jlimit(0, static_cast<int>(frequencies.size()) - 1, frequencyIndex))];
    const auto phaseStep = twoPi * frequency / currentSampleRate;
    const auto outputGain = 0.15f;

    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
    {
        const auto value = static_cast<float>(std::sin(phase)) * outputGain;
        phase = std::fmod(phase + phaseStep, twoPi);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.setSample(channel, sample, value);
    }
}

bool AdbSynthAudioProcessor::hasEditor() const
{
    return true;
}

juce::AudioProcessorEditor* AdbSynthAudioProcessor::createEditor()
{
    return new AdbSynthAudioProcessorEditor(*this);
}

void AdbSynthAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    const auto state = parameters.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AdbSynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
        parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AdbSynthAudioProcessor();
}