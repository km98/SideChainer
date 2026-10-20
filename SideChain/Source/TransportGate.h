/*
    PumpCurve - transport gating (0.4.0; user switch restored in 0.4.2).

    Pure, host-independent decision logic:

        transportActive = hostPlaying OR hostRecording
        triggerAllowed  = NOT gateEnabled OR transportActive

    `transportActive` is the raw host fact and is unchanged since 0.4.0: the
    processor feeds it from AudioPlayHead::getCurrentPosition() (isPlaying /
    isRecording; on this JUCE version isRecording implies isPlaying, so the OR
    is exactly the documented condition).

    `triggerAllowed` adds the user-facing switch that 0.4.2 restores to the
    interface as "ACTIVE ONLY DURING PLAY/REC":

      gate ENABLED (default) -> internal beats are scheduled only while the
          host transport plays or records; a stopped timeline produces no
          triggers (0.4.0 behaviour, now a visible choice again).

      gate DISABLED -> the beat clock free-runs at the host tempo whether or
          not the transport rolls, so the effect operates in a stopped DAW.

    The switch is deliberately NOT the pre-0.4.0 "sidechainWhileStopped"
    parameter restored under its old id: that parameter encoded the INVERSE
    condition with the opposite default, so re-using its id would silently
    flip behaviour for sessions saved by 0.3.x. The preference itself lives in
    the processor's `transportGate` parameter (see PluginProcessor.h), which
    serializes with plugin state and defaults to ON for older sessions.

    Kept as free functions on plain values so they are unit-testable without a
    host and free on the audio thread.
*/

#pragma once

namespace sid::transport
{

// True when the host transport is actively rolling (playing or recording).
inline bool transportActive (bool hostPlaying, bool hostRecording) noexcept
{
    return hostPlaying || hostRecording;
}

// True when the user-facing switch allows the internal beat clock to run.
inline bool triggerAllowed (bool gateOnlyDuringTransport,
                            bool hostPlaying,
                            bool hostRecording) noexcept
{
    return ! gateOnlyDuringTransport || transportActive (hostPlaying, hostRecording);
}

} // namespace sid::transport
