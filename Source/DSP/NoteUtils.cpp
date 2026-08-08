#include "NoteUtils.h"

#include <cmath>

namespace lh::NoteUtils
{
    namespace
    {
        const char* const kPitchClassNames[12] =
        {
            "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
        };
    }

    double frequencyToMidiNote (double frequencyHz, double a4Hz)
    {
        if (frequencyHz <= 0.0 || a4Hz <= 0.0)
            return 0.0;

        return 69.0 + 12.0 * std::log2 (frequencyHz / a4Hz);
    }

    double midiNoteToFrequency (double midiNote, double a4Hz)
    {
        return a4Hz * std::pow (2.0, (midiNote - 69.0) / 12.0);
    }

    int nearestMidiNote (double frequencyHz, double a4Hz)
    {
        if (frequencyHz <= 0.0)
            return -1;

        return static_cast<int> (std::lround (frequencyToMidiNote (frequencyHz, a4Hz)));
    }

    double centsFromNearestNote (double frequencyHz, double a4Hz)
    {
        if (frequencyHz <= 0.0)
            return 0.0;

        const double continuous = frequencyToMidiNote (frequencyHz, a4Hz);
        return (continuous - std::round (continuous)) * 100.0;
    }

    int midiNotePitchClass (int midiNote)
    {
        if (midiNote < 0)
            return -1;

        return midiNote % 12;
    }

    const char* pitchClassName (int pitchClass)
    {
        if (pitchClass < 0 || pitchClass > 11)
            return "?";

        return kPitchClassNames[pitchClass];
    }

    std::string midiNoteName (int midiNote)
    {
        if (midiNote < 0)
            return "--";

        // MIDI 60 == C4, so octave numbering starts one below note 0.
        const int octave = (midiNote / 12) - 1;
        return std::string (kPitchClassNames[midiNote % 12]) + std::to_string (octave);
    }
}
