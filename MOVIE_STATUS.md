# Movie feature: status and remaining work

Branch: `video`. Last commit at the time of writing: `dbbea164`. Nothing is pushed beyond `658d8c22` (`origin/video`).

## How to build and test

- Build: `cmake --build build --target viamd -j8` and `--target viamd_test`
- Tests: `./build/bin/viamd_test`
- Known failing test, independent of the movie work: `viamd_camera.default_view_of_a_planar_molecule_is_face_on`
- ffmpeg is not installed in the dev environment; the frame sink tests use a fake script.
- The GUI could not be run while developing, so everything marked "untested" below needs a manual check.

## Where things live

- Movie window, timeline window, recording, preview, overlays drawing: `src/main.cpp` (`draw_movie_window`, `draw_movie_strip`, `movie_*`)
- Movie state: `src/viamd.h` (`ApplicationState::movie`)
- Async frame writer: `src/frame_sink.{h,cpp}`
- Keyed look parameters and undo history: `src/movie_keys.{h,cpp}`
- Overlay maths (fade, scale bar): `src/movie_overlay.{h,cpp}`
- Camera path evaluation: `src/gfx/camera_utils.{h,cpp}` (`camera_keyframes_evaluate`, `keyed_curve_evaluate`, `camera_keyframes_evaluate_frame`)
- Workspace save/load: `src/viamd.cpp` (`[Movie]`, `[MovieOverlay]` sections)
- Parameter ids in `movie_param_table` (main.cpp) are saved in workspaces: never renumber, add at the end.

## Done

1. Undo/redo of keyframe edits, keyed look parameters (background, ambient occlusion, exposure, depth of field blur, clipping planes), per-segment easing (smooth, ease in/out, linear, hold), seamless loop.
2. Camera follows a target: "Set Follow Target" takes the current selection, keys made with "keys follow target" look at the middle of it. Blends between following and fixed keys.
3. Overlays on recorded frames: text, time stamp, scale bar, with time range and fades. Also shown in the viewport at the preview time.
4. Keyframe table: rows are reordered by dragging the number (times stay with their places in the list).
5. Trajectory can play backward, both with frame keys (a key with a lower frame than the previous) and with a start frame after the end frame.
7. Depth of field focus is separate from the camera distance: Settings > Depth of Field > Focus is "Look-at point" (as before), "Distance" (a number, keyable as the parameter "Focus distance") or "Follow target" (the middle of the follow target, wherever the camera looks). The keyframe table has a "Pose" popup to edit where a key looks at and its distance in 3D, a readout of eye / look-at / distance / focus at the preview time, and the viewport shows the focus plane (magenta). Workspace: `DofFocusMode`, `DofFocusDistance`.
6. Timeline in its own window, legend outside the plot, look parameter lane with draggable keys.

## Untested in the GUI

- Follow target: set a target, add keys at different frames with "with trajectory frame", play the preview. Check the camera tracks the target and that loading an old workspace still works.
- Overlays in the recorded video: they are drawn through ImGui's OpenGL backend straight into the G-buffer before read back. Record a short clip and check that text and bar appear, are not upside down, and fade correctly. Also check with more than one anti-aliasing sample per frame.
- Overlay positions in the viewport preview are only approximate when the viewport has a different shape from the movie.
- Backward trajectory: a reversed frame range with "Trajectory at Animation speed" on, and reversed frame keys.
- Drag and drop reordering of keyframe rows.

## Known limits

- An existing keyframe cannot be switched to follow the target; add a new key at the same time with the option on (it replaces the old one).
- The camera path drawn in the viewport does not show the follow motion.
- The follow target itself is not part of undo.
- Where a frame curve turns around, the trajectory slows to a stop. For a hard reversal, add a key at the turn-around frame with Linear easing.
- Overlay text is single style (no background plate, only a shadow).

## Still to do (agreed plan order)

4. Render ergonomics and output
   - Render only a time range, or one segment, so a fix does not need the whole movie again
   - Pause button and time-remaining estimate while recording
   - Lower-resolution preview
   - Configurable number of anti-aliasing samples per frame
   - Save the movie settings to a file so a render can be reproduced
   - Optional: H.265 and WebM presets, transparent background (lowest value for MD movies)
5. Strip comfort
   - Zoom and pan on the strip
   - Snap to output frames
   - Copy and paste of keys
   - "Fly to current selection" button that adds a key framing the selection
6. Representation keys (hardest, do last)
   - Stable representation ids first (representations only have an array index today)
   - Keyed values reuse the keyed-parameter machinery; discrete changes (type, filter) cannot be interpolated
   - Decide how manual edits in the Representations window interact with keys during recording
   - Cost: recolouring per frame can be heavy on large systems
7. Scene lock during recording
   - Cheap option: lock the Representations window while recording. The real fix (editing during a recording) is large.

## Ideas not yet planned

- Keyframe table column to toggle follow on an existing key
- Draw the follow-aware camera path in the viewport
- Overlay background plate, per-overlay font size in points, image/logo overlay

## Workspace example for the aspirin


        #01010110#01001001#01000001#01001101#01000100#01001101#01000001#01001001#01010110#
        #                                                                                #
        #            VIAMD — Visual Interactive Analysis of Molecular Dynamics           #
        #                                                                                #
        #                    github: https://github.com/scanberg/viamd                   #
        #                 manual: https://github.com/scanberg/viamd/wiki                 #
        #                    youtube playlist: https://bit.ly/4aRsPrh                    #
        #                                twitter: @VIAMD_                                #
        #                                                                                #
        #                If you use VIAMD in your research, please cite:                 #
        #   "VIAMD: a Software for Visual Interactive Analysis of Molecular Dynamics"    #
        #       Robin Skånberg, Ingrid Hotz, Anders Ynnerman, and Mathieu Linares        #
        #                 J. Chem. Inf. Model. 2023, 63, 23, 7382–7391                   #
        #                   https://doi.org/10.1021/acs.jcim.3c01033                     #
        #                                                                                #
        #01010110#01001001#01000001#01001101#01000100#01001101#01000001#01001001#01010110#
        

[Files]
MoleculeFile=./aspirin-phospholipase.gro
TrajectoryFile=./aspirin-phospholipase.xtc
CoarseGrained=0

[Animation]
Frame=0
Fps=2.94000006
Tension=0
Interpolation=2

[Timeline]
NumSubplots=1

[Distributions]
NumSubplots=1

[TimelineView]
FilterEnabled=0
FilterFrames=0,600
TemporalWindowEnabled=0
TemporalWindowExtent=10
ViewFrames=0,600

[RenderSettings]
BackgroundColor=1,1,1
BackgroundIntensity=24
SsaoEnabled=1
SsaoIntensity=6
SsaoRadius=20
SsaoBias=0.100000001
TonemapEnabled=1
Tonemapper=3
TonemapExposure=1
TonemapGamma=2.20000005
DofEnabled=0
DofFocusScale=10
FxaaEnabled=1
TaaEnabled=1
TaaJitter=1
TaaFeedbackMin=0.800000012
TaaFeedbackMax=0.949999988
MotionBlurEnabled=1
MotionBlurScale=1
SharpenEnabled=1
SharpenWeight=1
SimulationBoxEnabled=0
SimulationBoxColor=0,0,0,0.5

[Camera]
Position=-26.8763695,9.8744669,26.377327
Orientation=-0.195538357,0.40068686,0.582773089,-0.679404318
Distance=37.4789925
Mode=0
FovY=0.785398185

[Movie]
Resolution=0
ResX=1920
ResY=1080
Fps=24
StartFrame=0
EndFrame=600
DurationAuto=0
Duration=60
TrajectoryBegin=0
TrajectoryEnd=60
Playhead=27.2991772
FilenamePrefix=frame
AnimateCamera=1
ShowPath=1
Output=1
Crf=18
Loop=0
AnimateParams=1
KeyframeV2=0,0.785398185,22.7016335,12.1641417,16.4069672,-10.8746223,0.173237815,0.890601158,0.382237017,0.175251439,1,600,0,0,0,0,0,0,0,0
KeyframeV2=8.34630394,0.785398185,47.5503197,17.2197113,15.6602383,-39.2573357,-0.312284172,-0.924782276,-0.188275158,-0.108668298,1,459.362671,0,0,0,0,0,0,0,0
KeyframeV2=16.6778202,0.785398185,50.878334,-25.2729359,-12.5324497,44.0485764,-0.270015419,0.177603647,0.446325988,-0.834470987,1,441.003571,0,0,0,0,0,0,0,0
KeyframeV2=24.6793766,0.785398185,44.228878,-25.6979179,-4.42315102,37.5550652,-0.24084568,0.24573423,0.480179191,-0.806868076,1,410.40509,0,0,0,0,0,0,0,0
KeyframeV2=36.3105049,0.785398185,7.48665142,5.13289165,9.10312271,2.91028595,-0.0815942213,-0.743429363,-0.631116986,-0.20578216,1,86.0612488,0,0,0,0,0,0,0,0
KeyframeV2=40.3479958,0.785398185,7.48665142,5.13289165,9.10312271,2.91028595,-0.0815942213,-0.743429363,-0.631116986,-0.20578216,1,72.4728851,0,0,0,0,0,0,0,0
KeyframeV2=45.3844376,0.785398185,13.2622128,4.87409019,13.6182556,-2.5719223,-0.073842831,-0.857165813,-0.503416002,-0.0799147636,1,43.2233849,0,0,0,0,0,0,0,0
KeyframeV2=48.6840477,0.785398185,26.485796,7.48416901,-5.02088547,27.1905308,-0.0349881649,-0.0868612602,0.256600261,-0.961970568,1,24.8642979,0,0,0,0,0,0,0,0
KeyframeV2=53.5232887,0.785398185,31.4734917,-6.13366032,8.91358662,27.953474,0.101738691,0.246264577,0.426704586,-0.864248991,1,10,0,0,0,0,0,0,0,0
KeyframeV2=59.3252907,0.52359879,74.2463455,-20.6269436,25.424736,64.6528473,0.101738684,0.246264562,0.426704526,-0.864248931,1,0,0,0,0,0,0,0,0,0
ParamKey=0,0,1,1,1,0

[MovieOverlay]
Type=2
Enabled=1
Range=4.89077044,60,0.5,0.5
Anchor=6
Size=0.0500000007
Color=0,0,0,1
Length=0
Text=

[Operations]
Recenter=0
FixateOrientation=0
ApplyPbc=0
UnwrapStructures=0
RecalcBonds=0
RecenterQueryEnabled=0
RecenterQuery=

[Representation]
Name=protein
Filter=protein
Enabled=1
Type=4
ColorMapping=8
BaseColor=1,1,1,1
Saturation=1
TintColor=1,0,0,1
TintScale=0
SecondaryStructureColorUnknown=0.5,0.5,0.5,1
SecondaryStructureColorCoil=0.860000014,0.860000014,0.860000014,1
SecondaryStructureColorHelix=0.119999997,0.860000014,0.119999997,1
SecondaryStructureColorSheet=0.119999997,0.119999997,0.860000014,1
BondColor=0
BondSharpness=0.5
BondBaseColor=1,1,1,1
Param=1,1,1,1
DynamicEval=0

[Representation]
Name=ion
Filter=ion
Enabled=0
Type=0
ColorMapping=1
BaseColor=1,1,1,1
Saturation=1
TintColor=1,0,0,1
TintScale=0
SecondaryStructureColorUnknown=0.5,0.5,0.5,1
SecondaryStructureColorCoil=0.860000014,0.860000014,0.860000014,1
SecondaryStructureColorHelix=0.119999997,0.860000014,0.119999997,1
SecondaryStructureColorSheet=0.119999997,0.119999997,0.860000014,1
BondColor=0
BondSharpness=0.5
BondBaseColor=1,1,1,1
Param=1,1,1,1
DynamicEval=1

[Representation]
Name=ligand
Filter=not (protein or nucleic or water or ion)
Enabled=1
Type=2
ColorMapping=1
BaseColor=1,1,1,1
Saturation=1
TintColor=1,0,0,1
TintScale=0
SecondaryStructureColorUnknown=0.5,0.5,0.5,1
SecondaryStructureColorCoil=0.860000014,0.860000014,0.860000014,1
SecondaryStructureColorHelix=0.119999997,0.860000014,0.119999997,1
SecondaryStructureColorSheet=0.119999997,0.119999997,0.860000014,1
BondColor=1
BondSharpness=0.5
BondBaseColor=1,1,1,1
Param=1.523,1.19500005,1,1
DynamicEval=0

[Representation]
Name=water
Filter=residue(resname("SOL") and within(4.0,resname("AIN")))
Enabled=1
Type=2
ColorMapping=1
BaseColor=1,1,1,1
Saturation=1
TintColor=1,0,0,1
TintScale=0
SecondaryStructureColorUnknown=0.5,0.5,0.5,1
SecondaryStructureColorCoil=0.860000014,0.860000014,0.860000014,1
SecondaryStructureColorHelix=0.119999997,0.860000014,0.119999997,1
SecondaryStructureColorSheet=0.119999997,0.119999997,0.860000014,1
BondColor=0
BondSharpness=0.5
BondBaseColor=1,1,1,1
Param=0.800000012,0.537999988,1,1
DynamicEval=1

[Representation]
Name=rep
Filter=residue({5,9,18,21:23,27:31,44,47:48,63,100});
Enabled=1
Type=0
ColorMapping=5
BaseColor=1,1,1,1
Saturation=1
TintColor=1,0,0,1
TintScale=0
SecondaryStructureColorUnknown=0.5,0.5,0.5,1
SecondaryStructureColorCoil=0.860000014,0.860000014,0.860000014,1
SecondaryStructureColorHelix=0.119999997,0.860000014,0.119999997,1
SecondaryStructureColorSheet=0.119999997,0.119999997,0.860000014,1
BondColor=0
BondSharpness=0.5
BondBaseColor=1,1,1,1
Param=0.888000011,1,1,1
DynamicEval=0

[Script]
Text="""s1 = resname("ALA")[2:8];
d1 = distance(10,30);
a1 = angle(2,1,3) in resname("ALA");
r = rdf(element('C'), element('H'), 10.0);
v = sdf(s1, element('H'), 10.0);
{lin,plan,iso} = shape_weights(all);
sel1 = residue({5,9,18,21:23,27:31,44,47:48,63,100});
"""

[ActiveSelection]
Granularity=1

[Windows]
Timelines=0
Distributions=0
Representations=1
ScriptEditor=0
Animation=1
Movie=1
MovieTimeline=1
Attributes=0
Contacts=0
System=0
DensityVolume=0
Ramachandran=0
ShapeSpace=0

[Contacts]
GroupBy=0
Cutoff=1

[ElementDefault]
Element=6
Color=0.0115830302,0.0115830302,0.0115830302,1

[DensityVolume]
DvrEnabled=1
DvrColormap=5
DvrAlphaScale=1
DvrRange=0,1
IsoEnabled=0
IsoCount=0
ClipMin=0,0,0
ClipMax=1,1,1
LegendEnabled=1
LegendCheckerboard=1
LegendColormapMode=2
ResolutionScale=2
ClipVolumeColor=1,0,0,1
BoundingBoxColor=0,0,0,1
ShowBoundingBox=1
ShowReferenceStructures=1
ShowReferenceEnsemble=0
ShowCoordinateSystem=1
RepType=2
RepColorMapping=1
RepParam=1,1,1,1
RepColor=1,1,1,1

[Ramachandran]
BlurSigma=5
LayerAlpha=0.850000024,0.850000024,0.850000024
LayerDisplayMode=0,1,1
LayerColormap=6,5,4
LayerIsolineColor0=1,1,1,1
LayerIsolineColor1=1,1,1,1
LayerIsolineColor2=1,1,1,1
ShowLayer=1,1,1,1
LayoutMode=0

[ShapeSpace]
Filter=all
MarkerSize=1.5
UseMass=1