#pragma once

#include <string>
#include <vector>

namespace lh
{
    /**
        Minimal RIFF/WAVE reader for the test harness.

        Deliberately small and dependency-free rather than reaching for JUCE's
        audio format readers - this keeps the harness a plain console program
        that builds in a couple of seconds.

        Supports the formats a DAW actually bounces loops to:
          - PCM integer, 8 / 16 / 24 / 32 bit
          - IEEE float, 32 / 64 bit
          - WAVE_FORMAT_EXTENSIBLE wrapping either of the above

        Assumes a little-endian host, which covers x86 and ARM Windows.
    */
    struct WavFile
    {
        std::vector<float> mono;        ///< Channels averaged down to mono.
        double sampleRate = 0.0;
        int numChannels = 0;
        int bitsPerSample = 0;
        std::string formatName;         ///< "PCM" or "IEEE float", for reporting.

        std::size_t numFrames() const   { return mono.size(); }
        double durationSeconds() const;

        /** Returns false and fills `error` on failure. */
        static bool load (const std::string& path, WavFile& result, std::string& error);
    };
}
