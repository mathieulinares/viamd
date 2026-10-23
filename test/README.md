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
grouping and peak tracking, and the CSV record's 23-column shape, escaping,
units, baselines, validity and outlier flags. Run without launching the GUI:

```
cmake --build build --target viamd viamd_test
build/bin/viamd_test --filter='lavery_core*'
```

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
## Note on global state

The event system and the task system are both global and neither offers a way to unregister. Tests
therefore share them: each event test uses its own event type and its handler ignores everything
else, and the task system is initialized once on first use rather than per test.
