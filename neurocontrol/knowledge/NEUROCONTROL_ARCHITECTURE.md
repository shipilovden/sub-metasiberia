# NeuroControl architecture

The Qt client separates signal acquisition, signal display, research analysis, and avatar-command policy. EEG sources provide samples and source metadata through a common acquisition interface. The interface can display samples while diagnostics track connection state, sample rate, timestamps, and receiver arrival timing.

For LSL, source-provided timestamps and local monotonic arrival times are separate measurements. Diagnostics may assess timestamp quality, pauses, and observed receive rate; they do not silently replace source timestamps with a reconstructed clock.

Classifier evaluation and avatar commands are separate from observing a live LSL stream. Enabling one feature does not imply that another is connected. The user should inspect the active source and control state before an analysis or control action.
