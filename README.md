# AdbSynth

Minimal JUCE sine oscillator with one automatable parameter: `frequency`.

The standalone and plug-in editor contains one button. Each click tunes the oscillator to the next frequency in this set:

- 220 Hz
- 261.63 Hz
- 440 Hz
- 523.25 Hz
- 880 Hz

## Build

Requirements: CMake 3.22+, a C++17 compiler, and a working audio device for the standalone target.

```sh
cmake -B build -S .
cmake --build build --config Release
```

JUCE is downloaded by CMake into its dependency cache during configuration.