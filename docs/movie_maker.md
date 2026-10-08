# Making movies

VIAMD can record a movie of a trajectory. You set how long the movie is, and you can place camera keyframes, key look parameters (exposure, background and so on) and the properties of representations (show or hide one, change its size or color), and add overlays (text, a time stamp, a scale bar, a time bar, images, plots of properties of the trajectory, the visualization of a script property and the VIAMD logo). The result is an MP4 file (H.264 or H.265) or a WebM file, encoded by ffmpeg, or a numbered PNG sequence.

![The earlier two-window movie editor, with the camera path drawn in the viewport](images/movie/overview.png)

Open the combined editor with **Windows > Movie**:

- **Left:** timeline lanes for the trajectory frame, camera, distance, field of view, looks, representations and overlays. Drag the divider to resize the two panels.
- **Right:** tabs for **Output**, **Timing**, **Camera**, **Looks**, **Representations** and **Overlays**. Both panels scroll independently. The **Timeline** and **Controls** boxes hide either panel; at least one stays visible.
- **Top:** shared preview playback, repeat, scrubber and **Add Keyframe** controls. The large button at the top right switches between **Scene view** (plain viewport) and **Movie preview** (recording frame and overlays). It does not start or stop playback or alter the recording.

The screenshots below show the earlier two-window layout; the controls now live in the combined editor.

## Quick start

1. Load a trajectory and open **Windows > Movie**.
2. Under **Output**, click **Select Output Folder...** and pick a format.
3. Set **Movie length (s)** and press Enter. At first the trajectory fills the whole movie.
4. Move the top preview slider to 0, set up the view, and press **K** (or **Add Keyframe**).
5. Move the preview time, set up another view, and press **K** again. Repeat as needed.
6. In **Camera**, tick **Animate camera** and press **Play Preview** to watch the movie. Choose **Movie preview** with the top-right button to see the recording frame and overlays.
7. Press **Start Recording**. Press **Esc** or **Stop Recording** to stop early.

When the recording is done, your own view, the animation playback and the screenshot settings are put back as they were.

## The Movie window

![The Movie window](images/movie/movie_window.png)

The top line has **Start Recording**, with a summary of what will be written: frames, seconds and pixel size. **Undo** and **Redo** (Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z) apply to everything on the movie's timeline: camera keys, look parameter keys, overlays, the movie length and the trajectory timing.

### Output

| Setting | What it does |
|---|---|
| **Select Output Folder...** | Where the movie is written. Recording is disabled until one is chosen. |
| **Filename Prefix** | The file name: `prefix.mp4`, or `prefix_00000.png`, `prefix_00001.png`, ... |
| **Format** | **MP4 video, H.264 (ffmpeg)**, **MP4 video, H.265 (ffmpeg)** and **WebM video, VP9 (ffmpeg)** pipe the frames straight into ffmpeg, so no image files are written (ffmpeg needs libx264, libx265 or libvpx-vp9 for the one you pick). H.265 files are usually noticeably smaller than H.264 at a similar look and are tagged so that QuickTime plays them. **PNG sequence** writes one image per frame. |
| **Quality (CRF)** | Video formats only. Constant rate factor: lower is better and larger. H.264: 18 is close to lossless, 23 is the default. H.265: a value 4 to 6 higher looks the same (default 28). VP9: 15 to 35 is the usual range (default 32, up to 63). |
| **ffmpeg** | MP4 only. The ffmpeg executable. A bare name is looked up on the PATH. |
| **Copy ffmpeg command** | PNG only. Copies a command that encodes the PNG sequence into an MP4. |
| **Resolution** | **Window** (the window size when recording starts), Full HD, Quad HD, 4K, 8K or **Custom** (**Res X**, **Res Y**). |
| **Output FPS** | Frames per second of the movie. The number of frames is length × FPS. |
| **Scale** | The size of the frames as a part of the size above (100, 75, 50 or 25 %). A small one renders much faster: use it to check a movie before making it in full. |
| **Samples per frame** | With temporal anti-aliasing on: how many rendered images are averaged into each frame. 0 uses the length of the jitter sequence. Fewer is faster, more is smoother. |
| **Render only a range** | Renders the frames between two times of the movie (**Range (s)**), e.g. to redo a part of it. With a PNG sequence the files keep the numbers they have in the whole movie, so they can replace the old ones. **Start at preview time** and **End at preview time** take the times from the playhead. |
| **Save a workspace copy with the movie** | Writes `prefix.via` next to the movie, with the camera path, looks, overlays and settings it was made from. Opening it and recording again gives the same movie. |

While recording, **Pause** stops the recording where it is and **Resume** carries on. The progress bar shows how many frames are done and, after a few frames, about how long is left.

### Timeline: how long the movie is and how the trajectory plays

![Timeline and camera keyframes](images/movie/timing_and_keyframes.png)

- **Trajectory Frames**: the first and last trajectory frame used. If the start is after the end, the trajectory plays backwards.
- **Movie length (s)**: the length of the whole movie, applied when you press Enter. **Everything on the timeline is scaled with it**: camera keys, look parameter keys, overlays and their fades, and when the trajectory starts and ends. The movie keeps its shape and only becomes faster or slower. Frame numbers and spin counts are not changed.
- **Trajectory plays (s)**: when, in movie time, the trajectory is at its first and at its last frame. Before the start the trajectory is held on its first frame, for example to fly over the structure first. After the end it is held on its last frame. These two points are the blue **Start** and **End** lines in the Movie Timeline, where they can also be dragged.
- The grey line below shows the average speed, in trajectory frames per second and compared with the Animation window's speed. **Match Animation speed** changes the movie length so the trajectory plays at the Animation window's speed.

To make the trajectory play faster or slower in parts of the movie, pin frames on camera keyframes (see **Frame** below). Between two pinned frames the trajectory plays at whatever speed is needed to get from one to the other. A pinned frame before the Start line or after the End line moves that line out to it.

### Camera keyframes

A keyframe is a camera (where the eye is, where it looks, its distance and its field of view) at a time in the movie. Between keyframes the camera moves smoothly. Before the first and after the last keyframe it holds still.

- **Animate camera**: while previewing or recording, the camera follows the keyframes. When it is off, the camera stays where you leave it and only the trajectory plays.
- **Show path in viewport**: draws the camera's path over the viewport while the Movie editor is open. It is not part of the recorded frames.
    - The path of the eye is blue and the path of the look-at point yellow. Each is brighter ahead of the preview time than behind it.
    - **Time ticks**: a dot at round times along the path (the step adapts to the length of the movie) with the time next to some, and chevrons that show the direction. Dots close together are where the camera is slow, dots far apart where it is fast, so the easing can be read from them. Yellow diamonds mark the same times on the look-at path.
    - **Sight lines**: a thin line from the eye to what it looks at at each tick.
    - **Cameras**: a camera drawn at each key, orange at the preview time.
    - **Spin rings**: for a key with a spin, the circle the camera goes round, with arrows the way it turns and the number of turns.
    - **Keys** have two handles: a round one on the eye with the key's number (and its name), and a ring with a cross on what it looks at, joined by a line. They are white, teal for a key that follows the target, blue for one that looks at an atom, and orange at the preview time. Hovering one highlights the key in the camera lane, and the other way round. **Click** a handle to go to the key. **Drag** the eye handle to move the eye (the camera keeps looking at the same point), drag the ring to move what it looks at (the eye stays). Hold **Ctrl** to move both. The handle moves in the plane facing you; **Esc** cancels. A drag is one undo step.
    - **Ctrl + click on the path** adds a key at that time on the path, without moving the camera.
    - The **green camera** is where the camera is at the preview time: a green dot on the eye, a green ring with a cross where it looks (**looks at**), and a thick green line with an arrow from one to the other. The view the key handles are in is the viewport's, so move around the scene to see the path from other sides.
    - With keys that follow that follow a target or an atom, the path is the one the camera takes as the target moves through the trajectory: it is computed a few points at a time (the trajectory frames have to be read), so it appears from the start and grows for a moment, and the old path stays until the new one is done.
- **Seamless loop** and **Close Loop**: makes the camera path cyclic, so a movie played on repeat has no jump. **Close Loop** adds a final key in the first key's pose and turns the loop on.
- **Keep upright** (on for new movies), **Up** and **From view**: the camera stays level, with the chosen world axis (+X, +Y, +Z, -X, -Y, -Z) up on the screen, so loops and spins cannot leave it tilted or upside down. **From view** picks the axis closest to the view's up now. The keyframes then only give where the camera looks from and at; its tilt is each keyframe's **Roll (deg)** in the keyframe table (positive leans the camera to the left, **0** levels it), and the movie goes smoothly from one roll to the next. A spin around **Camera up** turns around the up axis. Off, the camera goes through the keyframes' own tilts, as in workspaces saved before this option, and the **Roll** column shows each keyframe's tilt with a **Level** button that straightens it.
- **Play Preview**, **Repeat** and **Preview time (s)**: play or scrub the movie in the viewport at its real speed, without recording.
- **Add Keyframe (current view)** (shortcut **K** while a movie window is open): adds a key of the current view at the preview time. A key at the same time is replaced. Tick **with trajectory frame** to also pin the trajectory frame shown now.
- **Key on Selection**: adds a key at the preview time that frames the selected atoms, seen from the direction the camera has now, and moves the viewport there. Molecules split by the periodic cell are framed as one.
- **Add Orbit**: the three fields before it set the number of turns, the duration and the axis (**Camera up**, **World X/Y/Z**). It adds a key at the preview time and another one *duration* later, where the camera has gone round the look-at point that many times. It is refused if the orbit would run past the end of the movie.

#### The keyframe table

![Keyframe table, with key 2 waiting for an atom to be clicked](images/movie/look_at_pick.png)

The columns can be resized by dragging their borders. When they are wider than the window the table scrolls sideways.

| Column | Meaning |
|---|---|
| **#** | The key's number. Drag it onto another row to move the key's pose there; the times stay in place. |
| **Name** | What you call the key ("intro", "close-up", up to 23 characters). It is shown next to the number in the camera lane and saved with the workspace. |
| **Time (s)** | When in the movie. The list re-sorts when you finish editing. |
| **FOV (deg)** | Field of view. Narrow it to zoom in without moving the camera. |
| **Roll (deg)** | With **Keep upright** on: the camera's tilt at this key (positive leans it to the left); **0** levels it. Off: the key's measured tilt, read only, and **Level** straightens it. |
| **Ease** | How the movie gets *to* this key from the previous one. **Smooth** passes through without stopping, **Ease in/out** slows down at both ends, **Linear** moves at constant speed, **Hold** stays put and then jumps. |
| **Frame** | Tick to pin a trajectory frame to this key, then drag the number. Pins control the trajectory's speed (see above). A frame lower than the previous pin plays backwards. |
| **Spin** | Extra whole turns around the look-at point on the way to this key. **...** opens the axis and a **Constant speed** option. |
| **Go To** | Moves the viewport to this key. Clicking a key's orange marker in the **Timelines** window does the same. |
| **Look at** | Click it, then click an atom in the viewport. The key now looks at that atom and **tracks it through the trajectory**. While picking, the button reads **Click atom**; press Esc to cancel. |
| **Update position** | Moves the key's eye to where the viewport camera is now, still looking at the same point. The field of view and, if pinned, the frame are taken too. |
| **Follow** / **Unfollow** | Makes the key look at a point that moves with the follow target (see below), kept where it is relative to the target now. Press **Go To** first, so that the trajectory is at the frame of the key. **Unfollow** makes it fixed again. |
| **Dup** | Copies the key to one second later. |
| **Copy** | Remembers the key. **Paste Keyframe** (above the table) puts it in at the preview time, replacing a key that is there. **Ctrl+C** copies the key at the preview time and **Ctrl+V** pastes, while a movie window is open. |
| **Remove** | Removes the key. |

So, to edit a keyframe: **Go To** it, move around, press **Update position** to set where the eye is, and use **Look at** to choose what it looks at. Below the table, a line shows the eye, the look-at point and the distance at the preview time, plus the focus distance when depth of field is on.

#### Follow target

**Set Follow Target** uses the current selection as a target. With **keys follow target** ticked, new keyframes look at a point that moves with the centre of the target, so a drifting molecule stays in view. Between a following key and a fixed key the camera blends from one to the other. **Look at** on a single key is the per-key version of this, tracking one atom.

### Look parameters

![Look parameters and overlays](images/movie/looks_and_overlays.png)

Some visual settings can change during the movie: background color and intensity, ambient occlusion and its radius, exposure, depth of field blur, near and far clipping planes, and focus distance.

1. Pick the parameter in the list.
2. Set it up as it should look, in **Visuals**, at some preview time.
3. Press **Key Now**.

The table lists the keys, with their time, value, ease and **Remove**. With **Animate parameters** ticked, the keys are followed when scrubbing, previewing and recording. When the recording ends, the parameters go back to what they were.

### Representations

The properties of a representation can be keyed too, to show or hide it at some time or to change its size or look:

- **Visible**: shown or hidden. A representation cannot fade (the solid ones have no transparency), so it grows in or shrinks away instead: at a key it starts to go to its new state, taking **Transition (s)** seconds (2 by default, set above the table; 0 makes it appear and vanish at once). Its sizes are scaled during that time and it is hidden when it reaches zero. Key it once when it should appear and once when it should go. Keys closer together than the transition make it turn around from where it was. The timeline lane shows the ramps. A representation of the electronic structure or a dipole cannot be scaled, so it is shown fully until the transition ends and then vanishes.
- The scales of the representation (**Radius scale**, **Ball scale** and **Bond scale**, **Width**, **Coil** and so on, whatever its type has), **Tint scale** and **Saturation**. These move smoothly between keys, with the same **Ease** choices as other keys.
- **Base color** (seen where the colour mapping is Uniform) and **Tint color** (seen where **Tint scale** is above zero). Colors are edited in the table; in the timeline lane their keys are squares in their color that can be dragged in time.

Pick the representation and the property, set it up in the **Representations** window as it should be at the preview time (the eye shows or hides it), and press **Key Now**. The table lists the keys. A key belongs to a representation by an id that stays with it when representations are moved, duplicated or removed, and is saved in the workspace; removing a representation removes its keys. **Animate parameters** switches them on and off together with the look parameters, and the values go back to what they were when the keys let go or the recording ends. The **Representations** window is locked while recording, because the frames are made from the representations as they are.

Changing tint or saturation recolors the atoms of that representation every frame, which is slow for very large systems. Scales and visibility are cheap.

#### Seeing and editing when representations are shown

The **Representation overview** lane has one row per system (protein, ligand, water, etc.). Each coloured, labelled block is a stretch where one representation is shown. Colours identify representation types. Systems are separated by a gap, a divider and alternating shading. A block that starts where another ends stays on the same line and the two cross-fade over **Transition (s)**; only representations shown at the same time stack. With **One line per system** (the default) every system has the same height and its stacked blocks share it; untick it to give each stacked block a full line, the lane then growing to fit unless **Fit to window** is on. A representation that is enabled without visibility keys has a block covering the whole movie.

- Drag a bar to move when the representation is shown, or one of its ends to change when it starts or stops. The times land on frames with **Snap to frames**, and a bar stays between its neighbours.
- **Double-click** empty space in a system row to choose which of its existing representations to add (a tenth of the movie long, up to that representation's next block). It may overlap other representations of the system.
- **Right-click** a block to switch that interval to another existing representation of the same system or **Remove block**. Switching preserves other systems and property keys; existing target blocks that overlap the interval are merged. Removing the last block keeps that representation hidden.
- Clicking a bar selects the representation (and its **Visible** property) for the **Representation lane**, which shows and edits the values.

*Systems.* Representations named alike up to the first hyphen share one row: `protein-cartoon`, `protein-cpk` and `protein-vdw` belong to `protein`. A plain `protein` belongs to the same row; its block uses the representation type as its label. Names without a hyphen otherwise give a system of their own. Rename representations in the Representations window to group them; switching a block chooses an existing representation, not a new molecular selection.

*Swap.* **Swap with the next of its group** hands over at the preview time from the selected representation to the next one of its group, with the transition: for example `protein-cartoon` shrinks away while `protein-cpk` grows in. A representation without keys is taken to have been shown (the one that goes) or hidden (the one that comes) until then.

Tip: name the representations of one selection after it (`protein-cartoon`, `protein-cpk`, `protein-vdw`) and the overview keeps them together.

### Depth of field

Under **Visuals > Depth of Field**, **Focus** chooses what is sharp:

- **Look-at point**: what the camera looks at. This is the default and follows the keyframes.
- **Distance**: a fixed distance from the camera (**Focus distance**, **From view** takes the current distance). Key **Focus distance** as a look parameter to pull focus during the movie.
- **Follow target**: the centre of the movie's follow target, even when the camera looks elsewhere.

### Overlays

Overlays are drawn into recorded frames and into the viewport in **Movie preview**. In the **Overlays** tab, use **Add overlay...** to choose a type. Select one overlay in the compact list to edit it: **Content** holds type-specific settings, **Timing** holds its range and fades, and **Appearance** holds position, size and colours. Subplot details and **Plot style** are expandable. Clicking an overlay bar in the left timeline selects it and opens its inspector. A new movie starts with the VIAMD logo in the top left corner for the whole movie; **Remove** it if you do not want it (a saved workspace remembers that).

#### What every overlay has

- An on/off box, **Duplicate** (another overlay just like it, right below it: handy to show something else at another time or place) and **Remove**. The **Type** can be changed.
- **Shown (s)**: the times it is shown from and to, with **Start at preview time** and **End at preview time**, and **Fade in (s)** and **Fade out (s)**.
- **Position**: one of nine anchors in the frame.
- **Size**: a part of the height of the frame, or in points. A point is a pixel of a frame that is 1080 pixels high, scaled with the frame, so a size looks the same at any resolution; changing the unit keeps the size as it is. For text it is the height of the text, for a logo or an image its height, for a timeline or a distribution the height of the whole stack.
- **Color** and a **Background** plate, to read it over a busy picture (no plate while its opacity is 0).
- In the left timeline panel every overlay is a bar in the overlay lane, where it can be dragged (see below).

#### Text, time stamp and scale bar

- **Text**: the words you type.
- **Time stamp**: the time that has gone since the movie started, in the unit of the timeline (**N frames** when the trajectory has no times). It starts at 0 and only grows, whichever way the trajectory is played, and it is the same whenever the stamp is shown: one that appears 40 s into the movie reads the time that has gone since the start, not since it appeared. It is the same quantity as **Time that has gone** of the time bar, and a hold does not advance it.
- **Scale bar**: a bar of a known length in the structure. It is as long on the frame as that length is at the distance the camera looks at, so it follows the zoom. **Length** 0 picks a round length (1, 2 or 5 times a power of ten).

#### Logo and images

The logo is the VIAMD icon (`icon/viamd.png`) on a transparent background. The light grey of the icon is made black, so the lettering has a strong contrast on a light picture; on a dark picture give it a light **Background** plate. Its **Color** multiplies the picture (white keeps it as it is). **Add overlay... > Image** asks for a png or jpg file (a group's logo, a figure) and shows it like the logo, with its transparency kept. The file is saved in the workspace as a path relative to it, so keep it with the workspace. **Reload** reads it again after it was changed, and the box turns red when it cannot be read.

#### Time bar

A bar that shows how far the trajectory has gone. It fills from left to right whichever way the trajectory is played (a trajectory played backward still fills it forward), fast where the trajectory is played fast, slowly where it is slowed down and not at all where it is held, so it shows the pace of the movie. **Width** is a part of the width of the frame and **Size** the height of its text. **Time that has gone** writes the trajectory time covered so far over the whole, and **Speed** how fast it plays as a multiple of the speed of the Animation window (x1.0 is the same).

The unfilled track is neutral grey, independent of the fill colour. New time bars also have a translucent grey background plate; adjust it under **Appearance > Background**. Existing workspaces keep their saved plate colour.

#### Timeline and distribution

**Add overlay... > Timeline** and **Distribution** draw subplots of the **Timelines** and **Distributions** windows into the movie, as it plays. A timeline is wide and low, at the bottom center of the frame, and a distribution is narrow and tall, at the middle of the right side; both can be moved and resized like any overlay.

*Which subplots.* A new overlay holds the subplots of its window that have series. **Add subplot** adds another, **Up** moves one up the stack and **Remove** takes it out; several subplots are stacked, each with its own scale and legend. What is drawn is what is in the subplot in its window, in the colours it has there, at most six series per subplot and the first member of a population. A subplot is found by an identity of its own and not by its position, so changing the number of subplots, or naming them, does not change what an overlay draws. Name the subplots in the **Subplots** menu of their window (**Names**). A subplot that is gone, or has no series, is reported in the overlay and not drawn.

*As the movie plays.* The plots grow with the movie (**As the movie plays**; with it off the whole plot is shown). A thin line and a dot mark the frame that is shown, and **Value** puts the value at that frame, in the unit of the plot, in the legend.

- **Timeline.** Its horizontal axis is by default **Elapsed time**: the trajectory time that the movie has covered, as in the time bar. The curve always grows to the right, also where the movie plays the trajectory backward (the series is then read backward; where the movie turns back the curve goes on to the right, and a hold adds nothing). **Trajectory time** is the time of the trajectory itself, turned around when the movie plays it backward. The axis is the whole movie, from 0 to the total time that the movie covers, and the curve is drawn from the start of the movie up to now, so a timeline that appears late already shows what happened before, with the same labels as ever. The overlay's own range only decides when it is visible. Timelines above each other share the axis, with its labels under the last.
- **Distribution.** It is counted over the frames that have been played since the movie started, so its bars grow to the shape of the whole, also when it appears late. A distribution of a script that is not over frames (an RDF, say) is drawn as it is.

*Number of bins.* Distribution overlays have a **Number of bins** control (2 to 4096), applied to all their subplots without changing the Distributions window. New overlays use 128 bins. **Use source bin counts** instead keeps each series' own setting, which is also the default for older workspaces. Script distributions can only be coarsened to a divisor of the resolution at which they were evaluated.

*Properties that come in at different times.* Each subplot in the list has **in at** and **out at** times (in movie seconds, inside the range the overlay is shown in; **stays to the end** when they are the same), and **In at preview time** takes the first from the preview. A subplot that is added while the preview is later in the overlay comes in there. It fades in and out with the fades of the overlay. A property that comes in later is drawn from the start of the movie, as if it had been there all along: a curve that comes in at 30 s appears with what it did from the start up to then and goes on growing, and a distribution has counted every frame played since the movie started. The plate only covers the subplots that are there, so it grows when one comes in, and the place of a subplot that is not there yet stays free, so the others do not move. For another place, use another overlay.

*Titles and look.*
- **Titles** writes a title above each subplot: the **Title** typed in its row, or else the name of the subplot (none if it has neither).
- **Text (points)** and **Lines (points)** set the text and the lines in points (0 follows the height). **Colours** gives the series a set of colours of their own instead of the ones they have in the plots, and the two buttons set light text on a dark plate or dark text on a light plate, for a dark or a light picture. **Width** is a part of the width of the frame.

*Markers.* Expand **Timeline markers** below the overlay inspector. **Add marker at the preview time** or **Add one at each camera key**, then set the time, label and target subplot. **All timeline subplots** preserves the previous behavior; choosing a subplot puts the line and label on that subplot only, in any timeline overlay that contains it. Targets use stable subplot identities and survive renaming and workspace save/load. Missing targets are reported instead of reassigned. **Markers** in an overlay hides its markers. Marker targets and bin counts are included in undo/redo; marker times scale with movie length.

#### Property visualization

**Add overlay... > Property** shows the visualization of a script property in Movie preview while the overlay is shown: the atoms, the geometry and the labels that hovering its plot shows. Type the identifier of the property or pick it from the script. It is in the recording too, with its labels, and it fades in and out with the **Fade in** and **Fade out** of the overlay. The script has to have evaluated the property.

#### Seeing them while you edit

The top-right button switches between **Movie preview** and **Scene view**. Movie preview draws the recording frame in the viewport, dims the outside and widens the view to match the recording. The overlays are laid out in that frame. Scene view hides the frame and overlays, including property visualization overlays, while keeping camera-path editing available. Switching modes does not change camera keys, playback or recorded output; it is disabled while recording.

## The left timeline panel

![The Movie Timeline](images/movie/movie_timeline.png)

Open **Windows > Movie** and leave **Timeline** ticked. Playback, the scrubber and **Add Keyframe** are shared in the editor's top toolbar. Expand **Lanes and layout** to choose lanes, snapping, zoom reset, lane height and fit behavior. The lanes have real axes and share the time axis:

- **Trajectory**: the trajectory frame shown at each moment. The blue **Start** and **End** lines are when the trajectory starts and stops; drag them. Orange dots are keys with a pinned frame: drag sideways to change the time and up or down to change the frame.
- **Camera lane** (tick **Camera lane**, at the top): the camera on one lane. The first row has the keys as orange dots with their number and name (the one at the preview time is yellow). Drag a key sideways to change when, click it to go there, right-click it for a menu with its name, **Go to** and **Remove**, and double-click on an empty place to add a key at that time on the camera's path, so the camera does not move. The row **Look at** has a band over the keys that look at the follow target (teal) or at an atom of their own (blue, with the atom's number), the row **Spin** a purple band over each stretch with extra turns (for instance `+2 x`) and the row **Frame** a green mark at each key that pins a trajectory frame, with the frame number. Hover a band for its details. A key added by double-click follows what the keys on both its sides follow; the turns of a spin stay with the part of the stretch that ends at the later key.
- **Camera distance**: how far the camera is from what it looks at, in your preferred length unit. Dots can be dragged sideways for the time and up or down for the distance (the camera moves along its line of sight and keeps looking at the same point).
- **Field of view** in degrees. Dots can be dragged sideways for the time and up or down for the angle (1 to 170 degrees).
- **Look parameter lane**: the parameter chosen in the **Look parameter** list at the top. Double-click to add a key, drag to change it, right-click to remove it.
- **Representation lane**: the property of a representation chosen with the two lists next to **Representation lane**. The line is its value over the movie and the dots are its keys: drag sideways for the time and up or down for the value, double-click to add one, right-click to remove one. For **Visible** a double-click turns it around at that time (shown becomes hidden and the other way), and the first one on a representation without keys also keys what it is now at time 0, so that it holds until then.
- **Representation overview** (tick **Representation overview**): when each representation is shown, as bars, grouped by name (see *Seeing and editing when representations are shown* above).
- **Overlay lane** (tick **Overlay lane**): the overlays as bars, one row each and a colour for each kind, with the notes of the movie (markers) as small yellow triangles at the bottom. Drag a bar to move when the overlay is shown, or one of its ends to change when it starts and stops (it lands on frames with **Snap to frames**). Moving a whole bar moves the in and out times of the subplots of a timeline or a distribution with it, and the small ticks in such a bar are where subplots come in later. Hover a bar for its name and times.

#### Picking keys and moving them together

Keys of the camera lane, the trajectory, distance and field of view lanes, the look parameter lane and the representation lane, the bars of the overlay lane and the blocks of the representation overview can be picked and moved together.

- **Pick**: click a key (a white ring shows it is picked). **Ctrl + click** adds a key to the picked ones or takes it away. **Drag on the empty background of a lane** to draw a box and pick the keys in it (with Ctrl or Shift the box adds to the picked keys; in the camera lane the box picks by time only). A click on the background puts all down. **Pick all** (or Ctrl + A) picks the camera keys and the keys of the look parameter and the representation property that are shown. A camera key is one key in every lane it is drawn in, so picking it in one lane picks it in all. Clicking a handle of a key in the viewport picks that key too.
- **Move**: drag any picked key and they all move by the same time, and, in a lane with a value, by the same amount: the trajectory frame (for the keys that pin a frame), the distance, the field of view, the look parameter, the representation property (for a color and for **Visible** only the time moves). A tooltip shows how far, for example `5 keys: +1.20 s  +0.5`. The group stays inside the movie as a whole, keeping the time between its keys, and lands on frames with **Snap to frames**. A key that is dragged without being picked is picked alone. A whole drag is one undo step.
- **Bars and blocks**: click a bar of the overlay lane or a block of the representation overview to pick it (Ctrl + click adds, a box on the background picks the ones it touches). Dragging a picked bar or block by its middle moves everything picked by the same time, keys included. A bar takes what is timed inside it along. A block stops at the other blocks of its representation, so it can move less than the rest. Dragging the end of a bar or a block still changes only that end. Delete removes the picked blocks and keys; an overlay is never removed this way. Unkeyed blocks (a representation that is simply on) cannot be picked.
- **Stretch**: the percentage box next to **Copy** stretches the time between the picked items (keys, bars, blocks, and what is timed inside a bar) about the first picked item or about the preview time (the list next to it). Drag it right to spread them out and left to bring them closer; 100 % is as it was. It stops where something would leave the movie. One undo step.
- **Inspector**: while items are picked, a line above the lanes says how many of each kind. **first at** is when the first one is: type a time and Enter to move all picked items so that the first is there. The easing list sets how the movie gets to every picked key (the first camera key and the visibility keys have none to choose; **(mixed)** is shown when they differ). With one camera key picked, its name can be edited there.
- **Where a key lands on another** (the same time, in the same lane) it replaces it when the mouse is released.
- **Keyboard** (with the mouse over the lanes): left and right arrows move the picked keys one frame (Shift: one second, Ctrl: ten frames), up and down change the value of the lane that was last used (Shift: ten steps), **Delete** removes them, **Esc** puts them down, **Ctrl + C** and **Ctrl + V** copy them and paste them with the first one at the preview time (a pasted key that lands on another replaces it). **Pick all**, **Put down**, **Delete**, **Copy** and **Paste** above the lanes do the same.
- **Panning**: because dragging the background picks, the time axis pans with the **middle mouse button**, with **Shift + wheel** or with sideways scrolling.

The yellow line is the playhead. Ctrl + wheel zooms the time, the middle button pans, and use **Show whole movie** to reset. Every lane gets the **Lane height** (a slider, 60 to 400 px) times its own proportion, which dragging a divider changes, and the window scrolls with the wheel when the lanes do not fit; the representation overview, overlay and camera lanes grow to fit their rows without making the other lanes taller. **Fit to window** instead shares the height of the window between the lanes, however small, and then the wheel zooms the time. Drag between lanes to change their heights. Untick **Camera lane**, **Trajectory**, **Distance**, **Field of view**, **Look parameter lane**, **Representation lane**, **Representation overview** or **Overlay lane** to hide a lane. Which lanes are shown, the lane height, **Fit to window** and **One line per system** are saved in the workspace. With **Snap to frames** ticked (the default), keys, the playhead and the trajectory's start and end that you drag, and keys added at the preview time, land on a frame of the movie (at the **Output FPS**), so a change happens on a frame and not between two.

## Saving

The movie (length, trajectory timing, camera keys including what they look at, look parameter keys, representation keys, overlays including distribution bin counts, markers including subplot targets, follow target and timeline lanes) is saved in the workspace (`.via`) under `[Movie]`, together with the names of the subplots of the Timelines and Distributions windows that the overlays use. Workspaces from earlier versions load and are converted. The viewport mode and which editor panels are visible are not saved.

## Example workspace

[`examples/aspirin_phospholipase_movie.via`](examples/aspirin_phospholipase_movie.via) is an 80 s movie of aspirin in phospholipase. The trajectory plays backward, from its last frame to its first. It has camera keys (the first ones follow a target), keyed depth of field blur, representation keys, two markers ("Pocket shown" and "Aspirin enters the pocket"), the logo, title, time stamp, scale bar, a time bar with a grey plate, four timeline subplots that come in at different times, a 128-bin distribution with a title, and three property visualization overlays. It opens at the saved preview time (about 55.45 s): drag the top preview slider to 0 and play it. The user's timing, positions, colours and keys are retained.

It refers to `aspirin-phospholipase.gro`, `.xtc` and `.edr` in its own folder, which are not part of the repository. Put your copies next to the workspace, then **File > Open Workspace**.

## Tips

- Plan the length first, then place keys. If the movie is too fast or too slow, change **Movie length** and everything stays in proportion.
- For a fly-over before the dynamics start, drag the blue **Start** line to the right. The trajectory waits on its first frame until then.
- To slow down an interesting event, pin frames on two keys around it and move them further apart in time.
- If a loop or spin leaves the camera tilted or upside down, tick **Keep upright** (use **From view** if your molecule's up is not +Y).
- **Smooth** keys never stop the camera. Use **Ease in/out** or **Hold** where the camera should rest.
- PNG sequences are safest for very long or very large movies. Encode them afterwards with **Copy ffmpeg command**.
- Check the motion at **Scale** 25 % and with few **Samples per frame** first, then make the final movie at full size. To fix one part afterwards, use **Render only a range** with a PNG sequence and replace the files.

## Known limits

- **Follow target** depth of field uses the global follow target, not a key's own **Look at** atom.
- A timeline or a distribution overlay draws at most six series of a subplot and the first member of a population, and a timeline overlay has one kind of axis for all its subplots.
- Solid representations cannot fade: they grow in and shrink away instead (see **Representations**).
- The Representations window is locked while recording.
- With **Keep upright**, a spin around a horizontal axis flips over at the top and bottom (the camera cannot stay level looking straight along the up axis). Roll eases as a number, so going from 170 to -170 degrees turns the long way round.
