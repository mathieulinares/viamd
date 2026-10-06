# Movie feature: status and remaining work

Branch: `video`. The user manual is `docs/movie_maker.md`, the pull request text is `PR_DESCRIPTION.md`.

## How to build and test

- Build: `cmake --build build --target viamd -j8` and `--target viamd_test`
- Tests: `./build/bin/viamd_test` (106 tests, all passing in a Release build)
- ffmpeg is not installed in the dev environment; the frame sink tests use a fake script.
- The GUI could not be run while the code was written, so everything under "Untested in the GUI" needs a manual check.

## Where things live

- Movie window, timeline window, recording, preview, overlay drawing: `src/main.cpp` (`draw_movie_window`, `draw_movie_strip`, `movie_*`)
- Movie state: `src/viamd.h` (`ApplicationState::movie`)
- Async frame writer: `src/frame_sink.{h,cpp}`
- Keyed look parameters and representation keys, undo history, render range, frame scaling, snapping, time left: `src/movie_keys.{h,cpp}`
- Overlay maths (fade, scale bar): `src/movie_overlay.{h,cpp}`
- Camera path evaluation: `src/gfx/camera_utils.{h,cpp}`
- Workspace save/load: `src/viamd.cpp` (`[Movie]`, `[MovieOverlay]`, `[Representation]` sections)
- Parameter ids in `movie_param_table` (main.cpp) and the numbers of `RepProp` (movie_keys.h) are saved in workspaces: never renumber, add at the end.

## Done

- Undo/redo of everything on the timeline, keyed look parameters, per-segment easing, seamless loop.
- Camera follows a target (middle of a selection); a key can also look at, and track, one atom.
- Depth of field focus separate from the camera distance (look-at point, a keyable distance, or the follow target).
- Overlays on recorded frames: text, time stamp, scale bar, with time range and fades; shown in the viewport too.
- Keyframe table: resizable and scrollable, rows reordered by dragging their number.
- Trajectory can play backward, with frame keys or with a start frame after the end frame.
- Movie length is the master value; movable trajectory start/end anchors; old workspaces are migrated (`Timeline=2`).
- Movie Timeline window with aligned tracks and real axes, linked zoom and pan, opened from the Windows menu.
- Render ergonomics: frame **Scale**, **Samples per frame**, **Render only a range**, **Pause/Resume**, time-left estimate, optional workspace copy next to the movie.
- Strip comfort (plan item 5): **Snap to frames** for dragged times, **Copy** / **Paste Keyframe**, **Key on Selection** (frames the selected atoms, periodic images placed together).
- Representation keys (plan item 6): representations have a stable `id` (saved, never reused); keys for **Visible** (held, changes at the key), the scales, **Tint scale** and **Saturation**; removing a representation removes its keys; the values go back when keys let go or a recording ends.
- The Representations window is locked while recording (the cheap form of plan item 7).

## Untested in the GUI

- Representation keys: key Visible at two times and scrub/preview; key a scale and a saturation; remove a representation that has keys; duplicate one (the copy must not share keys); save and reload a workspace (ids and keys survive, an old workspace without ids still loads and keys can be added); the values must go back after a recording.
- Key on Selection: select a molecule split over the periodic boundary and check it is framed whole; check the result with another viewing direction.
- Snap to frames: drag keys, anchors and the playhead; add a key with the playhead between frames; Add Orbit with snapping on (the orbit's end key must be where expected).
- Copy / Paste Keyframe (a pasted key replaces one at the same time, and keeps its spin, ease and follow settings).
- Representations window while recording: locked, and unlocked afterwards.
- Render ergonomics: Pause/Resume (the recording must carry on from the same frame, with no duplicate), a range recording (PNG numbers and the MP4 length), Scale 25 % (frame size even, overlays scale), Samples per frame, the time-left estimate, and the workspace copy (the open workspace's name must not change).
- Follow target and Look at atom: the camera tracks them through the trajectory; loading an old workspace still works.
- Overlays in a recorded video: they are drawn through ImGui's OpenGL backend straight into the G-buffer before read back. Check that text and bar appear, are not upside down, and fade correctly, also with more than one sample per frame.
- Movie timing: drag the blue anchors, set trajectory frames on camera keys, double the movie length and verify the same pacing at half speed. Undo/redo restores overlay timing.
- Backward trajectory (reversed frame range, reversed frame keys) and drag and drop reordering of key rows.
- Timeline window: linked time zoom/pan, row resizing, hiding tracks, frame-pin dragging.
- Not tested on Windows or macOS (the ffmpeg pipe uses `popen`/`_popen`).

## Known limits

- Representation keys are edited in a table in the Movie window; they are not on the timeline tracks. Only the properties listed above can be keyed: not the type, filter, colour mapping, base colour or the electronic structure settings (those cannot be blended, or are expensive to redo every frame).
- Tint scale and saturation recolor the atoms of the representation every frame while they change: slow for very large systems.
- Representation ids are assigned in the order they are created; a workspace written before they existed gets ids on load.
- An existing keyframe cannot be switched to follow the target; add a new key at the same time with the option on (it replaces the old one).
- The camera path drawn in the viewport does not show the follow motion.
- The follow target itself is not part of undo.
- Where a frame curve turns around, the trajectory slows to a stop. For a hard reversal, add a key at the turn-around frame with Linear easing.
- Overlay text is single style (no background plate, only a shadow).
- Default view (existing code from master): a perfectly symmetric flat molecule (exact ideal benzene geometry) can settle about 10 degrees off face-on, because the visibility scores of nearby directions tie. Real coordinates are not exactly symmetric and are fine.
- In "Follow target" depth of field mode, focus uses the global follow target, not a key's own Look at atom.
- Distance and field of view can only be moved in time on the timeline; their values are edited in the table or the viewport.

## Still to do

- Plan item 7, the real fix: allow editing representations during a recording (large; the lock covers the need for now).
- Optional output: H.265 and WebM presets, transparent background (lowest value for MD movies).
- Representation keys on the timeline (a lane like the look parameter one), and keys for colours (base colour, tint colour).
- Keyboard shortcuts for copy and paste of keys.

## Ideas not yet planned

- Keyframe table column to toggle follow on an existing key
- Draw the follow-aware camera path in the viewport
- Overlay background plate, per-overlay font size in points, image/logo overlay
