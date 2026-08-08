#pragma once

#include <string>

// Equal-tempered pitch <-> note-name conversions.
//
// Deliberately free of any JUCE dependency so the DSP layer can be built and
// tested outside the plugin.
namespace lh::NoteUtils
{
    inline constexpr double kDefaultA4Hz = 440.0;

    // Continuous MIDI note number (69.0 == A4). Returns 0.0 for non-positive input.
    double frequencyToMidiNote (double frequencyHz, double a4Hz = kDefaultA4Hz);

    double midiNoteToFrequency (double midiNote, double a4Hz = kDefaultA4Hz);

    // Nearest equal-tempered MIDI note. Returns -1 for non-positive input.
    int nearestMidiNote (double frequencyHz, double a4Hz = kDefaultA4Hz);

    // Signed deviation from the nearest note, in cents (-50.0 .. +50.0).
    // This is the number to watch when validating detector accuracy: a stable
    // note reading with +/-40 cents of jitter means the detector is latching
    // onto something, but not tightly enough to trust for key detection.
    double centsFromNearestNote (double frequencyHz, double a4Hz = kDefaultA4Hz);

    // Scientific pitch notation, so middle C (MIDI 60) is "C4" and the guitar's
    // low open E (MIDI 40) is "E2". Sharps only, no enharmonic flats - phase 3
    // key detection will decide how to spell things.
    std::string midiNoteName (int midiNote);

    // Pitch class 0..11, where 0 == C. Returns -1 for negative input.
    int midiNotePitchClass (int midiNote);

    // "C", "C#", "D", ... Returns "?" if pitchClass is out of range.
    const char* pitchClassName (int pitchClass);
}
