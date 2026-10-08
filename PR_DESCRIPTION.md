## Movie maker: keyframed camera, timeline and video recording

### Summary

This adds a **movie maker** to VIAMD. You set the length of the movie, place camera keyframes and keyed visual settings on a timeline, add overlays (text, time stamp, scale bar, time bar, images, plots of properties, the visualization of a script property, the VIAMD logo), and record. The output is an MP4 (H.264 or H.265) or a WebM (frames piped into ffmpeg) or a numbered PNG sequence.

It has one combined **Movie** editor, opened from **Windows**:
- **Left:** timeline lanes with real axes, draggable keys and overlays, scrolling, frame snapping and time zoom.
- **Right:** tabbed output, timing, camera, looks, representations and overlays. Drag the divider or collapse either side.
- **Top:** shared preview controls and a prominent **Scene view / Movie preview** switch. Scene view hides the frame and overlays without changing playback or recording.
- **Overlays:** compact selectable list and Content/Timing/Appearance inspector; expandable subplot and plot-style controls.
- Distribution overlays have their own bin count (or retain source counts); markers can target an individual timeline subplot by stable id. Both settings are saved and undoable. Time bars use a grey unfilled track and new ones have a grey plate.

The user manual with screenshots is in [`docs/movie_maker.md`](docs/movie_maker.md). [`docs/examples/aspirin_phospholipase_movie.via`](docs/examples/aspirin_phospholipase_movie.via) is a finished movie that uses most of the features (its trajectory files are not in the repository).

![Movie maker](docs/images/movie/overview.png)

### Features

**Recording**
- Output as an MP4 (H.264 or H.265) or a WebM (VP9) through an ffmpeg pipe with a CRF setting, or a PNG sequence, with a button that copies the ffmpeg command to encode it.
- Resolution can be the window size, presets up to 8K, or a custom size. Output FPS is configurable.
- Frames are written off the render thread by a new `frame_sink`. It has a fixed pool of buffers, so memory use stays bounded.
- When a recording finishes or is stopped, the user's view, playback state and screenshot settings are put back.
- Quick tests: a **Scale** (100/75/50/25 %) for the frame size, a number of **Samples per frame** for temporal anti-aliasing, and **Render only a range** of the movie. PNG files keep their numbers from the whole movie, so a redone part can replace them.
- **Pause** and **Resume**, and a time-left estimate from the pace so far.
- Optionally saves a workspace copy (`prefix.via`) next to the movie, so it can be made again.

**Timing**
- The movie length is the master value. Changing it scales everything on the timeline: camera and parameter keys, overlays and their fades, and the trajectory's start and end.
- The trajectory's start and end are movable anchors, shown as blue Start/End lines. Moving them allows a fly-over while the trajectory is held, or a hold at the end.
- Keyframes can pin a trajectory frame. The trajectory speed then varies between pins and can run backwards.
- "Match Animation speed" sets the length so the trajectory plays at the Animation window's speed.

**Camera keyframes**
- Each key stores the pose, field of view and time. Between keys the camera moves smoothly by default (monotone cubic interpolation along the shortest rotation).
- The way into each key can be set per key: Smooth, Ease in/out, Linear or Hold.
- **Look at**: click the button, then click an atom, and the key looks at that atom and tracks it through the trajectory. **Update position** moves the key's eye and keeps what it looks at.
- **Keep upright** (on for new movies): the camera stays level with a chosen world axis up (**Up**, or **From view**), so loops and spins never leave it tilted or upside down; each key has a **Roll** that eases between keys. Off, a **Level** button straightens a key.
- Extra spin turns per key, Add Orbit, a seamless loop with Close Loop, a follow target (centre of a selection), and drag-to-reorder rows.
- **Key on Selection** adds a key that frames the selected atoms. Keys can be copied and pasted at the preview time.
- **Snap to frames**: dragged times land on a frame of the movie.
- The camera path and the camera at each key can be drawn in the viewport.

**Look parameters**
- These can be keyed over time: background colour and intensity, ambient occlusion and its radius, exposure, depth of field blur, near and far clipping, and focus distance.

**Representation keys**
- Show or hide a representation at a time: it grows in or shrinks away over a transition time (2 s by default, settable, 0 for an instant change) rather than popping, and key its scales, tint scale, saturation, base colour and tint colour (smooth between keys).
- Representations now have a stable `id` (saved in the workspace, never reused), which keys refer to, so reordering, duplicating or removing representations does not break them. Removing one removes its keys.
- The Representations window is locked while a recording is running.

- Keys of the camera, trajectory, distance, field of view, look parameter and representation lanes can be picked (click, Ctrl + click, a box on the background) and moved together in time and, in a lane with a value, in value; arrow keys nudge, Delete removes, Ctrl + C and Ctrl + V copy and paste. Distance and field of view can now be edited by dragging their dots. The time axis pans with the middle button or Shift + wheel.

- The camera path in the viewport is drawn over it, with ticks at round times and chevrons that show speed and direction, brighter ahead of the preview time, sight lines, cameras at the keys, rings for spins, and a green camera with the line it looks along at the preview time. Each key has a handle on the eye and one on what it looks at: click to go to the key, drag to edit it (Ctrl moves both), Ctrl + click on the path adds a key.

- The **Camera lane** of the editor's left timeline shows the camera keys (with names that are saved) as dots that can be dragged, clicked, renamed and removed, bands where the camera follows the target or an atom or spins, and the keys that pin a trajectory frame; a double click adds a key on the path without moving the camera.

- The **Representation overview** has one row per system (name before the first hyphen), with labelled blocks coloured by representation type. Systems are separated by a gap, a divider and alternating shading. A block that starts where another ends stays on the same line and they cross-fade; only representations shown at the same time stack. **One line per system** (on by default) gives every system the same height, with stacked blocks sharing it. Blocks can be moved, resized, added and removed; their context menu switches the interval to another existing representation in the system. Target overlaps merge without disturbing other systems or property keys. Unkeyed enabled representations cover the movie; undo/redo and workspace saving retain edits.

**Depth of field**
- New focus modes: look-at point (the previous behaviour), a fixed distance that can be keyed, or the follow target.

**Overlays**
- Kinds: text, time stamp (the time that has gone since the movie started, so it only grows, also for a backward trajectory), scale bar, time bar, images (png or jpg), the VIAMD logo, a **timeline** and a **distribution** of properties, and the visualization of a script property.
- Each has a time range with fades, nine anchor positions, a size (a part of the frame height, or in points: a point is a pixel of a 1080 pixel high frame, scaled with the frame), a colour and an optional background plate. They can be duplicated, and each is a bar that can be dragged in the overlay lane of the Movie Timeline. A movie starts with the logo in the top left corner; it can be removed.
- The **time bar** fills forward whichever way the trajectory is played, so it shows the pace of the movie; it can show the time that has gone and the speed.
- **Timeline** (wide and low, bottom center) and **distribution** (narrow and tall, middle right) draw subplots of the Timelines and Distributions windows, stacked, as the movie plays: curves and bars grow with the part of the trajectory that has been played, and the legend gives the value at the frame that is shown. A timeline's axis is the elapsed trajectory time like the time bar, so it grows to the right whichever way the trajectory is played. The axis is the whole movie and the curves are drawn from the start of the movie, whenever the overlay appears; each subplot comes in and goes at times of its own. Titles, markers (notes on the movie's timeline), text and lines in points, a colour set of its own and light or dark plate presets.
- Subplots of the plot windows have saved ids and names, so overlays keep finding them when others are added or renamed. Workspaces from before are converted when read.
- The **property visualization** overlay shows the atoms, geometry and labels of a script property in the viewport and in the recording, fading with the overlay's fades.
- **Movie preview** draws the frame of the movie in the viewport (dimmed outside, in its proportions) and widens the view to match the recording; **Scene view** hides the frame and overlays for editing.

**Undo/redo** (Ctrl+Z, Ctrl+Y)
- Covers keys, overlays, length and timing.

**Workspace**
- Everything is saved in a `[Movie]` section (with `[MovieOverlay]` and `[MovieMarker]` sections), versioned with `Timeline=2`, and older formats are migrated on load. The subplots of the Timelines and Distributions windows have saved ids and names.
- Look parameter ids, the numbers of the representation properties and of the overlay kinds are stable and must never be renumbered.

### Fixes outside the movie code
- **Locale (`application.cpp`):** GTK, used by NativeFileDialog, switched the numeric locale. On machines with a decimal-comma locale, workspaces were then written with commas. `LC_NUMERIC` is now reset to `"C"`.
- **Folder picker:** `FileDialogFlag_Dir` is now supported.
- **`extract_flt_vec`:** accepts vectors of up to 32 values, and rejects input that has more values than requested. This lets newer keyframe formats load and older ones fall back correctly.

### New code
- `src/frame_sink.{h,cpp}`: asynchronous PNG/ffmpeg writer.
- `src/movie_keys.{h,cpp}`: parameter and representation keys, the snapshot used for undo, time scaling, render range, snapping.
- `src/movie_overlay.{h,cpp}`: overlay fades, sizes, scale bar length, the time bar and elapsed-curve maths, ticks and histogram counts of the plot overlays, the times of their subplots, the frame guide maths, the migration of older overlays and the default logo overlay.
- `src/plot_series.{h,cpp}`: the subplots of the Timelines and Distributions windows get a stable id and a name.
- `src/image.{h,cpp}`: decoding of an image in memory (the logo), the bounds of its visible part a fix for dark edges of transparent images and the light grey of the icon made black for the logo; `icon/viamd.png` is baked into the executable by CMake and is the logo.
- `Representation::id` (`viamd.h`): a stable id for each representation, saved in workspaces, which representation keys refer to.
- `src/gfx/camera_utils.{h,cpp}` and `camera.h`: keyframe evaluation, anchors, keyed curves, `camera_aim_at`, `camera_level` and `camera_roll` (keep upright, roll per key).
- Most of the UI is in `src/main.cpp`. State and workspace I/O are in `viamd.{h,cpp}`.

### Tests
New tests are in:
- `test_camera_utils`: interpolation, easing, spin, loop, follow, anchors, levelling and roll.
- `test_movie_keys`: scaling and undo, render range, frame scaling, time left, snapping, representation keys.
- `test_frame_sink`.
- `test_movie_overlay`: fades, sizes, the time bar, elapsed curves, ticks, histograms, subplot times, migration of older overlays, the frame fit.
- `test_image`.
- `test_serialization`.

All 234 tests pass in a Release build on Linux (`./build/bin/viamd_test`). The newer ones cover bin overrides, marker targets, system rows and bands, overlap packing and transition tails, unkeyed visibility, block switching/removal, undo/redo, camera levelling, roll and keep upright.

### Testing done and not done
- I recorded PNG sequences and H.264 MP4s on Linux early on, and the screenshots in the manual come from a real run with `1ALA-500.pdb`.
- Everything added since has been tried by hand in the GUI only in part: `gui-checklist.md` is the list to go through, and `MOVIE_STATUS.md` says what is there and what is left out.
- Not tested on Windows or macOS. The ffmpeg pipe uses `popen`/`_popen`.

### Known limits
- Solid representations cannot fade, so they grow in and shrink away at a Visible key. Tint and saturation keys recolor the atoms every frame, which is slow for very large systems.
- In "Follow target" depth of field mode, focus uses the global follow target, not a key's own Look at atom.
- While Scene view is shown, a small live preview of the movie camera (with the overlays) sits in the corner of the Movie window, so the path can be edited while watching the shot.

- Scene view is an editor camera that frames the whole camera path, while the movie camera is the green one that follows the preview time; Movie preview is the movie camera with the recording frame. Each mode keeps its own view; Tab switches, and holding Tab peeks at the other mode.

- The Movie Timeline has a time ruler (frame ticks and numbers, the notes of the movie, click or drag to scrub) and every lane shows its name.

- The picked items can be stretched in time (about the first of them or the preview time) and have their easing and start time set together from a line above the lanes.

- Picked overlay bars and representation blocks move with the picked keys; they are not copied or pasted, and an overlay is not deleted by the picking shortcuts.
- A timeline or distribution overlay draws at most six series of a subplot and the first member of a population.
