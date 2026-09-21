#pragma once

#include "ParameterSchema.h"

namespace adbsynth
{

struct SynthParams
{
    float frequency = 440.0f;
    float frequency2 = 440.0f;
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
};

} // namespace adbsynth
