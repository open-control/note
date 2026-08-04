#include "StepSequencerChord.hpp"

#include <config/PlatformCompat.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>

namespace oc::note::sequencer {
namespace {

constexpr uint8_t MAX_CUSTOM_VOICES =
    StepSequencerChordSpec::MAX_CUSTOM_VOICES;
constexpr uint8_t MAX_INTERVAL =
    StepSequencerChordSpec::MAX_CUSTOM_INTERVAL;

using ProjectionWorkspace = StepSequencerChordProjectionWorkspace;
using ProjectionCandidate = ProjectionWorkspace::Candidate;
using ProjectionState = ProjectionWorkspace::State;

struct EncodedFormula {
    bool valid = false;
    StepSequencerChordSpec spec{};
};

FLASHMEM uint16_t absoluteDifference(int16_t lhs, int16_t rhs) {
    const int difference = static_cast<int>(lhs) - static_cast<int>(rhs);
    return static_cast<uint16_t>(
        difference < 0 ? -difference : difference
    );
}

FLASHMEM bool directionAllows(
    StepSequencerScaleConstraintMode direction,
    int16_t candidate,
    int16_t desired
) {
    if (direction == StepSequencerScaleConstraintMode::ConstrainUp) {
        return candidate >= desired;
    }
    if (direction == StepSequencerScaleConstraintMode::ConstrainDown) {
        return candidate <= desired;
    }
    return true;
}

FLASHMEM bool lowerTieBreak(
    const std::array<int16_t, MAX_CUSTOM_VOICES>& lhs,
    const std::array<int16_t, MAX_CUSTOM_VOICES>& rhs,
    uint8_t count
) {
    for (uint8_t index = 0; index < count; ++index) {
        if (lhs[index] == rhs[index]) continue;
        return lhs[index] < rhs[index];
    }
    return false;
}

FLASHMEM bool betterState(
    const ProjectionState& candidate,
    const ProjectionState& current,
    uint8_t count
) {
    if (!current.valid) return true;
    if (candidate.inexactCount != current.inexactCount) {
        return candidate.inexactCount < current.inexactCount;
    }
    if (candidate.maximumMovement != current.maximumMovement) {
        return candidate.maximumMovement < current.maximumMovement;
    }
    if (candidate.totalMovement != current.totalMovement) {
        return candidate.totalMovement < current.totalMovement;
    }
    if (candidate.structuralDistortion !=
        current.structuralDistortion) {
        return candidate.structuralDistortion <
               current.structuralDistortion;
    }
    return lowerTieBreak(candidate.semitones, current.semitones, count);
}

FLASHMEM void appendProjectionVoice(
    ProjectionState& state,
    uint8_t voiceIndex,
    const ProjectionCandidate& candidate,
    const ProjectionWorkspace& workspace
) {
    state.valid = true;
    state.intervals[voiceIndex] = candidate.interval;
    state.semitones[voiceIndex] = candidate.semitones;

    const uint16_t movement = absoluteDifference(
        candidate.semitones,
        workspace.desired[voiceIndex]
    );
    if (movement != 0U) ++state.inexactCount;
    state.maximumMovement = std::max(
        state.maximumMovement,
        movement
    );
    state.totalMovement = static_cast<uint16_t>(
        state.totalMovement + movement
    );

    const int16_t candidateGap = static_cast<int16_t>(
        candidate.semitones - state.semitones[voiceIndex - 1U]
    );
    const int16_t desiredGap = static_cast<int16_t>(
        workspace.desired[voiceIndex] -
        workspace.desired[voiceIndex - 1U]
    );
    state.structuralDistortion = static_cast<uint16_t>(
        state.structuralDistortion +
        absoluteDifference(candidateGap, desiredGap)
    );
}

FLASHMEM void clearProjectionLayer(
    std::array<ProjectionState, MAX_INTERVAL>& layer
) {
    for (auto& state : layer) {
        state.valid = false;
    }
}

FLASHMEM ProjectionState runProjection(
    ProjectionWorkspace& workspace,
    uint8_t voiceCount,
    StepSequencerScaleConstraintMode direction
) {
    ProjectionState best{};
    if (voiceCount < 2U || voiceCount > MAX_CUSTOM_VOICES) {
        return best;
    }

    auto* previous = &workspace.states[0];
    auto* current = &workspace.states[1];
    clearProjectionLayer(*previous);
    clearProjectionLayer(*current);

    for (uint8_t candidateIndex = 0U;
         candidateIndex < workspace.candidateCount;
         ++candidateIndex) {
        const auto& candidate = workspace.candidates[candidateIndex];
        if (!directionAllows(
                direction,
                candidate.semitones,
                workspace.desired[1U]
            )) {
            continue;
        }
        ProjectionState state{};
        state.intervals[0] = 0U;
        state.semitones[0] = 0;
        appendProjectionVoice(state, 1U, candidate, workspace);
        (*previous)[candidateIndex] = state;
    }

    for (uint8_t voiceIndex = 2U;
         voiceIndex < voiceCount;
         ++voiceIndex) {
        clearProjectionLayer(*current);
        for (uint8_t candidateIndex = 0U;
             candidateIndex < workspace.candidateCount;
             ++candidateIndex) {
            const auto& candidate =
                workspace.candidates[candidateIndex];
            if (!directionAllows(
                    direction,
                    candidate.semitones,
                    workspace.desired[voiceIndex]
                )) {
                continue;
            }

            for (uint8_t previousIndex = 0U;
                 previousIndex < candidateIndex;
                 ++previousIndex) {
                const auto& previousState =
                    (*previous)[previousIndex];
                if (!previousState.valid ||
                    previousState.semitones[voiceIndex - 1U] >=
                        candidate.semitones) {
                    continue;
                }

                ProjectionState candidateState = previousState;
                appendProjectionVoice(
                    candidateState,
                    voiceIndex,
                    candidate,
                    workspace
                );
                if (betterState(
                        candidateState,
                        (*current)[candidateIndex],
                        static_cast<uint8_t>(voiceIndex + 1U)
                    )) {
                    (*current)[candidateIndex] = candidateState;
                }
            }
        }
        std::swap(previous, current);
    }

    for (uint8_t candidateIndex = 0U;
         candidateIndex < workspace.candidateCount;
         ++candidateIndex) {
        const auto& candidate = (*previous)[candidateIndex];
        if (candidate.valid &&
            betterState(candidate, best, voiceCount)) {
            best = candidate;
        }
    }
    return best;
}

FLASHMEM uint8_t resolvedScaleRoot(
    uint8_t rootNote,
    StepSequencerScaleSettings scale
) {
    scale.clamp();
    return resolveScaleNote(rootNote, scale).outputNote;
}

FLASHMEM uint8_t normalizedProjectionRoot(uint8_t rootNote) {
    // 36..47 leaves enough headroom for the largest canonical 31-degree
    // pentatonic interval while preserving the source pitch class.
    return static_cast<uint8_t>(36U + (rootNote % 12U));
}

FLASHMEM int16_t intervalAsSemitones(
    int16_t interval,
    bool usesScaleDegrees,
    uint8_t resolvedRoot,
    StepSequencerScaleSettings scale,
    bool& rangeLimited
) {
    if (!usesScaleDegrees) return interval;

    const uint8_t note = moveByScaleDegrees(
        resolvedRoot,
        static_cast<int8_t>(std::clamp<int16_t>(
            interval,
            std::numeric_limits<int8_t>::min(),
            std::numeric_limits<int8_t>::max()
        )),
        scale
    );
    if ((note == 0U && interval < 0) ||
        (note == 127U && interval > 0)) {
        rangeLimited = true;
    }
    return static_cast<int16_t>(
        static_cast<int16_t>(note) -
        static_cast<int16_t>(resolvedRoot)
    );
}

FLASHMEM StepSequencerScaleConstraintMode prepareWorkspace(
    ProjectionWorkspace& workspace,
    const StepSequencerChordFormula& source,
    uint8_t voiceCount,
    StepSequencerScaleSettings sourceScale,
    StepSequencerScaleSettings targetScale,
    uint8_t sourceRootNote,
    uint8_t targetRootNote,
    bool sourceUsesScaleDegrees,
    bool targetUsesScaleDegrees,
    bool& rangeLimited
) {
    workspace.candidateCount = 0U;
    workspace.desired.fill(0);
    const auto direction = targetUsesScaleDegrees
        ? targetScale.mode
        : StepSequencerScaleConstraintMode::ConstrainNearest;

    const uint8_t sourceRoot = sourceUsesScaleDegrees
        ? resolvedScaleRoot(
              normalizedProjectionRoot(sourceRootNote),
              sourceScale
          )
        : normalizedProjectionRoot(sourceRootNote);
    const uint8_t targetRoot = targetUsesScaleDegrees
        ? resolvedScaleRoot(
              normalizedProjectionRoot(targetRootNote),
              targetScale
          )
        : normalizedProjectionRoot(targetRootNote);

    workspace.desired[0] = 0;
    for (uint8_t index = 1U; index < voiceCount; ++index) {
        workspace.desired[index] = intervalAsSemitones(
            source.intervals[index],
            sourceUsesScaleDegrees,
            sourceRoot,
            sourceScale,
            rangeLimited
        );
    }

    int16_t previousSemitones = std::numeric_limits<int16_t>::min();
    for (uint8_t interval = 1U; interval <= MAX_INTERVAL; ++interval) {
        bool candidateRangeLimited = false;
        const int16_t semitones = intervalAsSemitones(
            interval,
            targetUsesScaleDegrees,
            targetRoot,
            targetScale,
            candidateRangeLimited
        );
        if (semitones <= 0 || semitones == previousSemitones) {
            continue;
        }
        workspace.candidates[workspace.candidateCount++] =
            ProjectionCandidate{
                .interval = interval,
                .semitones = semitones,
                .rangeLimited = candidateRangeLimited,
            };
        previousSemitones = semitones;
    }
    return direction == StepSequencerScaleConstraintMode::Free
        ? StepSequencerScaleConstraintMode::ConstrainNearest
        : direction;
}

FLASHMEM bool selectedProjectionRangeLimited(
    const ProjectionWorkspace& workspace,
    const ProjectionState& selected,
    uint8_t voiceCount
) {
    if (!selected.valid) return false;
    for (uint8_t voice = 1U; voice < voiceCount; ++voice) {
        for (uint8_t candidate = 0U;
             candidate < workspace.candidateCount;
             ++candidate) {
            const auto& choice = workspace.candidates[candidate];
            if (choice.interval != selected.intervals[voice]) continue;
            if (choice.rangeLimited) return true;
            break;
        }
    }
    return false;
}

FLASHMEM StepSequencerChordFormula projectedFormula(
    const ProjectionState& selected,
    uint8_t voiceCount,
    bool targetUsesScaleDegrees
) {
    StepSequencerChordFormula formula{};
    if (!selected.valid) return formula;

    formula.valid = true;
    formula.intervalUsesScaleDegrees = targetUsesScaleDegrees;
    formula.count = voiceCount;
    formula.harmony = StepSequencerChordHarmony::Custom;
    for (uint8_t index = 0; index < voiceCount; ++index) {
        formula.intervals[index] = selected.intervals[index];
    }
    return formula;
}

FLASHMEM EncodedFormula encodeFormula(
    const StepSequencerChordFormula& formula,
    StepSequencerChordSpec style,
    bool targetUsesScaleDegrees
) {
    EncodedFormula encoded{};
    if (!formula.valid || formula.count == 0U) return encoded;

    const auto harmony = recognizeChordFormula(
        formula,
        targetUsesScaleDegrees
    );
    const auto basis = targetUsesScaleDegrees
        ? StepSequencerChordIntervalBasis::ScaleDegrees
        : StepSequencerChordIntervalBasis::ChromaticSemitones;
    if (harmony != StepSequencerChordHarmony::Custom) {
        encoded.spec = StepSequencerChordSpec::semantic(
            harmony,
            formula.count,
            style.voicing(),
            std::min<uint8_t>(
                style.inversion(),
                static_cast<uint8_t>(formula.count - 1U)
            ),
            basis
        );
        encoded.valid = true;
    } else if (formula.count >= 2U &&
               formula.count <= MAX_CUSTOM_VOICES) {
        encoded.spec = StepSequencerChordSpec::semantic(
            StepSequencerChordHarmony::Custom,
            formula.count,
            style.voicing(),
            std::min<uint8_t>(
                style.inversion(),
                static_cast<uint8_t>(formula.count - 1U)
            ),
            basis
        );
        std::array<uint8_t, MAX_CUSTOM_VOICES> intervals{};
        for (uint8_t index = 1U; index < formula.count; ++index) {
            if (formula.intervals[index] <= 0 ||
                formula.intervals[index] > MAX_INTERVAL) {
                return EncodedFormula{};
            }
            intervals[index] =
                static_cast<uint8_t>(formula.intervals[index]);
        }
        encoded.spec.setCustomIntervals(intervals);
        encoded.spec = canonicalizeChordSpec(
            encoded.spec,
            targetUsesScaleDegrees
        );
        encoded.valid = true;
    }

    if (encoded.valid) {
        encoded.spec.strum = style.strum;
        encoded.spec.velocityCurve = style.velocityCurve;
        encoded.spec.clamp();
    }
    return encoded;
}

FLASHMEM bool projectedSemitonesAreExact(
    const ProjectionState& selected,
    const ProjectionWorkspace& workspace,
    uint8_t voiceCount
) {
    if (!selected.valid) return false;
    for (uint8_t index = 0U; index < voiceCount; ++index) {
        if (selected.semitones[index] != workspace.desired[index]) {
            return false;
        }
    }
    return true;
}

}  // namespace

FLASHMEM StepSequencerChordProjection projectChordSpec(
    StepSequencerChordSpec spec,
    StepSequencerScaleSettings sourceScale,
    StepSequencerScaleSettings targetScale,
    uint8_t sourceRootNote,
    uint8_t targetRootNote,
    bool sourceUsesScaleDegrees,
    bool targetUsesScaleDegrees,
    StepSequencerChordProjectionWorkspace& workspace
) {
    StepSequencerChordProjection result{};
    spec.clamp();
    result.spec = spec;
    result.sourceFormula = resolveChordFormula(
        spec,
        sourceUsesScaleDegrees
    );
    if (!result.sourceFormula.valid) return result;

    result.valid = true;
    if (sourceUsesScaleDegrees == targetUsesScaleDegrees) {
        result.spec.setIntervalBasis(
            targetUsesScaleDegrees
                ? StepSequencerChordIntervalBasis::ScaleDegrees
                : StepSequencerChordIntervalBasis::ChromaticSemitones
        );
        result.targetFormula = resolveChordFormula(
            result.spec,
            targetUsesScaleDegrees
        );
        result.exact = true;
        result.changed = !chordSpecsEqual(spec, result.spec);
        return result;
    }

    const uint8_t voiceCount = result.sourceFormula.count;
    if (voiceCount == 1U) {
        result.spec.setIntervalBasis(
            targetUsesScaleDegrees
                ? StepSequencerChordIntervalBasis::ScaleDegrees
                : StepSequencerChordIntervalBasis::ChromaticSemitones
        );
        result.targetFormula = resolveChordFormula(
            result.spec,
            targetUsesScaleDegrees
        );
        result.exact = true;
        result.changed = !chordSpecsEqual(spec, result.spec);
        return result;
    }
    if (voiceCount > MAX_CUSTOM_VOICES) {
        result.valid = false;
        result.adapted = true;
        result.rangeLimited = true;
        return result;
    }

    bool searchRangeLimited = false;
    const auto requestedDirection = prepareWorkspace(
        workspace,
        result.sourceFormula,
        voiceCount,
        sourceScale,
        targetScale,
        sourceRootNote,
        targetRootNote,
        sourceUsesScaleDegrees,
        targetUsesScaleDegrees,
        searchRangeLimited
    );
    auto selected = runProjection(
        workspace,
        voiceCount,
        requestedDirection
    );
    if (!selected.valid &&
        requestedDirection !=
            StepSequencerScaleConstraintMode::ConstrainNearest) {
        selected = runProjection(
            workspace,
            voiceCount,
            StepSequencerScaleConstraintMode::ConstrainNearest
        );
        result.directionLimited = selected.valid;
    }

    const auto formula = projectedFormula(
        selected,
        voiceCount,
        targetUsesScaleDegrees
    );
    const auto encoded = encodeFormula(
        formula,
        spec,
        targetUsesScaleDegrees
    );
    result.rangeLimited = searchRangeLimited;
    if (!encoded.valid) {
        result.valid = false;
        result.adapted = true;
        result.rangeLimited = true;
        return result;
    }

    result.spec = encoded.spec;
    result.targetFormula = resolveChordFormula(
        result.spec,
        targetUsesScaleDegrees
    );
    result.rangeLimited =
        result.rangeLimited ||
        selectedProjectionRangeLimited(
            workspace,
            selected,
            voiceCount
        );
    result.exact =
        !result.directionLimited &&
        projectedSemitonesAreExact(
            selected,
            workspace,
            voiceCount
        );
    result.adapted =
        result.directionLimited || !result.exact;
    result.changed = !chordSpecsEqual(spec, result.spec);
    return result;
}

}  // namespace oc::note::sequencer
