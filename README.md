# LoopHarmonizer

A JUCE audio plugin that listens to a monophonic guitar or synth loop and generates
MIDI to help a producer layer complementary parts — no music theory required.

It is an **audio-in / MIDI-out generator**: not an instrument (there is no internal
synth engine) and not a conventional audio effect (it does not transform the input).
The producer routes its MIDI output to whichever pad, bass or lead instrument they like.

## Planned pipeline

1. **Audio input** — a monophonic guitar/synth loop, live or loaded from file
2. **Pitch detection** — track melodic pitch over time (YIN or similar)
3. **Key/scale detection** — infer the most likely key from the detected pitch collection
4. **Root note tracking** — identify the dominant root per bar or phrase
5. **MIDI generation**
   - Chord/pad MIDI — diatonic chords per bar supporting the melody
   - Bassline MIDI — root-driven with basic rhythmic variation
   - Counter-melody MIDI *(future)*
6. **Output** — MIDI only, routed to the producer's own instruments

## Status

**Phase 1 — scaffold.** ✅ The plugin builds, loads and passes audio through untouched.

**Phase 2 — pitch detection.** ✅ YIN implemented in `LoopHarmonizerDSP`, a static library
with no JUCE dependency, validated by a standalone console harness. Not yet wired into
`processBlock`.

Later phases add key/scale detection, root tracking and the MIDI generators — each built
as a standalone module and tested before integration.

## Project layout

| Path | What it is |
|---|---|
| `Source/DSP/` | Analysis DSP. **No JUCE dependency** — builds as `LoopHarmonizerDSP`. |
| `Source/Plugin*.{h,cpp}` | JUCE plugin wrapper. |
| `Tools/PitchTest/` | Console harness for validating pitch detection against real loops. |

Keeping the DSP JUCE-free means each algorithm can be tested from a plain console program
that compiles in seconds, rather than through a plugin host.

## Pitch detection harness

```bash
cmake --build build --config Release --target PitchTest
```

Verify the algorithm against synthetic signals — pure sines, harmonic-rich tones that
provoke octave errors, several sample rates, and a silence-rejection check:

```bash
build\Release\PitchTest.exe --selftest
```

Analyse one of your own loops:

```bash
build\Release\PitchTest.exe myloop.wav
```

Each voiced frame prints as timestamp, frequency, nearest note, cents deviation, clarity
and RMS, followed by a pitch-class histogram — a preview of what Phase 3 key detection
will consume.

Useful flags: `--all` includes unvoiced frames, `--csv` emits CSV for plotting, and
`--frame` / `--hop` / `--fmin` / `--fmax` / `--threshold` / `--min-clarity` / `--min-rms`
tune the detector. Run with `--help` for the full list.

Reads PCM (8/16/24/32-bit) and IEEE float (32/64-bit) WAV, mono or multichannel
(downmixed). Re-export compressed files as PCM first.

### Tuning notes

`--frame` sets the floor on detectable pitch: the analysis window must hold two full
periods, so 2048 samples reaches down to about 43 Hz at 44.1 kHz. Going below that needs a
larger frame, which costs time resolution. The harness warns when the requested `--fmin`
is unreachable rather than silently narrowing the search.

Frames straddling a note change produce brief low-clarity readings, sometimes an octave
out. That is expected — it is what the `clarity` value exists to filter. Note segmentation
in a later phase will smooth these away.

## Requirements

- Visual Studio 2026 (MSVC v14.51) with the Desktop C++ workload
- CMake 3.22+ (the copy bundled with Visual Studio works)
- Git

## Building

Clone with submodules, or initialise them after the fact:

```bash
git submodule update --init --recursive
```

Configure and build:

```bash
cmake -B build -A x64
cmake --build build --config Release --parallel
```

Build output lands in:

- VST3 — `build/LoopHarmonizer_artefacts/Release/VST3/LoopHarmonizer.vst3`
- Standalone — `build/LoopHarmonizer_artefacts/Release/Standalone/LoopHarmonizer.exe`

To have CMake install the VST3 into the system plugin folder after each build, configure
with `-DLOOPHARMONIZER_COPY_PLUGIN=ON` and build from an elevated shell (the system VST3
directory lives under `Program Files`). Otherwise copy the `.vst3` bundle to
`C:\Program Files\Common Files\VST3\` yourself, or point your DAW at the build folder.

## Host compatibility note

Routing MIDI *out* of an audio-effect plugin is host-dependent. REAPER, Cubase, Studio One
and FL Studio support it; Ableton Live does not route MIDI from plugin outputs. Worth
confirming your target hosts early, since the whole product concept depends on it.

## Dependencies

[JUCE](https://github.com/juce-framework/JUCE) 8.0.15, pinned as a git submodule at `./JUCE`.
Note that JUCE has its own licensing terms — check which tier applies before distributing.
