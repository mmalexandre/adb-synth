#pragma once

// Single source of truth for the synth's automatable parameters. The plugin builds its APVTS
// layout from this list, and the offline renderer exports it as JSON so the training pipeline
// never hardcodes ranges. Adding a parameter should only require a new entry here plus a
// matching field in SynthParams.

#include <array>
#include <cmath>
#include <cstddef>
#include <sstream>
#include <string>

namespace adbsynth
{

enum class ParameterKind
{
    Float,
    Choice,
    Bool
};

struct ParameterDescriptor
{
    const char* id;
    const char* label;
    const char* unit;
    ParameterKind kind;
    float minValue;
    float maxValue;
    bool logScale;
    float defaultValue;
    const char* const* choices;
    int numChoices;
};

inline constexpr std::array<ParameterDescriptor, 2> parameterSchema { {
    { "frequency", "Frequency", "Hz", ParameterKind::Float, 20.0f, 20000.0f, true, 440.0f, nullptr, 0 },
    { "frequency2", "Frequency 2", "Hz", ParameterKind::Float, 20.0f, 20000.0f, true, 440.0f, nullptr, 0 },
} };

inline const ParameterDescriptor* findParameter(const std::string& id)
{
    for (const auto& descriptor : parameterSchema)
        if (id == descriptor.id)
            return &descriptor;

    return nullptr;
}

/** Maps a raw parameter value onto 0..1, logarithmically when the descriptor asks for it. */
inline float normaliseParameter(const ParameterDescriptor& descriptor, float value)
{
    if (descriptor.logScale)
    {
        const auto low = std::log(descriptor.minValue);
        const auto high = std::log(descriptor.maxValue);
        return static_cast<float>((std::log(value) - low) / (high - low));
    }

    return (value - descriptor.minValue) / (descriptor.maxValue - descriptor.minValue);
}

inline float denormaliseParameter(const ParameterDescriptor& descriptor, float normalised)
{
    if (descriptor.logScale)
    {
        const auto low = std::log(descriptor.minValue);
        const auto high = std::log(descriptor.maxValue);
        return static_cast<float>(std::exp(low + normalised * (high - low)));
    }

    return descriptor.minValue + normalised * (descriptor.maxValue - descriptor.minValue);
}

inline const char* parameterKindName(ParameterKind kind)
{
    switch (kind)
    {
        case ParameterKind::Float:  return "float";
        case ParameterKind::Choice: return "choice";
        case ParameterKind::Bool:   return "bool";
    }

    return "float";
}

inline std::string schemaToJson()
{
    std::ostringstream json;
    json << "{\n  \"schema_version\": 2,\n  \"parameters\": [\n";

    for (std::size_t index = 0; index < parameterSchema.size(); ++index)
    {
        const auto& descriptor = parameterSchema[index];

        json << "    {\n";
        json << "      \"id\": \"" << descriptor.id << "\",\n";
        json << "      \"label\": \"" << descriptor.label << "\",\n";
        json << "      \"unit\": \"" << descriptor.unit << "\",\n";
        json << "      \"kind\": \"" << parameterKindName(descriptor.kind) << "\",\n";
        json << "      \"min\": " << descriptor.minValue << ",\n";
        json << "      \"max\": " << descriptor.maxValue << ",\n";
        json << "      \"log_scale\": " << (descriptor.logScale ? "true" : "false") << ",\n";
        json << "      \"default\": " << descriptor.defaultValue << ",\n";
        json << "      \"choices\": [";

        for (int choice = 0; choice < descriptor.numChoices; ++choice)
            json << (choice > 0 ? ", " : "") << '"' << descriptor.choices[choice] << '"';

        json << "]\n    }" << (index + 1 < parameterSchema.size() ? "," : "") << '\n';
    }

    json << "  ]\n}\n";
    return json.str();
}

} // namespace adbsynth
