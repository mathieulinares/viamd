# GUI checklist for the movie maker

Everything here was written without being able to run the GUI. Tick a box when it works, and write what you saw next to it when it does not. Start from the example workspace: copy `docs/examples/aspirin_phospholipase_movie.via` next to `aspirin-phospholipase.gro` and `.xtc` and open it with **File > Open Workspace** (or use the copy `workspace_rep_keys.via` that sits next to the data).

Windows: **Windows > Movie** and **Windows > Movie Timeline** (it is closed on startup and after loading a workspace).

## 1. Workspace

- [ ] Open the example. It loads without errors in the log, the movie is 80 s long and the playhead is at 0.
- [ ] Save it as another name and open that. The keys, overlays, representation keys, **Transition (s)**, **Scale**, **Samples per frame** and the range settings are all there.
- [ ] Open an older workspace that has no `Id` on its representations, no overlays and no `Overlays` entry (for example `~/Desktop/try.via`). It loads, the logo is in the top left, and a representation key can be added.
- [ ] Open a workspace with keyframes from before the follow target existed. The keys are unchanged.

## 2. Overlays and the logo

- [ ] A new movie (no workspace loaded) shows the VIAMD logo in the top left corner of the viewport at the preview time.
- [ ] **Remove** the logo, save, open the file again: the logo is still gone.
- [ ] **Add Logo** adds another one. **Position**, **Size** (its height), **Color** (tint) and **Background** work.
- [ ] **Add Text**, **Add Time Stamp** and **Add Scale Bar**: each shows in the viewport. The scale bar follows the zoom of the camera. The time stamp shows the time of the trajectory frame and counts back when the trajectory plays backward.
- [ ] **Fade in** and **Fade out** work when scrubbing across the start and end of an overlay.
- [ ] **Background** plate of a text overlay: rounded, sized to the text, fades with it.
- [ ] Undo and redo (Ctrl+Z, Ctrl+Y) restore overlay edits, also **Background**.
- [ ] Record a few seconds. In the video the text, the bar and the logo are the right way up, not stretched, sharp at 1920x1080 and at **Scale** 25 %, and where the viewport showed them (the proportions can differ).
- [ ] Record with **Samples per frame** above 1: the overlays are not smeared or doubled.

## 3. Camera keys

- [ ] **Add Keyframe (current view)** and **K** add a key at the preview time. A key at the same time is replaced and keeps its spin and frame.
- [ ] **Key on Selection**: select a molecule and press it. The view moves to frame it, the key is added. Select one that is split over the periodic boundary: it is framed whole.
- [ ] **Copy** on a row, move the preview time, **Paste Keyframe**. Also Ctrl+C at a key's time and Ctrl+V somewhere else. A pasted key replaces one at that time and keeps its spin, ease and follow settings.
- [ ] Drag a row by its number onto another row. The pose moves, the times stay.
- [ ] **Set Follow Target** from a selection, tick **keys follow target**, add two keys at different trajectory frames (tick **with trajectory frame**), play the preview: the camera stays on the target.
- [ ] With a follow target and keys that follow, **Show path in viewport**: the blue (eye) and yellow (look-at) paths bend with the target through the trajectory and match what **Play Preview** does. The path grows for a moment and the interface does not stall. Moving a key or changing the target restarts it, and the old path stays until the new one is done. The green camera at the playhead is where the preview camera is.
- [ ] **Follow** on an existing key: **Go To** it first, then **Follow**. Without Go To an error is logged and nothing changes. **Unfollow** makes it fixed again.
- [ ] **Look at** on a key, click an atom: the key tracks that atom through the trajectory. **Update position** moves the eye and keeps what it looks at.
- [ ] Double-click on the viewport aims the camera at the clicked point with the eye fixed.
- [ ] **Add Orbit** with **Snap to frames** on: the end key is where it should be.
- [ ] **Close Loop** and **Seamless loop**: no jump when the preview repeats.

## 4. Timing and the Movie Timeline window

- [ ] **Snap to frames**: drag a key, a parameter key, a representation key, the blue anchors and the playhead. They land on frames of the movie. With it off they move freely.
- [ ] Drag the blue **Start** and **End** anchors: the trajectory waits before the start and holds after the end.
- [ ] Change **Movie length (s)** to double: the pacing is the same at half the speed, the overlays and their fades scale. Undo restores it.
- [ ] Zoom (scroll) and pan (drag) the timeline: all tracks move together. **Show whole movie** resets.
- [ ] Resize the rows by dragging between them. Hide **Trajectory**, **Distance** and **Field of view** with their boxes.
- [ ] Drag a frame pin on the trajectory track up and down: the frame changes. Distance and field of view dots only move in time.
- [ ] Click an orange marker on the timeline of the Timelines window: the view goes to that key.
- [ ] Open a workspace that was saved with the timeline open: it stays closed. Open it from the menu.

## 5. Backward trajectory

- [ ] Give two keys frames so that the second is lower than the first (tick **Frame** on both): the trajectory plays backward between them and slows to a stop where it turns.
- [ ] Without frame keys: set **Trajectory Frames** with the start above the end. The preview and a recording play backward.

## 6. Representations

- [ ] In the Movie window, **Representations**: pick `water`, **Visible**, **Key Now** at two times. Scrub across them: it grows in or shrinks away over **Transition (s)** (2 s), it does not pop.
- [ ] **Transition (s)** 0: it appears and vanishes at once. 10: a slow change. Two keys closer together than the transition: it turns around from where it was.
- [ ] A cartoon and a ribbon at very small sizes look right while they grow in and shrink away. A dipole or electronic-structure representation stays full size and vanishes at the end of the transition.
- [ ] Key a scale, **Saturation**, **Tint scale** and **Tint color**: scrubbing changes the picture. The example tints the protein blue-grey from 50 s to 60 s.
- [ ] **Base color** is seen on a representation with **Uniform** color mapping.
- [ ] Representation lane in the Movie Timeline: pick a representation and a property with the lists. Drag a key in time and value, double-click to add, right-click to remove. For **Visible** a double-click turns it around, and the first one also keys the state at time 0.
- [ ] Hide the lane with its box.
- [ ] Duplicate a representation that has keys: the copy has none. Remove one that has keys: its keys are gone from the table.
- [ ] Save and open the workspace: ids and keys survive.
- [ ] After a recording, or when **Animate parameters** is switched off, the representations are as they were before.
- [ ] The **Representations** window is locked while recording and unlocked afterwards.

## 7. Look parameters and depth of field

- [ ] Key a parameter (**Key Now**) at two times, scrub: it follows. Drag its keys in the lane, double-click to add, right-click to remove.
- [ ] **Animate parameters** off lets go of the parameters and the representations.
- [ ] Depth of field **Focus** modes: look-at point, distance (key **Focus distance**), follow target. The focus plane shows in magenta in the viewport.

## 8. Recording and output

- [ ] **Start Recording** to a folder with **PNG sequence**: the files are numbered from 0, the view and the playback are put back afterwards.
- [ ] **MP4 video, H.264**: the file plays.
- [ ] **MP4 video, H.265** and **WebM video, VP9** (ffmpeg needs libx265 and libvpx-vp9): the files play, they have the right extension in the log and in the workspace copy. The **Quality (CRF)** slider range changes with the format (0 to 63 for VP9).
- [ ] **Pause** and **Resume** in the Movie window and on the banner: the recording carries on from the same frame, no duplicated frame, no skipped frame.
- [ ] The time left appears after a few frames and is about right.
- [ ] **Scale** 25 %: the frames are small (and even in size) and render quickly, the overlays scale.
- [ ] **Samples per frame**: 1 is fast and a bit rough at the edges, 8 or more is smooth.
- [ ] **Render only a range**: record 2 to 4 s. With PNG the files keep their numbers from the whole movie (frame 48 is `..._00048.png`). With MP4 the video is 2 s long.
- [ ] **Save a workspace copy with the movie**: `prefix.via` appears next to the movie, and the name of the workspace that is open in VIAMD does not change.
- [ ] **Esc** and **Stop Recording** stop it and the frames so far are kept.
- [ ] Open the saved copy and record again: the same movie.

## 9. Other platforms

- [ ] Windows: record an MP4 (the ffmpeg pipe uses `_popen`).
- [ ] macOS: record an MP4 (`popen`).
