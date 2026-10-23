# VIAMD
Visual Interactive Analysis of Molecular Dynamics

VIAMD is an interactive analysis tool for molecular dynamics (MD) written in C/C++. VIAMD is developed at the PDC Center for High Performance Computing (KTH, Stockholm). It exposes a rudimentary scripting language for declaring operations performed across trajectory frames.
The results can then be viewed in the different windows exposed in the application. 
<p align="center">
<img src="https://github.com/scanberg/viamd/assets/38646069/5651ef62-28bc-4f41-8234-75cf9ba85612" alt="This is an overview of the viamd software" width="800"/>
</p>

## Status
[![Windows (MSVC 19)](https://github.com/scanberg/viamd/actions/workflows/windows.yml/badge.svg?branch=master)](https://github.com/scanberg/viamd/actions/workflows/windows.yml)
[![Ubuntu 22.04 (GCC 11)](https://github.com/scanberg/viamd/actions/workflows/ubuntu22.yml/badge.svg)](https://github.com/scanberg/viamd/actions/workflows/ubuntu22.yml)
[![Ubuntu 24.04 (GCC 13)](https://github.com/scanberg/viamd/actions/workflows/ubuntu24.yml/badge.svg)](https://github.com/scanberg/viamd/actions/workflows/ubuntu24.yml)
[![MacOS (Clang)](https://github.com/scanberg/viamd/actions/workflows/macos.yml/badge.svg)](https://github.com/scanberg/viamd/actions/workflows/macos.yml)

## Running VIAMD 

### Windows
For Windows, we recommend using the latest binary available on the [release page](https://github.com/scanberg/viamd/releases/).

### macOS
For macOS, we recommend using the latest binary available on the [release page](https://github.com/scanberg/viamd/releases/).

**NOTE:** To be able to run this, you will have to do some magic in a terminal to be able to run the app bundle since we do not have code signing.

```xattr -dr com.apple.quarantine viamd.app```


### Building for Ubuntu and macOS
To [build](https://github.com/scanberg/viamd/wiki/0.-Building) VIAMD on your machine, you can follow the procedure described in detail in the wiki for [Linux](https://github.com/scanberg/viamd/wiki/0.-Building#linux) and [MacOS](https://github.com/scanberg/viamd/wiki/0.-Building#mac).

On macOS, VIAMD builds without Xcode: CMake and the macOS Command Line Tools are
enough. If Apple's Metal compiler happens to be available, the Metal compute
kernels are compiled to `.metallib` during the build; otherwise VIAMD embeds the
shader source and compiles it with Metal's runtime shader compiler on first use,
which costs a fraction of a second at startup and is otherwise identical. Xcode
is only needed if you are changing the offline Metal shader compilation pipeline
itself.

## Documentation
Documentation about VIAMD is available on the github [wiki](https://github.com/scanberg/viamd/wiki). The first two chapters relate to the [visual](https://github.com/scanberg/viamd/wiki/1.-Visual) and [analysis](https://github.com/scanberg/viamd/wiki/2.-Analysis) features, respectively, where we highlight the interactive part of the software. The third chapter focuses on the VIAMD [language](https://github.com/scanberg/viamd/wiki/3.-Language) used for scripting, and the fourth chapter proposes a series of [tutorial](https://github.com/scanberg/viamd/wiki/4.-Tutorials) (under construction). 

A series of videos is available on [youtube](https://youtube.com/playlist?list=PLNx9MpJY8ffr9CeK7WefdOnuGRw_E5rSj&si=VatBHEwiL7jWyhPK).

## Experimental Lavery analysis

The Lavery window uses VIAMD's atomistic nucleic-backbone ranges, perceived base
rings, and glycosidic bonds to identify nucleotides. Geometric pairing uses
pairwise periodic images and approximately coplanar bases; pair identities are
fixed from trajectory frame zero. Steps require adjacent nucleotides on both
paired backbone ranges. Canonical A/G/C/T/U bases are least-squares fitted using
the numerical standard geometries in Curves+ v3.0's `standard_b.lib` (including
C1'). Tsukuba reference frames and symmetric rotation-vector/mid-frame
parameters follow [Lavery et al. (2009)](https://pmc.ncbi.nlm.nih.gov/articles/PMC2761274/).
The second base uses the Watson-Crick dyad convention; reverse Watson-Crick
pairs and noncanonical bases are not supported.

All 12 intra-pair/step parameters are regression-tested against the
[official Curves+ v3.0nc program](https://curvesplus.bsc.es/misc)
(`fit=T`, `refo=F`) across the repository's nucleosome structure: 147 pairs and
146 steps, to 0.006 Angstrom and 0.06 degree, allowing for reference output
rounding. This does not validate every nucleic-acid conformation or implement
Curves+'s helical-axis analysis; the module remains experimental.

Frame-zero pair detection uses geometric pairing
criteria (ring-center distance 3-9 Angstrom, absolute normal alignment at least
0.75, and displacement along either normal at most 2.5 Angstrom).
After frame zero, fixed partner identities are tracked, but pairing is
rechecked using the same criteria at every frame. A pair that fails is
unavailable (NaN), even if its relative transform remains mathematically
calculable. Both adjoining steps are also unavailable. Such samples are
excluded from means, standard deviations, outlier counts and events; they are
not interpreted as zero outliers. Quality flags explain the failed criteria,
which are geometric proxies, not proof of hydrogen-bond rupture.
Missing/unsupported reference atoms,
nonfinite coordinates, degenerate fitted frames and ambiguous relative transforms
still produce NaNs. A step is missing when an adjacent pair has no calculable
frame or its own transform is ambiguous. Distortions within the pairing criteria
remain measurable. Heatmap tooltips explain sample quality and missing
data, and the current-frame coverage readout reports valid members and quality
flags. Analysis is recomputed when the dataset loads, removing broken-pair
transforms from baselines produced by the earlier experimental implementation.

The heatmap shows frames on X and all detected base pairs or steps on Y for the
selected parameter. Click a cell to select its member; Ctrl-click jumps to its
frame without changing the selected member, and Ctrl-drag scrubs through frames.
Ordinary dragging pans without changing the selection. The taller heatmap can be resized vertically
by dragging its bottom border. Mouse-wheel zoom over an axis affects only that
axis; pan/zoom stay within the data bounds. Direct frame/member inputs, a
selected-member guide and a Reset view button simplify navigation. Angular values
are in degrees, translations in Angstroms, and heatmap colors are per-member
trajectory z-scores. Script attributes include `attr("lavery/step/twist")`.
Per-member finite-sample means and population standard deviations are cached
after trajectory analysis. Constant finite series have zero z-score; missing
samples remain NaN and are rendered as transparent heatmap cells. Pair labels
include one-based backbone-range IDs (`S1`,
`S2`, etc.), residue names and sequence IDs; step labels name the consecutive
residues on the first strand. Pair and step numbers in the UI are one-based.
Hover a heatmap cell for
its label, frame, value and z-score. The heatmap remains available even if the
selected member has no finite samples.

An outlier-count timeline above the heatmap counts all pairs/steps for the active
parameter at each frame, using the same per-member baselines and strict sigma
threshold as the outlier list and viewport. Changing the sigma slider updates
the counts immediately, without recalculating the trajectory. The X-axis uses
VIAMD's displayed trajectory time and unit when available, otherwise frame
indices. Both Lavery timelines use those same time units. Ctrl-click either
timeline to jump to the nearest frame; Ctrl-drag scrubs continuously, including
when dragging beyond the plot edges (clamped to the first/last frame).
Navigation stops playback and updates the shared Animation frame, all cursors,
outlier feedback and distribution's current-value marker. Either Ctrl key works,
including in VIAMD's main timeline. Ordinary dragging pans without jumping;
the distribution has a value axis, so it does not provide time navigation.
Hover for outlier and valid-member counts. Missing samples are excluded, constant
finite series count as valid but not outlying, and frames with no valid samples
are gaps rather than zero-outlier frames.

The parameter-value timeline can optionally overlay all members; the distribution remains
specific to the selected member. The current-frame outlier list covers all
members of the active parameter. Viewport synchronization highlights the base
atoms of **all** members exceeding the selected sigma threshold, using VIAMD's
normal highlight style, not a dedicated emissive/glow overlay. Hovering a row in
the current-frame outlier list temporarily highlights only that pair/step,
including when automatic highlighting is off. Leaving the row restores the
automatic mode; clicking a row selects the member, updates its timeline and
distribution, and centers the heatmap on that member and the current frame
while preserving the zoom level. The heatmap's Go to selection button does the
same without changing the selection. Other active hover highlights
take precedence over automatic highlighting, but not explicit outlier-row hover.

Lavery follows the master Animation frame during scrubbing and playback:
current-frame values, outlier counts/list, viewport highlights and plot cursors
update automatically. A zoomed heatmap pans horizontally when the new frame
leaves its visible range, preserving its zoom and vertical range. The
distribution retains its whole-trajectory baseline and marks the current
sample with a moving value line; missing samples have no value line.

### Combined outliers, events and export

Enable **Outlier in any selected parameter** to choose parameters within the
current intra-pair or step class. Each member is counted once if any selected
parameter exceeds its own baseline's sigma threshold. The count plot, event
list, current-frame list and viewport use the same selection; the heatmap
continues to display the active parameter. A member counts as valid when at
least one selected parameter is finite; an empty selection has no valid members.
Current-frame rows are sorted by absolute z-score severity, name their most
extreme selected parameter, and list all contributing parameters on hover.

Previous/Next outlier frame finds the nearest earlier/later frame with an outlier
(no wrapping). Consecutive outlying frames form events, split by zero-outlier or
no-data frames. Each event reports its frame span, elapsed time between endpoint
samples, peak absolute z-score and its frame, and maximum simultaneous outlier
count. Clicking an event jumps to its severity peak. A one-frame event has zero
elapsed duration. Count/tolerance controls stay above a separately scrollable
detail area; member timelines and distributions are collapsed until requested.

**Export CSV** writes all six parameters of the current class over all frames
and members, not just outliers. Rows include run, frame, displayed time/unit,
member and residue/pair identities, raw value/unit, validity, quality flags and
reasons, baseline mean/population SD, z-score, sigma, selected-parameter mask,
per-parameter and combined-member outlier flags. Missing values are `nan`;
unpaired/invalid samples have quality reasons and are excluded from statistics.
Selection bits 0-5 follow
Buckle/Propeller/Opening/Shear/Stretch/Stagger or
Tilt/Roll/Twist/Shift/Slide/Rise. Export each class separately to obtain all 12
parameters. Failed writes are reported in the application log.

## Update
If you want to stay informed about the latest updates of VIAMD, please register your email address on the [form](https://forms.gle/fAxuWob8nMLcrS5h9). 

## Citations:
* General Framework:
  * R Skånberg, I Hotz, A Ynnerman, M Linares, VIAMD: a Software for Visual Interactive Analysis of Molecular Dynamics, J. Chem. Inf. Model. 2023, 63, 23, 7382–7391 https://doi.org/10.1021/acs.jcim.3c01033
  * R Skånberg, C König, P Norman, M Linares, D Jönsson, I Hotz, A Ynnerman, VIA-MD: Visual Interactive Analysis of Molecular Dynamics, 2018, Eurographics Proceedings, p. 19–27

* Specific tool:
  * Selection tool: Robin Skånberg, Mathieu Linares, Martin Falk, Ingrid Hotz, Anders Ynnerman, MolFind-Integrated Multi-Selection Schemes for Complex Molecular Structures, 2019, The Eurographics Association, p. 17-21​
  * Shape Space and Spatial Distribution Function: Robin Skånberg, Martin Falk, Mathieu Linares, Anders Ynnerman, Ingrid Hotz, Tracking Internal Frames of Reference for Consistent Molecular Distribution Functions, 2021, IEEE Transactions on Visualization and Computer Graphics, 28 (9), 3126-3137​

## Financial Support
<p align="center">
<img src="https://raw.githubusercontent.com/scanberg/viamd/master/docs/images/serc.png"  height="75" >
 &nbsp;&nbsp;&nbsp;
<img src="https://raw.githubusercontent.com/scanberg/viamd/master/docs/images/wallenberg.jpg"  height="75" >
 &nbsp;&nbsp;&nbsp;
<img src="https://raw.githubusercontent.com/scanberg/viamd/master/docs/images/infravis.png"  height="75" >
 
</p>

* VIAMD has received financial support since 2018 from the Swedish e-Research center ([SeRC](https://e-science.se/)) and the [Wallenberg Foundation](https://www.wallenberg.org/en)
* VIAMD is currently supported as a development project in [InfraVis](https://infravis.se/)

## Acknowledgements

https://github.com/glfw/glfw

https://github.com/dougbinks/enkiTS

https://github.com/ocornut/imgui

https://github.com/epezent/implot

https://github.com/BalazsJako/ImGuiColorTextEdit

https://github.com/skaslev/gl3w

https://github.com/max0x7ba/atomic_queue

https://github.com/mlabbe/nativefiledialog

https://github.com/nothings/stb

#
<p align="center">
<img src="https://github.com/user-attachments/assets/39b69b10-88a1-43a7-9d69-68513ac4e632"  width="150" alt="This is the VIAMD logo" >
</p>
