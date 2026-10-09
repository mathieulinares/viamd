# Movie feature: status

Branch `video`; no pull request yet.

Merged `scanberg/viamd` master through `381f3fad`, including renderer, ASE loader and camera/recentering fixes. Movie focus modes, recording, editor and live preview are retained. Depth-of-field blur now uses aperture units; legacy blur and occlusion-radius keys are retained but inactive (see the manual).

Each camera key now has its own **Look at** (a fixed point, or atoms tracked through the
trajectory) and **Focus on** (what is sharp, with transition and ease), set with two
buttons in the key table. Both can be a selection typed in the filter language (`resname("AIN")`). The blur is the **Depth of field blur** look parameter. The focus is the look-at point by default and a key that does not set
it keeps the focus of the key before. The viewport shows three paths (camera, look-at, focus)
and the camera lane has Look at and Focus rows. The opt-in "Independent camera / look-at / focus"
switch and the separate tracks are gone: workspaces that used them (and keys that followed a
follow target) are converted when loaded, which can change the eye path of such a movie slightly.

| File | What it is |
|---|---|
| `docs/movie_maker.md` | The user manual (also the text for the GitHub wiki, a separate repository) |
| `gui-checklist.md` | Manual GUI checks, including look at and focus on per key |
| `PR_DESCRIPTION.md` | The pull request text |
| `docs/examples/aspirin_phospholipase_movie.via` | The first example workspace (its trajectory files are not in the repository) |
| `docs/examples/aspirin_binding_movie.via` | A 60 s example with a dolly zoom, a bullet-time orbit, representation hand-overs and energy plots; see the manual |
| `docs/examples/fov_and_focus.via` | Self-contained 36 s FOV tutorial with movie-local moving focus, a rack to a fixed target, and a no-blur key |

## Build and test

- Build: `cmake --build build --target viamd viamd_test -j8`; run `cmake -S . -B build` after adding files.
- Tests: all 266 tests pass, including atom sets, eye and aim evaluated separately, the default focus and focus transitions, Spin pivot/axis, camera bands, conversion of older tracks and follow keys (with the optics tutorial), focus encoding, history/time scaling and group edits that keep a key's look-at and focus. They cover pure logic only; the frame sink tests use a fake script, so ffmpeg is not needed.
- VS Code CMake Tools currently reports no active configure preset. The existing `build` tree successfully builds `viamd` and `viamd_test` through a temporary VS Code Make task. VS Code's test runner does not discover the C++ tests; the executable passes.
- The GUI could not be run while the code was written, so everything in `gui-checklist.md` beyond blocks 1 to 5 is untested. Windows and macOS are untested (the ffmpeg pipe uses `popen` / `_popen`).

## Where things live

| What | Where |
|---|---|
| All movie GUI: editor window, lanes (`draw_movie_*_lane`, `draw_movie_strip`, `draw_movie_ruler`), camera path overlay (`movie_draw_camera_path`), picking and group edits (`movie_key_point`, `movie_pick_click`, `movie_group_drag`, `movie_lane_box`, `movie_selection_edit`, `movie_selection_shortcuts`), Scene view / Movie preview (`movie_scene_view`, `movie_camera_pose`, `movie_scene_fit_path`, `movie_set_scene_view`), live picture (`movie_render_pip`, `render_scene`), recording, overlay drawing | `src/main.cpp` |
| Movie state (`ApplicationState::movie`) | `src/viamd.h` |
| Workspace save and load (`[Movie]`, `[MovieOverlay]`, `[MovieMarker]`, `[Representation]`) | `src/viamd.cpp` |
| Subplot ids and names of the Timelines / Distributions windows | `src/plot_series.cpp` |
| Async frame writer | `src/frame_sink.{h,cpp}` |
| Keyed look parameters and representation keys, key selection and group edits (shift, scale, resolve, delete, copy, paste), undo snapshot, render range, snapping, time left, representation system rows | `src/movie_keys.{h,cpp}` |
| Overlay maths (fades, sizes, scale bar, time bar, elapsed curves, ticks, histograms, subplot times, frame fit, migration of older overlays) | `src/movie_overlay.{h,cpp}` |
| Logo (`icon/viamd.png`, baked into `gen/viamd_icon.inl` by CMake) and image decoding | `src/image.{h,cpp}` |
| Camera path evaluation, levelling, roll | `src/gfx/camera_utils.{h,cpp}` |

Numbers that are saved and must never be renumbered, only added to: parameter ids in `movie_param_table` (main.cpp), `RepProp` (movie_keys.h), `MovieOverlayType` (movie_overlay.h; 6 and 7 are timeline and distribution, 9 is the old one-figure version split on read), the `Lanes` bits (1, 2, 4 tracks; 8 parameter; 16 representation lane; 32 overlay; 64 overview; 256 camera; 1024 ruler; 128, 512, 2048 are the "set" flags of the last three) and the `PathOptions` bits (mask 31, default 15; bit 16 shows the path in Movie preview).

## What is there

- **Editor:** one window, timeline left and tabbed controls right, resizable and collapsible; shared playback; Scene view / Movie preview switch (Tab); frame placed beside the Movie window with a shifted projection; Show path / Fit path / Live picture on the top row; green Preview button plays the movie with the windows hidden and a bottom control bar (Space, Esc) with separate poses, **Fit path** and a live picture of the movie camera.
- **Timing:** movie length as the master value, trajectory start/end anchors, frame pins, backward trajectory, **Match Animation speed**, **Snap to frames**.
- **Camera:** keys with a per-key look-at (fixed or atoms) and focus (target, transition, ease; blur is a look parameter); ease, spin, orbit, loops, keep upright with roll, names; three colored viewport paths (camera, look-at, focus) with draggable handles; camera lane rows for keys, look at, focus, spin and frame.
- **Looks and representations:** keyed look parameters; keyed representation properties (visibility with transition, scales, tint, saturation, colors) with stable representation ids; the overview lane by system with block editing and swap.
- **Overlays:** text, time stamp, scale bar, time bar, logo, image, timeline, distribution, property visualization; markers (labels on up to three rows); subplot ids and names; overlays shown together are moved apart (`movie_overlay_avoid`).
- **Timeline panel:** lane toggles with presets and a Layout popup; ruler, trajectory, camera, lens (FOV and distance on two axes), look parameter, representation lane, overview and overlay lanes; multi-select across all of them with group move, stretch, inspector, keyboard and copy/paste; lane layout saved in the workspace.
- **Output:** PNG sequence, MP4 H.264 / H.265, WebM VP9 via ffmpeg; scale, samples, render range, pause/resume, time left, workspace copy.
- Undo/redo for everything on the timeline. Workspaces from earlier versions are migrated on read.

## Known limits

- Only some representation properties can be keyed (not type, filter, color mapping, the other colors or electronic structure settings). Tint and saturation recolor atoms every frame: slow for very large systems.
- The global follow target of older workspaces is not part of undo and can no longer be set (a key's **Look at** / **Focus on** replace it).
- Where a frame curve turns around the trajectory slows to a stop; for a hard reversal add a key at the turn-around frame with Linear ease.
- Overlay text has one style (shadow and optional plate).
- Timeline / distribution overlays draw at most six series per subplot and the first member of a population; a timeline overlay has one axis kind for all subplots.
- Tracked path drawings use the current trajectory frame, not future-frame sampling.
- A key that looks at atoms aims at their centre; the offset that a legacy follow key kept is not preserved on conversion.
- Picked overlay bars and representation blocks move with picked keys but are not copied, pasted or (overlays) deleted by the picking shortcuts; unkeyed blocks cannot be picked.
- With **Keep upright**, a spin around a horizontal axis flips at the poles; roll eases as a number, so 170 to -170 degrees turns the long way.
- Master code, not ours: a perfectly symmetric flat molecule (exact ideal benzene) can settle about 10 degrees off face-on in the default view.

## Left for later

- The screenshots in `docs/images/movie` show the earlier two-window layout; retake them from the combined editor.
- Text overlays with live values (`{time}`, `{frame}`, a script property) and entrance/exit animations.
- Saved overlay templates.
- Editing representations during a recording, and a transparent background.
- Not planned: merging the Timelines and Distributions windows, 2D plots, exporting figures other than the movie.
- Upstream strays left alone: `Claude outputs/windows-arm64.yml` (tracked on master) and `TODO.md`.
