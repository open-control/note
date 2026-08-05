# Canonical production source inventory for OpenControl Note consumers.
# Keep this list sorted and mirror it in library.json's PlatformIO srcFilter.
set(OC_NOTE_SOURCE_PATHS
    src/oc/note/clock/InternalClock.cpp
    src/oc/note/sequencer/StepSequencerChord.cpp
    src/oc/note/sequencer/StepSequencerChordPreset.cpp
    src/oc/note/sequencer/StepSequencerChordProjection.cpp
    src/oc/note/sequencer/StepSequencerChordSpec.cpp
    src/oc/note/sequencer/StepSequencerEngine.cpp
    src/oc/note/sequencer/StepSequencerExpander.cpp
    src/oc/note/sequencer/StepSequencerGraph.cpp
    src/oc/note/sequencer/StepSequencerPlaybackRegion.cpp
    src/oc/note/sequencer/StepSequencerState.cpp
)

set(OC_NOTE_SOURCES ${OC_NOTE_SOURCE_PATHS})
list(TRANSFORM OC_NOTE_SOURCES
    PREPEND "${CMAKE_CURRENT_LIST_DIR}/../")
