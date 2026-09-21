#pragma once

#include "ParameterSchema.h"

namespace adbsynth
{

struct SynthParams
{
    float frequency = 440.0f;
    float attack = 0.01f;
    float decay = 0.1f;
    float sustain = 0.8f;
    float release = 0.2f;
    float frequency2 = 440.0f;
    float attack2 = 0.01f;
    float decay2 = 0.1f;
    float sustain2 = 0.8f;
    float release2 = 0.2f;
    bool gate = true;
};

/** Deliberately free of JUCE types so the plugin and the offline renderer share the same DSP. */
class SynthEngine
{
public:
    void prepare(double sampleRateToUse);
    void reset(double initialPhase = 0.0);

    void render(float* const* channels, int numChannels, int numSamples, const SynthParams& params);

private:
    double sampleRate = 44100.0;
    double phase = 0.0;
    double phase2 = 0.0;
    bool previousGate = false;
    float envelope = 0.0f;
    float envelope2 = 0.0f;
};

} // namespace adbsynth
