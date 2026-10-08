# Movie feature: status

Branch `video`; no pull request yet. The user manual (also the text for the GitHub wiki, which is a separate repository) is `docs/movie_maker.md`. The pull request text is `PR_DESCRIPTION.md`. The list of things to check by hand is `gui-checklist.md`. `docs/examples/aspirin_phospholipase_movie.via` is the one example workspace (its trajectory files are not in the repository).

## Build and test

- Build: `cmake --build build --target viamd -j8` and `--target viamd_test`; run `cmake -S . -B build` after adding files.
- Tests: `./build/bin/viamd_test` (231 tests, all passing). They cover pure logic; ffmpeg is not needed because frame sink tests use a fake script.
- VS Code CMake Tools: a local, gitignored `CMakeUserPresets.json` (preset `release`, binary dir `build`, unit tests on) and `.vscode/settings.json` make it configure; select the `release` preset after reloading the window. VS Code's test runner does not discover the C++ tests; run the executable.
- The GUI could not be run while the code was written, so everything in `gui-checklist.md` is untested by the author and needs a manual check. Windows and macOS are untested too (the ffmpeg pipe uses `popen` / `_popen`).

## Where things live

- Combined Movie editor, timeline lanes, recording, preview, frame guide and overlay drawing: `src/main.cpp` (`draw_movie_window`, `draw_movie_settings_panel`, `draw_movie_timeline_panel`, `draw_movie_strip`, `draw_movie_*_lane`, `movie_overlays_draw`, `movie_figure_draw`, `movie_frame_guide`, `movie_*`)
- Movie state: `src/viamd.h` (`ApplicationState::movie`)
- Workspace save and load: `src/viamd.cpp` (`[Movie]`, `[MovieOverlay]`, `[MovieMarker]`, `[Representation]`); the plot windows' own sections with subplot ids and names: `src/plot_series.cpp`
- Async frame writer: `src/frame_sink.{h,cpp}`
- Keyed look parameters and representation keys, undo history, render range, frame scaling, snapping, time left: `src/movie_keys.{h,cpp}`
- Overlay maths (fades, sizes, scale bar, time bar and elapsed curves, ticks, histograms, subplot panels and their times, the frame fit and the widened view, migration of older overlays): `src/movie_overlay.{h,cpp}`; the logo is `icon/viamd.png` (cropped to its visible part when it is loaded), baked into `gen/viamd_icon.inl` by CMake and decoded with `src/image.{h,cpp}`
- Camera path evaluation: `src/gfx/camera_utils.{h,cpp}`
- Numbers that are saved and must never be renumbered, only added to: the parameter ids in `movie_param_table` (main.cpp), `RepProp` (movie_keys.h) and `MovieOverlayType` (movie_overlay.h; 6 and 7 are the timeline and the distribution, 9 is the old one-figure version that is split when read).

## What is there

- **Combined editor:** timeline left, tabbed controls right, resizable divider, independently scrolling and collapsible panels. Shared preview toolbar and a large top-right Scene view / Movie preview switch. Overlays use a compact list with a selected-item inspector (Content, Timing, Appearance); subplot details and plot style are expandable. Distribution overlays can override bin counts without changing source plots. Markers can target a stable timeline subplot id (0 keeps legacy all-subplot behavior). Time bars have a grey track; new ones have a grey plate. The user-edited example is retained, with only the time-bar plate and explicit 128-bin distribution added.
- **Representation systems:** one overview row per system (name before the first hyphen), with labelled blocks coloured by representation type. Systems get bands separated by a gap, a divider and alternating shading; a block that starts where another ends stays on the same line (cross-fade), only simultaneous representations stack. **One line per system** gives each band the same height. Right-click switches a block to another existing representation or removes it; double-click empty space chooses a representation to add. Unkeyed enabled representations appear for the whole movie. Edits reuse visibility keys and undo, so no new workspace format is needed.

- **Camera:** the path in the viewport (ticks at round times, chevrons, sight lines, cameras, spin rings, the green camera at the preview time) with handles on the eye and the look-at of each key that can be clicked and dragged, Ctrl + click to add a key on the path; keyframes with easing per segment, spin, orbit, seamless loop, keep upright with a roll per keyframe, follow target, a key that looks at (and tracks) an atom, depth of field focus modes, the camera path drawn in the viewport (follow-aware), **Show frame** (the frame of the movie in the viewport, the view widened to match the recording), undo and redo of everything on the timeline.
- **Picking keys:** click, Ctrl + click and a box on the background pick keys across the camera, trajectory, distance, field of view, look parameter and representation lanes; a drag moves all picked keys in time and in the value of the lane (distance and field of view included), arrow keys nudge, Delete, Ctrl + C and Ctrl + V work on the picked keys; the pure logic is in `src/movie_keys.{h,cpp}` (`KeySelection`, `movie_keys_shift`, `movie_keys_resolve`, `movie_keys_copy`, `movie_keys_paste`), the drawing and input in `movie_key_point`, `movie_lane_box` and `movie_selection_shortcuts` in `src/main.cpp`. The time axis pans with the middle button.
- **Timing:** the movie length is the master value; the trajectory start and end anchors; frame pins on keys; backward trajectory; **Snap to frames**.
- **Looks:** keyed look parameters, and keyed properties of representations (visible with a transition, scales, tint, saturation, colours; representations have stable ids).
- **Overlays:** text, time stamp (the time that has gone since the movie started, only grows), scale bar, time bar (fills forward whichever way the trajectory plays), logo, images, **timeline** and **distribution** (subplots of the Timelines and Distributions windows, stacked, drawn as the movie plays; elapsed or trajectory-time axis; subplots that come in at their own times; the axis is the whole movie and the curves are drawn from the start of the movie, whenever the overlay appears; titles; markers; a look of their own), and the **property visualization** (atoms, geometry and labels of a script property, with fades). Size in percent or in points, fades, a background plate, **Duplicate**. Subplots of the plot windows have saved ids and names, so overlays keep finding them.
- **Left timeline panel:** camera, trajectory, distance, field of view, looks, representation properties, one overview row per system with overlapping representation blocks, and overlay bars. Expand **Lanes and layout** for lane selection, lane height, **Fit to window**, **One line per system**, and the selected-system swap shortcut. Ctrl + wheel zooms time; lanes are saved in the workspace.
- **Output:** PNG sequence, MP4 H.264 or H.265, WebM VP9, through ffmpeg; frame **Scale**, **Samples per frame**, render range, **Pause / Resume**, time left, a workspace copy next to the movie. The Representations window is locked while recording.
- Workspaces from earlier versions are migrated when read.

## Known limits

- Only some properties can be keyed: not the type, filter, colour mapping, the other colours (bond, secondary structure) or the electronic structure settings.
- Tint scale and saturation recolor the atoms of the representation every frame: slow for very large systems.
- The follow target itself is not part of undo.
- Where a frame curve turns around, the trajectory slows to a stop. For a hard reversal, add a key at the turn-around frame with Linear easing.
- Overlay text has one style (a shadow and an optional background plate).
- A timeline or a distribution overlay draws at most six series of a subplot and the first member of a population; a timeline overlay has one kind of axis for all its subplots.
- In "Follow target" depth of field mode, focus uses the global follow target, not a key's own Look at atom.
- Picked overlay bars and representation blocks move with the picked keys but are not copied, pasted or (for overlays) deleted by the picking shortcuts; unkeyed blocks cannot be picked.
- With **Keep upright**, a spin around a horizontal axis flips at the poles (the camera cannot stay level looking straight along the up axis). Roll eases as a number, so 170° to -170° turns the long way.
- Default view (code from master): a perfectly symmetric flat molecule (exact ideal benzene geometry) can settle about 10 degrees off face-on. Real coordinates are fine.

## Left for later

- Text overlays with live values (`{time}`, `{frame}`, a script property) and entrance or exit animations for overlays.
- Overlay templates that can be saved and applied to another movie.
- Editing representations during a recording (the lock covers the need for now), and a transparent background.
- Not planned: merging the Timelines and Distributions windows, 2D plots, and exporting figures other than the movie.
