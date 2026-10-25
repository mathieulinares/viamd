# Lavery parameters: reference

This page describes the Lavery component in
`src/components/lavery/`. For the short version, see
[lavery-quickstart.md](./lavery-quickstart.md).

## 1. Concepts

| Class | Members | Parameters (unit) |
|---|---|---|
| Intra-base pair | each detected base pair | Shear, Stretch, Stagger (Å); Buckle, Propeller, Opening (°) |
| Inter-base pair (step) | each pair of consecutive base pairs | Shift, Slide, Rise (Å); Tilt, Roll, Twist (°); Minor and Major groove width (Å) |
| Nucleotide | each paired nucleotide (two per base pair) | Alpha, Beta, Gamma, Delta, Epsilon, Zeta, Chi, Pucker phase, Epsilon − Zeta (°); Pucker amplitude (°, linear) |

Each frame produces one value per member and parameter, or no value when the
geometry is invalid. Quality flags explain why a value is missing or suspicious.

### Baselines and z-scores

Each member and parameter is compared with its own trajectory baseline:

```
z = (value - center) / scale
outlier  <=>  |z| >= Outlier sigma
```

| Baseline | center | scale |
|---|---|---|
| Mean/SD (default) | finite-sample mean | population standard deviation |
| Median/MAD | median | 1.4826 x median absolute deviation; falls back to SD when MAD = 0 |

The Mean/SD baseline is pulled towards long excursions, so the excursion
partly hides itself. Median/MAD is robust to these, so use it when a
distortion lasts a sizeable fraction of the trajectory.

Constant series have a zero z-score. Non-finite values are excluded.

**Angular parameters.** Buckle, Propeller, Opening, Tilt, Roll and Twist are
treated as circular quantities:

- Before computing the baseline, the series is unwrapped around its circular
  mean, so values such as 179° and -179° count as 2° apart.
- Deviations are then wrapped into [-180°, 180°).
- Unwrapping only changes values that are at least 180° from the reference.
  Ordinary data is therefore processed exactly as before.

Changing the baseline method or reference interval recomputes statistics on
edit release; the geometric parameters are not recalculated.

### Baseline range versus detection range

Both controls default to **Full trajectory**. Choose **Custom** for inclusive
**From / To** time bounds (ps/ns), or inclusive frame bounds without timestamps.
Alternatively choose **Timeline period** and select an existing named VIAMD
period. Dragging a timeline period onto either range selector selects it directly.
The period chooser highlights hovered entries in VIAMD's timeline.

- **Baseline range** defines the reference center/scale, circular unwrapping
  reference and within-state statistics/occupancy.
- **Detection range** defines where outlier events and residence-qualified state
  switches are searched for. It does not change the reference statistics.

The intervals may be disjoint. For example, select an equilibrated 300 K plateau
as the reference and detect deviations during heating. Heatmaps and count plots
still cover the full trajectory, using that reference; raw timelines and density
distributions remain full-trajectory as well.

Period links use VIAMD's persistent period IDs, not names or list positions, so
renaming or reordering does not break them. Resizing updates analysis on edit
release. Resolution reuses `timeline_period_frames`: endpoints are normalized
and clamped to the trajectory; following periods track the rounded current frame.
The UI shows the name, resolved time/frame bounds and whether the period follows.
Markers are allowed as one-sample intervals (subject to the ordinary zero-spread
baseline and minimum-duration rules). Following baselines recompute as their
resolved frames change, so long trajectories may be slower during playback;
fixed equilibrium periods are preferable for a stable scientific reference.

Deleted/unselected periods and periods covering no trajectory samples are
reported explicitly, with no fallback to Full trajectory. A missing baseline
period removes sigma references but not baseline-independent state switches.
A missing detection period clears both event and state-switch searches.
Old workspaces retain Full/Custom behavior; period modes and IDs are saved
alongside VIAMD's own periods.

Only finite reference samples contribute. A member/state with no reference
samples is labelled **No baseline**: its z-score is `nan`, it is excluded from
sigma-tested coverage and it cannot become a sigma outlier. No other state's
statistics are substituted. State-switch detection still works without that
reference because it uses state assignments and residence, not sigma.
Singleton/constant reference samples retain the existing zero-spread behavior
(zero z-score, no sigma outlier), not a statistically calibrated reference.

Time intervals may intersect the trajectory without matching sample timestamps
exactly; only saved samples inside them are used. Empty, reversed or non-finite
baseline intervals report an error and remove sigma references instead of
silently using the full trajectory. Both ranges are saved in the workspace;
older workspaces retain their detection range and use a full-trajectory baseline.

### Nucleotide class (sugar and backbone)

Nucleotide members are numbered 2·pair + side; the inspector and tooltips label
them as `S1:DA5 (bp 3)`. Only nucleotides that belong to a detected base pair
are analysed. Values are computed for every frame, even when the pair is
broken. Nucleotide quality is tracked separately for each parameter, not
inherited from the parent pair.

| Parameter | Atoms (IUPAC) |
|---|---|
| Alpha | O3′(i−1)–P–O5′–C5′ |
| Beta | P–O5′–C5′–C4′ |
| Gamma | O5′–C5′–C4′–C3′ |
| Delta | C5′–C4′–C3′–O3′ |
| Epsilon | C4′–C3′–O3′–P(i+1) |
| Zeta | C3′–O3′–P(i+1)–O5′(i+1) |
| Chi | O4′–C1′–N9–C4 (purines), O4′–C1′–N1–C2 (pyrimidines) |
| Pucker phase P, amplitude τm | Altona–Sundaralingam pseudorotation from ν0..ν4 |
| Epsilon − Zeta | BI (< 0°) / BII (> 0°) indicator |

The neighbouring O3′(i−1), P(i+1) and O5′(i+1) are used only when an O3′–P
bond exists, so Alpha is missing at a 5′ end and Epsilon/Zeta at a 3′ end.
Torsions use minimum-image bond vectors. Amplitude is linear; all other
nucleotide parameters are angular.

**Nucleotide quality policy.** Missing defining atoms, nonfinite coordinates
and degenerate torsions make only the affected parameters unavailable (NaN).
They are excluded from baselines, distributions, sigma events and state-switch
runs by the existing missing-data rules. A flat sugar with amplitude below
0.0001 degree has undefined phase but retains its finite, near-zero amplitude.
For Epsilon - Zeta, quality is the union of the Epsilon and Zeta flags.
Sugar-ring failures affect phase/amplitude, not unrelated backbone torsions;
a shared atom can of course affect more than one parameter.

Distortion is **warning-only**, not an exclusion criterion:

- A defining heavy-atom bond outside 1.0-2.1 Angstrom flags the torsions using
  it. Phase/amplitude check all five sugar-ring edges, including ring closure.
- A five-torsion Fourier-fit RMS residual above 5 degrees flags phase and
  amplitude. This diagnoses poor first-harmonic ring agreement; it does not
  change the production Altona-Sundaralingam values or state definitions.

These broad warning thresholds are heuristic, not a calibrated force-field
validation. Finite warning-bearing measurements remain in all analysis and
event detection: unusual deformation may be the signal of interest.
The selected parameter's quality appears in Pairing & coverage and plot
tooltips. Viewport tooltips name each affected parameter and its reasons.
Parameter CSV rows carry the individual parameter's flags and explanations;
warnings do not change the `valid` column to zero.
Scripts receive a U16 frame-by-nucleotide attribute alongside each parameter,
e.g. `lavery/nt/alpha_quality` alongside `lavery/nt/alpha`.
Flag bits are 512 (missing atoms), 128 (nonfinite coordinates), 4096
(degenerate nucleotide geometry), 8192 (unusual bond) and 16384 (poor pucker fit).
The latter two are warnings; zero means no issue detected.
New thresholds/flags do not change pair/step quality handling.

**Grooves.** Minor and Major groove widths belong to the step class and use
the 3DNA phosphorus-distance definition (El Hassan & Calladine), for 1-based
step *i* along one helix:

```
minor(i) = ½(|P1(i+2) − P2(i−2)| + |P1(i+3) − P2(i−1)|) − 5.8 Å
major(i) = |P1(i−1) − P2(i+2)| − 5.8 Å
```

P1/P2 are the phosphorus atoms on the two strands. The 5.8 Å phosphate
diameter is subtracted (3DNA reports the raw P–P distance). Steps too close to
a helix end, or with a missing P, have no value. Grooves are computed from the
phosphates even when the step itself is flagged by quality checks.

**Within-state baselines.** Phase (North [270°, 90°) / South [90°, 270°)),
Epsilon − Zeta (BI / BII), Chi (anti / syn: syn between 270° and 90°), Alpha
and Gamma (g+ [0°,120°) / t / g−) have discrete states. Each member has a separate
center/scale for each state; a sample's z-score uses its current state's
reference. A switch does not acquire an enormous z merely because the dominant
state is tightly clustered. Singleton/constant states have zero z, not a
borrowed reference from another state. Tooltips and the inspector show the
current state and the dominant state's fraction of baseline samples. The heatmap
uses the same baselines. The inspector reference and parameter CSV center/scale
also use the current state; the total reference count includes every state
represented inside the baseline range. Occupancy is a sample fraction, not
elapsed-time weighting for irregular sampling.

### Residence-qualified state switches

The **State switches** tab detects changes independently of sigma outlier
events. A source run and a destination run must each meet **Min residence**:
elapsed first-to-last sample time with physical timestamps, or contributing
samples in frame fallback. Any positive time minimum rejects singleton runs.
Short excursions are ignored. Missing data resets the remembered source.
Detection starts fresh at the selected range boundary; no transition is inferred
from an unseen source outside it. No gaps are bridged and no hierarchy is applied.

The initial residence follows the event duration default; users can override it
independently. Rows report source/destination states, destination-run residence
and the shortest signed angular change between the two qualified runs' circular
means. These are not sigma scores and do not enter the outlier-event ranking.
The Event parameters mask and Detection range apply. Clicking a row selects
the nucleotide/parameter at destination onset, isolates its feedback and overlays
the destination-run distribution. Playback or frame changes end inspection.
The separate state-switch CSV includes state names, frames/times, angular change,
residence and the minimum used. Standard state boundaries are heuristic:
validate residence thresholds and transitions scientifically.

#### Timing context and incomplete residences

Rows distinguish **adjacent runs** from **separated runs**. Adjacent means that
the last source sample immediately precedes the first destination sample, not
that the transition time or path between those samples is known. Separated
runs contain finite excursions that did not qualify as a new remembered source.
The most recent qualified run of the same source state replaces an earlier one,
just as before; missing data still resets the source. This presentation does not
change detection, ranking, thresholds or run qualification.

Hover a row or select it to read the source and destination spans, observed
residences and intervening sample count. The parameter timeline shades the
source blue, intervening interval amber and destination purple.
The inspector selection summary includes member, parameter, state change,
angular change, timing and current-sample quality; an outlier-event selection
instead shows event type, region, span, selected member/parameter and quality.
No permanent status text is added to the molecule.

Source runs touching the detection start have unknown onset. Destination runs
touching the detection end have unknown completion: their observed residence
is a lower bound. The UI distinguishes the actual trajectory boundary from
a restricted detection-range boundary. An end-boundary label does not predict
that the state persists beyond the last available sample.

State-switch CSV retains its original columns and appends:
`source_begin_frame,source_end_frame,intervening_samples,source_clipped,`
`destination_clipped,destination_at_trajectory_end,source_begin_time,source_end_time`.
Frames are zero-based, inclusive; source times use the existing `time_unit`.
`source_clipped` and `destination_clipped` mean the run touches the corresponding
detection boundary (including a trajectory boundary), and do not assert that
the full run ends there. `destination_at_trajectory_end` distinguishes the actual
trajectory end from a restricted range. Source frame zero identifies a
trajectory-start boundary. The existing `residence` remains the observed
destination residence, not an estimated full lifetime.

Quality presentation uses finiteness to distinguish **Unavailable - excluded
from analysis** from **Warning - finite value retained**. Finite pair/step
values with contextual pairing flags are labelled **Quality flags - finite
value retained**, preserving, for example, a calculable groove despite invalid
pair-dependent geometry. Individual reasons remain visible in the inspector,
coverage and plot/viewport tooltips. No quality policy or validity rule changes.

## 2. Window layout

The Lavery workspace is one non-dockable window with child sections separated
by draggable dividers. No dock tabs or separate panel title bars consume space;
each section scrolls independently:

```
+------------+------------------------------------------+
|            |  Events & quality                        |
|  Settings  |  (events table, pairing quality)         |
|  (sidebar) +=====================+====================+
|            |  Overview           |  Inspector         |
|            |  (count + heatmap)  |  (timeline + dist) |
+------------+---------------------+--------------------+
```

Drag a divider (sidebar | right, events = bottom, overview | inspector) to
resize; the split is stored as a fraction of the window. A thin handle under
the count plot, timeline, distribution and current-frame list resizes that
element (60-2000 px). Double-clicking any divider or handle restores its
default, and **Visual feedback... > Reset layout** restores all of them. Sizes are saved
in the workspace (see [Workspace keys](#workspace-keys)).

To keep the window light, help text lives in tooltips: hover a label, a
section header or a `(?)` marker. Rarely changed settings sit in collapsed
sections.

- **Settings** (left sidebar):
  *Selection* (class, parameter, member, frame; valid-member counts in the
  member tooltip) and *Detection* (Tolerance, Min duration / Min frames,
  Baseline range, Detection range) are always visible. Collapsed sections:
  *Event parameters* (the 2x6 grid; the header names the subset),
  *Advanced detection* (baseline, neighbourhood or whole structure,
  hierarchy and burst extra, hysteresis, pair loss, bridge gaps, Time in ns,
  count any selected parameter) and *Export & tools*
  (parameter CSV, **Export events CSV**, Recompute analysis).
  The **Visual feedback...** popup provides **Event regions** and
  **Measurements** tabs for region style, contour/glow strength, native
  measurement styling, labels, axis, heatmap and layout reset.
- **Events & quality** (top): the outlier events table (described below) and
  per-member pairing quality and coverage.
- **Overview** (bottom left): a single summary line (current-frame outliers /
  valid members, with coverage, chance totals and controls in its tooltip, and
  a "near chance" warning when the trajectory total is below twice the chance
  level), arrow buttons stepping to the previous/next outlying frame, then the
  outlier count per frame, with events shaded
  as bands (matching the event contour colours) and a dashed line
  at the count expected by chance (see [Chance level](#chance-level)).
  Hovering lists the events at that frame. Below it, the heatmap (Reset view,
  Go to selection) shows every member against frame, with outliers marked.
- **Inspector** (bottom right): the **Timeline** and **Distribution** of the selected
  member and parameter, with values, baseline (mean/SD or median/scaled MAD),
  z-scores and quality flags, followed by the **Current-frame outliers** list
  (outliers at the displayed frame, strongest first; hovering a row previews
  it in the viewport). **Both plots refit their axes automatically**
  whenever the series changes: a new parameter, member, baseline, **All
  members** toggle or **Compare** member (for example after clicking an event parameter).
  After that you can zoom freely until the next change. When the current
  frame changes (playback, an event click, a jump) and leaves the visible
  range, the timeline, count plot and heatmap pan to recentre it, keeping
  your zoom; panning while paused is not overridden.

## 3. Outlier events

### Event distribution comparison

Click an event, member or parameter to inspect it. The Inspector's distribution
keeps the selected member's full-trajectory density and overlays that member
and parameter across the event's **inclusive frame span**, including valid
samples in bridged gaps and frames where it did not contribute. Missing values
are excluded, with valid and missing counts shown explicitly; pairing-loss-only
spans may have no valid parameter samples.

The event overlay uses the same bins as the trajectory and is independently
normalized to unit area. Samples have equal weight, not elapsed-time weight,
so irregular sampling can affect density estimates. It uses the event colour. With fewer than 10 valid
samples, diamonds at the sample values replace the density curve (repeated
values may overlap). The current-frame value line remains a separate cursor.
Event median and min/max summarize the full-span samples, not just the peak.
These summaries use the displayed raw values, not circular statistics; angular
values straddling the wrap boundary require care when interpreting the median.

The comparison follows the temporary inspection selection: resuming playback
or changing frame returns to trajectory-only distributions. Changing event,
member or parameter recomputes the cached overlay and refits the plot.

### Detection

Events are built from **samples**: one sample is one member (base pair or
step) in one frame. A sample takes part when one of the following holds:

- it is *weak*: at least one parameter in **Event parameters** has
  |z| ≥ tolerance − **Hysteresis** (the continue threshold, never below 0.5);
- it is *strong*: at least one parameter has |z| ≥ tolerance (the start
  threshold);
- it is *lost*: with **Pair loss** on, a base pair that has a baseline loses
  its pairing in this frame. Its quality flags show separation, alignment,
  out-of-plane offset or missing H-bonds, and nothing else. Lost samples count
  as strong even when no intra-base-pair parameter is selected.

**Event parameters** selects the parameters, independently of the class shown in the
plots (default: all 12). The 2x6 grid (Intra column, Inter
column) picks any subset; a column header toggles the whole class and shows a
mixed state when only part of it is selected. The collapsed section header
names the subset (or "N of 12"). **Use plotted parameters** copies
the class and parameter(s) of the count plot. With nothing selected, only pair
losses (if on) form events. The CSV `scope` column is `all`, `intra`, `inter`,
or the chosen names joined by `+` (for example `Roll+Twist`).

Two samples are **connected** when both of these hold:

1. **Time**: quiet time between contributions is within **Bridge gaps**
   (defined below). In frame fallback mode they are at most **Bridge gaps** + 1
   frames apart. With zero gap, only the same or consecutive frames connect.
2. **Space**: they are at most **Neighbourhood** base pairs apart along the
   helix. Distance is measured on the chain *pair – step – pair*, where a step
   is half a base pair from each of its two pairs:

   | From | To | Distance (bp) |
   |---|---|---|
   | pair i | pair i+1 | 1 |
   | pair i | step (i, i+1) | 0.5 |
   | pair i | step (i+1, i+2) | 1.5 |

   Members that no chain of steps links, such as separate duplexes or
   pairs on either side of a missing step, are never neighbours. With
   **Whole structure**, space is ignored and only time matters.

An event is a connected group of samples that contains at least one strong
or lost sample. Groups of only weak samples are discarded, which is the
hysteresis: a weak sample can extend an event but never start one.

With physical time metadata, **Min duration** drops events whose elapsed time
`time[end] - time[begin]` is below the minimum. This includes bridged gaps.
A single sample has zero duration: any positive minimum removes it, zero keeps
it. **Bridge gaps** is the maximum quiet time between sampled contributions:
for samples at frames `a < b`, this is `time[b-1] - time[a]`. Thus adjacent
samples always join temporally, even with irregular sampling; spatial grouping
still applies. For uniform sampling, one missing sample costs one sampling
interval. A large interval between consecutive observations cannot establish
whether an event stopped in between; it is not treated as observed quiet time.

Time controls use ps or ns (**Time in ns**), stored in ps. The initial minimum
is two median positive sampling intervals; legacy workspaces migrate their
saved minimum as `(EventMinFrames - 1) * median_interval`, and their gap limit as
`EventMaxGap * median_interval`. This is a starting point, not a universal
physical lifetime. Subsequent edits are physical times, independent of sampling
rate. Without physical time metadata, the sidebar explicitly uses **Min frames**
(default 3 contributing frames) and a quiet-frame gap limit. Invalid, repeated
or decreasing physical timestamps are reported, not silently interpreted as frames.

**Detection range** restricts clustering to samples in the inclusive **From / To**
interval (or inclusive frame bounds in fallback mode). The independently selected
baseline range, full-trajectory count plots and distributions remain unchanged.
Out-of-trajectory bounds
can intersect the trajectory; an empty, reversed or non-finite interval reports an
error and clears detected events. Events touching an interior sample boundary
are marked **clipped** (a `*` after the table Region, explained in its tooltip),
meaning potentially truncated, not proven continuation
outside the interval. Natural trajectory endpoints are not marked. Minimum
duration still applies to clipped events. No contributions outside the range
are borrowed to extend or seed an event.

The table's **Duration** column header carries the time unit and shows elapsed
time; its tooltip gives the contributing frame count. Without timestamps it
shows the inclusive frame span. One sample remains one contributing sample,
even though its elapsed time is zero. Sorting uses elapsed time when available,
otherwise the inclusive frame span.

For each event, the following are recorded:

| Field | Meaning |
|---|---|
| Frames | First and last frame. Duration is the elapsed time between them |
| Region | Lowest and highest base pair touched; a step touches both of its pairs |
| Peak frame / Peak sigma | Frame and value of the largest contributing \|z\| |
| Score | Sum, over the event's samples, of each sample's largest contributing \|z\|; a lost sample scores the tolerance |
| Type | Rule-based description; see below |
| Max simultaneous | Largest number of samples in one frame (shown in the row tooltip) |

**Why both time and space?** Grouping by time alone puts two unrelated
distortions at opposite ends of the molecule into one event whenever they
overlap in time. That makes the member list noisy and the peak misleading.
The default neighbourhood of 2 bp keeps a kink together with the
compensating Roll and Twist of the neighbouring steps, while separating it
from a distortion further along the helix. Use **Whole structure** for
global changes such as unwinding of the whole duplex.

### Hierarchical events: broad context plus local bursts

**Hierarchical events** is on by default in the GUI. It keeps the ordinary
events above as parents and performs one additional, higher-threshold pass:

1. Child start threshold = tolerance + **Burst extra** (default +1 sigma).
   Child continue threshold = max(0.5, child start − Hysteresis). Only parameters
   already contributing to the parent are eligible; pair-loss samples remain eligible.
2. The higher-threshold samples are clustered with the same minimum duration,
   **Bridge gaps** and local **Neighbourhood**. Even with **Whole structure**
   enabled for parents, children use the local neighbourhood and never join
   separate duplexes.
3. Keep a candidate only when every one of its member/frame/parameter
   contributions belongs to one parent, its time range lies inside that
   parent's, and it touches strictly fewer distinct base pairs. A step touches
   both endpoint pairs. An unchanged broad component is not duplicated.
4. Retain the parent unchanged and link the child to it. The hierarchy has two
   levels; it is not recursive.

Example: a 100-frame event spanning the DNA can retain a 5-frame opening burst
on a few base pairs as a separate child. At tolerance 2.5 sigma and Burst extra
1, a child needs a 3.5-sigma sample (or pair loss) to start and continues down
to 3.0 sigma with the default hysteresis. A merely different but not stronger
local distortion is not automatically split out. If the broad background still
connects everything at the child threshold, increase Burst extra, or use
background mode.

#### Sub-events: Stronger vs Stand out from background

The **Sub-events** combo (Advanced detection) chooses how step 1 selects child
samples:

- **Stronger** (default): |z| ≥ tolerance + Burst extra, as above.
- **Stand out from background**: for each parent and frame, the background of
  each parameter is the median signed z over all valid members of the same
  class (base pairs or steps) inside the parent's base-pair region, outlying or
  not; a step counts only if both its pairs are in the region. A child sample
  needs |z − background| ≥ **Residual** (default 2 sigma) and continues above
  max(0.5, Residual − Hysteresis). A uniform shift cancels, so a local swing
  inside an equally strong whole-DNA event becomes a child. Steps 2–4 are
  unchanged: only parameters already contributing to the parent qualify, pair
  loss still qualifies, and children must be local and smaller.

The background needs at least 5 valid members per frame and class; otherwise
those samples cannot start a child. With fewer than 10 base pairs in the parent
region the median is noisy, and the row tooltip warns. Reported z, peaks and
scores remain raw z-scores; the residual only decides membership. Hover text
says "stands out from parent background by X sigma" and the CSV `burst_mode`
column is `background` (or `stronger`; empty for top-level events).

The table reports parent and burst counts separately. Children have their own
chronological IDs and show **in #N** in the table, members, scores, peaks and colour
swatches. Hover text and main-timeline notes include the parent ID and burst
threshold. A child gets a different colour from its active parent. At shared
bases, the child owns the silhouette contour so its local patch remains visible
inside the parent's region. Between events of the same hierarchy level, the
higher score wins (chronological order breaks ties). Colours are not averaged,
so the contour still matches an event-table swatch.

Parents and children intentionally share samples: **do not sum their scores or
member counts as independent observations**. The count plot still counts each
member once, and viewport detail markers merge contributions by class/member.
Switch hierarchy off to restore the previous flat event grouping.

### Types

The **Type** comes from the *dominant parameter*: the one with the largest
summed contributing |z| over the event. The sign of its summed z decides
between the two labels where there are two:

| Dominant parameter | Type |
|---|---|
| Buckle, Propeller | Buckle / propeller |
| Opening | Opening (breathing / flip) |
| Shear | Shear (wobble) |
| Stretch | Pair stretching (+) / Pair compression (−) |
| Stagger | Stagger |
| Tilt | Tilt |
| Roll | Kink (roll) |
| Twist | Overwinding (+) / Unwinding (−) |
| Shift, Slide | Step translation |
| Rise | Unstacking (+) / Compressed step (−) |
| Minor groove, Major groove | Widening (+) / Narrowing (−) |
| Pucker phase, Pucker amplitude, Delta | Sugar fluctuation |
| Epsilon, Zeta, Epsilon − Zeta | Epsilon / zeta fluctuation |
| Alpha, Gamma | Alpha / gamma fluctuation |
| Beta | Beta crankshaft |
| Chi | Chi fluctuation |

When the event contains a lost pair, the type starts with **Pair loss**, for
example "Pair loss + Opening (breathing / flip)", or just "Pair loss" when
no parameter is outlying.

The rules are deliberately simple. Treat the type as a hint for reading the
table, not as a structural classification.

### Chance level

Even at random, a fraction of samples exceeds the tolerance. Assuming a
Gaussian distribution and independent parameters, one parameter exceeds
σ with probability *p = erfc(σ/√2)*. A member tested on *k* parameters
exceeds it with probability *1 − (1 − p)^k*.

The count plot draws the number expected per frame (valid members ×
that probability) as a dashed line. The summary-line tooltip compares observed and
expected totals over the trajectory. For example, at 2.5 σ, p ≈ 1.2%. With
24 base pairs each tested on all six intra parameters (aggregate mode), that
is about 1.7 chance outliers per frame.

When the curve stays near the dashed line, the hits are mostly noise.
Rank events by **Score** rather than by count, and raise the tolerance or
**Min duration** (or **Min frames** without timestamps). With the Median/MAD baseline, the Gaussian figure is
approximate.

### Members and parameters

Each event lists the members involved, sorted by peak |z|, with an
`[intra]`, `[inter]` or `[nt]` tag. A nucleotide joins events at the position
of its base pair; a step covers both of its pairs. Each member row shows, on one line:

- one coloured button per outlying parameter (in the Type column), labelled
  with its name and peak z. Crimson means positive, cyan negative. A grey
  **Pair lost** button appears when the pair broke;
- the number of outlying frames inside the event (`fr`), and how many of them
  the pair was lost;
- the peak frame and its z-score, and the parameter that produced the peak.

Members are sorted by peak |z|. Members that were only lost come last.

### Interaction

Every event is one table row: colour swatch, `#N` and its time span (frame
span without timestamps; local children show `in #P`), Region, Type,
Duration, Peak sigma and Score. **Peak frame** is hidden by default;
right-click any column header to show or hide columns. The toolbar above the
table shows the event count (local bursts separately; hover for thresholds and
usage help), the **Active now** filter, a text **filter** and a **Timeline...** menu.

The filter splits on spaces; every term must match. A number (`7`) or range
(`5-9`) keeps events whose base-pair region overlaps it; any other term is a
case-insensitive substring of the event type, the names of its outlying
parameters, or `loss`/`lost` for events with a lost pair. The filter is not
saved in the workspace. **Timeline... > Add all shown events** respects the
filters.

With the events table focused (and no text field active), **Up/Down** inspects
the previous/next shown event in table order and scrolls it into view; the
table takes ownership of these keys, so ImGui keyboard navigation does not move.

Hovering an event row adds a **footprint** to its tooltip: one row per member
(ordered along the helix, steps between their pairs) against the event's frames,
in the event colour, amber for lost-pair samples, with a line at the current
frame and a triangle at the peak. Bridged gaps appear empty.

| Action | Result |
|---|---|
| Click a column header | Sort by that column (default: Score, descending) |
| Click an event row | Jump to its peak, stop playback and inspect only this event |
| Expand an event (arrow) | Show its members |
| Click a member | Select its class, the member, its strongest parameter and the peak frame; playback stops |
| Click a parameter button | Select that parameter at its own peak frame |
| Hover an event, member or button | Add a viewport preview while paused; tooltips always give details |
| Right-click an event > **Inspect at peak** | Same as clicking the row |
| Right-click an event > **Select atoms** | Replace the VIAMD selection with the nucleotides of every member of the event |
| Right-click an event > **Add to timeline** | Add the event to the main timeline as a coloured period with a note (region, frames, peak, score, members) |
| **Timeline... > Add all shown events** | Add every shown event in table order, until the timeline's limit of 64 periods |
| **Timeline... > Remove added events** | Remove the periods added from Lavery events |
| **Active now** | Show only events whose span contains the current frame |

The event that contains the current frame is highlighted. Event numbers (#N)
are chronological and match the `event` column of the events CSV, whatever the
table order.

**Temporary paused inspection.** Clicking an event, its member or a parameter
shows only that event's contour and current-frame contributions, excluding
overlapping events (including its parent or children). Only that event's table
row is selected. This remains until playback resumes or the frame changes,
then automatic all-active-event feedback returns. Detection-setting changes
or recomputation also clear inspection; changing the plotted class alone does
not. Hover previews cannot add other events during inspection. No persistent
focus mode, pin controls, or viewport status box is used.

Timeline periods are ordinary VIAMD periods: they can be renamed, recoloured
and used to restrict distributions, and they are saved with the workspace.
Lavery only remembers which ones it added for the current session, so
**Remove added events** does not remove periods loaded from a workspace.

## 4. Viewport feedback

| Glyph | Meaning |
|---|---|
| Rings on both bases | Intra-base-pair outlier |
| Bar (cylinder with end spheres) between two pair midpoints | Inter-base-pair (step) outlier |
| One/two phosphate-spanning bands with endpoint caps | Major/minor groove measurement, respectively |
| Sugar-ring outline and short torsion path | Nucleotide outlier, readable on the DNA cartoon |
| Crimson / cyan | Positive / negative deviation of the most severe contributing parameter |
| Grey | Member being previewed that is not outlying at this frame |
| Halo (optional) | Soft transparent glow that makes markers easier to spot |
| Native event-coloured silhouette contour | Active events' displayed regions; no interior tint; colour matches the event-table swatch, not deviation sign |
| Amber rings | An active event's lost base pair |

Which markers are shown:

- With **Highlight event contributions** on (default), all active events are
  shown automatically, while playing or paused. An event is active inside its
  inclusive frame range. No event selection or focus is required.
- Clicking an event while paused temporarily replaces this with that event
  alone, even when automatic highlighting is off. Play or change frame to
  restore automatic feedback. Hover tooltips follow the same inspection filter.
- Each active event's overall member region gets a thin silhouette contour. Events
  overlapping in time (including a shared endpoint frame) receive different
  colour slots. Successive events prefer successive samples of the Viridis
  palette, cycling from purple through blue/green to yellow rather than always
  reusing the first colour. Chronological rank determines the preferred sample.
  Higher-scoring events are assigned first; if their preferred colour is already
  used by an overlapping event, the next available sample is used. Six evenly
  spaced Viridis samples are used; any
  further overlapping event is grey, so the weakest events lose their colour
  first. The overview shows **+N grey** when that happens at the current frame.
  A grey contour means overflow; grey detail markers are previews of
  non-outlying members.
  Colours remain stable during playback and table sorting; changing detection
  settings may reassign them. Table swatches, count-plot bands and newly added
  main-timeline periods use the same colours. Existing timeline periods retain
  their saved or user-edited colour.
  Shared bases are outlined once. A child wins over its parent; otherwise the
  higher-scoring event wins, with chronological order breaking ties.
  Hover still identifies every involved event.
  Precise rings and bars show only exact current-frame contributions from all
  active events, across all three classes. Shared members are drawn once, with the
  union of contributing parameters.
- Contributions include the lower hysteresis threshold. Bridged gaps retain
  context but do not invent detail markers. Pair-loss rings remain visible even
  when pair geometry is invalid.
- While paused, hovering event/member/parameter rows or the current-frame list
  adds a preview without hiding active-event contributions. Playback ignores
  these previews so leaving the mouse on a row does not clutter the animation.

There is no permanent viewport status box. Hovering the context or an
involved atom shows event IDs, types, frame ranges and whether the hovered
member is contributing now or is context only. Hovering a detailed marker lists
each involved event and its current parameter names, with values and z-scores
below. Current parameters are marked **EVENT CONTRIBUTOR**. This works when
several events overlap; none needs to be pinned.

The event contour and detail markers obey **Signed outlier overlay**;
**Soft marker halos** controls only the small marker halos (the saved
`OverlayHalo` workspace key is unchanged).
The **Event parameters** mask, not the plotted class, determines automatic event
feedback. Changing the plotted class, parameter or member does not hide the
other active events.

Step bars are computed in periodic boxes using minimum-image displacement
between the two pair midpoints.

**Hover tooltip.** Hovering a ring or bar shows a tooltip containing:

- the member label;
- the frame, the sigma threshold and the baseline method;
- all parameters of the member's class with value, unit and z, with contributing parameters
  flagged OUTLIER and the peak marked "(peak)";
- any quality flags.

Hovering an atom of a highlighted base adds the same section to the normal atom
tooltip.

**Click.** Clicking a ring or bar selects that member and its most extreme
parameter, so every plot follows.

The markers use the application picking system. Besides member IDs, measurements
have their own parameter/member IDs through `EventType_ViamdPickingRangeReserve`.
Clicking measurement geometry selects that exact parameter rather than whichever
parameter has the largest z-score.

### Cartoon-first measurements

The default DNA cartoon is sufficient: no CPK representation is needed. The
existing atom-highlight pass emphasizes the involved base/sugar slabs and
backbone; compact signed markers and event-coloured context remain separate.
The selected member is also shown when its selected parameter is not outlying.
Paused parameter-button and viewport-marker hovers can temporarily override the
selected measurement without changing the plots.

Only one parameter has detailed measurement annotations at a time:

- **Minor/Major groove:** two/one translucent bands between the actual
  phosphate endpoints, with visible caps. The minor-groove spans are averaged.
  The same index helper drives numerical widths and their visual endpoints.
  The native distance geometry labels the raw P–P distances; a separate
  label gives the reported width after subtracting 5.8 Å. Hover identifies
  strand/residue/atom endpoints. The flanking backbone sections are included
  in the highlight. Missing flanking phosphates produce an unavailable label,
  not a misleading central-step replacement.
- **Alpha–Zeta and Chi:** VIAMD's native dihedral visualization on the four
  defining atoms, plus small endpoint markers and their connecting path.
  Native measurement values are tested against the Lavery torsions. Alpha,
  Gamma and Chi also label their state and dominant-state occupancy.
- **Epsilon − Zeta:** both defining dihedrals and a derived difference/state
  label. The difference is not drawn as a single four-atom dihedral.
- **Pucker phase/amplitude:** a sugar-ring outline and labels for both
  pseudorotation quantities; phase also identifies North/South. Neither is
  misrepresented as an ordinary atom angle. Amplitude has degree units but
  retains linear (not circular) statistics.
- **Pair/step parameters:** reference axes at the exact local midpoint frame,
  with a signed translation arrow or rotation-vector component arc. These
  diagrams are explicitly labelled as local-frame components, not ordinary
  interatomic distances or angles.

Measurement geometry uses a depth-tested transparent pass. Selected
distance/dihedral paths also have faint dashed through-structure cues, rendered
only where opaque geometry occludes the path. These thin cues write colour only,
not picking or velocity; they do not duplicate visible measurement geometry.
Custom local-component paths use same-radius rounded ends, and their arcs use
angle-dependent tessellation without a sphere at every subdivision, avoiding
bright white bead clusters on small angles. Native distance/dihedral feedback
now uses the same shared script-geometry submission as VIAMD's own visualization:
the generated points, thin lines and planes, with the application's point/line/
triangle colours. It is not redrawn as extra capsules. The periodic unwrapping,
parameter picking, depth-tested pass, optional labels and occluded cues remain.
Event context uses the native representation silhouette contours; clicking a
child isolates its context and contributions. Text visibility does not alter contours.
Minimum-image geometry is constructed before applying display rotation.
Visible detailed measurement geometry is pickable; occluded hints do not
intercept cartoon picking.

#### Integration with VIAMD

Lavery supplies nucleotide atom groups and its existing Viridis palette through
the shared `RepresentationContours` request. VIAMD draws those groups using
the same representation mark pass as selection/highlight, including cartoon
sugar/base slabs, backbone bands, visibility filters, periodic images and
assembly copies. The same post-processing mask filter and silhouette edge
calculation produce the contour, with its own thickness multiplier and
framebuffer scale. **Visual feedback... > Event regions > Contour thickness** ranges from 1x to 4x,
defaults to 1.5x and updates live without redrawing the molecular scene or masks.
The multiplier is saved as `ContourThickness` in the Lavery workspace section;
workspaces without this setting use 1.5x. Native selection/highlight thickness
is independent. Contour-only mode uses no surrounding proxy spheres or interior fill.

The **Visual feedback...** button replaces the sidebar Display section with a
compact popup containing **Event regions** and **Measurements** tabs. Event
region styles are **Contour** (default), **Glow**, **Both**, and **Off**.
Glow restores the original three base-centred translucent sphere layers, using
the same palette and child-priority ownership as contours. **Glow strength**
ranges from 0 to 1, defaults to 0.5, and scales the original layer alphas
(18/10/5 of 255). It is a colour-only overlay: no depth, picking or velocity
writes. Both combines this faint context with the native silhouette. Off removes
region context, not precise contribution markers or native measurements.
**Signed outlier overlay** remains the master switch and **Soft marker halos**
affects the small contribution markers only.

`EventVisualStyle` (0 contour, 1 glow, 2 both, 3 off) and `GlowStrength`
are stored in the Lavery workspace section. Older workspaces use contour/0.5.
Event geometry/strength/style changes invalidate the relevant rendering;
contour thickness changes only presentation, not the mask or scene history.

The Measurements tab exposes VIAMD's shared point/line/plane colours, **Line
thickness** (1-6 logical screen pixels), **Line opacity** and **Plane opacity**.
It uses the same controls as the script editor's **Settings > Measurement
visuals**, not separate Lavery copies. Native defaults remain 1 px, line alpha
0.6, and plane alpha 0.5. The shared line style is applied to all native script
geometry submitted through this helper, including distance, angle and dihedral;
Lavery's non-atomic local-frame diagrams remain separate.
Thicker native lines are screen-space quads from the existing generated line
segments, not world-space capsules or driver-dependent wide OpenGL lines.
Near-plane clipping is performed before perspective division, and widths scale
with the render resolution/HiDPI scale without changing when zooming.
The original native geometry path is retained at one framebuffer pixel.
Hidden measurement cues also follow the shared line colour/opacity.

Shared measurement settings persist under `[Script]` as
`MeasurementLineWidth`, `MeasurementPointColor`, `MeasurementLineColor` and
`MeasurementPlaneColor` (RGBA, including opacity). Workspace reset restores
native defaults before applying saved settings. **Reset visual defaults** in
the popup resets both event display and shared measurement style, without
changing detection or user selections; the script editor also offers a
measurement-only reset.

The contour layer is independent of selection and hover: neither their masks,
colours nor styles are replaced. Native atom/glyph flags are restored immediately
after the contour pass, before the ordinary marks are drawn. Selection/highlight
are composited above event contours. Automatic Lavery contributions no longer
set an additional yellow filled highlight over an already outlined event region;
explicit hover previews and normal VIAMD selection remain available.
Contour masks have their own lazy-render key, separate from the scene history,
and are regenerated on changes to regions, geometry, camera or output size.
Closing Lavery, disabling its overlay or removing the trajectory removes the
layer without stale replay. Like native selection marks, this path uses the
standard md_gl renderer, not the experimental gfx backend.

Hover and unmodified clicks on the native cartoon/atoms resolve through VIAMD's
picking domains, rather than an invisible glow sphere. Event hover details and
plot synchronization remain available; modified selection gestures are not
intercepted. Native bond/ring hits are also resolved through VIAMD's picking helpers.

A surfaceless EGL/OpenGL check of the actual mark and post-processing shaders
verifies cartoon-style slab silhouettes, unchanged interiors, distinct contour
colours, occluded geometry, native selection above contours, framebuffer-scaled
width, visibility filtering, layer removal and target resizing. A native
dihedral check verifies pixel-identical points/lines/planes through the shared
submission helper and retained exact-parameter picking. These rendering checks
do not replace visual inspection on a complete trajectory.
Additional pixel checks verify 1/2/4/6 px native line widths at two orthographic
zooms and in perspective, opacity 0/0.25/1, and near-plane crossing.

**Visual feedback... > Measurements > Measurement value labels** is off by default and saved with the
workspace. Geometry and hover tooltips remain available without text.
When enabled, value labels share the native measurement text/background styling, with
screen-space separation for nearby labels. They are redrawn every UI frame
even when the 3D scene is lazily cached, so a paused measurement does not vanish.
Playback retains the one selected measurement, but ignores hover previews.
All other active contributors stay compact rather than acquiring a label cloud.

## 5. CSV exports

### Parameter CSV (Export CSV)

One row per frame, member and parameter of the current class. `class` is
`bp`, `step` or `nt`; for a nucleotide, `pair_a` is its base pair.

```
run,frame,time,time_unit,class,member,label,pair_a,pair_b,parameter,unit,value,valid,
quality_flags,quality_reason,mean,sd,zscore,sigma,parameter_selected,parameter_outlier,
member_outlier,selected_mask,baseline,baseline_begin,baseline_end
```

The `baseline` column is `mean_sd` or `median_mad`. The `mean` and `sd` columns
contain that baseline's center and scale for the current state. Appended
`baseline_begin` / `baseline_end` give the first/last included reference sample
coordinates in `time_unit` (or `frame`); invalid intervals export `nan`.
These are interval endpoints, not a claim that every member/state has valid data
throughout. `valid` still describes the measured value: a finite value can have
`nan` reference statistics/z-score if its state is absent from the baseline.
There are 26 columns.

### Events CSV (Export events CSV)

One row per event, member and parameter:

```
run,event,begin_frame,end_frame,begin_time,end_time,time_unit,event_peak_frame,
event_peak_sigma,event_score,event_type,first_pair,last_pair,scope,baseline,sigma,
end_sigma,neighbourhood,class,member,label,member_outlier_frames,member_lost_frames,
parameter,unit,contributing,parameter_peak_frame,parameter_peak_zscore,parameter_peak_value,
parent_event,burst_sigma,truncated_begin,truncated_end,min_duration,max_gap_duration,
detection_begin,detection_end,burst_mode,baseline_begin,baseline_end
```

| Column | Meaning |
|---|---|
| event | Chronological event number (#N in the table) |
| event_score, event_type | Score and Type as shown in the table |
| first_pair, last_pair | 1-based base-pair region |
| sigma, end_sigma | Start and continue thresholds (hysteresis) |
| neighbourhood | Neighbourhood in base pairs; −1 means Whole structure |
| member_outlier_frames | Frames in which the member was outlying or lost |
| member_lost_frames | Frames in which the pair was lost |
| parent_event | One-based parent event ID, or 0 for a top-level event |
| burst_sigma | Child start threshold (residual threshold in background mode), or 0 for a top-level event |
| burst_mode | `stronger` or `background` for children; empty for top-level events |
| truncated_begin, truncated_end | 1 when touching an interior detection-range boundary; potentially truncated |
| min_duration, max_gap_duration | Detection minimum and quiet-gap limit, in `time_unit`; in frame fallback the minimum is contributing frames and the gap is quiet frames |
| detection_begin, detection_end | Requested inclusive detection bounds in `time_unit`, or trajectory endpoints when unrestricted |
| baseline_begin, baseline_end | First/last included baseline sample coordinates in `time_unit`, independent of detection bounds |

The events CSV has 40 columns. Time-valued settings are converted from internal
ps to the exported trajectory `time_unit`; fallback exports use `frame`.
For a period-based search, `detection_begin` / `detection_end` are the resolved
first/last sample coordinates, not unused custom-entry bounds. Baseline columns
likewise describe the resolved reference samples. Event-to-timeline annotations
also include the selected period names.
`sigma` and `end_sigma` describe that row's
event (the higher thresholds for children), and child `neighbourhood` is
always local, even under a Whole structure parent. Lost-pair score weight
remains the base tolerance for both levels.

`contributing` is 1 when the parameter was an outlier at some frame of the
event. All parameters of each member's class are written (6, 8 or 10). Non-contributing
parameters are reported at the member's peak frame, so their values remain
available for context. A member that was only lost has `nan` values.

Fields are RFC 4180-quoted when needed. Write failures are reported in the
application log.

## 6. Workspace keys (`[Lavery]` section)

| Key | Type | Default |
|---|---|---|
| ShowWindow, ShowHeatmap, SyncViewport, CompareAll, ShowAxis, SignedOverlay, OverlayHalo, AggregateOutliers | bool | - |
| SelectedParameters | bit mask, class-local (bit k = k-th parameter of the plotted class) | 1023 |
| MeasurementText | bool, viewport value text only | 0 |
| StateMinTimePs | float, independent minimum source/destination residence; -1 follows event minimum | -1 |
| StateMinFrames | int, residence samples in frame fallback | 3 |
| OutlierSigma | float | - |
| Metric, Member | int | - |
| Baseline | 0 = Mean/SD, 1 = Median/MAD | 0 |
| BaselineRangeEnabled | bool; 0 = Full trajectory, 1 = Custom or Timeline period | 0 |
| BaselineRangePeriod | bool; when enabled, use Timeline period instead of Custom | 0 |
| BaselinePeriodId | uint32, persistent VIAMD period ID; 0 = unselected | 0 |
| BaselineRangeBeginPs, BaselineRangeEndPs | float, inclusive reference bounds in ps | 0 |
| BaselineRangeFirst, BaselineRangeLast | int, inclusive reference frame fallback bounds | 0 |
| EventParameters | int (24-bit mask, bit k = Metric k: Buckle..Stagger, Tilt..Rise, grooves, Alpha..Epsilon − Zeta) | 16777215 |
| EventMinFrames | int, 1-100000 | 3 |
| EventMaxGap | int, 0-100000 | 0 |
| EventMinTimePs | float, nonnegative ps; -1 requests migration | -1 |
| EventGapTimePs | float, nonnegative ps; -1 requests migration | -1 |
| EventRangeEnabled | bool | 0 |
| EventRangePeriod | bool; when enabled, use Timeline period instead of Custom | 0 |
| EventPeriodId | uint32, persistent VIAMD period ID; 0 = unselected | 0 |
| EventRangeBeginPs, EventRangeEndPs | float, inclusive ps bounds | 0 |
| EventRangeFirst, EventRangeLast | int, inclusive frame fallback bounds | 0 |
| EventTimeNs | bool, time-control display unit | 0 |
| EventNeighbourhood | int (bp), 0-1000 | 2 |
| EventWholeStructure | bool | 0 |
| EventHysteresis | float (sigma), 0-2 | 0.5 |
| EventPairLoss | bool | 1 |
| EventHierarchy | bool | 1 |
| EventBurstExtra | float (sigma), 0.25-4 | 1 |
| EventBurstMode | int, 0 Stronger / 1 Stand out from background | 0 |
| EventResidualSigma | float (sigma), 0.5-6 | 2 |
| EventsActiveOnly | bool, events table "Active now" filter | 0 |
| LayoutSidebar | float, sidebar width / window width, 0.12-0.5 | 0.24 |
| LayoutEvents | float, events height / right-area height, 0.1-0.8 | 0.34 |
| LayoutOverview | float, overview width / bottom width, 0.2-0.8 | 0.52 |
| LayoutCountPlot, LayoutTimelinePlot, LayoutDistributionPlot, LayoutOutlierList | float, pixels, 60-2000 | 180, 220, 180, 160 |

If a workspace has no new keys, it loads with these defaults. An older 12-bit
`EventParameters` mask keeps its choices and gains the grooves and all
nucleotide parameters. Out-of-range or
non-finite layout values are clamped or reset.

## 7. Implementation notes

`lavery_core.h/.cpp` contains the parts that do not depend on the UI:

- `compute_statistics(out, values, count, angular, Baseline, state_metric)`;
- `torsion`, `pseudorotation`, `compute_nucleotide`, `groove_widths`,
  `helix_chains`, and the class helpers `class_first_metric`,
  `class_parameter_count`, `metric_class`, `metric_state*`;
- `MemberStatistics::deviation`, `zscore` and `is_outlier`;
- `detect_events(frames, pair_count, steps, EventOptions)`. This clusters samples
  with union-find. Each sample is connected to the most recent sample of every
  member within reach, which is enough because samples of one member are
  chained. Reach is precomputed by a breadth-first search over the
  pair–step–pair graph, two edges per base pair;
- `event_kind`, `event_dominant_metric` and `chance_outlier_probability`;
- `csv_header`/`csv_sample` and `csv_event_header`/`csv_event_row`.

`lavery.cpp` contains the UI. It handles the following:

- **Picking**: reserving the picking range (pairs, steps, nucleotides),
  filling tooltips and handling   clicks. Nucleotide markers outline the sugar and show the defining-atom
  path of a contributing torsion.
- **Events and markers**: `update_counts` builds the samples, using the start
  threshold, the continue threshold and pair loss. It also draws the events
  table (sorting, filtering, row context menu), builds markers from all active events
  or the temporarily inspected event, adds paused hover previews when not
  inspecting, and draws the signed overlay.
- **Count plot**: event bands, and the dashed chance line drawn on the plot's
  draw list.
- **Caching**: event counts are cached by a hash of the inputs; the overlay
  render key hashes the marker fields explicitly. Event types and lowercase
  filter text are cached per event after detection, so filtering and sorting
  do not rebuild strings every frame. While any widget is active (e.g. a
  slider being dragged), changed settings set a pending flag and the previous
  result is kept; detection runs once on release. Detection is synchronous: a
  100k-frame synthetic benchmark takes about 0.1–0.2 s per run in Release, so a
  worker thread is not used.
- **Background sub-events**: `EventOptions::residual_sigma` and `member_z`
  (a callback returning the z-score of any member/frame/parameter, NaN when
  invalid). The per-parent background median is computed lazily per frame and
  needs `background_min_members` (5) valid values.

## 8. Limitations

- Events default to all 24 parameters. Many nucleotide torsions are tested per
  member, so the chance level rises; untick the Nucl. column (or single torsions)
  to focus on base-pair geometry.
- Nucleotides outside detected base pairs (overhangs, loops) are not analysed.
  Nucleotide distortion warnings are heuristic and do not certify a physically
  meaningful transition or automatically exclude finite geometry.
- Groove widths follow 3DNA minus 5.8 Å; they are not Curves+ groove widths and
  are missing for the 2–3 steps nearest each helix end.
- Changing the baseline method/range recomputes statistics synchronously on edit release, which can take a
  moment on very long trajectories.
- The outlier count plot always uses the plotted class and parameter(s). Only the
  events use the Event parameters setting, so event bands can come from parameters
  that are not plotted.
- Value labels are restricted to the selected/previewed parameter. They are
  screen-space annotations, not reconstructed groove surfaces. Scientific and
  GUI validation remain separate.
- Neighbourhood is measured along the helix, not in 3D. Distortions that are
  far apart in sequence but in contact in space, such as RNA tertiary contacts,
  are separate events unless **Whole structure** is used.
- The chance level assumes Gaussian, independent parameters. Real parameters
  are correlated (Roll with Twist, Slide with Twist), so treat it as an order
  of magnitude.
- Event types come from a single dominant parameter. A compensated kink can be
  labelled by whichever of Roll or Twist deviates more.
- The main timeline holds at most 64 periods.
- The hierarchy has two levels. Stronger mode does not separate equal-strength
  local activity; background mode does, but its background is the median of
  the whole parent region, not a sliding local window, and it needs at least
  5 valid members.
- Only six event colours; further simultaneous events are grey.

## 9. Testing

### Local trajectory smoke check (2026-10-25)

The user-supplied `dna.gro` and `box-nvt.xtc` were checked locally without
modifying them: 758 atoms, 191 frames from 0 to 1000 ps, 24 nucleotides,
12 fixed base pairs and 11 steps. Sampling intervals vary from 5 to 10 ps
(median 5 ps).

- All frames passed production frame analysis, statistics and distribution
  construction for all 24 parameters. Every nonempty member histogram
  integrated to one; full-span and detected-event sample/missing counts
  matched their inclusive frame spans.
- Gamma, Delta, Chi, pucker phase and amplitude were finite for all 4584
  nucleotide samples. Alpha, Beta, Epsilon, Zeta and Epsilon − Zeta each had
  4202 finite samples; the missing values correspond to the strand ends.
  Both grooves had seven valid steps per frame; the remaining steps lack
  the required flanking phosphates.
- Mean Twist was 33.06° and mean Rise 3.35 Å. Mean phosphate-corrected
  Minor/Major groove widths were 6.02/13.68 Å. These are descriptive averages,
  not independent accuracy checks.
- Forty of 2292 pair samples were quality-flagged. Pair-dependent values were
  missing there, while nucleotide geometry remained available.
- Before the within-state baseline correction, with all parameters,
  start/continue thresholds 2.5/2.0 sigma, a 10 ps
  minimum duration and no gap bridging, Stronger hierarchy (3.5 sigma)
  returned one full-run parent and 17 children. Background mode returned
  only the parent. Restricting Stronger detection to 250–750 ps returned
  one parent and five children, all within the requested range.

This exercises the numerical pipeline, event contributions and event
distributions, **not the GUI or an independent 3DNA/Curves+ comparison**.
Event type labels are heuristic; the full-run parent also illustrates how
testing every nucleotide parameter can join otherwise separate activity.

After the within-state correction, the local test separately found 172
residence-qualified switches at a 10 ps minimum (Alpha 4, Gamma 0, Chi 81,
Pucker phase 59, Epsilon − Zeta 28). Within-state outlier samples at 2.5 sigma
were 58, 72, 64, 60 and 75 respectively. These switches do not enter the
sigma-event score. Counts depend strongly on state boundaries and residence;
they are not independently validated molecular transition assignments.

The visual-feedback regression run on the same trajectory additionally checked
**1275 native dihedral values**, **4011 native phosphate distances**, and **2674
groove widths** against their Lavery calculations and displayed phosphate-index
definitions across all 191 frames. It can be repeated without copying the data
into the repository:

```
VIAMD_LAVERY_VALIDATION_GRO=/path/to/dna.gro \
VIAMD_LAVERY_VALIDATION_XTC=/path/to/box-nvt.xtc \
./build/bin/viamd_test --filter='lavery*'
```

The optional local-data test is skipped unless both variables are supplied.
It disables trajectory cache writes. These checks establish consistency between
geometry and values; they do not establish independent scientific accuracy or
replace visual inspection of the cartoon.

The nucleotide-quality run found 382 missing samples each for Alpha, Beta,
Epsilon, Zeta and Epsilon - Zeta (strand-end geometry), and none for the other
five metrics. No nonfinite coordinates, degenerate geometry or distortion
warnings were found on this NVT trajectory at the documented thresholds.
All finite values, Curves+ comparisons and state-switch counts remain
unchanged. Synthetic regressions cover missing/nonfinite/degenerate geometry,
zero-amplitude phase, distorted rings, minimum-image flag invariance, CSV
warning provenance and retention of warning-bearing samples in statistics
and state detection.

### NVT sensitivity check (2026-10-25)

The optional local-data test now retains all 24 time series and repeats:

- Mean/SD and Median/MAD baselines, with state-conditioned references;
- pair + step, nucleotide-only, and all-parameter detection scopes;
- start/continue thresholds 2.5/2.0 and 3.5/3.0 sigma;
- 10 and 50 ps minimum event durations;
- Stronger (+1 sigma) and background (1 sigma residual) children;
- 10, 25, 50 and 100 ps minimum state residence.

There are 48 event configurations. They use a two-base-pair neighbourhood,
no gap bridging and pair-loss detection in every scope, as in the GUI.
Histogram normalization and full-span sample/missing counts are checked for
every member and metric under both baselines. Each detected event member's
peak-parameter distribution is also checked against its inclusive span.
Event bounds, child containment and elapsed minimum durations are asserted.
Counts are diagnostic output, not fixed regression expectations for arbitrary
user-supplied trajectories.

Selected **Mean/SD, Stronger** results:

| Scope | Start / continue sigma | Minimum (ps) | Parents | Children | Frames covered by parent spans |
|---|---|---:|---:|---:|---:|
| Pair + step | 2.5 / 2.0 | 10 | 8 | 7 | 190 / 191 |
| Pair + step | 3.5 / 3.0 | 10 | 7 | 1 | 23 / 191 |
| Pair + step | 3.5 / 3.0 | 50 | 0 | 0 | 0 / 191 |
| Nucleotide | 3.5 / 3.0 | 10 | 16 | 3 | 60 / 191 |
| All | 2.5 / 2.0 | 10 | 1 | 21 | 191 / 191 |
| All | 2.5 / 2.0 | 50 | 1 | 2 | 191 / 191 |
| All | 3.5 / 3.0 | 10 | 21 | 4 | 81 / 191 |
| All | 3.5 / 3.0 | 50 | 2 | 0 | 15 / 191 |

Increasing duration alone does not split the whole-run parent at 2.5 sigma.
Increasing the start/continue thresholds makes this dataset more selective,
but is not a statistically calibrated significance test. Median/MAD is not
automatically quieter: all parameters at 3.5/3.0 sigma and 10 ps produce 30
parents and seven Stronger children, covering 159/191 frames.
Background mode finds five children for all parameters at 3.5/3.0 sigma and
10 ps under either baseline; at 2.5/2.0 sigma it finds none. This does not
establish sensitivity to a known physical local event.

| Parameter | Switches at 10 ps | 25 ps | 50 ps | 100 ps |
|---|---:|---:|---:|---:|
| Alpha | 4 | 0 | 0 | 0 |
| Gamma | 0 | 0 | 0 | 0 |
| Chi | 81 | 22 | 8 | 4 |
| Pucker phase | 59 | 14 | 5 | 0 |
| Epsilon - Zeta | 28 | 21 | 8 | 4 |
| **Total** | **172** | **57** | **21** | **8** |

Across all valid samples, broad state occupancies are Chi 3911 anti / 673
syn, pucker 402 North / 4182 South, and Epsilon - Zeta 3387 BI / 815 BII.
These are pooled sampled-frame counts, not time-weighted equilibrium
populations; sampling is irregular and state boundaries are heuristic.

At 50 ps residence, seven of the eight Chi candidates involve terminal pairs
1 or 12. Examples to inspect in the GUI (labels use the fixed pair order):

| Parameter / member | Destination run (ps) | Classified change | Signed angular change |
|---|---|---|---:|
| Chi / 12b | 155-335 | anti to syn | +65.30 degrees |
| Chi / 1a | 265-430 | anti to syn | +80.32 degrees |
| Pucker phase / 3a | 625-685 | South to North | -67.67 degrees |
| Pucker phase / 3a | 705-760 | North to South | +53.49 degrees |
| Epsilon - Zeta / 4a | 420-585 | BI to BII | -138.22 degrees |

The angular change is the shortest circular difference of qualified run means,
not the displacement at a single frame. Destination runs may reach the
trajectory end; their eventual lifetime is then unknown. These candidates
need coordinate inspection and independent state assignment before being
called base flips, pucker transitions or BI/BII transitions.

The sensitivity audit uses Mean/SD, 3.5 sigma and 10 ps for brief outlier-event
screening, with separate pair/step and nucleotide passes, and 25-50 ps
for state-switch inspection. For a less flickery exploratory starting point,
use 25 ps minimum event duration and 50 ps minimum state residence, then compare
50 ps events and 100 ps residence. With 5-10 ps saved-frame spacing, a 10 ps
span can contain only two or three samples. These are sampling-dependent filters,
not universal DNA timescales: state residence, transition duration and waiting
time are distinct. A grouped event can combine different contributors and does
not establish continuous base-pair opening throughout its span. Use the same
thresholds when comparing temperature phases of an annealing trajectory.
**No detection defaults were changed:** one short
trajectory without labelled true/false events cannot establish sensitivity,
specificity or universal thresholds.

Informational local timings were about 0.2-0.3 ms per production frame analysis
(excludes trajectory I/O and native measurement cross-checks), 0.5-2.3 ms to
prepare event samples, and 0.04-5 ms per detection configuration. The
statistics/histogram/z-score phase also executes validation assertions and is
not a clean GUI benchmark. These small-system results do not validate
long-trajectory playback, rendering or memory scaling.

At the time of the initial sensitivity sweep, no independent analysis executable
was available on PATH. The follow-up Curves+ comparison below supersedes that
limitation. No original trajectory was modified or uploaded.

### NVT candidate interpretation audit (2026-10-25)

This analysis uses the existing 191-frame exported measurements and exact XTC
timestamps. Production code, state boundaries, detection defaults and the
running annealing simulation were not changed.

All 21 state-switch candidates at 50 ps residence were compared with 25, 75
and 100 ps residence, and with state sectors rotated by -10, -5, +5 and
+10 degrees at 50 ps residence. Rotation shifts all boundaries of a parameter
together on the circle, including the BI/BII boundary at the angular wrap;
it is an exploratory classification stress test, not a calibrated uncertainty
or a proposed new physical state definition.

A candidate matches another detection when metric, member, source and
destination labels agree, and their destination runs overlap for at least
half the original candidate's elapsed duration. Thus an altered start/end can
match; a different source state or a short overlap does not. The exact match
criterion matters when comparing counts or reproducing this audit.

| Metric | Candidates at 50 ps | Match all four shifted-boundary tests | Original candidates matched at 75 ps | Original candidates matched at 100 ps |
|---|---:|---:|---:|---:|
| Chi | 8 | 2 | 4 | 2 |
| Sugar phase | 5 | 0 | 0 | 0 |
| Epsilon - Zeta | 8 | 8 | 4 | 4 |
| **Total** | **21** | **10** | **8** | **6** |

These last two columns count matches to the original 21 candidates, not all
detections at that residence. There are nine total detections at 75 ps and
eight at 100 ps: changing residence changes which source run qualifies,
so candidate identities are not necessarily nested. Similarly, some 50 ps
candidates do not match the 25 ps list even though their destination run still
qualifies, because an intervening qualified state changes the remembered source.

At 50 ps, shifting the boundaries by -10/-5/+5/+10 degrees gives respectively
18/20/17/20 total detections, matching 13/16/15/15 of the original candidates.
This sensitivity is a reason to review traces, not a reason to tune boundaries
until a preferred candidate count is obtained.

**Critical timing interpretation:** thirteen original candidates have sampled
short excursions between the qualified source and destination runs. The longest
source-end to destination-start separation is 480 ps. The detector deliberately
remembers a qualified source across short finite excursions; missing values
reset it. Its output describes a *change between residence-qualified states*,
not necessarily a direct transition at the destination's first frame.
For example:

- Chi `9b`, 550-600 ps, compares with a source ending at 125 ps; its circular
  mean change is only +30.8 degrees and it is boundary-sensitive. Do not call
  this a base flip at 550 ps.
- Sugar phase `3a`, 625-685 ps, compares with a source ending at 145 ps.
  A more direct local comparison is its 705-760 ps South run against the
  625-685 ps North run, although this return is also boundary-sensitive.
- Epsilon - Zeta `4a`, 420-585 ps, compares with a source ending at 360 ps,
  with intervening excursions. The 870-1000 ps return compares with a source
  ending at 850 ps.

Three destination runs reach the trajectory end (`4a` Epsilon - Zeta,
`3b` Epsilon - Zeta, `3a` sugar phase); their observed residence is a lower
bound, not a completed lifetime.

**Inspection priority, not confirmed molecular assignments:**

1. Epsilon - Zeta `9b` at 115-275 ps: source ends at 110 ps, all boundary
   shifts match, and it survives 100 ps residence. `4a` at 420-585 ps and
   its 870-1000 ps return are also boundary-robust and survive 100 ps;
   inspect the intervening excursions and end censoring.
2. Chi `1a` at 265-430 ps: boundary-robust and survives 100 ps, but terminal.
   Its source/destination circular means are approximately -135.6/-55.3
   degrees. The current broad anti/syn classification is not independent
   evidence of a complete base flip or canonical syn geometry. Chi `12b`
   returning to anti at 340-430 ps is the other boundary-robust Chi candidate.
3. All five sugar-phase candidates and the remaining Chi candidates merit
   trace inspection but are boundary-sensitive under this test. None of the
   original five phase candidates matches the 75 ps list.

Source/destination means of associated pair Opening/Stretch and adjacent
step Roll were also compared, excluding invalid pair/step samples. These are
descriptive differences, not significance tests or proof of coupling.
For example, sugar phase `3a` at 625-685 ps accompanies about +5.16 degrees
mean Opening and at most 6.77 degrees adjacent mean Roll difference, but its
source run is far earlier; this cannot establish synchronous causation.
Chi `1a` at 265-430 ps accompanies only about +0.87 degrees mean Opening.
Pair validity can change independently: Epsilon - Zeta `1a` at 885-935 ps
has pair values available in only 7/11 destination samples, despite finite
nucleotide geometry throughout.

The local `validation-nvt/candidate-audit.csv` preserves all 21 candidates,
source/destination bounds and circular means, angular boundary margins,
fractions within 10 degrees of a boundary, intervening sample counts,
residence/boundary matches, pair coverage, associated parameter differences
and end censoring. Fractions are sampled-frame fractions, not time-weighted
occupancies. A candidate marked boundary-robust is still unconfirmed:
coordinate-based inspection and physical state assignment remain outstanding.

### Protocol prepared for the annealing trajectory

No annealing files were analysed while the simulation was running. Once it is
finished, verify the *effective* run input, thermostat groups, target schedule,
time origin, restart history and available coordinate/energy timestamps before
assigning temperature phases. The planned periodic cycle, relative to its
verified schedule origin, is:

| Cycle-relative time (ps) | Target-temperature phase |
|---|---|
| 0-500 | 300 K plateau |
| 500-1000 | Linear heating, 300 to 350 K |
| 1000-1500 | 350 K plateau |
| 1500-2000 | Linear cooling, 350 to 300 K |
| 2000-2500 | 300 K recovery plateau |

Use half-open windows so boundary samples belong to exactly one phase; the
final available sample is retained in its applicable window. Keep cycles
separate initially, including incomplete final cycles. Recovery and the next
cycle's initial plateau share the same target but should retain their labels
for the first-pass comparison.

1. Verify topology, fixed pair/member mapping, sampling intervals and quality
   coverage. Keep invalid measurements distinct from finite warnings and from
   pairing loss. Flag sparse windows and missing intervals.
2. Report state occupancies per member and phase using elapsed-time weighting
   for irregular sampling, with an explicit interpolation convention (e.g.
   nearest-sample midpoint cells clipped to each window). Do not extend weights
   outside the observed time range or bridge long missing intervals. Report
   coverage and both episode and contributing-sample counts alongside durations.
3. Start with one whole-run baseline so a common z scale can show
   temperature-associated broadening. Compare raw angular/linear values and
   state occupancies as well: sigma alone cannot distinguish heating from a
   local conformational change.
4. Repeat with plateau-specific references using **Baseline range**, and
   restrict each search using the independent **Detection range**. A common
   reference and per-plateau references answer different questions; report both.
   Heating/cooling ramps
   are nonstationary and should not be pooled with equilibrium plateaus.
   Neither a 500 ps plateau nor several cycles automatically guarantees
   equilibrium or independent observations.
5. Compare 25/50/100 ps residence, modest boundary perturbations, terminal
   versus internal members, and direct versus excursion-separated qualifying
   runs. Mark trajectory/window-edge censoring; do not infer a switch source
   across a window boundary from within-window detection alone.
6. Test recovery in raw values, occupancy and pair quality against the initial
   300 K segment and the NVT reference, without assuming that recovery must
   occur. Keep cycle-to-cycle variability visible rather than treating every
   frame as an independent replicate.
7. Distinguish scheduled target temperature from measured kinetic temperature.
   If an energy trajectory can be read locally, align actual temperature with
   coordinates; otherwise label all phase comparisons as target-schedule-based.

This protocol does not add temperature-aware statistics to the application,
change detection defaults, or certify any NVT/annealing event as a transition.

### Independent Curves+ audit (2026-10-25)

Official Curves+ v3.0nc (09/2016) was downloaded from the
[Curves+ distribution site](https://curvesplus.bsc.es/misc) and compiled locally
with gfortran, outside the repository at `$HOME/.local/opt/curvesplus/Cur+`.
The source archive is
`https://curvesplus.bsc.es/static/app/curves%2B_v3.0nc.tar.gz`.
Analysis used `fit=.t.`, `refo=.f.`, the supplied `standard` library, and explicit
fixed pairing (strand 1 residues 1:12, strand 2 residues 24:13).
3DNA was not installed, per the user's decision to use Curves+ only.

**Correction to the first comparison:** raw XTC coordinates were initially
exported directly to PDB. Curves+ does not reconstruct their periodic images:
some bases/sugars were split across the box, and inter-base translations could
span an entire box length. Those outputs were not a valid comparison and must
not be interpreted as errors in VIAMD's pair/step calculations.

The corrected exporter makes each bonded strand whole, then translates the
entire second strand into the first pair's nearest image. Only the temporary
snapshot copy is changed. Each frame is reanalysed without a unit cell, asserting
the same finite/missing values and agreement within 0.003 in the metric's units
with the original periodic calculation for all 24 metrics. The 191 whole-duplex
PDB snapshots are then analysed independently by Curves+.

| Compared quantity | Valid comparisons | RMS difference | Maximum absolute difference |
|---|---:|---|---|
| Pair rotations (each of three metrics) | 2252 each | 0.0287-0.0289 degrees | 0.0501 degrees |
| Pair translations (each of three metrics) | 2252 each | 0.0029-0.0030 Angstrom | 0.0058 Angstrom |
| Step rotations (each of three metrics) | 2053 each | 0.0288-0.0291 degrees | 0.0501 degrees |
| Step translations (each of three metrics) | 2053 each | 0.0029 Angstrom | 0.0051 Angstrom |
| Alpha, Beta, Epsilon, Zeta | 4202 each | 0.0287-0.0293 degrees | 0.0501 degrees |
| Gamma, Delta, Chi | 4584 each | 0.0290-0.0292 degrees | 0.0501 degrees |
| Epsilon - Zeta (derived from printed torsions) | 4202 | 0.0403 degrees | 0.0980 degrees |

Curves+ prints angles to 0.1 degree and translations to 0.01 Angstrom. These
differences are consistent with that precision and small numerical differences.
VIAMD's quality-rejected fixed pairs/adjacent steps are excluded rather than
forcing a comparison with a Curves+ value. Angular differences are wrapped,
so equivalent values near -180/180 are not counted as large errors.

**Pucker is a convention comparison, not a discovered calculation bug.**
VIAMD uses the Altona-Sundaralingam formula; Curves+ uses the first Fourier
component of the five ring torsions:

```
a = (2/5) sum_j nu_j cos(144 degrees * j)
b = -(2/5) sum_j nu_j sin(144 degrees * j)
phase = atan2(b, a)
amplitude = hypot(a, b)
```

Here `nu_0` is C1'-C2'-C3'-C4', followed cyclically around
C1'-C2'-C3'-C4'-O4'. Curves+ prints signed phase; comparison wraps it.
On 4584 sugars, the existing Altona-Sundaralingam phase differs from Curves+
by 0.857 degrees RMS (3.631 maximum), amplitude by 0.961 degrees RMS
(3.680 maximum). Evaluating the Fourier formula on the same minimum-image
ring torsions reproduces both Curves+ quantities within 0.051 degrees.
The production formula and detection defaults remain unchanged.

**Regression checks.** The optional local test accepts
`VIAMD_LAVERY_VALIDATION_CURVES=/path/to/reference-directory`, containing
`curves.csv` with the header `frame,class,member,metric,value`, zero-based
VIAMD frame/class/member/metric IDs and one row per non-groove metric/sample.
Missing values are `nan`. It validates dimensions, duplicate rows and schema,
then checks every finite production value against its reference (Fourier
convention for pucker). Tolerances are 0.061 degrees, 0.0061 Angstrom and
0.11 degrees for the difference of the two rounded Epsilon/Zeta torsions.
It reports comparison counts and maximum errors and fails on disagreement.

```
VIAMD_LAVERY_VALIDATION_GRO=/path/to/dna.gro \
VIAMD_LAVERY_VALIDATION_XTC=/path/to/box-nvt.xtc \
VIAMD_LAVERY_VALIDATION_CURVES=/path/to/reference-directory \
./build/bin/viamd_test --filter='lavery*'
```

Set `VIAMD_LAVERY_VALIDATION_EXPORT` to an existing directory to regenerate the
whole-duplex `frame_N.pdb` snapshots, `pairing.txt` and `viamd.csv` for this
single-duplex workflow. Curves+ input for snapshot N is:

```
&inp file=frame_N.pdb, lis=whole_N, fit=.t., refo=.f.,
lib=/path/to/curvesplus/standard, &end
2 1 -1 0 0
1:12
24:13
```

The local reference directory is
`$HOME/.local/opt/curvesplus/validation-nvt`; its `whole_N.lis` files preserve
the raw independent outputs, `curves.csv` their mapped values, and
`comparison.txt` the per-metric error summary. No user coordinates are stored
in the repository. The public 1RNA fixture also has a permanent 28-sugar
Fourier reference, generated by the same executable and tested without local
DNA files or Curves+ being installed.

Curves+ curvilinear grooves are **not** the direct phosphate-distance quantity
implemented here, so equality is neither asserted nor claimed. Agreement of
the other metrics does not validate event labels, residence thresholds or
false-positive rates.

For the rendering changes, a local surfaceless OpenGL check using the actual
immediate renderer verified that hidden hints are suppressed in front of an
opaque depth plane, drawn behind it, and not replayed after queue submission,
with no GL errors. Full cartoon appearance still requires GUI inspection.

```
cd build
make -j16 viamd_test
./bin/viamd_test --filter='lavery*'
```

The core tests cover:

- gap bridging and the minimum-frames filter;
- neighbourhood splitting, separate duplexes and Whole structure;
- hysteresis (weak samples extend but never start an event);
- lost pairs, the score and event types;
- the Gaussian chance probability;
- event members and parameter peaks;
- exact per-frame event contributions for playback, including parameter changes,
  pair loss, bridged gaps and isolation from overlapping events;
- automatic contributions from all active events, all three classes and merged
  parameters for overlapping members;
- stable Viridis event colour slots, chronological cycling, inclusive overlaps, safe reuse, unsorted input,
  more than eight simultaneous events and score priority;
- background sub-events: local activity inside an equally strong broad event,
  uniform shifts, too few members, invalid thresholds;
- a 100k-frame synthetic benchmark (prints timings, asserts results);
- hierarchical local bursts inside broad events, flat-mode equivalence, parent
  ID remapping, duplicate/short/weak rejection, Whole structure parents,
  mixed pair/step contributions and pair loss;
- minimum three contributing frames (not elapsed span), single-frame opt-in,
  and isolation of paused inspection from overlapping parent/child events;
- angular wrapping across ±180°;
- class layout, metric names/paths, torsion sign and minimum image,
  pseudorotation (North and South), missing backbone atoms, 3DNA groove
  indices and helix chains;
- within-state baselines (including robust/circular and constant minority
  states), source/destination residence filtering, spikes, missing data, range
  boundaries, irregular timestamps and state-switch CSV provenance;
- measurement-span indices and boundary rejection, defining-atom identity,
  exact step midframes, native dihedral/value agreement and degree units for
  the linearly treated pucker amplitude;
- Median/MAD robustness;
- independent baseline/detection intervals, full-range equivalence, circular
  and robust references, absent-state sigma coverage and invalid/empty intervals;
- native timeline periods as reference/search windows, reversed/clipped bounds,
  following windows, markers, edits and missing IDs;
- the events CSV and the baseline method/interval columns in parameter CSV.
