# Movie feature: status and remaining work

Branch: `video`, pushed to `origin/video`; no pull request yet. The user manual (also the text for the GitHub wiki, which is a separate repository and is not updated from here) is `docs/movie_maker.md`, the pull request text is `PR_DESCRIPTION.md`, and `docs/examples/aspirin_phospholipase_movie.via` is a finished example that is updated with the features (its trajectory files are not in the repository).

## How to build and test

- Build: `cmake --build build --target viamd -j8` and `--target viamd_test`
- Tests: `./build/bin/viamd_test` (117 tests, all passing in a Release build)
- ffmpeg is not installed in the dev environment; the frame sink tests use a fake script.
- The GUI could not be run while the code was written, so everything under "Untested in the GUI" needs a manual check.

## Where things live

- Movie window, timeline window, recording, preview, overlay drawing: `src/main.cpp` (`draw_movie_window`, `draw_movie_strip`, `movie_*`)
- Movie state: `src/viamd.h` (`ApplicationState::movie`)
- Async frame writer: `src/frame_sink.{h,cpp}`
- Keyed look parameters and representation keys, undo history, render range, frame scaling, snapping, time left: `src/movie_keys.{h,cpp}`
- Overlay maths (fade, scale bar, the default logo overlay): `src/movie_overlay.{h,cpp}`; the logo is `icon/viamd_logo.png`, baked into `gen/viamd_logo.inl` by CMake and decoded with `src/image.{h,cpp}`
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
- Representation keys (plan item 6): representations have a stable `id` (saved, never reused); keys for **Visible** (grows in / shrinks away over **Transition (s)**, 2 s by default, from the key; solid representations cannot fade), the scales, **Tint scale**, **Saturation**, **Base color** and **Tint color**; removing a representation removes its keys; the values go back when keys let go or a recording ends.
- Image overlays (png or jpg from a file, path saved relative to the workspace, **Reload**, red message when it cannot be read).
- Overlay size in percent of the frame height or in points (1 pt = 1 px at 1080p, scaled), per overlay.
- The camera path in the viewport follows the target or atom of keys that follow (sampled along the movie, a few points per frame, cached).
- Logo overlay: the VIAMD logo (`icon/viamd_logo.png`, baked into the executable) in the top left corner by default for the whole movie; **Add Logo** adds more. A workspace remembers that it was removed (`Overlays=1`).
- Follow / Unfollow in the keyframe table, for a key that already exists.
- Output formats H.265 (mp4) and VP9 (webm) besides H.264, with their own quality ranges; Ctrl+C / Ctrl+V for keyframes; a background plate for overlays.
- The Representations window is locked while recording (the cheap form of plan item 7).
- Representation keys on the timeline: a lane for the chosen representation and property, keys dragged, added by double-click and removed by right-click, like the look parameter lane.

## Untested in the GUI

- Representation lane in the timeline: drag, add (double-click) and remove (right-click) keys; Visible turns around on double-click; hiding the lane.
- Visible transition: a representation grows in at its key and shrinks away at the next; scrub through a transition (also with the lane); transition 0 pops; a key closer than the transition turns around; Cartoon and Ribbons at very small sizes look right; electronic structure and dipole representations vanish at the end of the transition.
- Image overlay: **Add Image...** with a png that has transparency and with a jpg; it shows in the viewport and in a recording; **Browse...** and **Reload** work; save the workspace, move the workspace and the image together to another folder and open it: the image is found; a missing file turns the message red and does not crash.
- Overlay size in points: switch an overlay to points, the look is kept; 24 pt looks the same at 1080p and 4K; a saved workspace keeps the unit.
- Follow-aware camera path: with a follow target and keys that follow, the blue and yellow paths bend with the target and match what the preview does; the green camera at the playhead too; moving a key or the target restarts it without the old path vanishing; no stall while it is being made.
- Follow / Unfollow in the keyframe table: Go To a key, Follow, scrub through the trajectory and check the camera keeps the target in view; without Go To first an error is logged.
- Logo overlay: appears top left in the viewport preview and in a recording (not upside down, not stretched, sharp at small sizes, fades with its range if edited); removing it and saving keeps it removed after loading; an older workspace without overlays gets it.
- Output formats: record a few seconds as H.265 and as WebM VP9 (needs ffmpeg with libx265 and libvpx-vp9) and play the files; the workspace copy and the log name the right extension.
- Ctrl+C / Ctrl+V on keyframes (also while a text box is not focused); the overlay Background plate over a busy picture and its fades.
- Representation keys: key Visible at two times and scrub/preview; key a scale, a saturation and a tint color (the example workspace tints the protein blue-grey from 50 s to 60 s); remove a representation that has keys; duplicate one (the copy must not share keys); save and reload a workspace (ids and keys survive, an old workspace without ids still loads and keys can be added); the values must go back after a recording.
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

- Only the properties listed above can be keyed: not the type, filter, colour mapping, the other colours (bond, secondary structure) or the electronic structure settings (those cannot be blended, or are expensive to redo every frame).
- Tint scale and saturation recolor the atoms of the representation every frame while they change: slow for very large systems.
- Representation ids are assigned in the order they are created; a workspace written before they existed gets ids on load.
- The follow target itself is not part of undo.
- Where a frame curve turns around, the trajectory slows to a stop. For a hard reversal, add a key at the turn-around frame with Linear easing.
- Overlay text is single style (a shadow and an optional background plate).
- Default view (existing code from master): a perfectly symmetric flat molecule (exact ideal benzene geometry) can settle about 10 degrees off face-on, because the visibility scores of nearby directions tie. Real coordinates are not exactly symmetric and are fine.
- In "Follow target" depth of field mode, focus uses the global follow target, not a key's own Look at atom.
- Distance and field of view can only be moved in time on the timeline; their values are edited in the table or the viewport.

## Still to do

- Plan item 7, the real fix: allow editing representations during a recording (large; the lock covers the need for now).
- Optional output: transparent background (lowest value for MD movies).

## Ideas not yet planned

- Per-overlay font in another typeface
