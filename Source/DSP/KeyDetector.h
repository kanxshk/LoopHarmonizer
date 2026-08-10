#pragma once

#include <array>
#include <string>
#include <vector>

namespace lh
{
    //==============================================================================
    /** One detected pitch with a duration and a reliability weight.

        This is the unit key detection consumes. It is deliberately decoupled from
        NoteEvent so the detector can be fed from anywhere - segmented audio, a
        MIDI clip, or a hand-written test fixture. */
    struct PitchEvent
    {
        double frequencyHz     = 0.0;
        double durationSeconds = 0.0;
        double clarity         = 0.0;
    };

    //==============================================================================
    enum class ScaleType
    {
        major,
        naturalMinor,
        dorian,
        mixolydian,
        phrygian,
        lydian,
        harmonicMinor
    };

    const char* scaleTypeName (ScaleType scale);

    /** The scale's pitch classes as a 12-bit mask, with bit 0 as the tonic. */
    unsigned int scaleIntervalMask (ScaleType scale);

    //==============================================================================
    struct KeyCandidate
    {
        int       tonicPitchClass = -1;     ///< 0 == C.
        ScaleType scale = ScaleType::major;
        double    correlation = 0.0;        ///< Pearson r against the key profile.

        bool isValid() const noexcept       { return tonicPitchClass >= 0; }

        /** e.g. "B minor", "G Mixolydian". */
        std::string name() const;

        /** The absolute pitch classes this key contains, as a 12-bit mask. */
        unsigned int pitchClassMask() const;

        /** True when both keys are built from an identical set of notes - relative
            major/minor, or two modes of the same parent scale. Such pairs cannot
            be told apart by note content alone, only by which note is emphasised. */
        bool sharesNoteSetWith (const KeyCandidate& other) const;
    };

    //==============================================================================
    /** How decisively one choice beat its nearest alternative. */
    struct ConfidenceBreakdown
    {
        /** 0..1. Combines how well the winner fits with how far clear of the
            alternative it finished - a perfect fit that ties with a rival scores
            low, because the question was never actually settled. */
        double value = 0.0;

        /** Raw correlation gap to the nearest alternative. */
        double margin = 0.0;

        /** The alternative that came closest. */
        KeyCandidate runnerUp;
    };

    //==============================================================================
    struct KeyDetectionResult
    {
        bool valid = false;

        KeyCandidate best;
        KeyCandidate runnerUp;

        /** How sure we are of the root, independent of the scale built on it.
            Its runnerUp is the strongest candidate on a *different* tonic. */
        ConfidenceBreakdown tonic;

        /** How sure we are of the scale, given that root. Its runnerUp is the
            strongest candidate sharing the winning tonic.

            These are reported separately because they fail independently. A
            bassline pins its root down emphatically while saying almost nothing
            about the mode, and one blended number would hide the half that is
            actually trustworthy. Root tracking and root-driven basslines only
            need the tonic; diatonic chord generation needs the mode too. */
        ConfidenceBreakdown mode;

        /** True when neither third degree above the winning tonic occurs in the
            material. Major and minor are then indistinguishable, since the third
            is the note that separates them. Common in basslines, which tend to
            state roots and fifths and leave the third to other instruments. */
        bool thirdAbsent = false;

        /** best.correlation - runnerUp.correlation. Small means the two fit the
            material about equally well. */
        double margin = 0.0;

        /** 0..1 fit quality, from the winning correlation. Note this measures how
            well the material matches that key profile, NOT how sure we are that
            it beat the alternatives - read it together with `margin`. */
        double confidence = 0.0;

        /** True when the winner is not meaningfully ahead of the runner-up. */
        bool ambiguous = false;

        /** True when best and runnerUp are built from the same notes, so the
            ambiguity is about emphasis rather than note content. */
        bool runnerUpSharesNoteSet = false;

        /** The pitch classes that separate best from runnerUp but never appear in
            the material, so no amount of analysis could have chosen between them.
            A minor phrase that never plays its second degree, for instance,
            cannot be told apart from the Phrygian mode on the same tonic. */
        std::vector<int> missingDecidingPitchClasses;

        /** True when every distinguishing note is absent - the winner was picked
            by convention (major and minor before modes), not by evidence. */
        bool runnerUpUndecidable = false;

        std::array<double, 12> histogram {};   ///< Weighted, normalised to sum 1.
        int    distinctPitchClasses = 0;
        int    eventsUsed = 0;
        double totalWeight = 0.0;

        /** All candidates, best first. */
        std::vector<KeyCandidate> ranked;
    };

    //==============================================================================
    /**
        Infers the most likely key of a melodic phrase from its pitch content.

        Builds a pitch-class histogram in which each event contributes its
        duration scaled by its clarity, then correlates that histogram against a
        set of key profiles at all twelve rotations, Krumhansl-Schmuckler style.

        Weighting by duration x clarity is what stops a 12 ms artefact during a
        slide from counting as much as a held note, which matters a great deal on
        guitar where bends and hammer-ons throw off brief unstable readings.

        A caveat on the profiles: the major and natural-minor profiles are the
        empirically derived Krumhansl-Kessler ones. The modal profiles have no
        such experimental basis - each is derived from whichever of those two it
        resembles, by exchanging the weights of the one degree the mode alters.
        That keeps their statistics identical to the empirical profiles so the
        correlations stay comparable, but modal results still deserve more
        scepticism than major or minor.
    */
    class KeyDetector
    {
    public:
        struct Params
        {
            /** Events below this clarity are ignored entirely. */
            double minClarity = 0.60;

            /** Events shorter than this are ignored entirely. */
            double minDurationSeconds = 0.04;

            /** Raising this above 1.0 biases the histogram further towards
                confidently detected notes. */
            double clarityExponent = 1.0;

            /** Include the modal profiles alongside major and natural minor. */
            bool includeModes = true;

            /** Include harmonic minor. Off by default: it differs from natural
                minor by one note and tends to win spuriously on short phrases. */
            bool includeHarmonicMinor = false;

            /** Margin below which the result is reported as ambiguous. */
            double ambiguityMargin = 0.05;

            /** Correlation gap treated as fully settling the tonic. Larger than
                the mode equivalent because twelve tonics compete, against at most
                seven scales on any one of them. */
            double tonicDecisiveMargin = 0.06;

            /** Correlation gap treated as fully settling the mode. */
            double modeDecisiveMargin = 0.05;
        };

        KeyDetector() = default;
        explicit KeyDetector (const Params& paramsToUse);

        void setParams (const Params& newParams)      { params = newParams; }
        const Params& getParams() const noexcept      { return params; }

        KeyDetectionResult analyse (const std::vector<PitchEvent>& events) const;

        /** Analyses a pre-built weighted histogram directly. Exposed for testing
            and for callers that accumulate weights themselves. */
        KeyDetectionResult analyseHistogram (const std::array<double, 12>& weights) const;

    private:
        Params params;
    };
}
