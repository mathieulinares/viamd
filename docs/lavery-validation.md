# Lavery scientific validation

This record summarizes independent comparisons and trajectory checks. Results
apply to the tested structures and settings, not all conformations or the
physical significance of detected events. See the [Lavery reference](lavery.md)
for definitions, [tests](lavery.md#9-testing) and
[future improvements](lavery.md#future-improvements).

## Current conventions

The default Legacy mixed mode uses Curves+-style pair and step parameters and
3DNA-style helical parameters and refined groove widths. The Lavery sidebar
offers Legacy mixed, Curves+ and 3DNA modes. Nucleotide torsions are
atom-defined; sugar pucker uses Altona-Sundaralingam pseudorotation.
Groove widths subtract 5.8 Å for phosphate diameter. Where the required
flanking phosphate for refinement is missing, VIAMD falls back to a direct
phosphate distance; this is not compared with a missing refined reference.

Pair identities and pairing types are fixed at the start of the trajectory.
Curves+-style pair and step signs follow the per-frame direction of the next
pair; 3DNA-style helical frames align base normals per frame. External tools
can detect or orient pairs differently, especially in
distorted or noncanonical structures. Comparisons below use matched pair order and
exclude missing or quality-rejected values; they do not count as agreement.
Temporary snapshots are made whole across periodic boundaries before external
analysis, and the exported geometry is checked against the periodic calculation.
That check exposed a periodic groove-width bug during the full 1BNA run,
which was subsequently fixed as described below.

The application defaults to Legacy mixed; users can switch to Curves+ or 3DNA
using the Analysis convention selector. The worker dispatches each mode
separately. The Curves+ path stores pair-level axis, inter-axis step
and half-level groove series separately instead of coercing their different
shapes into 3DNA step fields. Event controls and jobs omit metrics unavailable
in the active internal convention while preserving saved selections for other
modes. Generic measurement exports also ignore unavailable metric bits in
selection counts, reference statistics and CSV rows. Curves+ axis and groove
outputs are available in the profile, attributes and dedicated CSV. The profile
supports per-helix means and population SD over the whole trajectory or a
selected period, alongside the current-frame series. It can import the dedicated Curves+
geometry CSV, validate its schema and records, and overlay reference means
matched by helix, exact member label, quantity and groove half-level. Angular
means are circular; absent or unmatched values remain missing. This is separate
from the generic parameter-reference system. Curves+ geometry also has native
per-feature event detection, a dedicated table and CSV export. It uses
quality-valid series, mean/SD baselines, the Detection thresholds/range and
missing-sample breaks. The baseline can use the selected current-trajectory
interval or the imported Curves+ geometry reference; exact unmatched features
are ignored. Geometry events remain separate from metric events. The selector makes these
separate output shapes and their topology limitations explicit.

The classic 3DNA standard base geometry data used by VIAMD is distributed with
its [license](../src/components/lavery/3dna-license.txt).

## Current combined-pipeline results

The production calculation was compared with Curves+ v3.0nc and classic 3DNA
v2.4 on 328 sampled frame instances from eight trajectory sets, rerun after
the periodic groove and Curves+ orientation fixes. These include
319 regularly spaced samples and nine targeted Seq174 base-flip snapshots;
some overlap, so they are not 328 independent frames. The optional exporter
applies the application's first-frame pair typing and orientation rules.

| Sample set | Frame instances |
|---|---:|
| 1BNA annealing | 41 |
| Seq152 | 42 |
| Seq174 overview / targeted base flip | 40 / 9 |
| Nucleosome | 41 |
| Minor-groove complex | 11 |
| IRF11-DNA, two helices | 51 |
| Proflavine MD / pulling | 51 / 42 |

| Quantity | Comparable | Outside tolerance | Tolerance |
|---|---:|---:|---|
| Curves+ pair and step | 120,882 | 41 | 0.061° / 0.0061 Å |
| Curves+ nucleotide torsions, including Epsilon − Zeta | 166,747 | 0 | 0.061°, or 0.11° for Epsilon − Zeta |
| 3DNA helical | 58,998 | 1 | 0.0052° / Å |
| 3DNA refined grooves | 15,992 | 0 | 0.051 Å |
| 3DNA nucleotide torsions, including Epsilon − Zeta | 167,359 | 0 | 0.061°, or 0.11° for Epsilon − Zeta |
| 3DNA pucker phase and amplitude | 42,596 | 0 | 0.061° |

Reference rounding contributes to these tolerances. The single 3DNA helical
exception is a nucleosome Tip difference of 0.005263° against two-decimal
output. On the same exported snapshot, a diagnostic build printing eight
decimals gives −13.87509390°, versus VIAMD's −13.8747368° (difference 0.000357°).
A precision problem at small
helical inclination/tip angles was found during the audit and fixed with a
small-angle regression test. The table reports the corrected calculation.

Among the Curves+ pair/step comparisons, the nucleosome contributed 38
exceptions: 17 around one near-singular fit,
18 around a second fit and three small residuals. A diagnostic Curves+ build
with a lower fit cutoff eliminated the large discrepancies at both sites;
it is not the official reference. The minor-groove complex contributed two
small residuals and IRF11 one. Previously 74 sign differences across the
sampled Seq174, minor-groove and IRF11 comparisons arose from Curves+'s
per-frame inversion rule. Applying that rule eliminates those exceptions in
these comparisons; the remaining exceptions are retained, not normalized away.

Coverage is important: for Curves+ pair/step, 4,632 values had a reference
but no VIAMD value. For 3DNA helical, 2,622 did. In both cases these are
quality-rejected VIAMD geometries. For 3DNA grooves, 1,669 values were
present in VIAMD but missing in the refined reference, and 2,879 were
missing in both. The 15,992 finite groove comparisons include 408 values
whose associated step had a quality flag: groove calculation uses phosphates
independently of pair-frame quality. For the eight torsion metrics, 3,025
values were missing in both tools; Curves+ also omitted 612 finite VIAMD
values. Missing counts are per metric, not per frame.

VIAMD and Curves+ use different pucker formulas. Across 21,298 finite sugars,
their direct phase/amplitude differences were 0.769°/0.994° RMS, respectively;
these are convention differences, not failures of an identical definition.
The samples do not establish universal agreement.

An internal Curves+ nucleotide calculation uses Fourier pucker, restricts
neighbouring atoms to the selected strand and omits torsions when a defining
bond exceeds 2.5 Å. The convention-dispatched worker uses it in Curves+ mode;
the default mixed pipeline and 3DNA mode retain the Altona-Sundaralingam
pucker and keep unusual finite torsions as warnings. Curves+ reports zero
phase for exactly zero pucker amplitude; the other paths mark near-zero phase
unavailable. Fourier pucker matches the independent 1RNA
fixture for all 28 sugars within 0.06° for phase (modulo 360°) and amplitude.

On the 328 sampled frame instances above, including the two-helix IRF11
sample, the internal calculation was compared with the Curves+ nucleotide
output using matching selected helix members:

| Sample | Comparable values | Outside tolerance | Missing in both |
|---|---:|---:|---:|
| 1BNA | 9,430 | 0 | 410 |
| Seq152 | 16,380 | 0 | 420 |
| Seq174 | 15,600 | 0 | 400 |
| Seq174 targeted base flips | 3,339 | 0 | 81 |
| Nucleosome | 120,130 | 0 | 410 |
| Minor-groove complex | 2,530 | 0 | 110 |
| IRF11, two helices | 19,482 | 0 | 918 |
| Proflavine MD | 12,750 | 0 | 510 |
| Proflavine pulling | 9,702 | 0 | 378 |
| **Total** | **209,343** | **0** | **3,637** |

These counts cover all ten nucleotide values, including phase, amplitude
and epsilon minus zeta, with 0.061° tolerance or 0.11° for epsilon minus
zeta. There were no one-sided missing values. The maximum difference was
0.0992° for epsilon minus zeta. These comparisons use topology-inferred
backbone connections and do not establish equivalence for all broken,
circular, missing-atom or unusual structures.

### Internal 3DNA pair/step path

The new internal pair/step path fits 3DNA base frames independently of the
Curves+ frame-quality result. It retains VIAMD's fixed pairs and contact and
geometry quality checks. It is wired to the production worker and selectable
as the 3DNA mode.
All 1RNA fixture pair and step values agree with classic 3DNA to 0.0055° or Å.
One-base periodic wrapping and disrupted or degenerate pairs are also tested.
An internal frame path now combines these pair/step values with helical
parameters derived from the same 3DNA pair frames, strict refined grooves
and the existing nucleotide calculation. It rejects parallel, circular and
branching step topology rather than presenting it as validated 3DNA geometry.

On the stratified DNA samples below, the internal path was compared with
classic 3DNA v2.4 using the matching fixed pair order and previously exported
whole-molecule PDB snapshots. The reference reports two decimal places. Counts
are per parameter value; 0.0052° or Å is the rounding-aware tolerance.

| Sample | Frames | Comparable | Outside tolerance | Reference finite, VIAMD missing |
|---|---:|---:|---:|---:|
| 1BNA | 41 | 5,250 | 0 | 408 |
| Seq152 | 42 | 9,732 | 0 | 96 |
| Seq174 | 40 | 8,952 | 0 | 408 |
| Nucleosome | 41 | 70,932 | 0 | 1,146 |
| Seq174 targeted base flips | 9 | 1,872 | 0 | 126 |
| IRF11, two helices | 51 | 10,854 | 0 | 774 |
| **Total** | **224** | **107,592** | **0** | **2,958** |

The maximum difference was 0.005189° or Å. VIAMD's missing values are
quality-rejected pairs and adjacent steps; they are not counted as matches.
Some targeted and stratified frames may overlap. These checks do not establish
complete mode equivalence or validate every possible pair assignment.
The combined internal frame instead retains finite reference-style values
when contact or geometry checks flag a pair, while preserving the quality
flags. It still reports missing values for failed fits or transforms. Across
the same 224 frame instances, it produced **110,550** comparable pair/step
values with no missing values; two measurements exceeded 0.0052° or Å. Both
are the same Seq174 snapshot appearing in the overview and targeted sets:
Twist was 142.515457° versus the printed 142.51°. All **54,450** helical
values were available; one exceeded tolerance, the nucleosome rounding case
described above. Strict-groove counts below were unchanged. A test verifies
that a failed Curves+ fit does not suppress valid 3DNA measurements, and
that retained flagged values remain flagged. This policy is internal and
does not change the current application's quality-gated results.

The combined frame's nucleotide values had **184,805** comparable values,
all within 0.061° or 0.11° for epsilon minus zeta, and **2,195** missing in
both; there were no one-sided missing nucleotide values on these sets.

### Internal strict 3DNA groove widths

An internal refined-width policy now returns missing values at the 3DNA
terminal boundaries or when the required phosphate flanks are unavailable,
instead of substituting direct distances. The current application still uses
the direct-distance fallback described above. The strict policy currently
assumes antiparallel strands; parallel strands and reference-reported helix
breaks need separate validation before use in a complete mode.

For the same six sample sets above, all **14,850** comparable strict groove
values matched classic 3DNA within 0.051 Å. Both tools reported **3,300**
values missing, with no one-sided missing values. These counts cover only the
tested snapshots and fixed pair order, not all missing-atom or broken-helix
cases. Periodic wrapping, unavailable flanks and degenerate geometry are
covered by focused tests.

### Internal Curves+ duplex axis

The selectable Curves+ mode uses an internal non-circular duplex axis
calculation from whole, ordered base frames and pair midpoints. It rejects the
entire axis when an input frame or a strand screw transform is invalid or singular; periodic unwrapping,
quality-rejected and missing bases, circular and single-stranded structures
still need an explicit end-to-end policy.
An internal per-helix wrapper now reconstructs each strand with local
minimum-image links, checks that the two strand images are consistent, and
reports unavailable axis and groove geometry separately. It rejects unsupported
parallel, circular and branching step topology. The five sampled trajectory
sets below were rerun through this periodic wrapper and against the same whole
snapshots; their reported axis, parameter and groove counts remain unchanged.
This does not validate all possible periodic or missing-atom cases.
As a focused missing-phosphate check, removing any one of seven internal or
required endpoint phosphates from the 1RNA input caused Curves+ v3.0nc to
report no groove analysis for that helix; the wrapper likewise marks its
grooves unavailable without discarding its axis. This does not establish
behavior for every missing-atom pattern.
An internal Curves+ frame groups these axis and groove results separately
from the pair/step and Fourier nucleotide values; it clears the incompatible
3DNA helical and groove fields rather than mixing definitions. It retains
finite pair/step results even when VIAMD's pairing checks flag them, while
preserving those flags. Across the 328 sampled frame instances, **125,514**
pair/step values were comparable, with no one-sided missing values and the
same **41** exceptions described above. The nucleotide
sample counts above and the five axis/groove sample sets below were rerun
through this combined path. The worker stores the Curves+ axis and groove
series separately from shared metrics and publishes them as separate
attributes; the Curves+ profile, dedicated full-trajectory CSV export and
viewport axis preserve each quantity's native level. Event selection and
automated external-reference comparison for these additional quantities
remain unfinished.

The axis agrees with the independent 1RNA Curves+ v3.0nc fixture at all 14
positions within 0.0052 for each direction component and 0.0052 Å for each
coordinate (the reference prints two decimals). Rigid transformations and
singular input are also tested.

For 173 sampled frame instances, each axis was evaluated from the unwrapped
duplex and compared with Curves+ `test=.t.` on the corresponding exported
whole snapshot. The table counts axis direction and coordinate components.

| Sample | Frames | Comparable components | Outside 0.0052 |
|---|---:|---:|---:|
| 1BNA | 41 | 2,952 | 0 |
| Seq152 | 42 | 5,040 | 0 |
| Seq174 | 40 | 4,800 | 0 |
| Seq174 targeted base flips | 9 | 1,026 | 0 |
| Nucleosome | 41 | 36,162 | 66 |
| **Total** | **173** | **49,980** | **66** |

All nucleosome exceptions occurred in two frames; the largest component
difference was 0.1044 Å. The diagnostic Curves+ build with the lower base-fit
cutoff described above produced no exceptions in that dataset. This diagnostic
is not the official reference, and those 66 differences are not excluded from
the table. Some sampled frames overlap; the checks do not establish universal
agreement.

Internal pair-to-axis displacement, inclination, tip and axis-bend calculations
and inter-axis rise and twist also agree with Curves+ on the 1RNA fixture:
displacements and rise within 0.0052 Å, angles within 0.051°. The first
axis-bend is missing in both. Displacement and orientation are per-pair
quantities, unlike 3DNA's per-step local helical parameters.

The same five trajectory sample sets were compared for these seven values:

| Sample | Comparable values | Outside tolerance | Missing in both |
|---|---:|---:|---:|
| 1BNA | 3,321 | 0 | 123 |
| Seq152 | 5,754 | 0 | 126 |
| Seq174 | 5,480 | 0 | 120 |
| Seq174 targeted base flips | 1,170 | 0 | 27 |
| Nucleosome | 42,066 | 98 | 123 |
| **Total** | **57,791** | **98** | **519** |

There were no one-sided missing values. The 98 nucleosome exceptions include
95 in the two frames affected by the reference base-fit cutoff and three
small residuals in two other frames. A lower-cutoff diagnostic reference
reduces the nucleosome exceptions to four, including one small residual in a
previously affected frame. These calculations are not yet exposed in the app.

### Internal Curves+ groove geometry

The internal non-circular groove calculation samples the two phosphate
backbones and measures W12/D12 and W21/D21 at pair and half-step levels.
Eighteen sampled backbone positions across both strands of the independent
1RNA Curves+ output match within 0.0007 Å per coordinate. All 25 groove
levels, including missing values, match that fixture within 0.051 Å.

The same five trajectory sample sets used for the axis were compared against
Curves+ v3.0nc on whole snapshots, including all four fields at every level:

| Sample | Frames | Comparable values | Outside 0.051 Å | Missing in both |
|---|---:|---:|---:|---:|
| 1BNA | 41 | 1,842 | 0 | 1,602 |
| Seq152 | 42 | 4,606 | 0 | 1,610 |
| Seq174 | 40 | 4,404 | 0 | 1,516 |
| Seq174 targeted base flips | 9 | 918 | 0 | 342 |
| Nucleosome | 41 | 45,476 | 1 | 2,248 |
| **Total** | **173** | **57,246** | **1** | **7,318** |

There were no one-sided missing values. The one exception is a nucleosome
depth of 9.048 Å versus the reference's printed 9.1 Å, a difference of
0.052 Å. These samples do not establish universal agreement. Rigid
transformation, missing inputs, ambiguous minima and output preservation are
also tested. The convention-dispatched worker now stores these calculations
per frame and supports multiple helices, but only antiparallel non-circular
topologies are accepted. Reference comparison and failure behavior for every
trajectory-level missing-input pattern remain unvalidated.

### Full-trajectory checks

The same comparisons were extended to all 2,001 1BNA annealing frames and
501 Seq152 frames. These runs include frames from the sampled table above.
Tolerances and exclusions are as above; counts are per value.

| Quantity | 1BNA comparable / outside | Seq152 comparable / outside |
|---|---:|---:|
| Curves+ pair and step | 259,968 / 0 | 116,268 / 17 |
| Curves+ nucleotide torsions | 364,182 / 0 | 155,310 / 0 |
| 3DNA helical | 123,672 / 0 | 56,562 / 1 |
| 3DNA refined grooves | 20,010 / 0 | 13,026 / 0 |
| 3DNA nucleotide torsions | 364,182 / 0 | 155,310 / 0 |
| 3DNA pucker phase and amplitude | 96,048 / 0 | 40,080 / 0 |

The 17 Seq152 Curves+ exceptions occur around one pair in frame 164. A
diagnostic build with a lower fit cutoff agrees within the stated tolerances
for those values; it is not the official reference. The Seq152 helical
exception is a Tip difference of 0.005262° against two-decimal output; the
diagnostic eight-decimal output differs from VIAMD by 0.000339°.

Before the fix, five 1BNA major widths in frames 722, 867, 868, 1191 and
1787 disagreed with the whole-duplex snapshots: the minimum-image P–P vector
selected a different periodic image. In frame 722, for example, one component
of the whole-duplex vector is 26.08 Å in a 50 Å box, but minimum image changes
it to −23.92 Å. The corrected calculation reconstructs each span using local
backbone and paired C1′ links, and rejects inconsistent or missing paths.
Both full-trajectory periodic-versus-whole checks now pass, and the five
1BNA widths agree with 3DNA within the stated tolerance. This assumes local
links themselves are shorter than the relevant periodic-image separation.

The 1BNA / Seq152 runs respectively excluded 16,170 / 966 Curves+ pair/step
and 8,394 / 552 helical values with a reference but no VIAMD value. For
grooves, 8,004 / 2,004 VIAMD values lacked a refined reference, and
16,008 / 4,008 were missing in both. The finite groove comparisons include
270 / 74 values with an associated step quality flag.

The public 1RNA fixture tests the same combined path: 14 pairs and 13 steps
against independent Curves+ pair/step references and classic 3DNA helical
references, plus seven steps with both refined groove widths. The test also
checks that failure of a 3DNA-style fit does not invalidate an otherwise
valid Curves+ step.

## Historical independent checks

These earlier measurements used the then-current implementation, not the
current combined pipeline:

- **Curves+ NVT:** 191 whole-duplex snapshots from a 12-pair DNA trajectory
  were compared using fixed pairing. Pair/step rotations differed by at most
  0.0501°, translations by at most 0.0058 Å, and the seven nucleotide torsions
  by at most 0.0501°. Epsilon minus Zeta differed by at most 0.098° because
  it uses two rounded torsions. Quality-rejected pairs and adjacent steps were
  excluded. The initial raw periodic-coordinate export was invalid; these
  results use the corrected whole-duplex snapshots.
- **Pucker:** on 4,584 sugars, VIAMD's Altona-Sundaralingam phase and amplitude
  differed from Curves+'s Fourier convention by 0.857° and 0.961° RMS,
  respectively. Applying the Fourier definition to the same ring torsions
  reproduced Curves+ within 0.051°; this is a convention difference.
- **Classic 3DNA:** on 1RNA, the helical transformation using 3DNA's rounded
  pair frames matched 78 reference values within 0.03° or Å. Fourteen refined
  groove values matched within 0.051 Å after the phosphate correction. On
  15 localized 1BNA snapshots, 140 comparable refined groove values matched
  within 0.050 Å.
- **Experimental full 3DNA-style pipeline:** on eight DNA datasets plus
  targeted Seq174 base-flip snapshots, 47,526 finite, quality-accepted pair,
  step and helical values matched classic 3DNA within 0.0052° or Å. This
  experiment is not the current pair/step convention and is not equivalent to
  validating the current combined pipeline across those datasets.

Curves+ curvilinear grooves differ in definition from VIAMD's refined
phosphate widths and are not expected to match.

## Trajectory and event checks

The historical 191-frame NVT smoke test exercised numerical analysis,
histograms, quality flags and event detection; native dihedrals and phosphate
distances were also checked against the corresponding measurements. These are
consistency checks, not independent proof of accuracy. Terminal atoms and
invalid geometry accounted for missing values.

Event counts changed substantially with parameter scope, thresholds, baseline
and minimum duration. State-switch candidates were also sensitive to
residence time and state boundaries. A switch may compare
residence-qualified states separated by short excursions; it is not
necessarily a direct transition at the reported destination frame.
Event labels, false-positive rates and physical assignments require further
validation and inspection.

## Reproducing checks

The bundled tests require no external reference installation:

```sh
cmake --build build --target viamd_test -j 4
./build/bin/viamd_test --filter='lavery*'
```

An optional local trajectory test can be run with user-supplied topology and
trajectory files; it is skipped when they are not supplied:

```sh
VIAMD_LAVERY_VALIDATION_GRO=/path/to/dna.gro \
VIAMD_LAVERY_VALIDATION_XTC=/path/to/trajectory.xtc \
./build/bin/viamd_test --filter=lavery_core.optional_local_trajectory_measurement_validation
```

Set `VIAMD_LAVERY_VALIDATION_EXPORT` to an existing scratch directory to
export snapshots, pair mappings and per-value VIAMD quality flags for
independent comparison. With `VIAMD_LAVERY_VALIDATION_CURVES` set to a
directory containing `curves.csv`, the optional test also compares Curves+
pair, step and nucleotide values (using the Fourier pucker convention for
that comparison). Helical and groove values are not compared to Curves+.
The historical sampled inputs, comparator and raw outputs are local research
artifacts, not repository fixtures; the complete external audits cannot be
reproduced from this repository alone.

The 1RNA reference fixture is included in the repository. To regenerate
classic 3DNA output independently in a scratch directory, set `X3DNA`, put
its `bin` directory on `PATH`, and run:

```sh
find_pair /path/to/lavery_1rna.pdb pairs.inp
analyze pairs.inp
```

See [test/README.md](../test/README.md) for additional test details. Automated
tests do not replace GUI inspection.
