# Lavery scientific validation record

Historical measurements, sensitivity checks and independent comparisons.
These results describe the recorded datasets and settings, not universal
event significance or a current GUI acceptance checklist.

See the [current reference](./lavery.md#9-testing) for test commands and
[future improvements](./lavery.md#future-improvements) for remaining work.

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
with no GL errors. This is a historical renderer check, not a current GUI
acceptance checklist.

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
