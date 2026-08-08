#include "WavFile.h"

#include <cstdint>
#include <cstring>
#include <fstream>

namespace lh
{
    namespace
    {
        std::uint16_t read16 (const std::uint8_t* p)
        {
            return static_cast<std::uint16_t> (p[0] | (p[1] << 8));
        }

        std::uint32_t read32 (const std::uint8_t* p)
        {
            return static_cast<std::uint32_t> (p[0])
                 | (static_cast<std::uint32_t> (p[1]) << 8)
                 | (static_cast<std::uint32_t> (p[2]) << 16)
                 | (static_cast<std::uint32_t> (p[3]) << 24);
        }

        bool matchesId (const std::uint8_t* p, const char* id)
        {
            return std::memcmp (p, id, 4) == 0;
        }

        constexpr std::uint16_t kFormatPcm         = 0x0001;
        constexpr std::uint16_t kFormatIeeeFloat   = 0x0003;
        constexpr std::uint16_t kFormatExtensible  = 0xFFFE;

        /** Converts one sample to float, given the container format. */
        double decodeSample (const std::uint8_t* p, std::uint16_t format, int bits)
        {
            if (format == kFormatIeeeFloat)
            {
                if (bits == 32)
                {
                    float value = 0.0f;
                    std::memcpy (&value, p, sizeof (value));
                    return static_cast<double> (value);
                }

                double value = 0.0;
                std::memcpy (&value, p, sizeof (value));
                return value;
            }

            switch (bits)
            {
                case 8:
                    // 8-bit WAV is unsigned, centred on 128.
                    return (static_cast<double> (p[0]) - 128.0) / 128.0;

                case 16:
                    return static_cast<double> (static_cast<std::int16_t> (read16 (p))) / 32768.0;

                case 24:
                {
                    // Sign-extend the top byte into a 32-bit container.
                    std::int32_t value = static_cast<std::int32_t> (
                        (static_cast<std::uint32_t> (p[0]) << 8)
                      | (static_cast<std::uint32_t> (p[1]) << 16)
                      | (static_cast<std::uint32_t> (p[2]) << 24));

                    return static_cast<double> (value >> 8) / 8388608.0;
                }

                case 32:
                    return static_cast<double> (static_cast<std::int32_t> (read32 (p))) / 2147483648.0;

                default:
                    return 0.0;
            }
        }
    }

    double WavFile::durationSeconds() const
    {
        return sampleRate > 0.0 ? static_cast<double> (mono.size()) / sampleRate : 0.0;
    }

    bool WavFile::load (const std::string& path, WavFile& result, std::string& error)
    {
        result = WavFile{};

        std::ifstream stream (path, std::ios::binary | std::ios::ate);

        if (! stream)
        {
            error = "could not open file: " + path;
            return false;
        }

        const std::streamsize fileSize = stream.tellg();
        stream.seekg (0, std::ios::beg);

        if (fileSize < 12)
        {
            error = "file is too small to be a WAV";
            return false;
        }

        std::vector<std::uint8_t> bytes (static_cast<std::size_t> (fileSize));

        if (! stream.read (reinterpret_cast<char*> (bytes.data()), fileSize))
        {
            error = "failed to read file contents";
            return false;
        }

        if (! matchesId (bytes.data(), "RIFF") || ! matchesId (bytes.data() + 8, "WAVE"))
        {
            error = "not a RIFF/WAVE file";
            return false;
        }

        std::uint16_t format = 0;
        std::uint16_t channels = 0;
        std::uint32_t rate = 0;
        std::uint16_t bits = 0;
        bool haveFormat = false;

        const std::uint8_t* dataStart = nullptr;
        std::size_t dataSize = 0;

        // Walk the chunk list. Chunks are word-aligned, so an odd-sized chunk is
        // followed by a pad byte that is not counted in its declared size.
        std::size_t offset = 12;

        while (offset + 8 <= bytes.size())
        {
            const std::uint8_t* header = bytes.data() + offset;
            const std::uint32_t chunkSize = read32 (header + 4);
            const std::size_t body = offset + 8;

            if (body + chunkSize > bytes.size())
            {
                // Truncated final chunk: salvage what is actually present rather
                // than rejecting the file, since partial bounces are common.
                if (matchesId (header, "data"))
                {
                    dataStart = bytes.data() + body;
                    dataSize = bytes.size() - body;
                }

                break;
            }

            if (matchesId (header, "fmt ") && chunkSize >= 16)
            {
                const std::uint8_t* p = bytes.data() + body;

                format   = read16 (p);
                channels = read16 (p + 2);
                rate     = read32 (p + 4);
                bits     = read16 (p + 14);

                if (format == kFormatExtensible)
                {
                    if (chunkSize < 40)
                    {
                        error = "WAVE_FORMAT_EXTENSIBLE chunk is too short";
                        return false;
                    }

                    // The real format lives in the first two bytes of the SubFormat GUID.
                    format = read16 (p + 24);
                }

                haveFormat = true;
            }
            else if (matchesId (header, "data"))
            {
                dataStart = bytes.data() + body;
                dataSize = chunkSize;
            }

            offset = body + chunkSize + (chunkSize & 1u);
        }

        if (! haveFormat)
        {
            error = "no 'fmt ' chunk found";
            return false;
        }

        if (dataStart == nullptr)
        {
            error = "no 'data' chunk found";
            return false;
        }

        if (format != kFormatPcm && format != kFormatIeeeFloat)
        {
            error = "unsupported WAV encoding (format tag " + std::to_string (format)
                  + "); only PCM and IEEE float are handled, so re-export "
                    "compressed files as PCM";
            return false;
        }

        if (channels == 0 || rate == 0)
        {
            error = "invalid channel count or sample rate in 'fmt ' chunk";
            return false;
        }

        const bool bitsAreValid = (format == kFormatIeeeFloat)
            ? (bits == 32 || bits == 64)
            : (bits == 8 || bits == 16 || bits == 24 || bits == 32);

        if (! bitsAreValid)
        {
            error = "unsupported bit depth: " + std::to_string (bits);
            return false;
        }

        const std::size_t bytesPerSample = static_cast<std::size_t> (bits) / 8;
        const std::size_t bytesPerFrame = bytesPerSample * channels;
        const std::size_t frameCount = dataSize / bytesPerFrame;

        result.sampleRate = static_cast<double> (rate);
        result.numChannels = static_cast<int> (channels);
        result.bitsPerSample = static_cast<int> (bits);
        result.formatName = (format == kFormatIeeeFloat) ? "IEEE float" : "PCM";
        result.mono.resize (frameCount);

        // Downmix by averaging. For the monophonic loops this tool targets the
        // channels are near-identical anyway, and averaging avoids picking a
        // channel that happens to be silent.
        for (std::size_t frame = 0; frame < frameCount; ++frame)
        {
            const std::uint8_t* framePtr = dataStart + frame * bytesPerFrame;
            double sum = 0.0;

            for (std::size_t channel = 0; channel < channels; ++channel)
                sum += decodeSample (framePtr + channel * bytesPerSample, format, bits);

            result.mono[frame] = static_cast<float> (sum / static_cast<double> (channels));
        }

        return true;
    }
}
