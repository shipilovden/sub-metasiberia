# XR Pose Trace Analysis

The Qt/OpenXR client writes `%APPDATA%/Cyberspace/xr_pose_trace.csv` during XR runs.
This file is the primary way to debug:

- headset neutral alignment
- floor height / STAGE vs LOCAL behaviour
- raw vs corrected pitch
- left/right eye symmetry
- whether the problem is in OpenXR calibration or in avatar/model grounding

## Run

```bash
python C:/programming/substrata/scripts/analyze_xr_pose_trace.py
```

Or pass an explicit file:

```bash
python C:/programming/substrata/scripts/analyze_xr_pose_trace.py C:/Users/<user>/AppData/Roaming/Cyberspace/xr_pose_trace.csv
```

## Important interpretation detail

In the current trace format, `pitch_deg=90` means "looking at the horizon".
So:

- `pitch_deg < 90` means looking up
- `pitch_deg > 90` means looking down
- `pitch_from_horizon_deg = pitch_deg - 90`

The helper script converts to this more intuitive `pitch_from_horizon_deg` view.

## Recommended capture procedure

1. Launch the XR-enabled Qt client.
2. Put the headset on normally.
3. Wait until the OpenXR session is `FOCUSED`.
4. Look straight at the horizon with the headset worn correctly.
5. Press `Home` or `End` to request manual XR recenter.
6. Keep the head still for about half a second.
7. Move naturally for a few seconds, then inspect the trace.

## XR teleport locomotion

The Qt/OpenXR client now also supports basic `teleport locomotion` on top of the same OpenXR action backend:

- hold the controller `trigger` or `select` action to show the teleport beam
- green landing marker means the surface is walkable and teleport is valid
- blue landing marker means the current surface or ray end is not a valid teleport target
- release the button to teleport

The implementation currently prefers the right hand when both hands are pressed, and falls back to the left hand otherwise.

## XR smooth locomotion

The same OpenXR action backend now also exposes controller `move2d` input for smooth locomotion:

- left thumbstick or trackpad moves the player in the direction of the current HMD yaw
- right thumbstick or trackpad `x` rotates the player smoothly
- supported bindings now cover Oculus Touch, Valve Index, HTC Vive wand trackpad, HTC Vive Cosmos, HTC Vive Focus 3, and Microsoft motion controllers

This means that if teleport appears but sticks do not move the player, the first thing to inspect is the live XR diagnostics block for `move2d_active` and `move2d_value` on each hand.

## XR controller visibility

The Qt/OpenXR client now also draws simple local controller proxies from the tracked hand poses:

- the left controller proxy is cyan
- the right controller proxy is orange
- they stay visible after the runtime hands/controllers overlay disappears on focus hand-off
- short pose dropouts are now held briefly instead of hiding the proxies immediately, to avoid visible flicker

This is intentional: some runtimes show their own temporary controller overlay before the app is fully focused, then stop drawing it once the application takes ownership of XR rendering.

## What "good" looks like

- A stable focused window appears after recenter.
- In that window, `world pitch from horizon` is close to `0 deg`.
- Standing users usually show `world_z` around `1.5-1.8 m`.
- Left/right eye world pitch delta stays near zero.

## How to distinguish bug classes

- Raw pitch is wrong, but corrected world pitch is near zero:
  head neutralisation works; the physical headset mount or natural resting angle is being corrected.
- Stable world height is reasonable, but avatar still looks sunk or tiny:
  the likely issue is avatar rig grounding or eye-height assumptions, not OpenXR floor space.
  Imported avatars now try to anchor from `LeftEye` / `RightEye` bones first, then `Head`, and only then fall back to `1.67 m`.
- Trace looks good, but the HMD image still feels heavily zoomed or cropped:
  inspect the XR stereo render path, especially whether the eye swapchain resolution is also driving the engine main viewport/offscreen buffers.
  If XR renders with a desktop-sized main framebuffer and only the GL viewport changes to the eye size, the headset can look strongly zoomed even though tracking and recenter are correct.
- Trace looks good and scale feels right, but you can see parts of your own face/head in the headset:
  do not push the XR camera forward as a workaround.
  The correct fix is to keep the HMD camera at the tracked eye position and suppress the local avatar head/body mesh for first-person XR rendering.
- No stable neutral focused window:
  recenter probably happened too early, while not focused, or with the headset low / tilted.
- Left/right eye world transforms disagree:
  inspect per-eye pose composition or projection path.
