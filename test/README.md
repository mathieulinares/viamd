# viamd tests

Unit tests for viamd's own code, using the same `utest.h` as mdlib's suite so the two behave
alike. Build them with the rest of the project and run everything with one command from the
repository root:

```
cmake .
make -j
ctest --output-on-failure
```

`ctest` picks up both this suite and mdlib's. Pass `-DVIAMD_UNITTEST=OFF` to leave this target out.

## What belongs here

Tests for the application's own units - `src/`, `src/app/`, `src/gfx/`. A unit can be tested only
if it links without an application: no ImGui, no OpenGL, no `ApplicationState`. Today that is

| Unit | Covered by |
|---|---|
| `src/serialization_utils.cpp` | `test_serialization.cpp` — the workspace text format |
| `src/event.cpp` | `test_event.cpp` — delivery, ordering, delays |
| `src/task_system.cpp` | `test_task_system.cpp` — range partitioning, dependencies |
| `src/color_utils.h` | `test_color_utils.cpp` — the colour space conversions, which are header only |
| `src/loader.cpp` | `test_loader.cpp` — the format dispatch tables and what `init` makes of a path |
| `src/file_list.cpp` | `test_file_list.cpp` — the files of a system and its runs: the role a file takes from its place, and the workspace's `[System]`, `[Run]` and `[Input]` |
| `src/gfx/camera_utils.cpp` | `test_camera_utils.cpp` — the view and projection transforms, as inverse pairs |
| `src/cartoon_glyphs.cpp` | `test_cartoon_glyphs.cpp` — the spheres and tubes of the lipid cartoon, built without the GL molecule they are derived from on the GPU |
| `src/representation_tree.cpp` | `test_representation_tree.cpp` — the tree of the Representations window: sibling order, every placement a drag can ask for, and that a group never goes within itself |

**Adding a test file:** drop a `.cpp` in this folder. It is globbed, so there is nothing to edit.

**Adding a unit under test:** list it in `UNIT_FILES` in `CMakeLists.txt`. That list stays explicit
on purpose: these files are the application, so which of them can be linked without one is part of
its structure and worth writing down. Components are the opposite case - see below.

## Testing a component

Component tests do **not** live in this folder, and `CMakeLists.txt` here never names a component.
Adding a component to viamd means adding its folder and nothing else; testing one works the same
way. A component declares itself testable by its layout:

```
src/components/foo/foo.cpp            the ImGui drawing and the event handling
src/components/foo/foo_core.{h,cpp}   the computation, linkable without an application
src/components/foo/tests/*.cpp        its tests
```

The `_core` suffix is the declaration of separability - the test build collects `*_core.cpp` from
every component folder, and that component's `tests/` alongside it. A component with no `_core`
unit contributes nothing, which is what keeps a component that is mid-split, or half-landed from
another branch, inert rather than a broken build: its tests simply wait for the unit.

Components as they stand are single translation units mixing computation with drawing and event
handling, so the work is the split itself. Keep the handling and the drawing in `foo.cpp`, and move
the computation into `foo_core.{h,cpp}` taking plain data and returning plain data. The point is
not the file boundary but what it forces: a core that knows about a clearance field or a histogram,
and nothing about viamd, can be checked against cases with closed-form answers rather than by
loading a system and looking at it.

A component's tests include its headers through the component root, as `<foo/foo_core.h>`, so two
components may name their headers alike without colliding.

## Headless heatmap rendering

Heatmap rendering regressions run separately in `viamd_plot_test`. They create
headless ImGui/ImPlot contexts without a display or OpenGL, verifying that NaN
and infinite cells are transparent (with and without labels, in both matrix
layouts), including a 147-by-2501 nucleosome-sized matrix. Build with
`cmake --build build --target viamd_plot_test`, then run
`ctest --test-dir build --output-on-failure -R '^viamd_plot_test$'`.

## Lavery numerical reference

The Lavery core regressions also cover rejection of broken fixed
partners, stale-frame clearing, adjoining-step invalidation, exclusion from
statistics, periodic pairing, geometric quality flags, missing/unsupported fit diagnostics,
multi-parameter contributor masks and strict sigma thresholds, consecutive event
grouping and peak tracking, and the parameter/event CSV records' 28/42-column shapes, escaping,
units, baselines, validity and outlier flags. Run without launching the GUI:

```
cmake --build build --target viamd viamd_test
build/bin/viamd_test --filter='lavery_core*'
```

`test_lavery_analysis.cpp` additionally tests the production per-frame pipeline,
canonical H-bond gating, explicit-H versus heavy-contact fallback, loss/recovery
and adjacent-link exclusion, periodic/gap-aware smoothed axes and unit tangents,
shared distribution bins and density normalization, displayed-axis CSV shape,
run-relative script compilation **and evaluation**, publication/replacement and
cleanup. Task-system regressions cover cooperative interruption inside a single
callback and a queued callback interrupted before execution. No GUI launch is
needed for these checks.

Pooled-reference regressions cover persistent local anomalies, separate helices,
singleton pairs, pair/step/nucleotide grouping, baseline intervals, missing
samples, circular and within-state references, unchanged own-history behavior,
full-span anomalous regions, and exported reference-scope metadata.
Transition display-filter regressions cover all-member, selected-pair (both
nucleotides), selected-nucleotide and missing-selection scopes, From/To
directions and clearing filters independently of transition detection.
Sample-comparison regressions cover unequal group lengths, shared histogram
bins, independently unit-normalized densities, population SD, constant/empty/
missing groups and circular wrapping across +/-180 degrees.

Display-summary regressions exercise 500,000 frames, strided series,
single-frame positive/negative peaks, missing-data breaks, exact samples on
zoom, refined range boundaries, signed heatmap peak selection, and empty/invalid
inputs. They assert bounded output size and report construction/query timings;
they are not an end-to-end GUI FPS benchmark.
Maximum-shown-duration tests cover elapsed physical time, ps/ns scaling,
inclusive frame fallback, exact thresholds, singleton spans, bridged gaps,
independent children under hidden parents, Unlimited and invalid input rejection.

`test_lavery_reference.cpp` covers old/new raw CSV import, quoted fields, duplicate
samples, stage-summary missing/state/reference denominators and signed/RMS z,
bulk cross-class parameter/member selection and deduplication,
disruption reason text versus unchanged conservative chemistry classification,
malformed records, unit/class rejection, member identities and ambiguous
mapping, external mean/MAD/circular/state-conditioned references, missing-state
exclusion, pooled deduplication, exact RMS ranking, ties, time separation, invalid
features, zero-scale rejection, and native XTC/GRO roundtrip. Coordinate export
checks atom count, first-frame GRO coordinates, chronological original times,
triclinic boxes, score rank preservation and refusal to overwrite outputs.

Categorical-state regressions cover complete directional-contact evidence,
conservative WC/A-T/U Hoogsteen orientation, ambiguous versus unpaired versus
unavailable, hydrogen-free annotations, missing hydrogen coordinates, reversible
pair separation, raw-run preservation, exact residence thresholds, unavailable
source resets and restricted ranges. Categorical display summaries exercise
500000 frames and retain one-frame state changes and missing-data mixtures.

```
build/bin/viamd_test --filter='lavery_core.display_summary*'
```

### Local trajectory validation

The opt-in production-pipeline test accepts a matching GRO structure and XTC
trajectory. For TPR inputs, extract the structure with `gmx editconf -f input.tpr
-o input.gro`; do not change the atom selection relative to the XTC.

```
VIAMD_LAVERY_VALIDATION_GRO=/path/input.gro \
VIAMD_LAVERY_VALIDATION_XTC=/path/input.xtc \
build/bin/viamd_test --filter=lavery_core.optional_local_trajectory_measurement_validation
```

This processes every frame, prints per-pair rejection reasons, cross-checks
native dihedrals and phosphate distances, and tests both statistical baselines,
first-quarter reference intervals, and middle-half event/state-switch search
intervals. Duration sweeps scale with the median sampling interval. Timings
separate frame analysis from statistics; the test's peak memory includes its
reference arrays and event sensitivity sweeps, not just application analysis.
It also reports the strongest pooled-helix Rise anomaly and its fraction of
finite samples above 2.5 sigma for both statistical methods.

For a small sampled XTC, set `VIAMD_LAVERY_VALIDATION_EXPORT` to an existing
directory to export unwrapped PDB snapshots and `viamd.csv`. Each helix has a
`pairing_N.txt` selection file; `members.csv` maps global pair/step indices to
components. The legacy `pairing.txt` selects the first helix only. Curves+ skips
protein residues when numbering its input, so remove proteins and remap the
component selections to the resulting nucleotide order before running it.
Independent comparisons must also account for Curves+'s helix-direction sign
normalization in strongly distorted structures. Its groove and sugar-pucker
definitions are not identical to the production measurements.

Snapshot export also writes `pairing-states.csv` (raw frame/pair chemistry and
canonical contact counts) and checks invariance under periodic unwrapping.
Alternatively set `VIAMD_LAVERY_VALIDATION_PAIRING=/path/pairing.csv` to export
only those state records without writing PDBs, useful for full event windows.
Chemistry labels require explicit donor hydrogens; heavy-atom-only fixtures
retain their original numerical geometry but are annotated Unavailable.

For an end-to-end real-data external-reference/export check, also set
`VIAMD_LAVERY_VALIDATION_REFERENCE=/new/path/reference.csv`. The opt-in test
exports all parameter classes, reimports/matches them and compares Mean/SD and
Median/MAD statistics and every heatmap sample with the original full-precision
calculation, including circular and within-state references. Optional
`VIAMD_LAVERY_VALIDATION_REFERENCE_RANGE_PS=0,1995` restricts the reference to
inclusive physical-time bounds while comparisons still cover the full run. Set
`VIAMD_LAVERY_VALIDATION_REPRESENTATIVES=/new/path/frames.xtc` to also rank and
export five real frames with a 100 ps separation (if enough qualify), combining
central-pair Opening, central-step Rise/Twist and both central-pair nucleotide
Chi features. The output is reloaded and every exported atom coordinate and
timestamp is checked against its original frame.
For a localized or per-hold comparison, override the one-based feature list
with `VIAMD_LAVERY_VALIDATION_FEATURES=step/twist:3,step/rise:3` and restrict
candidate frames using `VIAMD_LAVERY_VALIDATION_SEARCH_RANGE_PS=8000,10000`.
The reference range remains independent. The mapping CSV records the exact
features, reference and search bounds, and every selected frame is round-tripped.
The test does not overwrite existing XTC/GRO/CSV outputs.
`VIAMD_LAVERY_VALIDATION_SAMPLES=/path/samples.csv` additionally exports all raw
parameters with production per-parameter quality flags. The independent
`VIAMD_LAVERY_VALIDATION_EVIDENCE=/path/evidence.csv` exports pair quality and
canonical contact counts per frame, without producing thousands of PDBs.

`src/components/lavery/tests/test_lavery_core.cpp` checks all 12 intra-pair and
step metrics against the numerical oracle in its `data/` folder. The oracle was
generated locally with the official Curves+ v3.0nc executable and its
`standard_b.lib`, downloaded from <https://curvesplus.bsc.es/misc>. No reference
executable or network access is needed to run these tests.

To regenerate it, convert the first 294 residues of
`ext/mdlib/test_data/nucl-dna.gro` to PDB, multiplying coordinates by 10 to
convert nm to Angstrom. Preserve residue order, use consecutive residue numbers
1-294, and omit the proteins. Run Curves+ with:

```
&inp file=nucleosome.pdb, lis=reference, lib=standard, &end
2 1 -1 0 0
1:147
294:148
```

The defaults are `fit=T` and `refo=F`. Sections B and C of `reference.lis`
provide the six metrics for 147 pairs and 146 steps. Store rotation components
before translation components, as in the Lavery metric enum. The test tolerances
of 0.06 degree and 0.006 Angstrom allow for the listing's rounding.

### Independent RNA reference

`src/components/lavery/tests/data/lavery_1rna.pdb` contains only coordinate,
chain-boundary and unit-cell records from the public
[RCSB 1RNA PDB data](https://files.rcsb.org/download/1RNA.pdb), the A-U RNA
duplex U(U-A)6A (Dock-Bregeon et al., 1989,
<https://doi.org/10.1016/0022-2836(89)90010-7>).
The accompanying `lavery_rna_curves_reference.h` contains the 14 intra-pair and
13 inter-pair reference rows from the official Curves+ v3.0nc calculation:

```
&inp file=lavery_1rna.pdb, lis=rna_reference, lib=standard, &end
2 1 -1 0 0
1:14
28:15
```

The same tolerances apply. All 162 RNA parameter values pass through the
production canonical-contact validity pipeline, using the explicitly labeled
hydrogen-free heavy-atom fallback. Numerical fitting of the DNA fixture still
checks all 1,758 reference transforms independently of contact validity; its
production pipeline separately rejects one geometrically calculable pair with
no canonical H-bonds, leaving 146 valid pairs and 144 valid inter-pair links.

The midpoint-axis tests validate the documented smoothing/interpolation
algorithm, not equivalence to Curves+'s screw-axis method. Docking, plot gestures,
and 3D appearance require the user's manual GUI testing.

## Note on global state

The event system and the task system are both global and neither offers a way to unregister. Tests
therefore share them: each event test uses its own event type and its handler ignores everything
else, and the task system is initialized once on first use rather than per test.
