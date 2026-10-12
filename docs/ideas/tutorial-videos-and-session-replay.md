# Tutorial videos and GUI session replay (idea, for later)

Status: idea only, nothing implemented. Wait until `uinification` and `video` are merged, because the GUI is still changing.

## Idea

Record what is done in the GUI (mouse movement, clicks, drags, keys) and replay it to produce a tutorial video without a screen recorder. The result should be easy to redo after a GUI change, and the steps should double as the video script.

### Not in the movie maker

The movie maker renders the 3D scene at a fixed time step with keyed camera, looks and overlays. A tutorial video shows the whole GUI with a cursor. Keep them apart. The existing frame-to-ffmpeg pipeline of the movie code can be reused.

### Design sketch

1. **Recorder:** log the input that ImGui receives each frame (mouse position, buttons, wheel, keys, text) with timestamps.
2. **Replay:** feed the events back at a fixed time step, render every frame offline, send it to ffmpeg. Draw a cursor and a ring at each click.
3. **Steps file** next to the `.via`: timed events and captions. Editable by hand. It is the list of commands for one video.
4. **Narration:** one text per video, one step per tutorial heading, drafted from the wiki page and checked by a person.
5. **One source for text and video:** each tutorial page has a single list of steps that both the wiki text and the narration follow.

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
