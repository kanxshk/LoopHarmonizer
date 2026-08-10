#include "NoteSegmenter.h"
#include "NoteUtils.h"

#include <algorithm>

namespace lh
{
    NoteSegmenter::NoteSegmenter (const Params& paramsToUse)
        : params (paramsToUse)
    {
    }

    std::vector<NoteEvent> NoteSegmenter::segment (const std::vector<PitchFrame>& frames,
                                                   double hopSeconds) const
    {
        std::vector<NoteEvent> events;

        if (frames.empty() || hopSeconds <= 0.0)
            return events;

        // State for the run currently being accumulated.
        int currentNote = -1;
        double startSeconds = 0.0;
        double clarityTotal = 0.0;
        int frameCount = 0;
        int gapFrames = 0;
        double lastFrameEnd = 0.0;
        std::vector<double> frequencies;

        const auto flush = [&]
        {
            if (currentNote < 0 || frameCount == 0)
                return;

            NoteEvent event;
            event.midiNote = currentNote;
            event.startSeconds = startSeconds;
            event.durationSeconds = lastFrameEnd - startSeconds;
            event.meanClarity = clarityTotal / static_cast<double> (frameCount);
            event.frameCount = frameCount;

            // Median rather than mean: a single stray frame inside an otherwise
            // stable note should not drag the reported frequency.
            const std::size_t middle = frequencies.size() / 2;
            std::nth_element (frequencies.begin(),
                              frequencies.begin() + static_cast<std::ptrdiff_t> (middle),
                              frequencies.end());
            event.medianFrequencyHz = frequencies[middle];

            if (event.durationSeconds >= params.minDurationSeconds)
                events.push_back (event);

            currentNote = -1;
            frameCount = 0;
            clarityTotal = 0.0;
            frequencies.clear();
        };

        for (const auto& frame : frames)
        {
            const bool usable = frame.voiced && frame.clarity >= params.minClarity;
            const int note = usable ? NoteUtils::nearestMidiNote (frame.frequencyHz) : -1;

            if (! usable)
            {
                // Tolerate a short dropout before deciding the note has ended.
                if (currentNote >= 0 && ++gapFrames > params.maxGapFrames)
                    flush();

                continue;
            }

            if (note == currentNote)
            {
                gapFrames = 0;
                ++frameCount;
                clarityTotal += frame.clarity;
                frequencies.push_back (frame.frequencyHz);
                lastFrameEnd = frame.timeSeconds + hopSeconds;
                continue;
            }

            // A different note: close the previous run and open a new one.
            flush();

            currentNote = note;
            startSeconds = frame.timeSeconds;
            lastFrameEnd = frame.timeSeconds + hopSeconds;
            clarityTotal = frame.clarity;
            frameCount = 1;
            gapFrames = 0;
            frequencies.assign (1, frame.frequencyHz);
        }

        flush();

        return events;
    }
}
