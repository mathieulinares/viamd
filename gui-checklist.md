# GUI checklist for the movie maker

Everything here was written without being able to run the GUI. Tick a box when it works, and write what you saw next to it when it does not. Start from the example workspace `docs/examples/aspirin_phospholipase_movie.via`: put it next to `aspirin-phospholipase.gro` and `.xtc` and open it with **File > Open Workspace**.

Open **Windows > Movie**: the timeline is on the left and tabbed controls on the right. Expand **Lanes and layout** for the timeline's options. In the checks below, "Movie Timeline" means this left panel; the old **Add Text**, **Add Timeline**, etc. buttons are now entries in **Overlays > Add overlay...**. Select an overlay to edit its **Content**, **Timing** or **Appearance**.

## 0. Combined editor and compact inspector

- [ ] Resize the editor and drag its divider: timeline left, controls right, both independently scrollable. Hide either panel with **Timeline** / **Controls**; one always stays visible. Shared playback, repeat, scrubber and Add Keyframe still work with either panel hidden.
- [ ] Use the large top-right button to switch between **Scene view** and **Movie preview**. Scene view hides the movie frame, overlays and property highlights; Movie preview restores them. Camera-path editing stays available. Neither switch starts/stops preview playback or changes the recording settings.
- [ ] **Output**, **Timing**, **Camera**, **Looks**, **Representations** and **Overlays** show their own controls without a long stack of sections. The viewport-mode button and editing controls are disabled during recording, while Stop/Pause/Resume remain usable.
- [ ] **Add overlay...** offers every supported type. The list stays compact and scrollable; selecting an item shows only its settings. Enable/disable, Duplicate, Remove and undo/redo still work, including an empty list and removing its last item.
- [ ] Clicking an overlay bar on the left selects the same overlay on the right and opens **Overlays** (also after hiding Controls). Expand a subplot to edit its timing/title; expand **Plot style** for fonts, lines, colours and plate presets.
- [ ] A time bar has a neutral grey unfilled track, including with a purple fill. New time bars and the updated example have a grey plate behind the labels and bar. Change **Appearance > Background**, fade, preview and record: colours and transparency match.
- [ ] Select a distribution and set **Number of bins** to 32, 37 and 128: temporal histograms use those counts. The Distributions window's own settings do not change. **Use source bin counts** restores each series' count. A script distribution only coarsens to an available divisor.
- [ ] In **Timeline markers**, target one of the four example timeline subplots: both the line and label appear only there, including when that subplot is not first in the stack. **All timeline subplots** restores the previous behavior.
- [ ] Bin counts and marker targets survive save/load, undo/redo and movie length changes. Rename a subplot or reorder panels: marker targets retain their identity. A missing target is reported and is not silently moved to another subplot.
- [ ] The edited example retains its four timeline subplots, camera/representation keys, overlay positions/colours and preview time (about 55.45 s). Its distribution has 128 bins and its time bar a grey plate.

## 1. Workspace

- [ ] Open the example. It loads without errors in the log, the movie is 80 s long and the playhead is at about 55.45 s. Drag the playhead to 0 and play the preview.
- [ ] Save it under another name and open that. Keys, overlays (with the subplots and the in and out times of their plots), markers, representation keys, **Transition (s)**, **Scale**, **Samples per frame**, the range settings and the lanes of the Movie Timeline are all there.
- [ ] Open an older workspace that has no `Id` on its representations, no overlays and no `Overlays` entry (for example `~/Desktop/try.via`). It loads, the logo is in the top left, and a representation key can be added.
- [ ] Open a workspace with keyframes from before the follow target existed. The keys are unchanged.
- [ ] Open a workspace from before the plot overlays were separate (a timeline or distribution overlay that picked a subplot by position, or one figure with both): each opens as a timeline and a distribution of the same subplots.

## 2. Overlays

### 2.1 Every overlay

- [ ] A new movie (no workspace loaded) has the VIAMD logo in the top left corner. **Remove** it, save, open the file again: it stays gone.
- [ ] The logo has a transparent background (the picture shows through, no white box) and no dark edge; its lettering is black and the molecule keeps its colours; on a light picture it has a strong contrast, on a dark one a light **Background** plate makes it readable. It is as high as its **Size** says, with no empty margin around it.
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
- [ ] When a property overlay ends (play past it, or scrub out of its range, with the mouse over a Movie window), its highlighted atoms go with it: nothing stays highlighted, not in the viewport and not in a recording. Atoms you highlight by hovering or select yourself are not cleared by it.
- [ ] In a recording the geometry and the labels are in the frames, in the right places and of a similar size.
- [ ] With **Fade in** and **Fade out** at 1 s the highlighted atoms, the lines or points and the labels fade in and out, in the preview and in the recording, and nothing is left behind after it ends.

### 2.4 Seeing them while you edit and recording them

- [ ] **Show frame**: with the Movie window open the viewport shows the frame of the movie (a box with the size above it, the outside dimmed) and the view is a little wider. Set the resolution to 1920x1080, then to a tall custom size (1080x1920) and back: the box follows the proportions and the overlays are laid out in it.
- [ ] A recording matches what the box showed: the camera, the title, the logo, the timeline and the distribution, the labels of a property overlay and their size, the scale bar length. In the video the text, the bar and the logo are the right way up, not stretched, sharp at 1920x1080 and at **Scale** 25 %.
- [ ] Switching to **Scene view** restores the plain view and hides overlays. The dimming and the wider view are gone while recording and when the Movie window is closed. Clicking atoms in the viewport still selects the right one.
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
- [ ] **Keep upright**: a keyframe captured upside down or tilted plays level; a spin or loop never flips the camera. **Up** / **From view** pick the up axis. **Roll (deg)** tilts a keyframe and the tilt eases between keys; **0** levels it. Go To, the path cameras and Ctrl + click on the path agree with the preview. Off: the Roll column shows each key's tilt and **Level** straightens it. Undo, save and load keep all of it; an older workspace opens with it off.

- [ ] Camera lane (Movie Timeline, **Camera lane**, at the top): orange dots with the key numbers on the first row; the one at the preview time is yellow.
- [ ] Camera lane, keys: drag a dot sideways (the key moves, the preview time follows, the list re-sorts when released, **Snap to frames** applies); click a dot (the view goes to the key); Ctrl+Z undoes a drag.
- [ ] Camera lane, menu: right-click a dot, type a name, Enter: the name is shown next to the number, in the **Name** column of the table, and comes back after saving and opening the workspace. **Go to** and **Remove** work; Ctrl+Z brings a removed key back.
- [ ] Camera lane, double-click an empty place: a key appears there and the camera does not jump (the view stays as it was; **Show path in viewport** shows the same path). It is not added where a key already is. Not possible while recording.
- [ ] Camera lane, bands: keys that **Follow** show a teal band over the run of keys, **Look at** an atom a blue band with the atom number, a key with **Spin** a purple band like `+2 x` over the stretch before it, a key with **with trajectory frame** a green mark with the frame. Hovering shows the details. A key added by double-click between two that follow also follows.

- [ ] Path in the viewport (**Show path in viewport**, with the Movie window or the Movie Timeline open): blue eye path, yellow look-at path, brighter after the preview time than before it. Scrubbing the preview time moves the green camera: a green dot on the eye, a green ring with a cross and the words **looks at**, a thick green line with an arrow between them.
- [ ] Path, ticks: dots at round times with labels (not on top of each other), chevrons pointing the way the camera goes, yellow diamonds on the look-at path at the same times. With an **Ease in/out** key the dots crowd together near it. Zoom the viewport in and out: the labels stay readable. **Time ticks** off removes them.
- [ ] Path, sight lines and cameras: **Sight lines** gives thin lines from eye to look-at at each tick, **Cameras** the pyramids at the keys (orange at the preview time), **Spin rings** a purple circle with arrows and `+2 x` for a key with spin turns, the arrows turning the same way as the camera in the preview. Each can be switched off. The choices are saved in the workspace.
- [ ] Path, keys: the eye handle is a numbered dot (name beside it), the look-at handle a ring with a cross and the number; white, teal for follow, blue for look at an atom, orange at the preview time. Hover a handle: the key lights up in the camera lane too; hover a dot in the camera lane: the handle in the viewport lights up.
- [ ] Path, click: clicking a handle goes to that key (the viewport must not select anything or rotate while the mouse is over a handle).
- [ ] Path, drag the eye handle: the eye follows the mouse in the plane facing you and the camera keeps looking at the same point (the ring does not move); drag the ring: what the camera looks at moves, the eye stays; Ctrl + drag moves both. Esc during a drag puts the key back. Ctrl+Z undoes a whole drag in one step. The camera lane, the table and the path follow.
- [ ] Path, keys that follow the target or an atom: the path is computed along the trajectory as before; dragging a handle of such a key still works.
- [ ] Path, Ctrl + click on the path (eye or look-at) shows a white ring with `add a key at ... s` and adds a key there; the camera does not jump; there is no key added where one is.
- [ ] The path is not in the recorded frames or a screenshot; it is hidden while recording and while a key waits for **Look at** (clicking an atom still works).
- [ ] Path with depth of field on: the pink focus frame is still drawn at the preview time.

## 4. Timing and the Movie Timeline window

- [ ] **Snap to frames**: drag a key, a parameter key, a representation key, the blue anchors and the playhead. They land on frames of the movie. With it off they move freely.
- [ ] Drag the blue **Start** and **End** anchors: the trajectory waits before the start and holds after the end.
- [ ] Change **Movie length (s)** to double: the pacing is the same at half the speed, the overlays and their fades scale. Undo restores it.
- [ ] Ctrl + wheel zooms the time and dragging the background pans: all lanes move together. The plain wheel scrolls the window. **Show whole movie** resets.
- [ ] **Lane height**: raising it makes every lane taller in proportion and the window scrolls; a long representation overview or overlay list grows only its own lane, the others keep the slider's height. Dragging a divider resizes only the two lanes beside it. **Fit to window** gives the old behaviour (the lanes share the window, the wheel zooms).
- [ ] Resize the lanes by dragging between them.
- [ ] Hide each lane with its box: **Camera lane**, **Trajectory**, **Distance**, **Field of view**, **Look parameter lane** (its picker goes too), **Representation lane**, **Representation overview**, **Overlay lane**. With all of them unticked the window says so.
- [ ] Save and open the workspace: the ticked lanes, the lane height and **Fit to window** come back.
- [ ] Overlay lane: one bar per overlay with its name and a colour per kind (dimmer when switched off). Drag a bar: the overlay moves (**Shown (s)** follows, a timeline's or a distribution's subplot in and out times move with it). Drag an end: only that end moves. The bars follow **Snap to frames**. The ticks in a bar are where its subplots come in, the yellow triangles are markers. Ctrl+Z undoes a drag.
- [ ] Drag a frame pin on the trajectory track up and down: the frame changes. Distance and field of view dots only move in time.
- [ ] Click an orange marker on the timeline of the Timelines window: the view goes to that key.
- [ ] Open a workspace saved with **Movie** open: the combined editor opens according to the workspace's Movie window setting; there is no separate Movie Timeline window.

## 5. Backward trajectory

- [ ] Give two keys frames so that the second is lower than the first (tick **Frame** on both): the trajectory plays backward between them and slows to a stop where it turns.
- [ ] Without frame keys: set **Trajectory Frames** with the start above the end. The preview and a recording play backward.

## 6. Representations

- [ ] In the Movie window, **Representations**: pick `water`, **Visible**, **Key Now** at two times. Scrub across them: it grows in or shrinks away over **Transition (s)** (2 s), it does not pop.
- [ ] **Transition (s)** 0: it appears and vanishes at once. 10: a slow change. Two keys closer together than the transition: it turns around from where it was.
- [ ] A cartoon and a ribbon at very small sizes look right while they grow in and shrink away. A dipole or electronic-structure representation stays full size and vanishes at the end of the transition.
- [ ] Key a scale, **Saturation**, **Tint scale** and **Tint color**: scrubbing changes the picture. The example shrinks the ball scale of the ligand from 30 s to 38 s.
- [ ] **Base color** is seen on a representation with **Uniform** color mapping.
- [ ] Representation overview: one row per system, with labelled blocks coloured by representation type. A plain enabled representation with no visibility keys covers the whole movie. Blocks grow in and fade away over **Transition (s)**; small triangles mark keys of other properties.
- [ ] Overview, editing: drag a block or either edge; snapping and same-representation neighbours constrain its times, but other representations may overlap freely. Shorten/lengthen a block at the movie end and move one away from the start. Right-click > Remove block and double-click empty space > choose a representation work. Removing the first or only block leaves the representation hidden there. Ctrl+Z restores each edit.
- [ ] Overview, systems: `protein-cartoon`, `protein-cpk`, `protein-vdw` and plain `protein` share one labelled row. Systems are clearly separated (gap, divider, alternate shading); blocks of one system sit close together. A block starting where another ends stays on the same line and cross-fades with it. **One line per system** gives every system the same height (stacked blocks split it); unticked, each stacked block gets a full line. Renaming regroups rows.
- [ ] Right-click a CPK block and switch it to VDW in the same system: its interval transfers, overlapping target blocks merge, other systems and property keys stay intact. Undo/redo and workspace save/load retain the result. The updated example groups protein/protein-cpk/protein-vdw and ligand/ligand-vdw without changing its saved keys.
- [ ] **Swap with the next of its group** (select a member by clicking its bar): at the preview time the selected one shrinks away while the next one grows in; with only one member the button is disabled; Ctrl+Z undoes it.
- [ ] Clicking a bar selects that representation for the **Representation lane** below; renaming a representation regroups the rows.
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
