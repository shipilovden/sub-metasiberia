# Native photo-mode regression

Standalone Qt test, without GUIClient, networking or real user settings. Uses a
temporary INI file, creates an invisible widget and optionally saves its preview.

```powershell
cmake -S scripts/photo_mode_tests -B C:/programming/substrata_photo_mode_tests `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_PREFIX_PATH=C:/programming/Qt/5.15.16-vs2022-64
cmake --build C:/programming/substrata_photo_mode_tests --config Release
$env:PATH = 'C:/programming/Qt/5.15.16-vs2022-64/bin;' + $env:PATH
$env:QT_QPA_PLATFORM = 'windows'
$env:QT_PLUGIN_PATH = 'C:/programming/Qt/5.15.16-vs2022-64/plugins'
C:/programming/substrata_photo_mode_tests/Release/photo_mode_tests.exe `
  C:/programming/substrata_photo_mode_tests/panel.png `
  C:/programming/substrata/resources/icons/lucide
```

Checks defaults, settings signals, saving/loading/resetting presets, live focus
readout without feedback, aspect ratio, immediate/timed capture and cancellation,
persistence of edits after selecting a preset, image toning/vignette/crop,
look presets, and recording/finalisation control states.

The same build also creates `photo_title_test.exe`. It compiles the production
dock-title helpers extracted from `MainWindow.cpp` at configure time, without
starting the client. Run with the Qt environment above:

```powershell
C:/programming/substrata_photo_mode_tests/Release/photo_title_test.exe `
  C:/programming/substrata_photo_mode_tests/title.png
```

Checks dark title pixels and light text in three palettes, docked/floating and
active/inactive colour groups, title translation, undock/redock and close buttons.
No real user settings or world/network connection are used.

Optional Windows encoder smoke (real Media Foundation encoder, no scene capture):

```powershell
cmake -S scripts/photo_mode_tests -B C:/programming/substrata_photo_mode_tests `
  -DGLARE_CORE_DIR=C:/programming/glare-core
cmake --build C:/programming/substrata_photo_mode_tests --config Release
C:/programming/substrata_photo_mode_tests/Release/photo_recording_test.exe `
  C:/programming/substrata_photo_mode_tests/recording-smoke.mp4
ffprobe -v error -show_entries stream=codec_name,width,height,r_frame_rate,nb_frames,duration `
  C:/programming/substrata_photo_mode_tests/recording-smoke.mp4
```

Choose a new output filename for every run (the test refuses to overwrite).
Expected: H.264, 320x240, 30 fps, approximately 1.1 seconds despite sparse input.
The top-left/top-right/bottom colours are red/green/blue, respectively. The test
also checks that an invalid destination reports failure instead of hanging.

After an authorised client launch, still check these integration flows:

- Camera HUD button and Window menu open the same right-hand dock; no legacy GL panel.
- With the map open, photo mode shares its right-hand dock area as a tab.
- Dock close disables photo mode, untoggles the camera HUD button and restores normal optics.
- Every camera mode, autofocus/manual focus, lens, blur, EV, saturation and roll affect the view.
- Resizing the view preserves crop/grid alignment; guides never intercept mouse input.
- PNG/JPEG (WebP only if installed) save to the selected directory, with correct crop and optional JSON.
- UI checkbox affects the saved world HUD; composition guides are never captured.
- Grid and crop guides are GL overlays, not child QWidgets covering the QGLWidget; verify the world stays visible.
- Bloom, toning, vignette and lens shifts affect preview and output; closing photo mode restores previous bloom and clears shifts.
- Record/stop creates a playable silent MP4; docking/closing photo mode stops recording and finalises the file. Check 720p/1080p, selected crop, filters, maximum duration and resizes.
- Gallery opens the chosen directory; upload opens the existing confirmation workflow.
- Closing photo mode cancels pending capture; reopening restores settings and does not show the old overlay on Escape.

World rendering, GL input, docking and real-world camera behavior are not proved
by these isolated tests. Full Qt client link and live scene checks remain separate.

## Pro photo-mode checks

The native panel now has six sections: optics, colour, effects, composition,
video and output. `photo_panel_pro_test.exe` checks the new controls, individual
resets, recipes, preset round trips, temporary before/after state, focus and
histogram signals, trajectory controls and compact layout. Audio always restores
disabled; enabling a saved preset cannot open a microphone.

```powershell
C:/programming/substrata_photo_mode_tests/Release/photo_panel_pro_test.exe
C:/programming/substrata_photo_mode_tests/Release/photo_path_test.exe
C:/programming/substrata_photo_mode_tests/Release/photo_frame_test.exe
C:/programming/substrata_photo_mode_tests/Release/photo_postprocess_test.exe
C:/programming/substrata_photo_mode_tests/Release/photo_postprocess_test.exe --qgl
C:/programming/substrata_photo_mode_tests/Release/photo_audio_test.exe `
  C:/programming/substrata_photo_mode_tests/audio-new-run
```

Use a new directory for each audio test. The audio test generates synthetic
PCM/frames, writes real H.264/AAC files and decodes them with Media Foundation.
It checks stereo/mixing, bounds/errors, sparse-frame timing, audio/video sync,
the direct engine-feed API, stop/destruction and unsupported process-loopback
errors. It never opens a microphone or records real user audio.

The GPU test uses hidden OpenGL contexts, not the actual client. It exercises the
production shader: neutral identity, effects, strength/compare bypass, Bradford
white balance, colour endpoints, LUT/cache/error handling, crop/guides, distorted
focus coordinates, resolution-aware grain and GL state restoration. `--qgl`
uses the Qt 5 QGLWidget context used by the native client. The geometry/path tests
check crop/resolution bounds, smooth interpolation, lens/focus and angle wrapping.

### Behavior and limits

- The same GPU adapter processes live view, photos and video. Guides, zebra and
  focus peaking are preview-only. Holding before/after never neutralises exports.
- White balance corrects the selected illuminant to neutral; 6500 K is identity.
  Artistic warm/cool tint remains separate. LUTs are 3D `.cube`, size 2–64,
  finite values in 0–1 and unit input domain; 1D/shaper/HDR LUTs are rejected.
- Peaking estimates image-edge contrast, not physical focus/depth. Histogram is
  an RGB sample of the graded image, refreshed at most four times per second.
- High-resolution output re-renders the scene, preserving the original camera
  field of view before crop. It does not enlarge a window screenshot. The full
  uncropped render is limited to 8192 pixels per side and 36 MP, plus GPU limits;
  an extreme crop at 8K may require selecting a lower output size.
- Output is currently SDR, not linear HDR/EXR. Halation is an artistic SDR
  highlight approximation. Genuine HDR requires a renderer API outside this repo.
- Camera paths hold up to 64 session-only keyframes, with smooth per-segment
  starts/stops and focus/lens/rotation interpolation. Loop runs forward/backward.
  They move the free camera, never the avatar. Escape stops playback; picking
  focus or selecting a different camera mode also stops it.
- World audio records the engine's final 48 kHz stereo mix, never other programs.
  Direct CEF browser audio is not part of that engine mix. Optional microphone
  capture requires explicit opt-in; microphone-device behavior needs a separate
  user-authorised live check. Video/audio options are fixed while recording.
- Hardware load can reduce capture cadence; the recorder repeats frames to
  preserve clip duration. Bounded queues fail explicitly rather than grow forever.

Additional live checks after the owner launches the new build: focus a surface
with telephoto/roll/lens shift/distortion, compare preview with cropped 4K output,
play a camera path while recording, verify world audio and explicit microphone
opt-in, stop/close during recording, and confirm restored view after every error.
These changes do not establish Web/SDL/XR support.
