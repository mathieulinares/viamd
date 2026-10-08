## Movie maker: keyframed camera, timeline editor and video recording

### Summary

A **movie maker** for VIAMD. Set the length of the movie, place camera keyframes and keyed look settings on a timeline, add overlays, and record to MP4 (H.264 / H.265), WebM (VP9) through ffmpeg, or to a numbered PNG sequence.

Everything is in one **Windows > Movie** editor: a timeline with draggable lanes on the left, tabbed controls (**Output**, **Timing**, **Camera**, **Looks**, **Representations**, **Overlays**) on the right, and shared preview playback on top. A **Scene view / Movie preview** switch gives you a free editor camera next to the movie camera, with a live picture of the movie camera while you edit.

The manual is [`docs/movie_maker.md`](docs/movie_maker.md). [`docs/examples/aspirin_phospholipase_movie.via`](docs/examples/aspirin_phospholipase_movie.via) is a finished 80 s movie using most features, and [`docs/examples/aspirin_binding_movie.via`](docs/examples/aspirin_binding_movie.via) a 60 s one with a dolly zoom, a bullet-time orbit, representation hand-overs and energy plots (their trajectory files are not in the repository).

![Movie maker](docs/images/movie/overview.png)

### Features

**Recording**
- MP4 H.264, MP4 H.265 or WebM VP9 through an ffmpeg pipe (CRF setting), or PNG sequence with a button that copies the ffmpeg command.
- Window size, presets up to 8K, or custom resolution; output FPS.
- Frames are written off the render thread by `frame_sink` with a fixed buffer pool, so memory stays bounded.
- Quick tests: **Scale** (100/75/50/25 %), **Samples per frame**, **Render only a range** (PNG files keep their whole-movie numbers).
- **Pause** / **Resume**, time left, optional workspace copy (`prefix.via`) next to the movie.
- The user's view, playback and screenshot settings are restored when it ends.

**Timing**
- The movie length is the master value: changing it scales everything on the timeline.
- Movable trajectory start/end anchors (fly-over before the dynamics, hold at the end); frame pins on keys vary the speed and allow backward play; **Match Animation speed**; **Snap to frames**.

**Camera**
- Keys with per-key ease (Smooth, Ease in/out, Linear, Hold), field of view, names, spin turns, **Add Orbit**, seamless loop, follow target, **Look at** an atom (tracked through the trajectory), **Key on Selection**, copy/paste.
- **Keep upright** with a roll per key, so loops and spins never tilt the camera.
- Depth of field focus on the look-at point, a keyable distance, or the follow target.
- The camera path is drawn in the viewport (ticks at round times, chevrons, sight lines, cameras, spin rings, the green camera at the preview time) with handles on the eye and the look-at point of each key: click to go there, drag to edit (Ctrl moves both), Ctrl + click on the path adds a key.

**Looks and representations**
- Keyable look parameters: background, ambient occlusion, exposure, depth-of-field blur, clipping, focus distance.
- Keyable representation properties: visibility (grow in / shrink away over a transition, since solid representations cannot fade), scales, tint, saturation, colors. Representations get a stable `id` that keys refer to, so reordering, duplicating and removing are safe.
- The **Representation overview** lane shows when each representation is shown, one row per system (name before the first hyphen), with block editing and swap. The Representations window is locked while recording.

**Overlays**
- Text, time stamp, scale bar, time bar, logo, images, **timeline** and **distribution** (subplots of the Timelines and Distributions windows, drawn as the movie plays, with markers), and the visualization of a script property.
- Time range with fades, nine anchors, size in percent or points, color, background plate, **Duplicate**. Compact list with a Content / Timing / Appearance inspector.
- Subplots of the plot windows get saved ids and names so overlays keep finding them.

**Timeline panel**
- Ruler, trajectory, camera, distance, field of view, look parameter, representation, overview and overlay lanes.
- Pick keys, overlay bars and overview blocks (click, Ctrl + click, box) and move, stretch, nudge, delete, copy and paste them together; an inspector sets start time and ease for the whole selection.
- Time zoom, middle-button pan, lane height / fit to window, lane selection saved in the workspace.

**Undo/redo** (Ctrl+Z, Ctrl+Y) cover everything on the movie.

**Workspace**
- Saved under `[Movie]` (with `[MovieOverlay]` and `[MovieMarker]`), versioned with `Timeline=2`; older formats are migrated on load.
- Look parameter ids, representation property numbers, overlay kinds and lane/path option bits are stable and must never be renumbered.

### Fixes outside the movie code
- **Locale (`application.cpp`):** GTK (NativeFileDialog) switched the numeric locale, so on decimal-comma machines workspaces were written with commas. `LC_NUMERIC` is reset to `"C"`.
- **Folder picker:** `FileDialogFlag_Dir` is supported.
- **`extract_flt_vec`:** accepts up to 32 values and rejects input with more values than requested, so newer keyframe formats load and older ones fall back correctly.

### New code
- `src/frame_sink.{h,cpp}`: asynchronous PNG/ffmpeg writer.
- `src/movie_keys.{h,cpp}`: parameter and representation keys, key selection and group edits, undo snapshot, time scaling, render range, snapping.
- `src/movie_overlay.{h,cpp}`: overlay maths, frame fit, migration of older overlays, default logo overlay.
- `src/plot_series.{h,cpp}`: stable subplot ids and names.
- `src/image.{h,cpp}`: in-memory image decoding for the logo (`icon/viamd.png`, baked in by CMake) and image overlays.
- `src/gfx/camera_utils.{h,cpp}`, `camera.h`: keyframe evaluation, anchors, keyed curves, `camera_aim_at`, `camera_level`, `camera_roll`.
- `Representation::id` (`viamd.h`).
- Most of the UI is in `src/main.cpp`; state and workspace I/O in `viamd.{h,cpp}`.

### Tests
New suites: `test_camera_utils`, `test_movie_keys`, `test_frame_sink`, `test_movie_overlay`, `test_image`, `test_serialization`. All 230 tests pass in a Release build on Linux (`./build/bin/viamd_test`).

### Testing done and not done
- PNG sequences and H.264 MP4s were recorded on Linux early on; the manual's screenshots come from a real run (they show an earlier two-window layout).
- GUI testing of the combined editor is in progress, following [`gui-checklist.md`](gui-checklist.md); [`MOVIE_STATUS.md`](MOVIE_STATUS.md) says what is there and what is left out.
- Not tested on Windows or macOS (the ffmpeg pipe uses `_popen` / `popen`).

### Known limits
- Solid representations cannot fade; they grow in and shrink away.
- Tint and saturation keys recolor atoms every frame: slow for very large systems.
- **Follow target** depth of field uses the global follow target, not a key's own **Look at** atom.
- Timeline / distribution overlays draw at most six series per subplot and the first member of a population.
- Picked overlay bars and representation blocks move with picked keys but are not copied or pasted.
