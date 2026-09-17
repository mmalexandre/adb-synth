#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>

AdbSynthAudioProcessor::AdbSynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout AdbSynthAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (const auto& descriptor : adbsynth::parameterSchema)
    {
        switch (descriptor.kind)
        {
            case adbsynth::ParameterKind::Float:
            {
                juce::NormalisableRange<float> range(descriptor.minValue, descriptor.maxValue);

                if (descriptor.logScale)
                    range.setSkewForCentre(std::sqrt(descriptor.minValue * descriptor.maxValue));

                layout.add(std::make_unique<juce::AudioParameterFloat>(
                    juce::ParameterID { descriptor.id, 1 },
                    descriptor.label,
                    range,
                    descriptor.defaultValue,
                    juce::AudioParameterFloatAttributes().withLabel(descriptor.unit)));
                break;
            }

            case adbsynth::ParameterKind::Choice:
            {
                juce::StringArray choices;

                for (int choice = 0; choice < descriptor.numChoices; ++choice)
                    choices.add(descriptor.choices[choice]);

                layout.add(std::make_unique<juce::AudioParameterChoice>(
                    juce::ParameterID { descriptor.id, 1 },
                    descriptor.label,
                    choices,
                    static_cast<int>(descriptor.defaultValue)));
                break;
            }

            case adbsynth::ParameterKind::Bool:
            {
                layout.add(std::make_unique<juce::AudioParameterBool>(
                    juce::ParameterID { descriptor.id, 1 },
                    descriptor.label,
                    descriptor.defaultValue > 0.5f));
                break;
            }
        }
    }

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
    engine.prepare(sampleRate);
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
    adbsynth::SynthParams params;
    params.frequency = parameters.getRawParameterValue("frequency")->load();

    engine.render(buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), params);
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