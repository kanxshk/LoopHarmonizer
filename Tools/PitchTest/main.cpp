// Standalone harness for the LoopHarmonizer analysis chain.
//
// Reads a WAV file, runs it through pitch detection -> note segmentation ->
// key detection, and prints each stage so accuracy can be checked against a
// real guitar or synth loop before any of this touches the plugin.

#include "DSP/KeyDetector.h"
#include "DSP/NoteSegmenter.h"
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
        lh::NoteSegmenter::Params segmenter;
        lh::KeyDetector::Params key;

        bool showFrames = false;
        bool printUnvoiced = false;
        bool showNotes = true;
        bool detectKey = true;
        bool csv = false;
        bool selfTest = false;
        int topCandidates = 5;
    };

    void printUsage()
    {
        std::printf (
            "LoopHarmonizer analysis harness\n"
            "\n"
            "Usage:\n"
            "  PitchTest <file.wav> [options]\n"
            "  PitchTest --selftest\n"
            "\n"
            "Pitch detection:\n"
            "  --frame N         analysis window in samples   (default 2048)\n"
            "  --hop N           frame advance in samples     (default 512)\n"
            "  --fmin HZ         lowest pitch to search for   (default 50)\n"
            "  --fmax HZ         highest pitch to search for  (default 2000)\n"
            "  --threshold T     YIN absolute threshold       (default 0.15)\n"
            "  --min-clarity C   voiced gate, 0..1            (default 0.5)\n"
            "  --min-rms R       silence gate                 (default 0.005)\n"
            "\n"
            "Note segmentation:\n"
            "  --seg-clarity C   clarity floor for notes      (default 0.80)\n"
            "  --seg-duration S  shortest note kept, seconds  (default 0.05)\n"
            "  --seg-gap N       frames of dropout bridged    (default 2)\n"
            "\n"
            "Key detection:\n"
            "  --no-key          skip key detection\n"
            "  --no-modes        major and natural minor only\n"
            "  --harmonic-minor  also consider harmonic minor\n"
            "  --top N           key candidates to list       (default 5)\n"
            "\n"
            "Output:\n"
            "  --frames          print the per-frame pitch table\n"
            "  --all             with --frames, include unvoiced frames\n"
            "  --no-notes        omit the note event list\n"
            "  --csv             emit per-frame CSV instead of a report\n"
            "  --selftest        run synthetic checks and exit\n"
            "  -h, --help        show this message\n");
    }

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
            else if (std::strcmp (arg, "--seg-clarity") == 0)   ok = nextDouble (argc, argv, i, options.segmenter.minClarity);
            else if (std::strcmp (arg, "--seg-duration") == 0)  ok = nextDouble (argc, argv, i, options.segmenter.minDurationSeconds);
            else if (std::strcmp (arg, "--seg-gap") == 0)       ok = nextInt (argc, argv, i, options.segmenter.maxGapFrames);
            else if (std::strcmp (arg, "--top") == 0)           ok = nextInt (argc, argv, i, options.topCandidates);
            else if (std::strcmp (arg, "--no-key") == 0)        options.detectKey = false;
            else if (std::strcmp (arg, "--no-modes") == 0)      options.key.includeModes = false;
            else if (std::strcmp (arg, "--harmonic-minor") == 0) options.key.includeHarmonicMinor = true;
            else if (std::strcmp (arg, "--frames") == 0)        options.showFrames = true;
            else if (std::strcmp (arg, "--all") == 0)           { options.showFrames = true; options.printUnvoiced = true; }
            else if (std::strcmp (arg, "--no-notes") == 0)      options.showNotes = false;
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

    std::string bar (double fraction, int width)
    {
        const int length = std::clamp (static_cast<int> (std::lround (fraction * width)), 0, width);
        return std::string (static_cast<std::size_t> (length), '#');
    }

    //==============================================================================
    void printFrameTable (const std::vector<lh::PitchFrame>& frames, const Options& options)
    {
        std::printf ("\n    time        Hz   note    cents  clarity      rms\n"
                     "  ------  --------  -----  -------  -------  -------\n");

        for (const auto& frame : frames)
        {
            if (! frame.voiced && ! options.printUnvoiced)
                continue;

            const int note = frame.voiced ? lh::NoteUtils::nearestMidiNote (frame.frequencyHz) : -1;
            const std::string noteName = frame.voiced ? lh::NoteUtils::midiNoteName (note) : "--";
            const double cents = frame.voiced ? lh::NoteUtils::centsFromNearestNote (frame.frequencyHz) : 0.0;

            std::printf ("  %6.3f  %8.2f  %-5s  %+7.1f  %7.3f  %7.4f%s\n",
                         frame.timeSeconds, frame.frequencyHz, noteName.c_str(),
                         cents, frame.clarity, frame.rms,
                         frame.voiced ? "" : "   (unvoiced)");
        }
    }

    void printNoteEvents (const std::vector<lh::NoteEvent>& notes, const lh::NoteSegmenter::Params& params)
    {
        std::printf ("\nnote events (min %.3f s, min clarity %.2f):\n",
                     params.minDurationSeconds, params.minClarity);

        if (notes.empty())
        {
            std::printf ("  none - no run of frames was stable enough to count as a note.\n");
            return;
        }

        std::printf ("    #   start      dur  note        Hz    cents  clarity\n"
                     "  ---  ------  -------  ----  --------  -------  -------\n");

        double totalPitched = 0.0;
        int index = 0;

        for (const auto& note : notes)
        {
            ++index;
            totalPitched += note.durationSeconds;

            std::printf ("  %3d  %6.3f  %7.3f  %-4s  %8.2f  %+7.1f  %7.3f\n",
                         index,
                         note.startSeconds,
                         note.durationSeconds,
                         lh::NoteUtils::midiNoteName (note.midiNote).c_str(),
                         note.medianFrequencyHz,
                         lh::NoteUtils::centsFromNearestNote (note.medianFrequencyHz),
                         note.meanClarity);
        }

        std::printf ("  %llu notes, %.2f s of pitched material\n",
                     static_cast<unsigned long long> (notes.size()), totalPitched);
    }

    void printHistogram (const lh::KeyDetectionResult& key)
    {
        std::printf ("\nweighted pitch-class histogram (duration x clarity):\n");

        double peak = 0.0;

        for (double weight : key.histogram)
            peak = std::max (peak, weight);

        if (peak <= 0.0)
            return;

        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
        {
            const double weight = key.histogram[static_cast<std::size_t> (pitchClass)];

            if (weight <= 0.0)
                continue;

            const bool inKey = key.valid
                && (key.best.pitchClassMask() & (1u << pitchClass)) != 0u;

            std::printf ("  %-3s %5.1f%%  %-40s %s\n",
                         lh::NoteUtils::pitchClassName (pitchClass),
                         weight * 100.0,
                         bar (weight / peak, 40).c_str(),
                         inKey ? "" : "(outside key)");
        }
    }

    void printKeyResult (const lh::KeyDetectionResult& key, int topCandidates)
    {
        std::printf ("\nkey estimate:\n");

        if (! key.valid)
        {
            std::printf ("  no pitched material survived the gates - nothing to analyse.\n");
            return;
        }

        // Root and scale are reported separately because they fail separately -
        // a bassline can pin the root down while saying nothing about the mode.
        std::printf ("  overall    : %-18s r = %.3f\n",
                     key.best.name().c_str(), key.best.correlation);
        std::printf ("  tonic      : %-18s confidence %.2f   (next best %s, margin %.3f)\n",
                     lh::NoteUtils::pitchClassName (key.best.tonicPitchClass),
                     key.tonic.value,
                     key.tonic.runnerUp.isValid()
                         ? lh::NoteUtils::pitchClassName (key.tonic.runnerUp.tonicPitchClass) : "-",
                     key.tonic.margin);
        std::printf ("  mode       : %-18s confidence %.2f   (next best %s, margin %.3f)\n",
                     scaleTypeName (key.best.scale),
                     key.mode.value,
                     key.mode.runnerUp.isValid()
                         ? scaleTypeName (key.mode.runnerUp.scale) : "-",
                     key.mode.margin);
        std::printf ("  based on   : %d events, %d distinct pitch classes\n",
                     key.eventsUsed, key.distinctPitchClasses);

        if (key.thirdAbsent)
        {
            const int minorThird = (key.best.tonicPitchClass + 3) % 12;
            const int majorThird = (key.best.tonicPitchClass + 4) % 12;

            std::printf ("\n  No third above %s: neither %s nor %s occurs anywhere in this loop.\n"
                         "  That is the note separating major from minor, so the mode cannot be\n"
                         "  determined from this material at all - the mode confidence above is\n"
                         "  reporting a guess. Root tracking and root-driven basslines are\n"
                         "  unaffected; diatonic chord generation would be inventing the third.\n"
                         "  Basslines do this routinely, stating roots and fifths and leaving the\n"
                         "  third to other instruments.\n",
                         lh::NoteUtils::pitchClassName (key.best.tonicPitchClass),
                         lh::NoteUtils::pitchClassName (minorThird),
                         lh::NoteUtils::pitchClassName (majorThird));
        }

        // Explain *why* the top two are close, when they are.
        if (key.runnerUpUndecidable)
        {
            std::string missing;

            for (int pitchClass : key.missingDecidingPitchClasses)
            {
                if (! missing.empty())
                    missing += " / ";

                missing += lh::NoteUtils::pitchClassName (pitchClass);
            }

            std::printf ("\n  %s and %s differ only by %s, and none of those notes occur in\n"
                         "  this loop - so nothing in the audio could separate them. %s was\n"
                         "  chosen by convention (major and minor before modes), not by evidence.\n"
                         "  Play the missing note and the ambiguity resolves itself.\n",
                         key.best.name().c_str(), key.runnerUp.name().c_str(),
                         missing.c_str(), key.best.name().c_str());
        }
        else if (key.runnerUpSharesNoteSet)
        {
            std::printf ("\n  %s and %s contain exactly the same notes, so note content alone\n"
                         "  cannot separate them - only which note the phrase treats as home.\n"
                         "  The winner was chosen on emphasis (duration x clarity per note).\n",
                         key.best.name().c_str(), key.runnerUp.name().c_str());
        }
        else if (key.ambiguous && ! key.thirdAbsent)
        {
            // Only worth saying when the missing third has not already explained it.
            std::printf ("\n  The top two fit about equally well but are built from different notes,\n"
                         "  which usually means chromatic content, too short a phrase, or a\n"
                         "  genuine modal mixture.\n");
        }

        if (key.distinctPitchClasses < 4)
        {
            std::printf ("\n  Only %d distinct pitch classes present. That is too little material for\n"
                         "  a trustworthy key estimate - treat this as a hint, not an answer.\n",
                         key.distinctPitchClasses);
        }

        if (topCandidates > 0)
        {
            std::printf ("\n  top candidates:\n");

            const int count = std::min (topCandidates, static_cast<int> (key.ranked.size()));

            for (int i = 0; i < count; ++i)
            {
                const auto& candidate = key.ranked[static_cast<std::size_t> (i)];
                std::printf ("    %d  %-18s %.3f\n", i + 1, candidate.name().c_str(), candidate.correlation);
            }
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

        if (wav.numFrames() < static_cast<std::size_t> (options.detector.frameSize))
        {
            std::fprintf (stderr, "error: file is shorter than one analysis frame\n");
            return 1;
        }

        lh::YinPitchDetector detector (options.detector);
        const auto frames = detector.process (wav.mono.data(), wav.numFrames(), wav.sampleRate);

        const double hopSeconds = static_cast<double> (options.detector.hopSize) / wav.sampleRate;

        if (options.csv)
        {
            std::printf ("time_s,frequency_hz,note,cents,clarity,rms,voiced\n");

            for (const auto& frame : frames)
            {
                const int note = frame.voiced ? lh::NoteUtils::nearestMidiNote (frame.frequencyHz) : -1;

                std::printf ("%.4f,%.3f,%s,%.1f,%.4f,%.6f,%d\n",
                             frame.timeSeconds, frame.frequencyHz,
                             frame.voiced ? lh::NoteUtils::midiNoteName (note).c_str() : "",
                             frame.voiced ? lh::NoteUtils::centsFromNearestNote (frame.frequencyHz) : 0.0,
                             frame.clarity, frame.rms, frame.voiced ? 1 : 0);
            }

            return 0;
        }

        std::printf ("file       : %s\n", options.inputPath.c_str());
        std::printf ("format     : %s, %d-bit, %d ch, %.0f Hz\n",
                     wav.formatName.c_str(), wav.bitsPerSample, wav.numChannels, wav.sampleRate);
        std::printf ("duration   : %.3f s (%llu samples)\n",
                     wav.durationSeconds(), static_cast<unsigned long long> (wav.numFrames()));
        std::printf ("detector   : frame %d, hop %d, threshold %.2f, band %.0f-%.0f Hz\n",
                     options.detector.frameSize, options.detector.hopSize,
                     options.detector.threshold,
                     options.detector.minFrequencyHz, options.detector.maxFrequencyHz);

        if (! detector.isConfigurationValid())
        {
            std::fprintf (stderr,
                          "warning: frame size %d cannot reach %.1f Hz at this sample rate; "
                          "the search band is really %.1f - %.1f Hz. Increase --frame to go lower.\n",
                          options.detector.frameSize, options.detector.minFrequencyHz,
                          detector.effectiveMinFrequencyHz(), detector.effectiveMaxFrequencyHz());
        }

        std::size_t voicedCount = 0;
        std::vector<double> voicedClarity;

        for (const auto& frame : frames)
        {
            if (frame.voiced)
            {
                ++voicedCount;
                voicedClarity.push_back (frame.clarity);
            }
        }

        const double voicedPercent = frames.empty()
            ? 0.0
            : 100.0 * static_cast<double> (voicedCount) / static_cast<double> (frames.size());

        std::printf ("frames     : %llu analysed, %llu voiced (%.1f%%), median clarity %.3f\n",
                     static_cast<unsigned long long> (frames.size()),
                     static_cast<unsigned long long> (voicedCount),
                     voicedPercent, median (voicedClarity));

        if (options.showFrames)
            printFrameTable (frames, options);

        const lh::NoteSegmenter segmenter (options.segmenter);
        const auto notes = segmenter.segment (frames, hopSeconds);

        if (options.showNotes)
            printNoteEvents (notes, options.segmenter);

        if (! options.detectKey)
            return 0;

        std::vector<lh::PitchEvent> events;
        events.reserve (notes.size());

        for (const auto& note : notes)
            events.push_back ({ note.medianFrequencyHz, note.durationSeconds, note.meanClarity });

        const lh::KeyDetector keyDetector (options.key);
        const auto keyResult = keyDetector.analyse (events);

        printHistogram (keyResult);
        printKeyResult (keyResult, options.topCandidates);

        return 0;
    }

    //==============================================================================
    // Synthetic checks, so the chain can be validated without needing audio files.

    std::vector<float> makeTone (double frequencyHz, double sampleRate, double seconds, int numHarmonics)
    {
        const std::size_t total = static_cast<std::size_t> (sampleRate * seconds);
        std::vector<float> samples (total, 0.0f);

        for (std::size_t i = 0; i < total; ++i)
        {
            const double t = static_cast<double> (i) / sampleRate;
            double value = 0.0;

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

    //==============================================================================
    /** Builds pitch events straight from (MIDI note, duration) pairs, bypassing
        audio entirely so key detection can be tested in isolation. */
    std::vector<lh::PitchEvent> eventsFromNotes (const std::vector<std::pair<int, double>>& notes)
    {
        std::vector<lh::PitchEvent> events;
        events.reserve (notes.size());

        for (const auto& [midiNote, duration] : notes)
            events.push_back ({ lh::NoteUtils::midiNoteToFrequency (midiNote), duration, 1.0 });

        return events;
    }

    bool checkKey (const char* label,
                   const char* expectedKey,
                   const std::vector<std::pair<int, double>>& notes,
                   bool expectAmbiguous = false)
    {
        const lh::KeyDetector detector;
        const auto result = detector.analyse (eventsFromNotes (notes));

        const bool keyMatches = result.valid && result.best.name() == expectedKey;
        const bool ambiguityMatches = ! expectAmbiguous || result.ambiguous;
        const bool passed = keyMatches && ambiguityMatches;

        std::printf ("  %s  %-26s got %-16s r=%.3f  runner-up %-16s margin %.3f%s\n",
                     passed ? "ok  " : "FAIL",
                     label,
                     result.valid ? result.best.name().c_str() : "(none)",
                     result.best.correlation,
                     result.valid ? result.runnerUp.name().c_str() : "(none)",
                     result.margin,
                     result.ambiguous ? "  [ambiguous]" : "");

        if (! keyMatches)
            std::printf ("        expected %s\n", expectedKey);

        return passed;
    }

    int runSelfTest()
    {
        std::printf ("LoopHarmonizer self-test\n\n");

        bool allPassed = true;

        std::printf ("pitch detection - pure sine:\n");
        allPassed &= checkTone ("E2  low guitar E", 82.41, 1, 44100.0);
        allPassed &= checkTone ("A2", 110.00, 1, 44100.0);
        allPassed &= checkTone ("E3", 164.81, 1, 44100.0);
        allPassed &= checkTone ("A4  concert pitch", 440.00, 1, 44100.0);
        allPassed &= checkTone ("A5", 880.00, 1, 44100.0);

        std::printf ("\npitch detection - harmonic-rich (the octave-error trap):\n");
        allPassed &= checkTone ("E2  16 harmonics", 82.41, 16, 44100.0);
        allPassed &= checkTone ("A2  16 harmonics", 110.00, 16, 44100.0);
        allPassed &= checkTone ("D3  16 harmonics", 146.83, 16, 44100.0);
        allPassed &= checkTone ("A3  16 harmonics", 220.00, 16, 44100.0);

        std::printf ("\npitch detection - other sample rates:\n");
        allPassed &= checkTone ("A2  at 48 kHz", 110.00, 16, 48000.0);
        allPassed &= checkTone ("A2  at 96 kHz", 110.00, 16, 96000.0);

        std::printf ("\npitch detection - gating:\n");
        allPassed &= checkSilence();

        // MIDI reference: C4 = 60, so B3 = 59, D4 = 62, F#4 = 66, A4 = 69.
        std::printf ("\nkey detection - unambiguous tonic emphasis:\n");

        allPassed &= checkKey ("C major scale", "C major",
                               { { 60, 1.0 }, { 62, 0.5 }, { 64, 0.5 }, { 65, 0.5 },
                                 { 67, 0.8 }, { 69, 0.5 }, { 71, 0.4 }, { 72, 1.0 } });

        allPassed &= checkKey ("A minor, tonic-heavy", "A minor",
                               { { 57, 1.2 }, { 60, 0.6 }, { 64, 0.9 }, { 55, 0.4 },
                                 { 62, 0.4 }, { 65, 0.3 }, { 59, 0.3 }, { 57, 1.0 } });

        allPassed &= checkKey ("B minor riff", "B minor",
                               { { 59, 1.2 }, { 62, 0.6 }, { 66, 0.9 }, { 69, 0.5 },
                                 { 67, 0.4 }, { 64, 0.4 }, { 59, 1.0 }, { 66, 0.6 } });

        allPassed &= checkKey ("E Dorian", "E Dorian",
                               { { 64, 1.4 }, { 66, 0.5 }, { 67, 0.7 }, { 69, 0.5 },
                                 { 71, 0.9 }, { 73, 0.6 }, { 74, 0.4 }, { 64, 1.0 } });

        std::printf ("\nkey detection - weighting and gating:\n");

        // The same notes, but the tonic is stated briefly and a passing tone is
        // held. Duration weighting should follow the held note, not the count.
        {
            const lh::KeyDetector detector;
            const auto shortTonic = detector.analyse (eventsFromNotes (
                { { 60, 0.05 }, { 62, 0.05 }, { 64, 0.05 }, { 67, 2.0 } }));

            const bool passed = shortTonic.valid && shortTonic.best.name() != "C major";
            std::printf ("  %s  %-26s held G outweighs brief C -> %s\n",
                         passed ? "ok  " : "FAIL", "duration outweighs count",
                         shortTonic.valid ? shortTonic.best.name().c_str() : "(none)");
            allPassed &= passed;
        }

        // Low-clarity events must be discarded outright.
        {
            const lh::KeyDetector detector;
            std::vector<lh::PitchEvent> events = eventsFromNotes (
                { { 59, 1.2 }, { 62, 0.6 }, { 66, 0.9 }, { 69, 0.5 } });

            // A loud, long, but untrustworthy F natural - the kind of reading a
            // slide produces. It should not drag the key flatwards.
            events.push_back ({ lh::NoteUtils::midiNoteToFrequency (65), 3.0, 0.2 });

            const auto result = detector.analyse (events);
            const bool passed = result.valid && result.eventsUsed == 4;

            std::printf ("  %s  %-26s low-clarity event dropped (%d of 5 used) -> %s\n",
                         passed ? "ok  " : "FAIL", "clarity gate",
                         result.eventsUsed,
                         result.valid ? result.best.name().c_str() : "(none)");
            allPassed &= passed;
        }

        // Relative keys share a note set, so a phrase with no tonal centre should
        // report ambiguity rather than a confident guess.
        {
            const lh::KeyDetector detector;
            const auto result = detector.analyse (eventsFromNotes (
                { { 60, 0.5 }, { 62, 0.5 }, { 64, 0.5 }, { 65, 0.5 },
                  { 67, 0.5 }, { 69, 0.5 }, { 71, 0.5 } }));

            const bool passed = result.valid && result.ambiguous;
            std::printf ("  %s  %-26s flat diatonic set -> %s vs %s, margin %.3f%s\n",
                         passed ? "ok  " : "FAIL", "no tonal centre",
                         result.best.name().c_str(), result.runnerUp.name().c_str(),
                         result.margin,
                         result.runnerUpSharesNoteSet ? " (same notes)" : "");
            allPassed &= passed;
        }

        std::printf ("\nkey detection - tonic vs mode confidence:\n");

        // A bassline shape: root and fifth stated emphatically, no third anywhere.
        // The root should be settled and the mode should not be.
        {
            const lh::KeyDetector detector;
            const auto result = detector.analyse (eventsFromNotes (
                { { 59, 2.0 }, { 66, 0.5 }, { 64, 0.6 }, { 69, 0.6 }, { 59, 1.5 } }));

            const bool passed = result.valid
                             && result.thirdAbsent
                             && result.best.tonicPitchClass == 11
                             && result.tonic.value > result.mode.value;

            std::printf ("  %s  %-26s tonic %s conf %.2f, mode %s conf %.2f, third absent %s\n",
                         passed ? "ok  " : "FAIL", "bassline, no third",
                         lh::NoteUtils::pitchClassName (result.best.tonicPitchClass),
                         result.tonic.value,
                         scaleTypeName (result.best.scale), result.mode.value,
                         result.thirdAbsent ? "yes" : "no");
            allPassed &= passed;
        }

        // Positive control: the same root, now with its minor third present.
        {
            const lh::KeyDetector detector;
            const auto result = detector.analyse (eventsFromNotes (
                { { 59, 2.0 }, { 62, 1.0 }, { 66, 0.8 }, { 64, 0.6 },
                  { 69, 0.5 }, { 67, 0.4 }, { 61, 0.4 }, { 59, 1.5 } }));

            const bool passed = result.valid
                             && ! result.thirdAbsent
                             && result.best.name() == "B minor";

            std::printf ("  %s  %-26s tonic %s conf %.2f, mode %s conf %.2f, third absent %s\n",
                         passed ? "ok  " : "FAIL", "same root, third present",
                         lh::NoteUtils::pitchClassName (result.best.tonicPitchClass),
                         result.tonic.value,
                         scaleTypeName (result.best.scale), result.mode.value,
                         result.thirdAbsent ? "yes" : "no");
            allPassed &= passed;
        }

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
