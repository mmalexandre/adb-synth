# AdbSynth

Minimal JUCE sine oscillator with one automatable parameter: `frequency`, continuous over the
audible range (20 Hz - 20 kHz, logarithmic).

The standalone and plug-in editor contains one rotary knob showing the frequency in Hz.

The parameter list lives in [Source/ParameterSchema.h](Source/ParameterSchema.h) and is the single
source of truth: the plug-in builds its parameter layout from it, and `AdbSynthRender` exports it as
JSON for the training pipeline in [ml/](ml/).

## Build

Requirements: CMake 3.22+, a C++17 compiler, and a working audio device for the standalone target.

```sh
cmake -B build -S .
cmake --build build --config Release
```

JUCE is downloaded by CMake into its dependency cache during configuration.

## Offline renderer

`AdbSynthRender` shares its DSP with the plug-in, so datasets always match what the plug-in plays.

```sh
./build/AdbSynthRender --dump-schema
./build/AdbSynthRender --manifest patches.jsonl --outdir data/
```

## Parameter inference

See [ml/README.md](ml/README.md) for generating a dataset and training the model that estimates the
synth parameters from a short audio sample.
