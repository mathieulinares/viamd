# Movie feature: status and remaining work

Branch: `video`. Last commit at the time of writing: `dbbea164`. Nothing is pushed beyond `658d8c22` (`origin/video`).

## How to build and test

- Build: `cmake --build build --target viamd -j8` and `--target viamd_test`
- Tests: `./build/bin/viamd_test`
- Known failing test, independent of the movie work: `viamd_camera.default_view_of_a_planar_molecule_is_face_on`
- ffmpeg is not installed in the dev environment; the frame sink tests use a fake script.
- The GUI could not be run while developing, so everything marked "untested" below needs a manual check.

## Where things live

- Movie window, timeline window, recording, preview, overlays drawing: `src/main.cpp` (`draw_movie_window`, `draw_movie_strip`, `movie_*`)
- Movie state: `src/viamd.h` (`ApplicationState::movie`)
- Async frame writer: `src/frame_sink.{h,cpp}`
- Keyed look parameters and undo history: `src/movie_keys.{h,cpp}`
- Overlay maths (fade, scale bar): `src/movie_overlay.{h,cpp}`
- Camera path evaluation: `src/gfx/camera_utils.{h,cpp}` (`camera_keyframes_evaluate`, `keyed_curve_evaluate`, `camera_keyframes_evaluate_frame`)
- Workspace save/load: `src/viamd.cpp` (`[Movie]`, `[MovieOverlay]` sections)
- Parameter ids in `movie_param_table` (main.cpp) are saved in workspaces: never renumber, add at the end.

## Done

1. Undo/redo of keyframe edits, keyed look parameters (background, ambient occlusion, exposure, depth of field blur, clipping planes), per-segment easing (smooth, ease in/out, linear, hold), seamless loop.
2. Camera follows a target: "Set Follow Target" takes the current selection, keys made with "keys follow target" look at the middle of it. Blends between following and fixed keys.
3. Overlays on recorded frames: text, time stamp, scale bar, with time range and fades. Also shown in the viewport at the preview time.
4. Keyframe table: rows are reordered by dragging the number (times stay with their places in the list).
5. Trajectory can play backward, both with frame keys (a key with a lower frame than the previous) and with a start frame after the end frame.
7. Depth of field focus is separate from the camera distance: Settings > Depth of Field > Focus is "Look-at point" (as before), "Distance" (a number, keyable as the parameter "Focus distance") or "Follow target" (the middle of the follow target, wherever the camera looks). In the keyframe table, "Look at" then a click on an atom makes a key look at (and track) that atom, and "Update position" moves its eye to the current view while keeping what it looks at. There is a readout of eye / look-at / distance / focus at the preview time, and the viewport shows the focus plane (magenta). Workspace: `DofFocusMode`, `DofFocusDistance`.
6. Movie Timeline opens explicitly from the window menu and stays closed on startup/workspace load. Aligned, height-resizable tracks show trajectory frames, camera distance (in the preferred length unit), field of view (degrees), and a selected look parameter. Time zoom/pan and the playhead are shared. Camera tracks can be hidden; distance/FOV markers edit timing only. The old shaded trajectory band is removed; labelled start/end anchors remain in the trajectory track.
8. Movie length is set explicitly. The trajectory defaults to filling it; movable blue start/end anchors allow a still-frame fly-over before playback and a hold afterward. Camera keyframes with trajectory frames bend the speed between the anchors. Changing the length scales camera/parameter key times, anchors, overlays and fades, and the playhead; frame values and spin counts stay unchanged. Timing and overlays participate in undo/redo. Old workspace timing is migrated on load; new files store `Timeline=2` instead of `DurationAuto`.

## Untested in the GUI

- Follow target: set a target, add keys at different frames with "with trajectory frame", play the preview. Check the camera tracks the target and that loading an old workspace still works.
- Overlays in the recorded video: they are drawn through ImGui's OpenGL backend straight into the G-buffer before read back. Record a short clip and check that text and bar appear, are not upside down, and fade correctly. Also check with more than one anti-aliasing sample per frame.
- Overlay positions in the viewport preview are only approximate when the viewport has a different shape from the movie.
- Movie timing: drag the blue anchors, set trajectory frames on camera keys, then double the movie length and verify the same pacing at half speed. Check undo/redo restores overlay timing too.
- Backward trajectory: a reversed frame range and reversed frame keys.
- Drag and drop reordering of keyframe rows.
- Timeline GUI: verify linked time zoom/pan, row resizing, hiding tracks, frame-pin dragging and read-only distance/FOV values. Load a workspace with `MovieTimeline=1` and verify it stays closed, then open it from the window menu.

## Known limits

- An existing keyframe cannot be switched to follow the target; add a new key at the same time with the option on (it replaces the old one).
- The camera path drawn in the viewport does not show the follow motion.
- The follow target itself is not part of undo.
- Where a frame curve turns around, the trajectory slows to a stop. For a hard reversal, add a key at the turn-around frame with Linear easing.
- Overlay text is single style (no background plate, only a shadow).

## Still to do (agreed plan order)

4. Render ergonomics and output
   - Render only a time range, or one segment, so a fix does not need the whole movie again
   - Pause button and time-remaining estimate while recording
   - Lower-resolution preview
   - Configurable number of anti-aliasing samples per frame
   - Save the movie settings to a file so a render can be reproduced
   - Optional: H.265 and WebM presets, transparent background (lowest value for MD movies)
5. Strip comfort
   - Zoom and pan on the strip
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

