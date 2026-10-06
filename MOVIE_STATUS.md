# Movie feature: status and remaining work

Branch: `video`. The user manual is `docs/movie_maker.md`, the pull request text is `PR_DESCRIPTION.md`.

## How to build and test

- Build: `cmake --build build --target viamd -j8` and `--target viamd_test`
- Tests: `./build/bin/viamd_test` (102 tests, all passing in a Release build)
- ffmpeg is not installed in the dev environment; the frame sink tests use a fake script.
- The GUI could not be run while the code was written, so everything under "Untested in the GUI" needs a manual check.

## Where things live

- Movie window, timeline window, recording, preview, overlay drawing: `src/main.cpp` (`draw_movie_window`, `draw_movie_strip`, `movie_*`)
- Movie state: `src/viamd.h` (`ApplicationState::movie`)
- Async frame writer: `src/frame_sink.{h,cpp}`
- Keyed look parameters, undo history, render range, frame scaling, time left: `src/movie_keys.{h,cpp}`
- Overlay maths (fade, scale bar): `src/movie_overlay.{h,cpp}`
- Camera path evaluation: `src/gfx/camera_utils.{h,cpp}`
- Workspace save/load: `src/viamd.cpp` (`[Movie]`, `[MovieOverlay]` sections)
- Parameter ids in `movie_param_table` (main.cpp) are saved in workspaces: never renumber, add at the end.

## Done

- Undo/redo of everything on the timeline, keyed look parameters, per-segment easing, seamless loop.
- Camera follows a target (middle of a selection); a key can also look at, and track, one atom.
- Depth of field focus separate from the camera distance (look-at point, a keyable distance, or the follow target).
- Overlays on recorded frames: text, time stamp, scale bar, with time range and fades; shown in the viewport too.
- Keyframe table: resizable and scrollable, rows reordered by dragging their number.
- Trajectory can play backward, with frame keys or with a start frame after the end frame.
- Movie length is the master value; movable trajectory start/end anchors; old workspaces are migrated (`Timeline=2`).
- Movie Timeline window with aligned tracks and real axes, opened from the Windows menu.
- Render ergonomics (plan item 4): frame **Scale**, **Samples per frame**, **Render only a range** (PNG numbers stay those of the whole movie), **Pause/Resume**, time-left estimate, optional workspace copy next to the movie (`prefix.via`).
- Settings for the above are saved in the workspace (`ResScale`, `AaSamples`, `SaveCopy`, `RenderRange`).
- The face-on default view test now uses realistic (slightly jittered) coordinates; see "Known limits".

## Untested in the GUI

- Render ergonomics: Pause/Resume (the recording must carry on from the same frame, with no duplicate), a range recording (PNG numbers and the MP4 length), Scale 25 % (frame size even, overlays scale), Samples per frame, the time-left estimate, and the workspace copy (the open workspace's name must not change).
- Follow target and Look at atom: the camera tracks them through the trajectory; loading an old workspace still works.
- Overlays in a recorded video: they are drawn through ImGui's OpenGL backend straight into the G-buffer before read back. Check that text and bar appear, are not upside down, and fade correctly, also with more than one sample per frame.
- Movie timing: drag the blue anchors, set trajectory frames on camera keys, double the movie length and verify the same pacing at half speed. Undo/redo restores overlay timing.
- Backward trajectory (reversed frame range, reversed frame keys) and drag and drop reordering of key rows.
- Timeline window: linked time zoom/pan, row resizing, hiding tracks, frame-pin dragging.
- Not tested on Windows or macOS (the ffmpeg pipe uses `popen`/`_popen`).

## Known limits

- An existing keyframe cannot be switched to follow the target; add a new key at the same time with the option on (it replaces the old one).
- The camera path drawn in the viewport does not show the follow motion.
- The follow target itself is not part of undo.
- Where a frame curve turns around, the trajectory slows to a stop. For a hard reversal, add a key at the turn-around frame with Linear easing.
- Overlay text is single style (no background plate, only a shadow).
- Default view (existing code from master): a perfectly symmetric flat molecule (exact ideal benzene geometry) can settle about 10 degrees off face-on, because the visibility scores of nearby directions tie. Real coordinates are not exactly symmetric and are fine.
- In "Follow target" depth of field mode, focus uses the global follow target, not a key's own Look at atom.
- Distance and field of view can only be moved in time on the timeline; their values are edited in the table or the viewport.

## Still to do (agreed plan order)

Plan item 4 (render ergonomics) is done apart from optional output formats and a separate settings file (the workspace copy covers reproducing a render).

- Optional: H.265 and WebM presets, transparent background (lowest value for MD movies).

5. Strip comfort
   - Zoom and pan on the strip (the Movie Timeline window has linked zoom and pan; check whether anything is left for the old strip)
   - Snap to output frames
   - Copy and paste of keys
   - "Fly to current selection" button that adds a key framing the selection
6. Representation keys (hardest, do last)
   - Stable representation ids first (representations only have an array index today)
   - Keyed values reuse the keyed-parameter machinery; discrete changes (type, filter) cannot be interpolated
   - Decide how manual edits in the Representations window interact with keys during recording
   - Cost: recolouring per frame can be heavy on large systems
7. Scene lock during recording
   - Cheap option: lock the Representations window while recording. The real fix (editing during a recording) is large.

## Ideas not yet planned

- Keyframe table column to toggle follow on an existing key
- Draw the follow-aware camera path in the viewport
- Overlay background plate, per-overlay font size in points, image/logo overlay
