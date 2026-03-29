## XR Avatar Visibility Note

Date: 2026-03-29

### Problem

On the Qt/OpenXR client, the local avatar was disappearing in the normal desktop client path on machines where XR could auto-start from the active OpenXR runtime state.

The user-visible symptom looked like:

- launch client without an explicitly connected VR cable
- client enters its XR-capable path automatically
- local avatar does not appear in the normal non-VR experience

### Root Cause

The local-avatar visibility rule was widened too much.

The problematic logic used:

- `thirdPersonEnabled() && !isXRActive()`

This mixed two different concerns:

1. camera presentation mode
2. whether an XR session/runtime is active

That meant the local avatar could be suppressed just because XR was considered active, even though the desired product rule was:

- hide the local avatar for first-person presentation
- do not treat XR-active as a blanket reason to remove the local avatar in the normal desktop avatar path

### Relevant History

- `c85bf784` / `8c219f82`: `Hide-local-avatar-mesh-in-XR-first-person`
- `c69e34b4` / `ecd85bbf`: `Fix-XR-local-avatar-visibility-regression`
- `c3715596`: `Refine Qt OpenXR controller alignment and HMD calibration`

The March 29 change reintroduced the stricter XR-gated condition in `GUIClient.cpp`.

### Fix

Restore the visibility decision to camera-mode semantics in the two local-avatar load/update sites:

- `gui_client/GUIClient.cpp`
- remove `&& !isXRActive()` from the local `should_show_our_avatar_model` checks

### Rule To Avoid Repeating This

When changing local-avatar visibility:

- gate visibility by presentation/camera mode first
- do not use XR runtime/session activity alone as the deciding condition
- if XR-specific hiding is needed, document the exact scenario explicitly and test both:
  - desktop launch with no active VR usage
  - actual immersive XR session
