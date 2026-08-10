#pragma once

#include "YinPitchDetector.h"

#include <vector>

namespace lh
{
    //==============================================================================
    /** A sustained note, recovered from a run of consecutive analysis frames. */
    struct NoteEvent
    {
        int    midiNote        = -1;
        double startSeconds    = 0.0;
        double durationSeconds = 0.0;
        double meanClarity     = 0.0;
        double medianFrequencyHz = 0.0;
        int    frameCount      = 0;
    };

    //==============================================================================
    /**
        Groups per-frame pitch estimates into note events.

        This exists because duration-weighting is only meaningful once frames have
        been collapsed into notes. Weighting raw frames by their hop duration would
        give every frame identical weight, which is just frame counting again - a
        held note and a momentary slide artefact would count the same per frame.

        It also absorbs the single-frame glitches that appear at note boundaries,
        where an analysis window straddles two pitches and YIN reports something
        confident-looking but wrong, often an octave out.
    */
    class NoteSegmenter
    {
    public:
        struct Params
        {
            /** Frames below this clarity are treated as note gaps. Deliberately
                stricter than the detector's own voiced gate, because key
                detection is more sensitive to wrong notes than to missing ones. */
            double minClarity = 0.80;

            /** Notes shorter than this are discarded as artefacts. At the default
                hop that is roughly four frames. */
            double minDurationSeconds = 0.05;

            /** How many consecutive non-matching frames may be bridged before a
                note is considered finished. Absorbs momentary dropouts during
                bends and vibrato without merging genuinely separate notes. */
            int maxGapFrames = 2;
        };

        NoteSegmenter() = default;
        explicit NoteSegmenter (const Params& paramsToUse);

        void setParams (const Params& newParams)      { params = newParams; }
        const Params& getParams() const noexcept      { return params; }

        /** @param frames      per-frame output from YinPitchDetector::process()
            @param hopSeconds  time between consecutive frames */
        std::vector<NoteEvent> segment (const std::vector<PitchFrame>& frames,
                                        double hopSeconds) const;

    private:
        Params params;
    };
}
