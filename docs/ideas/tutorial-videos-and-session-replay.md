# Tutorial videos and GUI session replay (idea, for later)

Status: personal prototype on the `youtube-capture` branch, pushed to the user's `origin` fork (not `upstream`). VIAMD builds, smoke replay was live-tested, and GStreamer MP4 encoding works. Tutorial 1 sample: `~/Videos/VIAMD/tutorial-1-alanine.mp4` (80 s, H.264, 1280x720, 10 fps). Tutorial 2 draft and narration status are recorded below. Use `--preview` to watch GUI actions in VIAMD and leave it open afterward.

## Branch and scope

Develop the personal capture tool on a separate, private `youtube-capture` branch created from the current `docu`. This gives the tool the GUI and tutorial context it needs without adding tool commits to `docu`; do not push the tool to `scanberg/viamd` or include it in an upstream pull request. The local `docu` currently has 21 commits ahead of the local `upstream/master` ref, so it is not currently upstream-aligned. After the merges, reconcile `docu` with `scanberg/viamd` and rebase `youtube-capture` onto the refreshed `docu`.

The tool can read the tutorial pages and their screenshot TODOs from the separate wiki working copy. Keep the wiki as the source for tutorial content; keep capture scenarios and tool code in the VIAMD repository.

## Idea

Record what is done in the GUI (mouse movement, clicks, drags, keys) and replay it to produce a tutorial video without a screen recorder. The result should be easy to redo after a GUI change, and the steps should double as the video script.

### Not in the movie maker

The movie maker renders the 3D scene at a fixed time step with keyed camera, looks and overlays. A tutorial video shows the whole GUI with a cursor. Keep them apart. The existing frame-to-ffmpeg pipeline of the movie code can be reused.

### Design sketch

Start with authored scenarios rather than recording raw input. Raw mouse/key recordings are brittle when window layout, DPI, timing or GUI labels change; semantic steps are easier to edit and repair.

1. **Scenario:** a small file beside a `.via` names the workspace, fixed window size, ordered GUI actions, waits, captions and narration cues.
2. **Actions:** begin with semantic steps such as click, type, key, drag, wait for evaluation to finish, and capture. Reuse the existing Python/X11 capture helpers for the first prototype; keep it outside the VIAMD runtime.
3. **Replay:** launch VIAMD with the workspace, execute the steps deterministically, draw a cursor and click marker, and encode frames with ffmpeg.
4. **Narration:** one step should correspond to a tutorial beat. Draft narration from the wiki text, then review it manually.
5. **Shared source:** keep scenario steps linked to the relevant wiki headings so screenshots, narration and tutorial order can be checked together. Avoid generating or rewriting wiki prose automatically in the first version.

Prototype files: `tools/youtube-capture/`. The runner reads JSON scenarios, supports dry-run and live preview, and records MP4 with GStreamer. Captions/cursor overlays remain available, with a clean-frame mode for voiceover-led videos. Scroll and drag are supported; external workspaces are opened through VIAMD's GUI after startup so the built-in default dataset cannot overwrite them. Tutorial 1 has a captioned 80-second sample. Tutorial 2 has a verified 10-minute silent H.264 draft (`~/Videos/VIAMD/tutorial-2-aspirin-phospholipase.mp4`, 1280x720, 10 fps), time-coded narration, and production guide. The draft shows the aspirin complex and live Timeline, but both Distribution panels are empty and an atom tooltip appears in the opening frame; fix plot fitting and pointer placement before publication. Some GUI steps in the narration are explanatory rather than automated. All tool code is isolated in this directory; VIAMD application source and the existing docs screenshot helper are unchanged. Raw-input recording remains deferred.

## 2026-10-13 Handoff

- Tutorial 2 materials: `tools/youtube-capture/scenarios/tutorial-2-full.json`, `tools/youtube-capture/tutorials/tutorial-2-narration.md`, and `tools/youtube-capture/tutorials/tutorial-2-production.md`.
- Video: `~/Videos/VIAMD/tutorial-2-aspirin-phospholipase.mp4`, verified at 10:00, H.264, 1280x720, 10 fps, 148 MB. It is silent by design; WAV narration is not recorded or muxed yet. The tested WAV-to-AAC mux command is in the production guide.
- The startup helper now opens external `.via` files through File > Open Workspace after launching VIAMD without arguments. Otherwise the compiled-in 1ALA dataset loads after and replaces the workspace. The external aspirin data is staged by symlink from `~/Desktop/bioexcel/aspirin/` into `/tmp/shotwork`.
- On this X11 keyboard layout, `/` types as `Shift+7`; the replay helper's path typing now accounts for that.
- The first high-bitrate disk spool was stopped before completion. The final take uses `/dev/shm` for frame JPEGs and 2.2 Mbps H.264; storage remained within available RAM and disk.
- Final video review: aspirin complex and populated Timeline are visible; both Distribution panels are empty, and a native atom tooltip appears in the opening frame. A separate double-click reset test fitted one histogram but did not keep both subplots fitted together.
- Before publication: fix/reset both Distribution subplot ranges, move the pointer to a neutral area before frame zero, rerender and inspect minute samples, then record the separate voice track and mux it using the production guide. The narration includes tutorial actions that the current scenario explains but does not enact one-by-one.
- Code is on `youtube-capture` and was pushed to the user's fork `origin`; do not push it to `upstream` or merge it into `docu` until the user tests and approves it.

### Problems to solve

- Script evaluation, density computation and file loading take a variable number of frames. Replay needs "wait until idle" markers.
- Native file dialogs cannot be recorded. Start from a loaded workspace.
- The layout depends on window size, DPI and `imgui.ini`. Replay from a fixed size.
- Dear ImGui Test Engine does scripted GUI automation, but its licence must be checked for this project.

### Until then

Record with a screen recorder (OBS) from the example workspaces in the wiki, using the tutorial pages as the script.

## Tentative list of videos

The existing playlist (8 videos, December 2023 to January 2024): https://www.youtube.com/playlist?list=PLNx9MpJY8ffr9CeK7WefdOnuGRw_E5rSj

| # | Existing video | Wiki pages | Verdict |
|---|---|---|---|
| 1 | VIAMD - Visual Interactive Analysis of Molecular Dynamics | Home | redo as a short trailer with the new GUI |
| 2 | VIAMD - Quick Overview | Quick tour | redo |
| 3 | Loading Trajectory and Representations | Load data and workspaces, Representations | redo, small changes |
| 4 | Timeline, Distribution, Visual Feedback and Filtering | Timeline, Distributions, Working with series | redo: series model, chips, Duplicate, Export |
| 5 | Properties, Selection, Import/Export and Workspace | Script editor, Spatial view, Import and export | redo: `import()` is gone, `attr` and the Attributes window replace it |
| 6 | Spatial Distribution Function | Density volume | redo: layers as chips, isosurface by default |
| 7 | Remapping Element and Recentering Trajectory | Spatial view | check, probably only a retake of the screen |
| 8 | Dynamic Selection and Counting | Language reference, Tutorial 2 | check against the current language |

### New videos

| Topic | Wiki pages |
|---|---|
| Install on Linux, macOS and Windows | Install |
| The Attributes window and drag and drop into plots | Attributes |
| Ramachandran and Shape space with series | Ramachandran, Shape space |
| Keys that save time: `F1`, `F2`, `Space`, `Ctrl`+`Shift`+`A` | Keyboard and mouse |
| Movie maker 1: first movie, quick start | Movie maker |
| Movie maker 2: camera keyframes, look at and focus on | Camera and keyframes |
| Movie maker 3: looks, representations, overlays | Looks and representations, Overlays |
| Movie maker 4: output, timeline, recording | Output and timing, Timeline |
| Tutorials 1 to 4 as walk-throughs | Tutorial pages |
| VIAMD for VeloxChem | VeloxChem |

Suggested order: 4, 5, 6 first (the most changed), then 2, 3, the movie maker series, and the rest.
