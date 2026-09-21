#include "SynthEngine.h"

#include <algorithm>
#include <cmath>

namespace adbsynth
{

namespace
{
constexpr double twoPi = 6.283185307179586476925286766559;
constexpr float outputGain = 0.15f;

float advanceEnvelope(float value, bool gate, float attack, float decay, float sustain, float release,
                      double sampleRate)
{
    const auto attackSamples = std::max(1.0, static_cast<double>(attack) * sampleRate);
    const auto decaySamples = std::max(1.0, static_cast<double>(decay) * sampleRate);
    const auto releaseSamples = std::max(1.0, static_cast<double>(release) * sampleRate);

    if (gate)
    {
        if (value < 1.0f)
        {
            const auto attackStep = static_cast<float>(1.0 / attackSamples);
            value = std::min(1.0f, value + attackStep);
        }
        else
        {
            const auto decayStep = static_cast<float>((1.0f - sustain) / decaySamples);
            value = std::max(sustain, value - decayStep);
        }
    }
    else
    {
        const auto releaseStep = static_cast<float>(1.0 / releaseSamples);
        value = std::max(0.0f, value - releaseStep);
    }

    return value;
}
}

void SynthEngine::prepare(double sampleRateToUse)
{
    sampleRate = sampleRateToUse > 0.0 ? sampleRateToUse : 44100.0;
    phase = 0.0;
    phase2 = 0.0;
}

void SynthEngine::reset(double initialPhase)
{
    phase = std::fmod(initialPhase, twoPi);

    phase2 = phase;
    previousGate = false;
    envelope = 0.0f;
    envelope2 = 0.0f;

    if (phase < 0.0)
    {
        phase += twoPi;
        phase2 = phase;
    }
}

void SynthEngine::render(float* const* channels, int numChannels, int numSamples, const SynthParams& params)
{
    const auto& descriptor = *findParameter("frequency");
    const auto frequency = std::clamp(static_cast<double>(params.frequency),
                                      static_cast<double>(descriptor.minValue),
                                      static_cast<double>(descriptor.maxValue));
    const auto phaseStep = twoPi * frequency / sampleRate;
    const auto& descriptor2 = *findParameter("frequency2");
    const auto frequency2 = std::clamp(static_cast<double>(params.frequency2),
                                       static_cast<double>(descriptor2.minValue),
                                       static_cast<double>(descriptor2.maxValue));
    const auto phaseStep2 = twoPi * frequency2 / sampleRate;

    if (params.gate && !previousGate)
    {
        envelope = 0.0f;
        envelope2 = 0.0f;
    }

    for (int sample = 0; sample < numSamples; ++sample)
    {
        envelope = advanceEnvelope(envelope, params.gate, params.attack, params.decay,
                                    params.sustain, params.release, sampleRate);
        envelope2 = advanceEnvelope(envelope2, params.gate, params.attack2, params.decay2,
                                     params.sustain2, params.release2, sampleRate);
        const auto value = static_cast<float>(std::sin(phase)) * envelope * 0.5f * outputGain
                         + static_cast<float>(std::sin(phase2)) * envelope2 * 0.5f * outputGain;
        phase = std::fmod(phase + phaseStep, twoPi);
        phase2 = std::fmod(phase2 + phaseStep2, twoPi);

        for (int channel = 0; channel < numChannels; ++channel)
            channels[channel][sample] = value;
    }

    previousGate = params.gate;
}

} // namespace adbsynth
