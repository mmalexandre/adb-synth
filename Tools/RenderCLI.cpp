// Offline renderer used to build training datasets. It shares SynthEngine with the plugin so the
// dataset can never drift from the audio the plugin actually produces.
//
//   AdbSynthRender --dump-schema [--out schema.json]
//   AdbSynthRender --manifest patches.jsonl --outdir data/
//
// Each manifest line is a flat JSON object holding schema parameters plus the render-only
// nuisance fields: id, phase, duration, sample_rate, channels.

#include "SynthEngine.h"

#include <cctype>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{

std::map<std::string, std::string> parseFlatJsonObject(const std::string& line)
{
    std::map<std::string, std::string> fields;
    std::size_t position = 0;

    const auto skipSpace = [&] {
        while (position < line.size() && std::isspace(static_cast<unsigned char>(line[position])))
            ++position;
    };

    const auto readString = [&]() -> std::string {
        std::string value;
        ++position; // opening quote

        while (position < line.size() && line[position] != '"')
        {
            if (line[position] == '\\' && position + 1 < line.size())
                ++position;

            value += line[position++];
        }

        ++position; // closing quote
        return value;
    };

    skipSpace();
    if (position >= line.size() || line[position] != '{')
        return fields;

    ++position;

    while (position < line.size())
    {
        skipSpace();

        if (position < line.size() && line[position] == '}')
            break;

        if (position >= line.size() || line[position] != '"')
            break;

        const auto key = readString();
        skipSpace();

        if (position < line.size() && line[position] == ':')
            ++position;

        skipSpace();

        std::string value;

        if (position < line.size() && line[position] == '"')
        {
            value = readString();
        }
        else
        {
            while (position < line.size() && line[position] != ',' && line[position] != '}')
                value += line[position++];
        }

        fields[key] = value;
        skipSpace();

        if (position < line.size() && line[position] == ',')
            ++position;
    }

    return fields;
}

double fieldOr(const std::map<std::string, std::string>& fields, const std::string& key, double fallback)
{
    const auto entry = fields.find(key);

    if (entry == fields.end() || entry->second.empty())
        return fallback;

    try
    {
        return std::stod(entry->second);
    }
    catch (const std::exception&)
    {
        return fallback;
    }
}

void writeLittleEndian(std::ostream& stream, std::uint32_t value, int byteCount)
{
    for (int byte = 0; byte < byteCount; ++byte)
        stream.put(static_cast<char>((value >> (8 * byte)) & 0xff));
}

// 32-bit IEEE float WAV keeps the dataset lossless and is read directly by soundfile/torchaudio.
bool writeFloatWav(const std::string& path, const std::vector<float>& interleaved, int numChannels, int sampleRate)
{
    std::ofstream stream(path, std::ios::binary);

    if (! stream)
        return false;

    const auto dataBytes = static_cast<std::uint32_t>(interleaved.size() * sizeof(float));
    const auto byteRate = static_cast<std::uint32_t>(sampleRate * numChannels * sizeof(float));
    const auto blockAlign = static_cast<std::uint32_t>(numChannels * sizeof(float));

    stream.write("RIFF", 4);
    writeLittleEndian(stream, 36 + dataBytes, 4);
    stream.write("WAVEfmt ", 8);
    writeLittleEndian(stream, 16, 4);
    writeLittleEndian(stream, 3, 2); // IEEE float
    writeLittleEndian(stream, static_cast<std::uint32_t>(numChannels), 2);
    writeLittleEndian(stream, static_cast<std::uint32_t>(sampleRate), 4);
    writeLittleEndian(stream, byteRate, 4);
    writeLittleEndian(stream, blockAlign, 2);
    writeLittleEndian(stream, 32, 2);
    stream.write("data", 4);
    writeLittleEndian(stream, dataBytes, 4);
    stream.write(reinterpret_cast<const char*>(interleaved.data()), static_cast<std::streamsize>(dataBytes));

    return stream.good();
}

std::string argumentValue(int argc, char** argv, const std::string& name, const std::string& fallback = {})
{
    for (int index = 1; index + 1 < argc; ++index)
        if (name == argv[index])
            return argv[index + 1];

    return fallback;
}

bool hasFlag(int argc, char** argv, const std::string& name)
{
    for (int index = 1; index < argc; ++index)
        if (name == argv[index])
            return true;

    return false;
}

int renderManifest(const std::string& manifestPath, const std::string& outputDirectory)
{
    std::ifstream manifest(manifestPath);

    if (! manifest)
    {
        std::cerr << "Cannot open manifest: " << manifestPath << '\n';
        return 1;
    }

    std::ofstream labels(outputDirectory + "/labels.jsonl");

    if (! labels)
    {
        std::cerr << "Cannot write labels into: " << outputDirectory << '\n';
        return 1;
    }

    adbsynth::SynthEngine engine;
    std::string line;
    int rendered = 0;

    labels << std::setprecision(10);

    while (std::getline(manifest, line))
    {
        if (line.find_first_not_of(" \t\r\n") == std::string::npos)
            continue;

        const auto fields = parseFlatJsonObject(line);
        const auto identifier = fields.count("id") ? fields.at("id") : std::to_string(rendered);
        const auto sampleRate = static_cast<int>(fieldOr(fields, "sample_rate", 44100.0));
        const auto duration = fieldOr(fields, "duration", 0.5);
        const auto channels = static_cast<int>(fieldOr(fields, "channels", 1.0));
        const auto phase = fieldOr(fields, "phase", 0.0);
        const auto numSamples = static_cast<int>(duration * sampleRate);
        const auto noteDuration = duration * 0.75;
        const auto noteSamples = static_cast<int>(noteDuration * sampleRate);
        const auto releaseSamples = numSamples - noteSamples;

        std::map<std::string, double> values;

        for (const auto& descriptor : adbsynth::parameterSchema)
            values[descriptor.id] = fieldOr(fields, descriptor.id, descriptor.defaultValue);

        adbsynth::SynthParams params;
        params.frequency = static_cast<float>(values["frequency"]);
        params.attack = static_cast<float>(values["attack"]);
        params.decay = static_cast<float>(values["decay"]);
        params.sustain = static_cast<float>(values["sustain"]);
        params.release = static_cast<float>(values["release"]);
        params.frequency2 = static_cast<float>(values["frequency2"]);
        params.attack2 = static_cast<float>(values["attack2"]);
        params.decay2 = static_cast<float>(values["decay2"]);
        params.sustain2 = static_cast<float>(values["sustain2"]);
        params.release2 = static_cast<float>(values["release2"]);

        std::vector<std::vector<float>> channelData(static_cast<std::size_t>(channels),
                                                    std::vector<float>(static_cast<std::size_t>(numSamples), 0.0f));
        std::vector<float*> channelPointers;

        for (auto& channel : channelData)
            channelPointers.push_back(channel.data());

        engine.prepare(static_cast<double>(sampleRate));
        engine.reset(phase);
        params.gate = true;
        engine.render(channelPointers.data(), channels, noteSamples, params);

        params.gate = false;
        std::vector<std::vector<float>> releaseData(static_cast<std::size_t>(channels),
                                                    std::vector<float>(static_cast<std::size_t>(releaseSamples), 0.0f));
        std::vector<float*> releasePointers;

        for (auto& channel : releaseData)
            releasePointers.push_back(channel.data());

        engine.render(releasePointers.data(), channels, releaseSamples, params);

        for (int sample = 0; sample < releaseSamples; ++sample)
            for (int channel = 0; channel < channels; ++channel)
                channelData[static_cast<std::size_t>(channel)][static_cast<std::size_t>(noteSamples + sample)] =
                    releaseData[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)];

        std::vector<float> interleaved(static_cast<std::size_t>(numSamples) * static_cast<std::size_t>(channels));

        for (int sample = 0; sample < numSamples; ++sample)
            for (int channel = 0; channel < channels; ++channel)
                interleaved[static_cast<std::size_t>(sample * channels + channel)] = channelData[static_cast<std::size_t>(channel)][static_cast<std::size_t>(sample)];

        const auto relativePath = identifier + ".wav";

        if (! writeFloatWav(outputDirectory + "/" + relativePath, interleaved, channels, sampleRate))
        {
            std::cerr << "Failed to write " << relativePath << '\n';
            return 1;
        }

        labels << "{\"id\": \"" << identifier << "\", \"file\": \"" << relativePath
               << "\", \"sample_rate\": " << sampleRate
               << ", \"duration\": " << duration
               << ", \"phase\": " << phase;

        for (const auto& descriptor : adbsynth::parameterSchema)
            labels << ", \"" << descriptor.id << "\": " << values[descriptor.id];

        labels << "}\n";

        ++rendered;
    }

    std::cerr << "Rendered " << rendered << " clips into " << outputDirectory << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (hasFlag(argc, argv, "--dump-schema"))
    {
        const auto json = adbsynth::schemaToJson();
        const auto outputPath = argumentValue(argc, argv, "--out");

        if (outputPath.empty())
        {
            std::cout << json;
            return 0;
        }

        std::ofstream stream(outputPath);

        if (! stream)
        {
            std::cerr << "Cannot write schema to " << outputPath << '\n';
            return 1;
        }

        stream << json;
        return 0;
    }

    const auto manifestPath = argumentValue(argc, argv, "--manifest");
    const auto outputDirectory = argumentValue(argc, argv, "--outdir");

    if (manifestPath.empty() || outputDirectory.empty())
    {
        std::cerr << "Usage:\n"
                  << "  AdbSynthRender --dump-schema [--out schema.json]\n"
                  << "  AdbSynthRender --manifest patches.jsonl --outdir DIR\n";
        return 1;
    }

    return renderManifest(manifestPath, outputDirectory);
}
