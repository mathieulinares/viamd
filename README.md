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

Start with the [short task guide](docs/lavery-quickstart.md), or open the
[full reference and illustrated GUI tour](docs/lavery.md).

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
A third **Nucleotide** class adds backbone torsions (alpha-zeta, chi), sugar
pucker (pseudorotation phase and amplitude) and epsilon-zeta (BI/BII) for every
paired nucleotide; the step class also reports 3DNA minor/major groove widths
(minus 5.8 Å). Bimodal parameters use within-state baselines. Persistent
conformational switches (repuckering, BI/BII, syn/anti) are detected separately
by minimum source/destination residence, not ranked by exaggerated sigma.
Cartoon-first feedback includes phosphate-spanning groove bands, sugar
outlines, native distance/dihedral measurements, and labelled local-frame
components for the selected parameter. Measurement text is optional and off
by default; hover tooltips remain available. Event-coloured silhouette contours
reuse VIAMD's selection renderer, without interior tint or changing selection.
The compact **Visual feedback...** popup offers Contour / Glow / Both / Off,
contour thickness and glow strength, plus shared native measurement line
thickness, opacity and colours. Settings are saved with the workspace.

### Workspace

Lavery is a synchronized, resizable workspace (settings sidebar plus three panels) rather than one long
scrolling analysis window:

- **Overview:** outlier-count timeline and a heatmap that fills the remaining space.
- **Inspector:** selected-member timeline, normalized distribution comparisons,
  and severity-ranked current-frame outliers.
- **Events & quality:** event table and per-member pairing evidence/coverage.

One non-dockable window: **Settings** sidebar
on the left; **Events & quality** on top-right; **Overview** and **Inspector**
below it. Drag the thin dividers between sections, or under each plot and
list, to resize (double-click restores the default; **Visual feedback... > Reset layout**
restores all); sizes are saved in the workspace. The sections scroll
independently, without dock tabs or extra window title bars. Help text is in
tooltips, and rarely used settings sit in collapsed sections. The sidebar selects **Intra-base pair** or
**Inter-base pair** or **Nucleotide**, parameter, member, tolerance and detection time settings
always visible. Independent **Baseline range** (reference statistics) and
**Detection range** (event/state-switch search) both default to **Full trajectory**.
Each also accepts custom bounds or an existing named **Timeline period**, linked
by persistent ID and updated when the period changes.
Event parameters, advanced detection (baseline method, grouping,
hierarchy, hysteresis, gaps), exports (including events CSV)
and recomputation are collapsed. Visual controls sit in the compact
**Visual feedback...** popup with **Event regions** and **Measurements** tabs.
The inspector timeline and
distribution refit automatically when the selected series changes. Plot time navigation and selection
remain linked across all panels and Animation.

All 12 intra-pair/step parameters are regression-tested against the
[official Curves+ v3.0nc program](https://curvesplus.bsc.es/misc)
(`fit=T`, `refo=F`) across the repository's nucleosome structure: 147 pairs and
146 steps, and the A-U RNA duplex [RCSB 1RNA](https://www.rcsb.org/structure/1RNA):
14 pairs and 13 steps, to 0.006 Angstrom and 0.06 degree, allowing for reference
output rounding. This does not validate every nucleic-acid conformation or implement
Curves+'s helical-axis analysis; the module remains experimental.

Frame-zero pair detection uses geometric pairing
criteria (ring-center distance 3-9 Angstrom, absolute normal alignment at least
0.75, and displacement along either normal at most 2.5 Angstrom).
Only canonical A-T/U and G-C identities with the required contact atoms are
retained. Fixed identities are tracked even if their first-frame contacts are
already broken. At every frame, geometric criteria are combined with canonical,
directional donor-acceptor contacts:

- With explicit donor hydrogens, at least one canonical H-bond must remain,
  evaluated by mdlib's GROMACS criterion (D-A <=3.5 Angstrom and H-D-A <=30 degrees).
  Multiple hydrogens on one donor do not double-count a contact.
- When neither partner has a hydrogen on its canonical donors, at least two
  canonical heavy-atom contacts must be within 2.0-3.5 Angstrom. This is labeled
  **heavy-atom proxy**, not H-bond evidence. Partial hydrogen coverage does not
  silently fall back to the distance-only rule.

A pair that fails is
unavailable (NaN), even if its relative transform remains mathematically
calculable. Both adjoining steps are also unavailable. Such samples are
excluded from means, standard deviations, outlier counts and events; they are
not interpreted as zero outliers. Quality flags explain the failed criteria,
not an energetic or definitive chemical classification.
Missing/unsupported reference atoms,
nonfinite coordinates, degenerate fitted frames and ambiguous relative transforms
still produce NaNs. A step is missing when an adjacent pair has no calculable
frame or its own transform is ambiguous. Distortions within the pairing criteria
remain measurable. Heatmap tooltips explain sample quality and missing
data, and the current-frame coverage readout reports valid members and quality
flags. The header distinguishes **valid / tracked** pair and inter-pair counts:
N-1 inter-pair links only applies to an uninterrupted chain. One unavailable
internal pair invalidates two neighboring links; gaps and strand boundaries
are never bridged. For the nucleosome structure fixture, the 147 tracked pairs
include one without canonical H-bonds (one-based pair 127, D-A distances about
4.86 and 4.20 Angstrom), giving 146 valid pairs and 144 valid links among the
146 tracked links. Analysis is recomputed on dataset/run changes.

The heatmap shows frames on X and all detected base pairs or steps on Y for the
selected parameter. Click a cell to select its member; Ctrl-click jumps to its
frame without changing the selected member, and Ctrl-drag scrubs through frames.
Ordinary dragging pans without changing the selection. Resize the Overview panel
to resize its heatmap. Mouse-wheel zoom over an axis affects only that
axis; pan/zoom stay within the data bounds. Direct frame/member inputs, a
selected-member guide and a Reset view button simplify navigation. Angular values
are in degrees, translations in Angstroms, and heatmap colors are per-member
trajectory z-scores, cyan for negative and crimson for positive deviations.
Script attributes include `attr("lavery/step/twist")`. These resolve relative to
the evaluated run; no root-level temporal aliases shadow the run-relative paths.
Diagnostic attributes are `lavery/bp/quality`, `lavery/step/quality`,
`lavery/bp/contact_count`, and `lavery/bp/hydrogen_evidence` (1 for explicit
hydrogens, 0 for the heavy-atom proxy). Each is shaped [frames, members].
Per-member baselines are cached after trajectory analysis. The sidebar's
**Baseline** selector chooses the finite-sample mean and population standard
deviation (default) or the robust median and 1.4826 x median absolute deviation
(falling back to the SD when the MAD is zero), so large excursions cannot hide
themselves by inflating their own baseline. Angular parameters are measured on
the circle: series are unwrapped around their circular mean and deviations wrap
at +/-180 degrees; series that never approach that wrap are unchanged.
Constant finite series have zero z-score; missing
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

The parameter-value timeline can optionally overlay all members. The Inspector's
**Compare** selector overlays distributions of additional members alongside
the selected one, using shared bin edges and individually normalized probability
densities over finite samples. Comparisons are cached per parameter; empty
members are not plotted. Inspecting an event adds a full-span event distribution
for the selected member/parameter, in the event's colour, using shared bins and
independent normalization. Fewer than 10 valid samples appear as sample marks;
counts, median and range summarize the span. Valid gap/non-contributing frames
are included; missing samples are excluded. The current-value cursor remains.
The current-frame outlier list covers all
members of the active parameter. **Highlight event contributions** highlights
the base atoms of current-frame contributors from **all active events**, using VIAMD's
normal highlight style. A separate signed overlay uses crimson for
positive and cyan for negative deviations, with optional soft transparent halos:
intra-base-pair outliers draw rings on both bases, inter-base-pair (step)
outliers draw a bar between the two pair midpoints. Previewed members that are
not outlying in the displayed frame are drawn neutral grey. Hover a marker in
the viewport for a tooltip listing all parameters of that member's class,
their values and z-scores, which ones are outliers and any quality flags;
hovering an atom of a highlighted base adds the same section. Clicking a marker
selects that member and its most extreme parameter in all plots. These are
unlit, always-visible overlays, **not physically emissive bloom**. Hovering a row in
the current-frame outlier list adds a preview of that pair/step while paused,
including when automatic highlighting is off. Leaving the row removes the
preview; clicking a row selects the member, updates its timeline and
distribution, and centers the heatmap on that member and the current frame
while preserving the zoom level. The heatmap's Go to selection button does the
same without changing the selection. Other active hover highlights
take precedence over automatic highlighting, but not explicit outlier-row hover.

Nucleotide quality is parameter-specific: missing atoms, nonfinite coordinates
and degenerate geometry invalidate only affected measurements. Unusual defining
bond lengths and poor sugar-pucker fits are warnings, not exclusions: finite
values remain in statistics and events. Reasons appear in plot/viewport tooltips,
Pairing & coverage, parameter CSV rows and per-parameter script quality attributes.

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

Previous/Next outlier arrows find the nearest earlier/later frame with an outlier
(no wrapping). The count plot shades every outlier event and draws a dashed
line at the number of outliers expected by chance (Gaussian, independent
parameters); the summary-line tooltip compares observed and expected totals,
and a "near chance" note appears when observed is below twice expected.

Events group outlying (frame, member) samples that are close in time **and**
along the helix. Two samples join when their quiet gap is within **Bridge gaps**
(in frame fallback, at most **Bridge gaps** + 1 frames apart), and they are at
most **Neighbourhood** base pairs apart (default 2; a step
sits half-way between its two pairs). Pairs in separate duplexes never join.
**Whole structure** ignores position, which gives the earlier behaviour of
one event per stretch of outlying frames and suits global changes such as
unwinding. An event starts at the tolerance and continues while |z| stays
above the tolerance minus **Hysteresis** (default 0.5 sigma). With **Pair
loss** on (default), frames where a base pair breaks count as outlying
instead of ending the event. With timestamps, **Min duration** and **Bridge gaps**
use physical time (ps/ns); a positive minimum excludes single-sample spikes.
Initial/legacy thresholds migrate using the median sampling interval.
**Baseline range** chooses the reference interval; the independent
**Detection range** restricts the event/state-switch search. Both default to
**Full trajectory**. Distributions still cover the full trajectory;
states absent from the reference show **No baseline**, not borrowed statistics.
Boundary-touching events are marked
potentially clipped. Without time metadata, explicit frame controls remain.
The **Duration** column shows elapsed time (contributing frames in its
tooltip); clipped events carry a `*` after their region. **Event parameters** chooses the event parameters independently of the
plotted class (default all 12) via the 2x6 checkbox grid (column headers toggle a
whole class). **Use plotted parameters** copies the count-plot selection.

**Hierarchical events** (default on) retains those events as parents and adds
localized higher-threshold children, so a long DNA-wide event need not hide a
strong local burst. Child start = tolerance + **Burst extra** (default +1 sigma),
with the same hysteresis and duration/gap filters. Children always use the local
neighbourhood, even with Whole structure parents, and must touch fewer base
pairs and belong entirely to their parent. The table labels them **in #N**.
This is a two-level hierarchy; increase Burst extra if the broad background
still connects the burst, or set **Sub-events** to **Stand out from background**:
children are then where z differs from the parent's per-frame median z
(**Residual**, default 2 sigma), so local activity no stronger than the broad
event is found. Turning hierarchy off restores flat grouping.
Parent and child scores share samples and must not be added as independent counts.

The table is sortable and is sorted by **Score** by default: the sum, over
member-frames, of the largest contributing |z|, with each lost-pair frame
scoring the tolerance. Each event also shows its base-pair region, a
rule-based **Type** from its dominant parameter and sign (e.g. Unwinding,
Kink (roll), Unstacking, Opening (breathing / flip), Pair loss), duration,
peak frame and peak sigma. Clicking an event jumps to its severity peak and
temporarily shows only that event's feedback while paused. Resume playback or
change frame to restore all active events; no persistent pin or focus controls.
Member and parameter clicks also inspect their containing event. Its
arrow expands the intra- and inter-base pairs involved, each with its
outlying (and lost) frames, peak frame and z-score, and one button per
outlying parameter with its peak z-score. Clicking a member selects it
(switching class if needed), its strongest parameter and peak frame; clicking
a parameter selects that parameter at its own peak frame. Every plot follows.
Hovering an event, member or parameter adds a viewport preview while paused.
During playback, every active event automatically gets a thin coloured silhouette
contour over its overall member region, with precise rings/bars for current-frame
contributors and amber rings for lost pairs. Overlapping regions and shared
members are drawn once, without filling the molecular interior. Events overlapping in time have different stable colour slots;
colours match table swatches, count-plot bands and newly added timeline periods.
Children own the contour within their parent; otherwise the highest-scoring
event owns shared bases. Hover still identifies all involved events.
Six colour-blind-friendly colours go to the highest-scoring events; further
overlaps are grey (**+N grey** in the overview).
Crimson/cyan detail markers still encode deviation sign, not event identity.
Hover shows involved event IDs, types, parameter names,
values and z-scores; there is no permanent viewport status box or focus mode.
Bridged gaps retain context without invented detail. Right-click an event row
for **Select atoms** (the event's nucleotides become the VIAMD selection) or
**Add to timeline** (a coloured period with a note on the main timeline);
**Timeline...** above the table adds all events or removes the added periods.
**Active now** filters the table to events spanning the current frame; a filter
box matches type/parameter words and base-pair numbers or ranges; Up/Down steps
through events; hovering a row shows a members x frames footprint. Each
event is a single line; Peak frame is a hideable column (right-click the
header). A one-frame event has zero elapsed duration. Controls stay in the left sidebar;
events/quality live above the plots, and Inspector timeline and
distribution sections are initially expanded.

**Settings / Export > Export parameter CSV** writes all parameters of the current class over all frames
and members, not just outliers. Rows include run, frame, displayed time/unit,
member and residue/pair identities, raw value/unit, validity, quality flags and
reasons, baseline mean/population SD, z-score, sigma, selected-parameter mask,
per-parameter and combined-member outlier flags. Missing values are `nan`;
unpaired/invalid samples have quality reasons and are excluded from statistics.
Selection bits 0-5 follow
Buckle/Propeller/Opening/Shear/Stretch/Stagger or
Tilt/Roll/Twist/Shift/Slide/Rise. Export each class separately to obtain all 12
parameters. A final `baseline` column states `mean_sd` or `median_mad`; the
`mean`/`sd` columns then hold that baseline's center and scale.
**Export events CSV** (settings sidebar) writes one row per event, member and
parameter. Rows give the event span, peak, score, type and base-pair region; the
scope, baseline, start/continue sigma and neighbourhood (-1 = whole structure);
the member label and its outlying and lost frames; and whether the parameter
contributed, with its peak frame, z-score and value.
Failed writes are reported in the application log.

### Displayed midpoint axis and calculation lifecycle

The optional gold **smoothed midpoint axis** is built from valid pair-frame
origins. It unwraps each connected segment independently using periodic images,
applies a local binomial smoothing filter, and samples a cubic Hermite /
Catmull-Rom centerline. Broken pairs and topology gaps split the spline.
Geometry is fitted from the currently displayed/interpolated coordinates in
the lattice frame and transformed back with VIAMD's display rotation.
Invalid displayed geometry and unavailable nearest-frame samples are omitted.
This is a visualization of pair midpoints, **not the Curves+ screw/helical axis**,
and does not add Curves+ axis-relative helical parameters.

**Export displayed axis CSV** writes run, fractional displayed frame, segment,
sample, displayed coordinates in Angstrom, and unit tangents. It requires an
enabled axis with at least one valid connected segment.

Analysis shows frame progress and a **Cancel analysis** button. Cancellation
cooperates with VIAMD's global task interruption; reset, dataset/run changes,
and shutdown wait for the worker before releasing its storage. Failed/cancelled
runs have a visible retry action and never publish partial results. Script
readers finish before analysis attributes are replaced or removed, and publication
failures are reported rather than shown as successful calculations.

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
