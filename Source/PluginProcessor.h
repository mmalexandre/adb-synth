#pragma once

#include <JuceHeader.h>

#include "SynthEngine.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class AdbSynthAudioProcessor : public juce::AudioProcessor
{
public:
    AdbSynthAudioProcessor();
    ~AdbSynthAudioProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;
    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    bool loadAudioFile(const juce::File& file);
    juce::File getLoadedAudioFile() const;
    std::vector<float> getWaveform() const;
    void triggerFilePlayback();
    void setSynthHeld(bool held);
    bool runModelGuess(const juce::File& file, juce::String& result, juce::String& error) const;

    juce::AudioProcessorValueTreeState parameters;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    adbsynth::SynthEngine engine;
    juce::AudioFormatManager formatManager;
    std::shared_ptr<const juce::AudioBuffer<float>> loadedAudio;
    std::atomic<int> playbackPosition { 0 };
    std::atomic<bool> filePlaybackActive { false };
    std::atomic<bool> synthHeld { false };
    mutable std::mutex audioStateMutex;
    juce::File loadedAudioFile;
    std::vector<float> waveform;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AdbSynthAudioProcessor)
};