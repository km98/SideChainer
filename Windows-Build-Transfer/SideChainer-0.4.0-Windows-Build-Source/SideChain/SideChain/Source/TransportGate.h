/*
    SideChainer - transport gating (0.4.0).

    Pure, host-independent decision logic:

        transportActive = hostPlaying OR hostRecording

    In the 0.4.0 internal-trigger architecture the beat scheduler runs only
    while the transport is active (the DAW timeline cannot advance while
    stopped, so stopped playback has no musical meaning). The former
    "sidechain while stopped" override was removed with the external
    sidechain bus: there is no signal to analyse while stopped, and a
    stopped timeline cannot advance a beat grid.

    The processor feeds this from AudioPlayHead::getCurrentPosition()
    (isPlaying / isRecording; on this JUCE version isRecording implies
    isPlaying, so the OR is still exactly the documented condition).

    Kept as a free function on plain values so it is unit-testable without
    a host and free on the audio thread.
*/

#pragma once

namespace sid::transport
{

// True when the host transport is actively rolling (playing or recording).
inline bool transportActive (bool hostPlaying, bool hostRecording) noexcept
{
    return hostPlaying || hostRecording;
}

} // namespace sid::transport
