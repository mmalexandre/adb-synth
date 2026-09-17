#include "PluginProcessor.h"
#include "PluginEditor.h"

#include <cmath>
#include <iostream>
#include <torch/script.h>

#include <cstdlib>

#ifndef ADBSYNTH_PROJECT_DIR
#define ADBSYNTH_PROJECT_DIR "."
#endif

AdbSynthAudioProcessor::AdbSynthAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "Parameters", createParameterLayout())
{
        formatManager.registerBasicFormats();
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
    buffer.clear();

    const auto audio = std::atomic_load_explicit(&loadedAudio, std::memory_order_acquire);

    if (audio != nullptr && filePlaybackActive.load(std::memory_order_acquire))
    {
        const auto position = playbackPosition.load(std::memory_order_relaxed);
        const auto samples = audio->getNumSamples();

        if (position < samples)
        {
            const auto count = std::min(buffer.getNumSamples(), samples - position);

            for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            {
                const auto sourceChannel = std::min(channel, audio->getNumChannels() - 1);
                buffer.copyFrom(channel, 0, *audio, sourceChannel, position, count);
            }

            playbackPosition.fetch_add(count, std::memory_order_relaxed);

            if (position + count >= samples)
                filePlaybackActive.store(false, std::memory_order_release);
        }
        else
        {
            filePlaybackActive.store(false, std::memory_order_release);
        }
    }

    if (synthHeld.load(std::memory_order_acquire))
    {
        adbsynth::SynthParams params;
        params.frequency = parameters.getRawParameterValue("frequency")->load();

        juce::AudioBuffer<float> synthBuffer(buffer.getNumChannels(), buffer.getNumSamples());
        engine.render(synthBuffer.getArrayOfWritePointers(), synthBuffer.getNumChannels(), synthBuffer.getNumSamples(), params);

        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            buffer.addFrom(channel, 0, synthBuffer, channel, 0, buffer.getNumSamples());
    }
}

bool AdbSynthAudioProcessor::loadAudioFile(const juce::File& file)
{
    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(file));

    if (reader == nullptr || reader->lengthInSamples <= 0)
        return false;

    auto audio = std::make_shared<juce::AudioBuffer<float>>(static_cast<int>(reader->numChannels),
                                                            static_cast<int>(reader->lengthInSamples));

    if (!reader->read(audio.get(), 0, audio->getNumSamples(), 0, true, true))
        return false;

    std::vector<float> nextWaveform;
    constexpr int waveformPoints = 512;
    nextWaveform.reserve(waveformPoints);

    for (int point = 0; point < waveformPoints; ++point)
    {
        const auto start = point * audio->getNumSamples() / waveformPoints;
        const auto end = std::max(start + 1, (point + 1) * audio->getNumSamples() / waveformPoints);
        float peak = 0.0f;

        for (int sample = start; sample < std::min(end, audio->getNumSamples()); ++sample)
            peak = std::max(peak, std::abs(audio->getSample(0, sample)));

        nextWaveform.push_back(peak);
    }

    {
        const std::lock_guard lock(audioStateMutex);
        loadedAudioFile = file;
        waveform = std::move(nextWaveform);
    }

    std::atomic_store_explicit(&loadedAudio, std::shared_ptr<const juce::AudioBuffer<float>>(audio), std::memory_order_release);
    playbackPosition.store(0, std::memory_order_release);
    filePlaybackActive.store(false, std::memory_order_release);
    return true;
}

juce::File AdbSynthAudioProcessor::getLoadedAudioFile() const
{
    const std::lock_guard lock(audioStateMutex);
    return loadedAudioFile;
}

std::vector<float> AdbSynthAudioProcessor::getWaveform() const
{
    const std::lock_guard lock(audioStateMutex);
    return waveform;
}

void AdbSynthAudioProcessor::triggerFilePlayback()
{
    if (std::atomic_load_explicit(&loadedAudio, std::memory_order_acquire) != nullptr)
    {
        playbackPosition.store(0, std::memory_order_release);
        filePlaybackActive.store(true, std::memory_order_release);
    }
}

void AdbSynthAudioProcessor::setSynthHeld(bool held)
{
    synthHeld.store(held, std::memory_order_release);
}

bool AdbSynthAudioProcessor::runModelGuess(const juce::File& file, juce::String& result, juce::String& error) const
{
    const juce::File modelFile(ADBSYNTH_TORCH_MODEL);
    if (!modelFile.existsAsFile())
    {
        error = "Native model is missing. Run make ml-export after training.";
        return false;
    }

    const auto audio = std::atomic_load_explicit(&loadedAudio, std::memory_order_acquire);
    if (audio == nullptr || file != getLoadedAudioFile())
    {
        error = "Audio is no longer loaded.";
        return false;
    }

    try
    {
        constexpr int clipSamples = 16384;
        std::vector<float> clip(static_cast<std::size_t>(clipSamples), 0.0f);
        const auto sourceLength = audio->getNumSamples();
        const auto start = std::max(0, (sourceLength - clipSamples) / 2);
        const auto samplesToCopy = std::min(clipSamples, sourceLength - start);
        if (samplesToCopy > 0)
            std::copy_n(audio->getReadPointer(0, start), samplesToCopy, clip.begin());

        auto input = torch::from_blob(clip.data(), { 1, clipSamples }, torch::kFloat32).clone();
        torch::jit::script::Module model = torch::jit::load(modelFile.getFullPathName().toStdString());
        model.eval();
        torch::NoGradGuard noGrad;
        const auto outputs = model.forward({ input }).toTuple()->elements();

        auto* object = new juce::DynamicObject();
        for (std::size_t index = 0; index < adbsynth::parameterSchema.size(); ++index)
            object->setProperty(adbsynth::parameterSchema[index].id, outputs[index].toTensor().item<float>());
        result = juce::JSON::toString(juce::var(object));
        return true;
    }
    catch (const c10::Error& exception)
    {
        error = "Native model inference failed: " + juce::String(exception.what());
        std::cerr << "[AdbSynth] " << error << std::endl;
        return false;
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
    auto state = parameters.copyState();
    state.setProperty("audioFilePath", getLoadedAudioFile().getFullPathName(), nullptr);
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AdbSynthAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml(getXmlFromBinary(data, sizeInBytes));

    if (xml != nullptr && xml->hasTagName(parameters.state.getType()))
    {
        parameters.replaceState(juce::ValueTree::fromXml(*xml));

        const auto path = parameters.state.getProperty("audioFilePath").toString();
        if (path.isNotEmpty())
            loadAudioFile(juce::File(path));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AdbSynthAudioProcessor();
}