#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace adbsynth
{

namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;
constexpr float outputGain = 0.15f;
}

void SynthEngine::prepare(double sampleRateToUse)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;
    phase = 0.0;
}

void SynthEngine::reset(double initialPhase)
{
    phase = std::fmod(initialPhase, twoPi);

    if (phase < 0.0)
        phase += twoPi;
}

void SynthEngine::render(float* const* channels, int numChannels, int numSamples, const SynthParams& params)
{
    const auto& descriptor = parameterSchema[0];
    const auto frequency = std::clamp(static_cast<double>(params.frequency),
                                      static_cast<double>(descriptor.minValue),
                                      static_cast<double>(descriptor.maxValue));
    const auto phaseStep = twoPi * frequency / sampleRate;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        const auto value = static_cast<float>(std::sin(phase)) * outputGain;
        phase = std::fmod(phase + phaseStep, twoPi);

        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel][sample] = value;
    }
}

} // namespace adbsynth
