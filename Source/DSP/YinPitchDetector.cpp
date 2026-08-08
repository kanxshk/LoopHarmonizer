#include "YinPitchDetector.h"
#include "NoteUtils.h"

#include <algorithm>
#include <cmath>

namespace lh
{
    //==============================================================================
    int PitchFrame::nearestMidiNote() const
    {
        return voiced ? NoteUtils::nearestMidiNote (frequencyHz) : -1;
    }

    //==============================================================================
    YinPitchDetector::YinPitchDetector (const Params& paramsToUse)
        : params (paramsToUse)
    {
    }

    void YinPitchDetector::setParams (const Params& newParams)
    {
        params = newParams;

        // Force a re-prepare: the lag range and buffer size both depend on these.
        sampleRate = 0.0;
        tauMin = 0;
        tauMax = 0;
        yinBuffer.clear();
    }

    //==============================================================================
    void YinPitchDetector::prepare (double sampleRateToUse)
    {
        sampleRate = sampleRateToUse;

        // The difference function compares a half-frame against itself shifted by
        // up to a half-frame, so the longest representable lag is frameSize / 2.
        const int halfFrame = params.frameSize / 2;
        yinBuffer.assign (static_cast<std::size_t> (std::max (halfFrame, 1)), 0.0);

        if (sampleRate <= 0.0 || halfFrame < 4)
        {
            tauMin = tauMax = 0;
            return;
        }

        // Lag and frequency run in opposite directions: the highest frequency
        // gives the shortest lag.
        tauMin = std::max (2, static_cast<int> (std::floor (sampleRate / params.maxFrequencyHz)));
        tauMax = std::min (halfFrame, static_cast<int> (std::ceil (sampleRate / params.minFrequencyHz)) + 1);
    }

    void YinPitchDetector::reset()
    {
        std::fill (yinBuffer.begin(), yinBuffer.end(), 0.0);
    }

    bool YinPitchDetector::isPrepared() const noexcept
    {
        return sampleRate > 0.0 && ! yinBuffer.empty();
    }

    bool YinPitchDetector::isConfigurationValid() const noexcept
    {
        if (! isPrepared() || tauMin >= tauMax)
            return false;

        // A frame that cannot hold two full periods of minFrequencyHz will simply
        // never see that pitch, which is a silent failure worth surfacing.
        return effectiveMinFrequencyHz() <= params.minFrequencyHz * 1.001;
    }

    double YinPitchDetector::effectiveMinFrequencyHz() const noexcept
    {
        if (tauMax <= 1 || sampleRate <= 0.0)
            return 0.0;

        return sampleRate / static_cast<double> (tauMax - 1);
    }

    double YinPitchDetector::effectiveMaxFrequencyHz() const noexcept
    {
        if (tauMin <= 0 || sampleRate <= 0.0)
            return 0.0;

        return sampleRate / static_cast<double> (tauMin);
    }

    //==============================================================================
    double YinPitchDetector::parabolicInterpolate (int tau) const
    {
        const int lower = std::max (tau - 1, 0);
        const int upper = std::min (tau + 1, static_cast<int> (yinBuffer.size()) - 1);

        // At either edge of the search range there is nothing to fit through, so
        // fall back to whichever of the available lags dips lowest.
        if (lower == tau || upper == tau)
        {
            const int best = (yinBuffer[static_cast<std::size_t> (lower)]
                                < yinBuffer[static_cast<std::size_t> (upper)]) ? lower : upper;
            return static_cast<double> (best);
        }

        const double s0 = yinBuffer[static_cast<std::size_t> (lower)];
        const double s1 = yinBuffer[static_cast<std::size_t> (tau)];
        const double s2 = yinBuffer[static_cast<std::size_t> (upper)];

        const double denominator = 2.0 * (2.0 * s1 - s2 - s0);

        if (std::abs (denominator) < 1.0e-12)
            return static_cast<double> (tau);

        return static_cast<double> (tau) + (s2 - s0) / denominator;
    }

    //==============================================================================
    PitchFrame YinPitchDetector::analyseFrame (const float* frame)
    {
        PitchFrame result;

        if (frame == nullptr || params.frameSize <= 0)
            return result;

        const int halfFrame = params.frameSize / 2;

        // Frame level first: silence and near-silence are rejected before doing
        // the expensive part, and the caller gets an rms value either way.
        double sumOfSquares = 0.0;

        for (int i = 0; i < params.frameSize; ++i)
        {
            const double sample = static_cast<double> (frame[i]);
            sumOfSquares += sample * sample;
        }

        result.rms = std::sqrt (sumOfSquares / static_cast<double> (params.frameSize));

        if (! isPrepared() || tauMin >= tauMax || result.rms < params.minRms)
            return result;

        // Step 1: squared difference function d(tau).
        for (int tau = 1; tau < halfFrame; ++tau)
        {
            double sum = 0.0;

            for (int j = 0; j < halfFrame; ++j)
            {
                const double delta = static_cast<double> (frame[j])
                                   - static_cast<double> (frame[j + tau]);
                sum += delta * delta;
            }

            yinBuffer[static_cast<std::size_t> (tau)] = sum;
        }

        // Step 2: cumulative mean normalised difference d'(tau), in place.
        // This is what stops lag 0 from always winning.
        yinBuffer[0] = 1.0;
        double runningSum = 0.0;

        for (int tau = 1; tau < halfFrame; ++tau)
        {
            runningSum += yinBuffer[static_cast<std::size_t> (tau)];

            yinBuffer[static_cast<std::size_t> (tau)] = runningSum > 0.0
                ? yinBuffer[static_cast<std::size_t> (tau)] * static_cast<double> (tau) / runningSum
                : 1.0;
        }

        // Step 3: absolute threshold. Take the first dip below the threshold
        // rather than the global minimum - that is what biases YIN away from
        // octave errors, since a sub-harmonic dips just as deep but later.
        int tauEstimate = -1;

        for (int tau = tauMin; tau < tauMax; ++tau)
        {
            if (yinBuffer[static_cast<std::size_t> (tau)] < params.threshold)
            {
                // Walk to the bottom of this dip.
                int candidate = tau;

                while (candidate + 1 < tauMax
                       && yinBuffer[static_cast<std::size_t> (candidate + 1)]
                            < yinBuffer[static_cast<std::size_t> (candidate)])
                {
                    ++candidate;
                }

                tauEstimate = candidate;
                break;
            }
        }

        if (tauEstimate < 0)
        {
            // Nothing crossed the threshold. Report the global minimum anyway so
            // the caller can see what the signal looked like; the resulting low
            // clarity will normally fail the voiced gate below.
            int best = tauMin;

            for (int tau = tauMin + 1; tau < tauMax; ++tau)
                if (yinBuffer[static_cast<std::size_t> (tau)] < yinBuffer[static_cast<std::size_t> (best)])
                    best = tau;

            tauEstimate = best;
        }

        // Step 4: sub-sample refinement, then convert lag to frequency.
        const double refinedTau = parabolicInterpolate (tauEstimate);

        if (refinedTau <= 0.0)
            return result;

        result.frequencyHz = sampleRate / refinedTau;
        result.clarity = std::clamp (1.0 - yinBuffer[static_cast<std::size_t> (tauEstimate)], 0.0, 1.0);
        result.voiced = result.clarity >= params.minClarity;

        return result;
    }

    //==============================================================================
    std::vector<PitchFrame> YinPitchDetector::process (const float* samples,
                                                       std::size_t numSamples,
                                                       double sampleRateToUse)
    {
        prepare (sampleRateToUse);

        std::vector<PitchFrame> frames;

        if (samples == nullptr
            || params.frameSize <= 0
            || params.hopSize <= 0
            || numSamples < static_cast<std::size_t> (params.frameSize))
        {
            return frames;
        }

        const std::size_t frameSize = static_cast<std::size_t> (params.frameSize);
        const std::size_t hopSize   = static_cast<std::size_t> (params.hopSize);

        frames.reserve ((numSamples - frameSize) / hopSize + 1);

        for (std::size_t start = 0; start + frameSize <= numSamples; start += hopSize)
        {
            PitchFrame frame = analyseFrame (samples + start);
            frame.timeSeconds = static_cast<double> (start) / sampleRateToUse;
            frames.push_back (frame);
        }

        return frames;
    }
}
