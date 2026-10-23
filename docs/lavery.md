# Lavery parameters: reference

This page describes the Lavery component in
`src/components/lavery/`. For the short version, see
[lavery-quickstart.md](./lavery-quickstart.md).

## 1. Concepts

| Class | Members | Parameters (unit) |
|---|---|---|
| Intra-base pair | each detected base pair | Shear, Stretch, Stagger (Å); Buckle, Propeller, Opening (°) |
| Inter-base pair (step) | each pair of consecutive base pairs | Shift, Slide, Rise (Å); Tilt, Roll, Twist (°) |

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

Changing the baseline recomputes the statistics immediately; the parameters are
not recalculated.

## 2. Window layout

The Lavery workspace is one resizable, non-dockable window with fixed child
sections. No dock tabs or separate panel title bars consume space; each section
scrolls independently:

```
+------------+------------------------------------------+
|            |  Events & quality                        |
|  Settings  |  (events table, pairing quality)         |
|  (sidebar) +---------------------+--------------------+
|            |  Overview           |  Inspector         |
|            |  (count + heatmap)  |  (timeline + dist) |
+------------+---------------------+--------------------+
```

- **Settings** (left sidebar): every control, in sections:
  *Selection* (class, parameter, member, aggregate parameters), *Outliers*
  (sigma, baseline), *Events* (Detect on, neighbourhood or whole structure, bridge
  gaps, minimum frames, hysteresis, pair loss), *Display* (Highlight event contributions,
  Signed overlay, Halo, Axis) and *Export & tools* (parameter CSV, **Export
  events CSV**, Recompute analysis).
- **Events & quality** (top): the outlier events table (described below) and
  per-member pairing quality and coverage.
- **Overview** (bottom left): the outlier count per frame, with events shaded
  as bands (matching the event glow colours) and a dashed line
  at the count expected by chance (see [Chance level](#chance-level)).
  Previous/Next outlier buttons step between outlying frames, and hovering
  lists the events at that frame. Below it, the heatmap shows every member
  against frame, with outliers marked.
- **Inspector** (bottom right): the timeline and distribution of the selected
  member and parameter, with values, baseline (mean/SD or median/scaled MAD),
  z-scores and quality flags, followed by the current-frame list (outliers at
  the displayed frame; hovering a row previews it in the viewport). **Both plots refit their axes automatically**
  whenever the series changes: a new parameter, member, baseline, compare-all
  toggle or pinned member (for example after clicking an event parameter).
  After that you can zoom freely until the next change.

## 3. Outlier events

### Detection

Events are built from **samples**: one sample is one member (base pair or
step) in one frame. A sample takes part when one of the following holds:

- it is *weak*: at least one parameter in **Detect on** has
  |z| ≥ tolerance − **Hysteresis** (the continue threshold, never below 0.5);
- it is *strong*: at least one parameter has |z| ≥ tolerance (the start
  threshold);
- it is *lost*: with **Pair loss** on, a base pair that has a baseline loses
  its pairing in this frame. Its quality flags show separation, alignment,
  out-of-plane offset or missing H-bonds, and nothing else. Lost samples count
  as strong even when no intra-base-pair parameter is selected.

**Detect on** selects the parameters, independently of the class shown in the
plots. Presets: **Intra** (6 base-pair parameters), **Inter** (6 step
parameters), **Both** (all 12, default). The 2x6 grid (Intra column, Inter
column) picks any subset; a column header toggles the whole class and shows a
mixed state when only part of it is selected. **Use plotted parameters** copies
the class and parameter(s) of the count plot. With nothing selected, only pair
losses (if on) form events. The CSV `scope` column is `all`, `intra`, `inter`,
or the chosen names joined by `+` (for example `Roll+Twist`).

Two samples are **connected** when both of these hold:

1. **Time**: they are at most **Bridge gaps** + 1 frames apart. With Bridge
   gaps = 0, only samples in the same or consecutive frames connect.
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
hysteresis: a weak sample can extend an event but never start one. Finally,
**Min frames = M** drops events with fewer than M distinct outlying frames;
bridged frames do not count. The GUI default is **3 contributing frames** for
both parents and children; set 1 to retain single-frame spikes. Saved workspaces
keep their explicit minimum. The right minimum depends on trajectory sampling;
three frames is a noise-filtering starting point, not a physical lifetime criterion.

The table's **Frames / span** column shows contributing frame count separately
from elapsed time (when time units are available), or the inclusive frame span.
One sample is **1 contributing frame**, not "0 frames"; its elapsed time is
still zero. Sorting this column uses contributing frame count.

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
2. The higher-threshold samples are clustered with the same **Min frames**,
   **Bridge gaps** and local **Neighbourhood**. Even with **Whole structure**
   enabled for parents, children use the local neighbourhood and never join
   separate duplexes.
3. Keep a candidate only when every one of its member/frame/parameter
   contributions belongs to one parent, its time range lies inside that
   parent's, and it touches strictly fewer distinct base pairs. A step touches
   both endpoint pairs. An unchanged broad component is not duplicated.
4. Retain the parent unchanged and link the child to it. This is a two-level
   intensity hierarchy, not recursive segmentation or subtraction of a
   per-event background.

Example: a 100-frame event spanning the DNA can retain a 5-frame opening burst
on a few base pairs as a separate child. At tolerance 2.5 sigma and Burst extra
1, a child needs a 3.5-sigma sample (or pair loss) to start and continues down
to 3.0 sigma with the default hysteresis. A merely different but not stronger
local distortion is not automatically split out. If the broad background still
connects everything at the child threshold, increase Burst extra.

The table reports parent and burst counts separately. Children have their own
chronological IDs and show **child of #N**, members, scores, peaks and colour
swatches. Hover text and main-timeline notes include the parent ID and burst
threshold. A child gets a different colour from its active parent. At shared
bases, the child's colour receives three times the blend weight of the parent
so the local patch remains visible inside a broad glow, without adding opacity.

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
that probability) as a dashed line. The header compares observed and
expected totals over the trajectory. For example, at 2.5 σ, p ≈ 1.2%. With
24 base pairs each tested on all six intra parameters (aggregate mode), that
is about 1.7 chance outliers per frame.

When the curve stays near the dashed line, the hits are mostly noise.
Rank events by **Score** rather than by count, and raise the tolerance or
**Min frames**. With the Median/MAD baseline, the Gaussian figure is
approximate.

### Members and parameters

Each event lists the members involved, sorted by peak |z|, with a
`[intra]` or `[inter]` tag. Each member shows:

- the number of outlying frames inside the event, and how many of them the pair
  was lost;
- the peak frame and its z-score, and the parameter that produced the peak;
- one coloured button per outlying parameter, labelled with its name and peak z.
  Crimson means positive, cyan negative. A grey **Pair lost** button appears
  when the pair broke.

Members are sorted by peak |z|. Members that were only lost come last.

### Interaction

| Action | Result |
|---|---|
| Click a column header | Sort by that column (default: Score, descending) |
| Click an event row | Jump to its peak, stop playback and inspect only this event |
| Expand an event (arrow) | Show its members |
| Click a member | Select its class, the member, its strongest parameter and the peak frame; playback stops |
| Click a parameter button | Select that parameter at its own peak frame |
| Hover an event, member or button | Add a viewport preview while paused; tooltips always give details |
| **Select** | Replace the VIAMD selection with the nucleotides of every member of the event |
| **Timeline** | Add the event to the main timeline as a coloured period with a note (region, frames, peak, score, members) |
| **All to timeline** | Add every event in table order, until the timeline's limit of 64 periods |
| **Remove from timeline** | Remove the periods added from Lavery events |

The event that contains the current frame is highlighted. Event numbers (#N)
are chronological and match the `event` column of the events CSV, whatever the
table order.

**Temporary paused inspection.** Clicking an event, its member or a parameter
shows only that event's glow and current-frame contributions, excluding
overlapping events (including its parent or children). Only that event's table
row is selected. This remains until playback resumes or the frame changes,
then automatic all-active-event feedback returns. Detection-setting changes
or recomputation also clear inspection; changing the plotted class alone does
not. Hover previews cannot add other events during inspection. No persistent
focus mode, pin controls, or viewport status box is used.

Timeline periods are ordinary VIAMD periods: they can be renamed, recoloured
and used to restrict distributions, and they are saved with the workspace.
Lavery only remembers which ones it added for the current session, so
**Remove from timeline** does not remove periods loaded from a workspace.

## 4. Viewport feedback

| Glyph | Meaning |
|---|---|
| Rings on both bases | Intra-base-pair outlier |
| Bar (cylinder with end spheres) between two pair midpoints | Inter-base-pair (step) outlier |
| Crimson / cyan | Positive / negative deviation of the most severe contributing parameter |
| Grey | Member being previewed that is not outlying at this frame |
| Halo (optional) | Soft transparent glow that makes markers easier to spot |
| Broad coloured halo | Active events' overall regions; colour matches the event-table swatch, not deviation sign |
| Amber rings | An active event's lost base pair |

Which markers are shown:

- With **Highlight event contributions** on (default), all active events are
  shown automatically, while playing or paused. An event is active inside its
  inclusive frame range. No event selection or focus is required.
- Clicking an event while paused temporarily replaces this with that event
  alone, even when automatic highlighting is off. Play or change frame to
  restore automatic feedback. Hover tooltips follow the same inspection filter.
- Each active event's overall member region gets a broad coloured halo. Events
  overlapping in time (including a shared endpoint frame) receive different
  colour slots, allocated chronologically and reused only after an event ends.
  Eight contrasting colours are followed by generated hues for larger overlaps;
  very large numbers of simultaneous events may still be hard to distinguish.
  Colours remain stable during playback and table sorting; changing detection
  settings may reassign them. Table swatches, count-plot bands and newly added
  main-timeline periods use the same colours. Existing timeline periods retain
  their saved or user-edited colour.
  Shared bases use the average of involved event colours at the same opacity,
  rather than stacking brighter glows. Hover still identifies every involved event.
  Precise rings and bars show only exact current-frame contributions from all
  active events, across both classes. Shared members are drawn once, with the
  union of contributing parameters.
- Contributions include the lower hysteresis threshold. Bridged gaps retain
  context but do not invent detail markers. Pair-loss rings remain visible even
  when pair geometry is invalid.
- While paused, hovering event/member/parameter rows or the current-frame list
  adds a preview without hiding active-event contributions. Playback ignores
  these previews so leaving the mouse on a row does not clutter the animation.

There is no permanent viewport label or status box. Hovering the halo or an
involved atom shows event IDs, types, frame ranges and whether the hovered
member is contributing now or is context only. Hovering a detailed marker lists
each involved event and its current parameter names, with values and z-scores
below. Current parameters are marked **EVENT CONTRIBUTOR**. This works when
several events overlap; none needs to be pinned.

The broad halo and detail markers obey **Signed outlier overlay**; the existing
**Soft overlay halos** toggle controls only the small marker halos.
The **Detect on** mask, not the plotted class, determines automatic event
feedback. Changing the plotted class, parameter or member does not hide the
other active events.

Step bars are computed in periodic boxes using minimum-image displacement
between the two pair midpoints.

**Hover tooltip.** Hovering a ring or bar shows a tooltip containing:

- the member label;
- the frame, the sigma threshold and the baseline method;
- all six parameters with value, unit and z, with contributing parameters
  flagged OUTLIER and the peak marked "(peak)";
- any quality flags.

Hovering an atom of a highlighted base adds the same section to the normal atom
tooltip.

**Click.** Clicking a ring or bar selects that member and its most extreme
parameter, so every plot follows.

The markers use the application picking system. The module reserves one picking
index per pair and per step through `EventType_ViamdPickingRangeReserve`.

There are no permanent 3D text labels. Hover tooltips provide details without
stacking labels over the structure.

## 5. CSV exports

### Parameter CSV (Export CSV)

One row per frame, member and parameter of the current class:

```
run,frame,time,time_unit,class,member,label,pair_a,pair_b,parameter,unit,value,valid,
quality_flags,quality_reason,mean,sd,zscore,sigma,parameter_selected,parameter_outlier,
member_outlier,selected_mask,baseline
```

The `baseline` column is `mean_sd` or `median_mad`. The `mean` and `sd` columns
contain that baseline's center and scale.

### Events CSV (Export events CSV)

One row per event, member and parameter:

```
run,event,begin_frame,end_frame,begin_time,end_time,time_unit,event_peak_frame,
event_peak_sigma,event_score,event_type,first_pair,last_pair,scope,baseline,sigma,
end_sigma,neighbourhood,class,member,label,member_outlier_frames,member_lost_frames,
parameter,unit,contributing,parameter_peak_frame,parameter_peak_zscore,parameter_peak_value,
parent_event,burst_sigma
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
| burst_sigma | Child start threshold, or 0 for a top-level event |

The events CSV has 31 columns. `sigma` and `end_sigma` describe that row's
event (the higher thresholds for children), and child `neighbourhood` is
always local, even under a Whole structure parent. Lost-pair score weight
remains the base tolerance for both levels.

`contributing` is 1 when the parameter was an outlier at some frame of the
event. All six parameters of each member are written. Non-contributing
parameters are reported at the member's peak frame, so their values remain
available for context. A member that was only lost has `nan` values.

Fields are RFC 4180-quoted when needed. Write failures are reported in the
application log.

## 6. Workspace keys (`[Lavery]` section)

| Key | Type | Default |
|---|---|---|
| ShowWindow, ShowHeatmap, SyncViewport, CompareAll, ShowAxis, SignedOverlay, OverlayHalo, AggregateOutliers | bool | - |
| SelectedParameters | bit mask | - |
| OutlierSigma | float | - |
| Metric, Member | int | - |
| Baseline | 0 = Mean/SD, 1 = Median/MAD | 0 |
| EventParameters | int (12-bit mask, bit k = parameter k: Buckle..Stagger, Tilt..Rise) | 4095 |
| EventMinFrames | int, 1-100000 | 3 |
| EventMaxGap | int, 0-100000 | 0 |
| EventNeighbourhood | int (bp), 0-1000 | 2 |
| EventWholeStructure | bool | 0 |
| EventHysteresis | float (sigma), 0-2 | 0.5 |
| EventPairLoss | bool | 1 |
| EventHierarchy | bool | 1 |
| EventBurstExtra | float (sigma), 0.25-4 | 1 |

If a workspace has no new keys, it loads with these defaults.

## 7. Implementation notes

`lavery_core.h/.cpp` contains the parts that do not depend on the UI:

- `compute_statistics(out, values, count, angular, Baseline)`;
- `MemberStatistics::deviation`, `zscore` and `is_outlier`;
- `detect_events(frames, pair_count, steps, EventOptions)`. This clusters samples
  with union-find. Each sample is connected to the most recent sample of every
  member within reach, which is enough because samples of one member are
  chained. Reach is precomputed by a breadth-first search over the
  pair–step–pair graph, two edges per base pair;
- `event_kind`, `event_dominant_metric` and `chance_outlier_probability`;
- `csv_header`/`csv_sample` and `csv_event_header`/`csv_event_row`.

`lavery.cpp` contains the UI. It handles the following:

- **Picking**: reserving the picking range, filling tooltips and handling
  clicks.
- **Events and markers**: `update_counts` builds the samples, using the start
  threshold, the continue threshold and pair loss. It also draws the events
  table (sorting, Select and Timeline), builds markers from all active events
  or the temporarily inspected event, adds paused hover previews when not
  inspecting, and draws the signed overlay.
- **Count plot**: event bands, and the dashed chance line drawn on the plot's
  draw list.
- **Caching**: event counts are cached by a hash of the inputs; the overlay
  render key hashes the marker fields explicitly.

## 8. Limitations

- Changing the baseline recomputes statistics synchronously, which can take a
  moment on very long trajectories.
- The outlier count plot always uses the plotted class and parameter(s). Only the
  events use the Detect on setting, so event bands can come from parameters
  that are not plotted.
- There are no 3D text labels; the parameter names appear in tooltips only.
- Neighbourhood is measured along the helix, not in 3D. Distortions that are
  far apart in sequence but in contact in space, such as RNA tertiary contacts,
  are separate events unless **Whole structure** is used.
- The chance level assumes Gaussian, independent parameters. Real parameters
  are correlated (Roll with Twist, Slide with Twist), so treat it as an order
  of magnitude.
- Event types come from a single dominant parameter. A compensated kink can be
  labelled by whichever of Roll or Twist deviates more.
- The main timeline holds at most 64 periods.
- The hierarchy has two levels and uses a higher absolute z-score threshold,
  not a local residual baseline. It does not separate equal-strength local
  activity from a background that remains connected at the child threshold.

## 9. Testing

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
- automatic contributions from all active events, both classes and merged
  parameters for overlapping members;
- stable event colour slots, inclusive overlaps, safe reuse, unsorted input
  and more than eight simultaneous events;
- hierarchical local bursts inside broad events, flat-mode equivalence, parent
  ID remapping, duplicate/short/weak rejection, Whole structure parents,
  mixed pair/step contributions and pair loss;
- minimum three contributing frames (not elapsed span), single-frame opt-in,
  and isolation of paused inspection from overlapping parent/child events;
- angular wrapping across ±180°;
- Median/MAD robustness;
- the events CSV, and the `baseline` column in the parameter CSV.
