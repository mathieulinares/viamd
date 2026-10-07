# GUI checklist for the movie maker

Everything here was written without being able to run the GUI. Tick a box when it works, and write what you saw next to it when it does not. Start from the example workspace `docs/examples/aspirin_phospholipase_movie.via`: put it next to `aspirin-phospholipase.gro` and `.xtc` and open it with **File > Open Workspace**.

Windows: **Windows > Movie** and **Windows > Movie Timeline** (it is closed on startup and after loading a workspace).

## 1. Workspace

- [ ] Open the example. It loads without errors in the log, and the playhead is at 0.
- [ ] Save it under another name and open that. Keys, overlays (with the subplots and the in and out times of their plots), markers, representation keys, **Transition (s)**, **Scale**, **Samples per frame**, the range settings and the lanes of the Movie Timeline are all there.
- [ ] Open an older workspace that has no `Id` on its representations, no overlays and no `Overlays` entry (for example `~/Desktop/try.via`). It loads, the logo is in the top left, and a representation key can be added.
- [ ] Open a workspace with keyframes from before the follow target existed. The keys are unchanged.
- [ ] Open a workspace from before the plot overlays were separate (a timeline or distribution overlay that picked a subplot by position, or one figure with both): each opens as a timeline and a distribution of the same subplots.

## 2. Overlays

### 2.1 Every overlay

- [ ] A new movie (no workspace loaded) has the VIAMD logo in the top left corner. **Remove** it, save, open the file again: it stays gone.
- [ ] **Add Logo**, **Add Text**, **Add Time Stamp**, **Add Scale Bar**: each shows in the viewport; **Position**, **Size**, **Color** and **Background** work. The scale bar follows the zoom of the camera. The time stamp starts at 0 and only grows, also when the trajectory plays backward, and reads the time since the movie started when it is shown late (set its range to start at 40 s: it does not start at 0 there). It ends at the same value as **Time that has gone** of the time bar.
- [ ] **Add Image...** with a png that has transparency and with a jpg: it shows in the viewport and in a recording; **Browse...** and **Reload** work. Save, copy the workspace and the image together to another folder and open it: the image is found. Remove the file: the message turns red and nothing crashes.
- [ ] **Add Time Bar** on a movie with a slow stretch, a fast stretch and a hold: it fills slowly, fast and not at all. Where the trajectory plays backward it still fills forward. **Time that has gone** ends at the whole, **Speed** reads about x1.0 where the trajectory plays at the Animation speed.
- [ ] **Size**: switch the unit between % and points, the size does not jump. A recording at 1920x1080 and at 3840x2160 has text of the same size in points. A saved workspace keeps the unit.
- [ ] **Fade in** and **Fade out** work when scrubbing across the start and end of an overlay. The **Background** plate is rounded, sized to the text and fades with it.
- [ ] **Duplicate** adds a copy right below, which can be changed independently. The **Type** can be changed.
- [ ] Undo and redo (Ctrl+Z, Ctrl+Y) restore overlay edits, also the **Background**.

### 2.2 Timeline and distribution

- [ ] **Add Timeline** with series in two Timelines subplots (e.g. `d` and `nb`): wide and low at the bottom center, the subplots stacked, each with its own scale, legend and value, the x labels under the last. The curves grow to the right as the preview plays.
- [ ] A timeline that is shown from 25 s (set its range): its axis is still the whole movie, from 0, with the labels it would have anyway, and it appears already drawn up to the time that has gone. A distribution that appears late has already counted the frames played before.
- [ ] Frame keys that play the trajectory backward: the curves still grow to the right and the dot follows the frame. Where the movie turns back the curve goes on to the right, in a hold it does not grow. **Horizontal axis** *Trajectory time* turns the axis around for a backward movie.
- [ ] **Add Distribution** with a series in a Distributions subplot: narrow and tall at the middle right. The bars grow with the frames played to the shape of the whole, the line marks the shown frame. A script distribution (an RDF) is drawn as it is.
- [ ] **Add subplot**, **Up** and **Remove** work and only offer the subplots of the overlay's own window. A subplot without series is reported and not drawn. Changing **Type** between the two keeps only the subplots of the new kind.
- [ ] **As the movie plays** off shows the whole plot. **Value** and **Markers** off hide the number and the marker lines.
- [ ] Subplot names: name the subplots in the **Subplots** menu (**Names**), lower and raise **Num Subplots**, drag series between subplots, save and reopen: the overlay draws the same subplots.
- [ ] **Titles**: the typed **Title** of a subplot, else its name, above it; none for an unnamed one; nothing overlaps; unticking removes them.
- [ ] Properties at different times: set the second subplot of a timeline to come in at 30 s (**In at preview time**, or the drag; **out at** takes it away). The first is drawn from the start of the movie. The second appears at 30 s with a fade and with its curve from the start up to 30 s already there, and goes on growing. The plate is only behind the first before and grows downward when the second comes in (no empty grey box), and the first does not move. The same for a distribution (it has counted every frame since the movie started). A subplot added with **Add subplot** while the preview is later comes in there.
- [ ] Changing the movie length scales the in and out times; Ctrl+Z restores them; they come back after saving and opening.
- [ ] The look: **Text (points)** and **Lines (points)** stay the same size relative to the frame in a recording at another resolution; **Colours** gives the series another set of colours in the order of the stack; the two plate buttons give a readable plot on a dark and a light picture; **Width**, **Size**, **Position** and the fades work; nothing is cut off at the edge.
- [ ] Markers: **Add marker at the preview time** and **Add one at each camera key**; edit the time and the label; a timeline draws a line and the label where the movie gets to it (the label on the left of the line near the right edge); changing the movie length moves them; Ctrl+Z restores a removed one; they come back after saving and opening.

### 2.3 Property visualization

- [ ] **Add Property** and pick a script property (a distance, an angle): its visualization (highlight, lines, labels) is in the viewport at the preview time and not outside the range of the overlay. The warning shows for a name the script does not have.
- [ ] In a recording the geometry and the labels are in the frames, in the right places and of a similar size.
- [ ] With **Fade in** and **Fade out** at 1 s the highlighted atoms, the lines or points and the labels fade in and out, in the preview and in the recording, and nothing is left behind after it ends.

### 2.4 Seeing them while you edit and recording them

- [ ] **Show frame**: with the Movie window open the viewport shows the frame of the movie (a box with the size above it, the outside dimmed) and the view is a little wider. Set the resolution to 1920x1080, then to a tall custom size (1080x1920) and back: the box follows the proportions and the overlays are laid out in it.
- [ ] A recording matches what the box showed: the camera, the title, the logo, the timeline and the distribution, the labels of a property overlay and their size, the scale bar length. In the video the text, the bar and the logo are the right way up, not stretched, sharp at 1920x1080 and at **Scale** 25 %.
- [ ] Switching **Show frame** off restores the plain view. The dimming and the wider view are gone while recording and when the Movie window is closed. Clicking atoms in the viewport still selects the right one.
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
- [ ] Ctrl + wheel zooms the time and dragging the background pans: all lanes move together. The plain wheel scrolls the window. **Show whole movie** resets.
- [ ] **Lane height**: raising it makes the lanes taller and the window scrolls; every lane keeps at least that height. **Fit to window** gives the old behaviour (the lanes share the window, the wheel zooms).
- [ ] Resize the lanes by dragging between them.
- [ ] Hide each lane with its box: **Trajectory**, **Distance**, **Field of view**, **Look parameter lane** (its picker goes too), **Representation lane**, **Overlay lane**. With all of them unticked the window says so.
- [ ] Save and open the workspace: the ticked lanes, the lane height and **Fit to window** come back.
- [ ] Overlay lane: one bar per overlay with its name and a colour per kind (dimmer when switched off). Drag a bar: the overlay moves (**Shown (s)** follows, a timeline's or a distribution's subplot in and out times move with it). Drag an end: only that end moves. The bars follow **Snap to frames**. The ticks in a bar are where its subplots come in, the yellow triangles are markers. Ctrl+Z undoes a drag.
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
