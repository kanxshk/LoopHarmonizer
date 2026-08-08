// Standalone harness for the LoopHarmonizer pitch detector.
//
// Reads a WAV file, runs it through lh::YinPitchDetector, and prints one line
// per analysis frame so detection accuracy can be eyeballed against a real
// guitar or synth loop before any of this touches the plugin.

#include "DSP/NoteUtils.h"
#include "DSP/YinPitchDetector.h"
#include "WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace
{
    struct Options
    {
        std::string inputPath;
        lh::YinPitchDetector::Params detector;
        bool printUnvoiced = false;
        bool csv = false;
        bool selfTest = false;
    };

    void printUsage()
    {
        std::printf (
            "LoopHarmonizer pitch detection harness\n"
            "\n"
            "Usage:\n"
            "  PitchTest <file.wav> [options]\n"
            "  PitchTest --selftest\n"
            "\n"
            "Options:\n"
            "  --frame N         analysis window in samples   (default 2048)\n"
            "  --hop N           frame advance in samples     (default 512)\n"
            "  --fmin HZ         lowest pitch to search for   (default 50)\n"
            "  --fmax HZ         highest pitch to search for  (default 2000)\n"
            "  --threshold T     YIN absolute threshold       (default 0.15)\n"
            "  --min-clarity C   voiced gate, 0..1            (default 0.5)\n"
            "  --min-rms R       silence gate                 (default 0.005)\n"
            "  --all             also print unvoiced frames\n"
            "  --csv             emit CSV instead of a table\n"
            "  --selftest        run synthetic-signal checks and exit\n"
            "  -h, --help        show this message\n");
    }

    /** Returns false if the value is missing or unparseable. */
    bool nextDouble (int argc, char** argv, int& index, double& out)
    {
        if (index + 1 >= argc)
            return false;

        char* end = nullptr;
        const double value = std::strtod (argv[index + 1], &end);

        if (end == argv[index + 1])
            return false;

        out = value;
        ++index;
        return true;
    }

    bool nextInt (int argc, char** argv, int& index, int& out)
    {
        double value = 0.0;

        if (! nextDouble (argc, argv, index, value))
            return false;

        out = static_cast<int> (value);
        return true;
    }

    bool parseOptions (int argc, char** argv, Options& options)
    {
        for (int i = 1; i < argc; ++i)
        {
            const char* arg = argv[i];
            bool ok = true;

            if (std::strcmp (arg, "--frame") == 0)              ok = nextInt (argc, argv, i, options.detector.frameSize);
            else if (std::strcmp (arg, "--hop") == 0)           ok = nextInt (argc, argv, i, options.detector.hopSize);
            else if (std::strcmp (arg, "--fmin") == 0)          ok = nextDouble (argc, argv, i, options.detector.minFrequencyHz);
            else if (std::strcmp (arg, "--fmax") == 0)          ok = nextDouble (argc, argv, i, options.detector.maxFrequencyHz);
            else if (std::strcmp (arg, "--threshold") == 0)     ok = nextDouble (argc, argv, i, options.detector.threshold);
            else if (std::strcmp (arg, "--min-clarity") == 0)   ok = nextDouble (argc, argv, i, options.detector.minClarity);
            else if (std::strcmp (arg, "--min-rms") == 0)       ok = nextDouble (argc, argv, i, options.detector.minRms);
            else if (std::strcmp (arg, "--all") == 0)           options.printUnvoiced = true;
            else if (std::strcmp (arg, "--csv") == 0)           options.csv = true;
            else if (std::strcmp (arg, "--selftest") == 0)      options.selfTest = true;
            else if (std::strcmp (arg, "-h") == 0
                     || std::strcmp (arg, "--help") == 0)       { printUsage(); return false; }
            else if (arg[0] == '-')
            {
                std::fprintf (stderr, "error: unknown option '%s'\n", arg);
                return false;
            }
            else
            {
                options.inputPath = arg;
            }

            if (! ok)
            {
                std::fprintf (stderr, "error: option '%s' needs a numeric value\n", arg);
                return false;
            }
        }

        if (options.detector.frameSize % 2 != 0)
        {
            std::fprintf (stderr, "error: --frame must be even\n");
            return false;
        }

        return true;
    }

    //==============================================================================
    double median (std::vector<double> values)
    {
        if (values.empty())
            return 0.0;

        const std::size_t middle = values.size() / 2;
        std::nth_element (values.begin(), values.begin() + static_cast<std::ptrdiff_t> (middle), values.end());
        return values[middle];
    }

    void reportEffectiveRange (const lh::YinPitchDetector& detector,
                               const lh::YinPitchDetector::Params& requested)
    {
        if (detector.isConfigurationValid())
            return;

        std::fprintf (stderr,
                      "warning: frame size %d cannot reach %.1f Hz at this sample rate; "
                      "the search band is really %.1f - %.1f Hz. Increase --frame to go lower.\n",
                      requested.frameSize,
                      requested.minFrequencyHz,
                      detector.effectiveMinFrequencyHz(),
                      detector.effectiveMaxFrequencyHz());
    }

    //==============================================================================
    void printSummary (const std::vector<lh::PitchFrame>& frames)
    {
        std::size_t voicedCount = 0;
        std::vector<double> voicedClarity;
        int pitchClassCounts[12] = {};

        for (const auto& frame : frames)
        {
            if (! frame.voiced)
                continue;

            ++voicedCount;
            voicedClarity.push_back (frame.clarity);

            const int note = frame.nearestMidiNote();

            if (note >= 0)
                ++pitchClassCounts[note % 12];
        }

        const double voicedPercent = frames.empty()
            ? 0.0
            : 100.0 * static_cast<double> (voicedCount) / static_cast<double> (frames.size());

        std::printf ("\n");
        std::printf ("frames analysed : %llu\n", static_cast<unsigned long long> (frames.size()));
        std::printf ("voiced frames   : %llu (%.1f%%)\n",
                     static_cast<unsigned long long> (voicedCount), voicedPercent);

        if (voicedCount == 0)
        {
            std::printf ("\nNo voiced frames. If the loop is definitely pitched, try lowering\n"
                         "--min-clarity or --min-rms, or widening --fmin / --fmax.\n");
            return;
        }

        std::printf ("median clarity  : %.3f\n", median (voicedClarity));

        // Pitch-class distribution is a preview of what phase 3 key detection
        // will consume, and a quick sanity check that the detector is tracking
        // the actual notes of the loop.
        std::printf ("\npitch class distribution (voiced frames):\n");

        std::vector<std::pair<int, int>> ranked;

        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            if (pitchClassCounts[pitchClass] > 0)
                ranked.emplace_back (pitchClassCounts[pitchClass], pitchClass);

        std::sort (ranked.begin(), ranked.end(), std::greater<>());

        const int topCount = ranked.front().first;

        for (const auto& [count, pitchClass] : ranked)
        {
            const int barLength = std::max (1, (count * 40) / std::max (topCount, 1));
            std::printf ("  %-3s %5d  %s\n",
                         lh::NoteUtils::pitchClassName (pitchClass),
                         count,
                         std::string (static_cast<std::size_t> (barLength), '#').c_str());
        }
    }

    //==============================================================================
    int runFile (const Options& options)
    {
        lh::WavFile wav;
        std::string error;

        if (! lh::WavFile::load (options.inputPath, wav, error))
        {
            std::fprintf (stderr, "error: %s\n", error.c_str());
            return 1;
        }

        std::printf ("file       : %s\n", options.inputPath.c_str());
        std::printf ("format     : %s, %d-bit, %d ch, %.0f Hz\n",
                     wav.formatName.c_str(), wav.bitsPerSample, wav.numChannels, wav.sampleRate);
        std::printf ("duration   : %.3f s (%llu frames)\n",
                     wav.durationSeconds(), static_cast<unsigned long long> (wav.numFrames()));
        std::printf ("detector   : frame %d, hop %d, threshold %.2f, band %.0f-%.0f Hz\n",
                     options.detector.frameSize, options.detector.hopSize,
                     options.detector.threshold,
                     options.detector.minFrequencyHz, options.detector.maxFrequencyHz);

        if (wav.numFrames() < static_cast<std::size_t> (options.detector.frameSize))
        {
            std::fprintf (stderr, "error: file is shorter than one analysis frame\n");
            return 1;
        }

        lh::YinPitchDetector detector (options.detector);
        const auto frames = detector.process (wav.mono.data(), wav.numFrames(), wav.sampleRate);

        reportEffectiveRange (detector, options.detector);

        if (options.csv)
            std::printf ("time_s,frequency_hz,note,cents,clarity,rms,voiced\n");
        else
            std::printf ("\n    time        Hz   note    cents  clarity      rms\n"
                         "  ------  --------  -----  -------  -------  -------\n");

        for (const auto& frame : frames)
        {
            if (! frame.voiced && ! options.printUnvoiced)
                continue;

            const int note = frame.voiced ? lh::NoteUtils::nearestMidiNote (frame.frequencyHz) : -1;
            const std::string noteName = frame.voiced ? lh::NoteUtils::midiNoteName (note) : "--";
            const double cents = frame.voiced ? lh::NoteUtils::centsFromNearestNote (frame.frequencyHz) : 0.0;

            if (options.csv)
            {
                std::printf ("%.4f,%.3f,%s,%.1f,%.4f,%.6f,%d\n",
                             frame.timeSeconds, frame.frequencyHz, noteName.c_str(),
                             cents, frame.clarity, frame.rms, frame.voiced ? 1 : 0);
            }
            else
            {
                std::printf ("  %6.3f  %8.2f  %-5s  %+7.1f  %7.3f  %7.4f%s\n",
                             frame.timeSeconds, frame.frequencyHz, noteName.c_str(),
                             cents, frame.clarity, frame.rms,
                             frame.voiced ? "" : "   (unvoiced)");
            }
        }

        if (! options.csv)
            printSummary (frames);

        return 0;
    }

    //==============================================================================
    // Synthetic checks, so the algorithm can be validated without needing an
    // audio file to hand.

    std::vector<float> makeTone (double frequencyHz, double sampleRate, double seconds, int numHarmonics)
    {
        const std::size_t total = static_cast<std::size_t> (sampleRate * seconds);
        std::vector<float> samples (total, 0.0f);

        for (std::size_t i = 0; i < total; ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;
            double value = 0.0;

            // A falling 1/n harmonic series approximates a sawtooth, which is
            // where naive autocorrelation tends to drop an octave.
            for (int harmonic = 1; harmonic <= numHarmonics; ++harmonic)
            {
                const double partial = frequencyHz * harmonic;

                if (partial >= sampleRate * 0.5)
                    break;

                value += std::sin (6.283185307179586 * partial * t) / harmonic;
            }

            samples[i] = static_cast<float> (0.5 * value);
        }

        return samples;
    }

    bool checkTone (const char* label, double expectedHz, int numHarmonics, double sampleRate)
    {
        const auto samples = makeTone (expectedHz, sampleRate, 0.75, numHarmonics);

        lh::YinPitchDetector detector;
        const auto frames = detector.process (samples.data(), samples.size(), sampleRate);

        std::vector<double> detected;

        for (const auto& frame : frames)
            if (frame.voiced)
                detected.push_back (frame.frequencyHz);

        const double voicedPercent = frames.empty()
            ? 0.0
            : 100.0 * static_cast<double> (detected.size()) / static_cast<double> (frames.size());

        if (detected.empty())
        {
            std::printf ("  FAIL  %-22s expected %7.2f Hz, no voiced frames\n", label, expectedHz);
            return false;
        }

        const double medianHz = median (detected);
        const double centsError = 1200.0 * std::log2 (medianHz / expectedHz);
        const bool passed = std::abs (centsError) <= 10.0 && voicedPercent >= 90.0;

        std::printf ("  %s  %-22s expected %7.2f  got %7.2f  (%+5.1f cents, %.0f%% voiced)  %s\n",
                     passed ? "ok  " : "FAIL",
                     label, expectedHz, medianHz, centsError, voicedPercent,
                     lh::NoteUtils::midiNoteName (lh::NoteUtils::nearestMidiNote (medianHz)).c_str());

        return passed;
    }

    bool checkSilence()
    {
        const double sampleRate = 44100.0;
        std::vector<float> samples (static_cast<std::size_t> (sampleRate * 0.5), 0.0f);

        lh::YinPitchDetector detector;
        const auto frames = detector.process (samples.data(), samples.size(), sampleRate);

        std::size_t voiced = 0;

        for (const auto& frame : frames)
            if (frame.voiced)
                ++voiced;

        const bool passed = voiced == 0;
        std::printf ("  %s  %-22s %llu voiced frames (want 0)\n",
                     passed ? "ok  " : "FAIL", "silence rejected",
                     static_cast<unsigned long long> (voiced));

        return passed;
    }

    int runSelfTest()
    {
        std::printf ("YIN pitch detector self-test (defaults: frame 2048, hop 512, threshold 0.15)\n\n");

        bool allPassed = true;

        std::printf ("pure sine:\n");
        allPassed &= checkTone ("E2  low guitar E", 82.41, 1, 44100.0);
        allPassed &= checkTone ("A2", 110.00, 1, 44100.0);
        allPassed &= checkTone ("E3", 164.81, 1, 44100.0);
        allPassed &= checkTone ("A4  concert pitch", 440.00, 1, 44100.0);
        allPassed &= checkTone ("A5", 880.00, 1, 44100.0);

        std::printf ("\nharmonic-rich (sawtooth-like, the octave-error trap):\n");
        allPassed &= checkTone ("E2  16 harmonics", 82.41, 16, 44100.0);
        allPassed &= checkTone ("A2  16 harmonics", 110.00, 16, 44100.0);
        allPassed &= checkTone ("D3  16 harmonics", 146.83, 16, 44100.0);
        allPassed &= checkTone ("A3  16 harmonics", 220.00, 16, 44100.0);

        std::printf ("\nother sample rates:\n");
        allPassed &= checkTone ("A2  at 48 kHz", 110.00, 16, 48000.0);
        allPassed &= checkTone ("A2  at 96 kHz", 110.00, 16, 96000.0);

        std::printf ("\ngating:\n");
        allPassed &= checkSilence();

        std::printf ("\n%s\n", allPassed ? "all checks passed" : "SOME CHECKS FAILED");
        return allPassed ? 0 : 1;
    }
}

//==============================================================================
int main (int argc, char** argv)
{
    Options options;

    if (! parseOptions (argc, argv, options))
        return 1;

    if (options.selfTest)
        return runSelfTest();

    if (options.inputPath.empty())
    {
        printUsage();
        return 1;
    }

    return runFile (options);
}
