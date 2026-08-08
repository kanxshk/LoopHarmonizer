#pragma once

#include <cstddef>
#include <vector>

namespace lh
{
    //==============================================================================
    /** Result of analysing a single frame. */
    struct PitchFrame
    {
        double timeSeconds = 0.0;   ///< Time of the frame's first sample.
        double frequencyHz = 0.0;   ///< Detected f0. 0.0 when nothing was detected.
        double clarity     = 0.0;   ///< 0..1 periodicity confidence (1 - YIN aperiodicity).
        double rms         = 0.0;   ///< Frame RMS, for the silence gate.
        bool   voiced      = false; ///< True when clarity and rms both passed their gates.

        /** Nearest MIDI note, or -1 when unvoiced. */
        int nearestMidiNote() const;
    };

    //==============================================================================
    /**
        Monophonic fundamental-frequency estimator using the YIN algorithm
        (de Cheveigne & Kawahara, 2002).

        Intentionally free of any JUCE dependency: this class knows only about
        float sample buffers and a sample rate, so it can be exercised from a
        plain console harness before it is wired into the plugin's audio callback.

        Typical offline use:
        @code
            lh::YinPitchDetector detector;
            auto frames = detector.process (samples, numSamples, 44100.0);
        @endcode

        For the eventual real-time path, call prepare() once and then
        analyseFrame() per frame - after prepare() the analysis performs no
        allocation.

        Note on cost: the difference function is the textbook O(W^2) form, which
        is fine offline and for the modest frame sizes used here, but an
        FFT-based autocorrelation will likely be worth it once this runs inside
        processBlock.
    */
    class YinPitchDetector
    {
    public:
        //==============================================================================
        struct Params
        {
            /** Analysis window length in samples. Must be even, and at least
                twice the longest period you want to detect. */
            int frameSize = 2048;

            /** Advance between consecutive frames, in samples. */
            int hopSize = 512;

            /** Lowest f0 to search for. 50 Hz sits comfortably below a bass
                guitar's open E (~41 Hz is 5-string territory; drop it if needed). */
            double minFrequencyHz = 50.0;

            /** Highest f0 to search for. */
            double maxFrequencyHz = 2000.0;

            /** YIN absolute threshold. Lower is stricter. The paper suggests
                0.1; 0.15 is a common practical compromise. */
            double threshold = 0.15;

            /** Minimum clarity for a frame to count as voiced. */
            double minClarity = 0.5;

            /** Minimum frame RMS for a frame to count as voiced - rejects
                silence and the decay tail of a note. */
            double minRms = 0.005;
        };

        //==============================================================================
        YinPitchDetector() = default;
        explicit YinPitchDetector (const Params& paramsToUse);

        /** Replaces the parameters. Invalidates preparation; call prepare() again. */
        void setParams (const Params& newParams);
        const Params& getParams() const noexcept              { return params; }

        //==============================================================================
        /** Allocates working buffers and computes the lag search range.
            Must be called before analyseFrame(). */
        void prepare (double sampleRateToUse);

        /** Clears internal state without deallocating. */
        void reset();

        bool isPrepared() const noexcept;

        /** False when the configured frame size cannot represent minFrequencyHz,
            or the frequency bounds collapse to an empty lag range. Analysis will
            still run, but over a narrower band than requested - compare
            effectiveMinFrequencyHz() against Params::minFrequencyHz. */
        bool isConfigurationValid() const noexcept;

        /** Lowest frequency actually reachable given frameSize and sample rate. */
        double effectiveMinFrequencyHz() const noexcept;

        /** Highest frequency actually reachable. */
        double effectiveMaxFrequencyHz() const noexcept;

        //==============================================================================
        /** Analyses exactly Params::frameSize samples starting at `frame`.
            Performs no allocation once prepare() has been called. The returned
            frame's timeSeconds is left at zero - the caller owns timing. */
        PitchFrame analyseFrame (const float* frame);

        /** Offline convenience: calls prepare(), then slices the signal into
            hop-spaced frames and analyses each one. */
        std::vector<PitchFrame> process (const float* samples,
                                         std::size_t numSamples,
                                         double sampleRateToUse);

    private:
        //==============================================================================
        /** Refines an integer lag to sub-sample precision by fitting a parabola
            through its two neighbours in the normalised difference curve. */
        double parabolicInterpolate (int tau) const;

        Params params;
        double sampleRate = 0.0;
        int tauMin = 0;
        int tauMax = 0;

        // Holds d(tau), then is overwritten in place with the cumulative mean
        // normalised difference d'(tau).
        std::vector<double> yinBuffer;
    };
}
