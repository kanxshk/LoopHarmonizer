#include "KeyDetector.h"
#include "NoteUtils.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace lh
{
    namespace
    {
        //======================================================================
        // Krumhansl-Kessler probe-tone profiles. Empirically derived: listeners
        // rated how well each chromatic pitch fitted an established key context.
        constexpr std::array<double, 12> kMajorProfile
        {
            6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88
        };

        constexpr std::array<double, 12> kMinorProfile
        {
            6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17
        };

        /** Derives a modal profile from an empirical one by exchanging the
            weights of the degree the mode drops and the degree it adds.

            Building modal profiles from scratch does not work: a hand-assigned
            template is essentially a flat scale-membership mask, and a flat mask
            correlates better with a spiky real-world histogram than the nuanced
            Krumhansl curves do. The modes then beat major and minor on nearly
            every input, and modes sharing a weight structure tie with each other
            exactly. Swapping two weights instead leaves the profile's mean and
            variance untouched, so every correlation in the ranking stays directly
            comparable while the characteristic degree still distinguishes the
            mode. */
        std::array<double, 12> swapDegrees (const std::array<double, 12>& base,
                                            int droppedDegree,
                                            int addedDegree)
        {
            std::array<double, 12> profile = base;
            std::swap (profile[static_cast<std::size_t> (droppedDegree)],
                       profile[static_cast<std::size_t> (addedDegree)]);
            return profile;
        }

        const std::array<double, 12>& profileFor (ScaleType scale)
        {
            // Each mode differs from major or natural minor by exactly one degree.
            static const std::array<double, 12> dorian        = swapDegrees (kMinorProfile, 8, 9);   // b6 -> natural 6
            static const std::array<double, 12> phrygian      = swapDegrees (kMinorProfile, 2, 1);   // natural 2 -> b2
            static const std::array<double, 12> harmonicMinor = swapDegrees (kMinorProfile, 10, 11); // b7 -> natural 7
            static const std::array<double, 12> mixolydian    = swapDegrees (kMajorProfile, 11, 10); // natural 7 -> b7
            static const std::array<double, 12> lydian        = swapDegrees (kMajorProfile, 5, 6);   // natural 4 -> #4

            switch (scale)
            {
                case ScaleType::major:         return kMajorProfile;
                case ScaleType::naturalMinor:  return kMinorProfile;
                case ScaleType::dorian:        return dorian;
                case ScaleType::mixolydian:    return mixolydian;
                case ScaleType::phrygian:      return phrygian;
                case ScaleType::lydian:        return lydian;
                case ScaleType::harmonicMinor: return harmonicMinor;
            }

            return kMajorProfile;
        }

        /** Ranking order for scales whose correlations are tied. Major and minor
            come first because they are overwhelmingly the more likely reading of
            an arbitrary loop; a mode should win only on actual evidence. */
        int scalePriority (ScaleType scale)
        {
            switch (scale)
            {
                case ScaleType::major:         return 0;
                case ScaleType::naturalMinor:  return 1;
                case ScaleType::dorian:        return 2;
                case ScaleType::mixolydian:    return 3;
                case ScaleType::phrygian:      return 4;
                case ScaleType::lydian:        return 5;
                case ScaleType::harmonicMinor: return 6;
            }

            return 7;
        }

        /** Pearson correlation. Returns 0 when either input has no variance,
            which happens for a phrase sitting on a single pitch class. */
        double pearson (const std::array<double, 12>& a, const std::array<double, 12>& b)
        {
            double meanA = 0.0;
            double meanB = 0.0;

            for (std::size_t i = 0; i < 12; ++i)
            {
                meanA += a[i];
                meanB += b[i];
            }

            meanA /= 12.0;
            meanB /= 12.0;

            double covariance = 0.0;
            double varianceA = 0.0;
            double varianceB = 0.0;

            for (std::size_t i = 0; i < 12; ++i)
            {
                const double da = a[i] - meanA;
                const double db = b[i] - meanB;

                covariance += da * db;
                varianceA += da * da;
                varianceB += db * db;
            }

            const double denominator = std::sqrt (varianceA * varianceB);

            return denominator > 1.0e-12 ? covariance / denominator : 0.0;
        }
    }

    //==========================================================================
    const char* scaleTypeName (ScaleType scale)
    {
        switch (scale)
        {
            case ScaleType::major:         return "major";
            case ScaleType::naturalMinor:  return "minor";
            case ScaleType::dorian:        return "Dorian";
            case ScaleType::mixolydian:    return "Mixolydian";
            case ScaleType::phrygian:      return "Phrygian";
            case ScaleType::lydian:        return "Lydian";
            case ScaleType::harmonicMinor: return "harmonic minor";
        }

        return "?";
    }

    unsigned int scaleIntervalMask (ScaleType scale)
    {
        const auto maskOf = [] (std::initializer_list<int> degrees)
        {
            unsigned int mask = 0u;

            for (int degree : degrees)
                mask |= (1u << degree);

            return mask;
        };

        switch (scale)
        {
            case ScaleType::major:         return maskOf ({ 0, 2, 4, 5, 7, 9, 11 });
            case ScaleType::naturalMinor:  return maskOf ({ 0, 2, 3, 5, 7, 8, 10 });
            case ScaleType::dorian:        return maskOf ({ 0, 2, 3, 5, 7, 9, 10 });
            case ScaleType::mixolydian:    return maskOf ({ 0, 2, 4, 5, 7, 9, 10 });
            case ScaleType::phrygian:      return maskOf ({ 0, 1, 3, 5, 7, 8, 10 });
            case ScaleType::lydian:        return maskOf ({ 0, 2, 4, 6, 7, 9, 11 });
            case ScaleType::harmonicMinor: return maskOf ({ 0, 2, 3, 5, 7, 8, 11 });
        }

        return 0u;
    }

    //==========================================================================
    std::string KeyCandidate::name() const
    {
        if (! isValid())
            return "unknown";

        return std::string (NoteUtils::pitchClassName (tonicPitchClass))
             + " " + scaleTypeName (scale);
    }

    unsigned int KeyCandidate::pitchClassMask() const
    {
        if (! isValid())
            return 0u;

        const unsigned int relative = scaleIntervalMask (scale);
        unsigned int absolute = 0u;

        for (int degree = 0; degree < 12; ++degree)
            if ((relative & (1u << degree)) != 0u)
                absolute |= 1u << ((tonicPitchClass + degree) % 12);

        return absolute;
    }

    bool KeyCandidate::sharesNoteSetWith (const KeyCandidate& other) const
    {
        if (! isValid() || ! other.isValid())
            return false;

        // Same tonic and same scale is the same key, not a distinct sharing pair.
        if (tonicPitchClass == other.tonicPitchClass && scale == other.scale)
            return false;

        return pitchClassMask() == other.pitchClassMask();
    }

    //==========================================================================
    KeyDetector::KeyDetector (const Params& paramsToUse)
        : params (paramsToUse)
    {
    }

    KeyDetectionResult KeyDetector::analyse (const std::vector<PitchEvent>& events) const
    {
        std::array<double, 12> weights {};
        int used = 0;

        for (const auto& event : events)
        {
            if (event.frequencyHz <= 0.0
                || event.clarity < params.minClarity
                || event.durationSeconds < params.minDurationSeconds)
            {
                continue;
            }

            const int midiNote = NoteUtils::nearestMidiNote (event.frequencyHz);

            if (midiNote < 0)
                continue;

            // Duration carries the musical weight; clarity discounts it by how
            // much the detector should be believed.
            const double weight = event.durationSeconds
                                * std::pow (std::clamp (event.clarity, 0.0, 1.0),
                                            params.clarityExponent);

            weights[static_cast<std::size_t> (midiNote % 12)] += weight;
            ++used;
        }

        KeyDetectionResult result = analyseHistogram (weights);
        result.eventsUsed = used;

        return result;
    }

    KeyDetectionResult KeyDetector::analyseHistogram (const std::array<double, 12>& weights) const
    {
        KeyDetectionResult result;
        result.histogram = weights;

        double total = 0.0;

        for (double weight : weights)
        {
            total += weight;

            if (weight > 0.0)
                ++result.distinctPitchClasses;
        }

        result.totalWeight = total;

        if (total <= 0.0)
            return result;

        for (auto& bin : result.histogram)
            bin /= total;

        // Assemble the candidate scale types.
        std::vector<ScaleType> scales { ScaleType::major, ScaleType::naturalMinor };

        if (params.includeModes)
        {
            scales.push_back (ScaleType::dorian);
            scales.push_back (ScaleType::mixolydian);
            scales.push_back (ScaleType::phrygian);
            scales.push_back (ScaleType::lydian);
        }

        if (params.includeHarmonicMinor)
            scales.push_back (ScaleType::harmonicMinor);

        // Correlate the histogram against every profile at every rotation.
        for (ScaleType scale : scales)
        {
            const auto& profile = profileFor (scale);

            for (int tonic = 0; tonic < 12; ++tonic)
            {
                std::array<double, 12> rotated {};

                for (int degree = 0; degree < 12; ++degree)
                    rotated[static_cast<std::size_t> (degree)]
                        = result.histogram[static_cast<std::size_t> ((tonic + degree) % 12)];

                KeyCandidate candidate;
                candidate.tonicPitchClass = tonic;
                candidate.scale = scale;
                candidate.correlation = pearson (rotated, profile);

                result.ranked.push_back (candidate);
            }
        }

        // Exact ties are not a rounding artefact: two scales differing only in a
        // degree the phrase never plays produce identical correlations, because
        // the histogram is zero at both of the degrees whose weights differ.
        // Break those by convention rather than by array order.
        std::sort (result.ranked.begin(), result.ranked.end(),
                   [] (const KeyCandidate& a, const KeyCandidate& b)
                   {
                       if (std::abs (a.correlation - b.correlation) > 1.0e-9)
                           return a.correlation > b.correlation;

                       if (scalePriority (a.scale) != scalePriority (b.scale))
                           return scalePriority (a.scale) < scalePriority (b.scale);

                       return a.tonicPitchClass < b.tonicPitchClass;
                   });

        result.best = result.ranked.front();
        result.runnerUp = result.ranked.size() > 1 ? result.ranked[1] : KeyCandidate{};
        result.margin = result.best.correlation - result.runnerUp.correlation;
        result.confidence = std::clamp (result.best.correlation, 0.0, 1.0);
        result.ambiguous = result.margin < params.ambiguityMargin;
        result.runnerUpSharesNoteSet = result.best.sharesNoteSetWith (result.runnerUp);

        // Split the verdict into "which root" and "which scale on that root".
        // The list is already sorted, so the first entry that differs in the
        // relevant way is by construction the strongest such alternative.
        const auto fitness = [&params = params] (double correlation, double margin, double decisiveMargin)
        {
            const double settled = decisiveMargin > 0.0
                ? std::clamp (margin / decisiveMargin, 0.0, 1.0)
                : 1.0;

            return std::clamp (correlation, 0.0, 1.0) * settled;
        };

        for (const auto& candidate : result.ranked)
        {
            if (candidate.tonicPitchClass != result.best.tonicPitchClass)
            {
                result.tonic.runnerUp = candidate;
                result.tonic.margin = result.best.correlation - candidate.correlation;
                break;
            }
        }

        for (const auto& candidate : result.ranked)
        {
            if (candidate.tonicPitchClass == result.best.tonicPitchClass
                && candidate.scale != result.best.scale)
            {
                result.mode.runnerUp = candidate;
                result.mode.margin = result.best.correlation - candidate.correlation;
                break;
            }
        }

        result.tonic.value = fitness (result.best.correlation, result.tonic.margin,
                                      params.tonicDecisiveMargin);
        result.mode.value = fitness (result.best.correlation, result.mode.margin,
                                     params.modeDecisiveMargin);

        // Without a third of either quality, major and minor are unreachable.
        const std::size_t minorThird = static_cast<std::size_t> ((result.best.tonicPitchClass + 3) % 12);
        const std::size_t majorThird = static_cast<std::size_t> ((result.best.tonicPitchClass + 4) % 12);

        result.thirdAbsent = result.histogram[minorThird] <= 0.0
                          && result.histogram[majorThird] <= 0.0;

        // Work out whether the evidence could ever have separated the top two.
        if (result.runnerUp.isValid())
        {
            const unsigned int differing = result.best.pitchClassMask()
                                         ^ result.runnerUp.pitchClassMask();
            bool anyDecidingNotePresent = false;

            for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            {
                if ((differing & (1u << pitchClass)) == 0u)
                    continue;

                if (result.histogram[static_cast<std::size_t> (pitchClass)] > 0.0)
                    anyDecidingNotePresent = true;
                else
                    result.missingDecidingPitchClasses.push_back (pitchClass);
            }

            result.runnerUpUndecidable = ! anyDecidingNotePresent
                                       && ! result.missingDecidingPitchClasses.empty();
        }

        result.valid = true;

        return result;
    }
}
