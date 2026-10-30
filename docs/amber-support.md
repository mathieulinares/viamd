# Amber support in VIAMD

VIAMD reads Amber simulations through the same pipeline it uses for GROMACS. Amber
trajectories and energies are not a separate feature with their own UI: they become runs
and attributes in the system's attribute table. Playback, analysis, scripting, the
**System → Series** panel, the Timeline and the Distributions windows then work on them
without any Amber-specific code.

This document explains what is supported, how the readers are implemented, and how they
fit into VIAMD next to GROMACS XTC/TRR and EDR.

## Contents

1. [Scope](#scope)
2. [How VIAMD models simulation data](#how-viamd-models-simulation-data)
3. [The GROMACS ↔ Amber correspondence](#the-gromacs--amber-correspondence)
4. [Building](#building)
5. [Trajectory reader (Amber NetCDF)](#trajectory-reader-amber-netcdf)
6. [Energy reader (`mdout` and `mden`)](#energy-reader-mdout-and-mden)
7. [VIAMD integration](#viamd-integration)
8. [Using it](#using-it)
9. [Tests](#tests)
10. [Limitations and future work](#limitations-and-future-work)
11. [References](#references)

## Scope

| Supported | Not supported (yet) |
|---|---|
| Amber NetCDF trajectories (`.nc`, `.netcdf`), classic (CDF-1) and 64-bit-offset (CDF-2) | NetCDF-4/HDF5 trajectories, CDF-5 |
| Physical time (ps) or no time; periodic (any triclinic) or no box | Velocities, forces, replica/ensemble dimensions, cpptraj compressed coordinates |
| Amber energy output: the `mdout` text log and the `mden` energy file (`ntwe > 0`) | Minimization output, restarted runs concatenated in one file |
| Structure from a separately loaded file with identical atom order (usually a PDB) | Native Amber topology (`.prmtop`/`.parm7`) and restarts (`.inpcrd`/`.rst7`) |
| | Legacy text trajectories (`.mdcrd`/`.crd`) |

The NetCDF reader is optional at build time (it needs NetCDF-C). The energy reader is plain
text parsing and is always built.

## How VIAMD models simulation data

Everything VIAMD knows about a loaded dataset lives in the system's attribute table
(`md_attributes_t`, in [md_attributes.h](../ext/mdlib/src/md_attributes.h)). An attribute has a
path, a format (type, rank, shape, components), a unit, a label and either resident data or a
*virtual provider* that reads it on demand.

Three roles matter here:

- **System**: atoms, residues, chains, bonds, elements, from a structure or topology file
  (GRO, PDB, TPR, ...).
- **Run**: a trajectory, published under `run/<name>` by `md_run_publish`
  ([md_system.h](../ext/mdlib/src/md_system.h)). Its `time` attribute is a **frame axis**
  (flag `MD_ATTRIBUTE_FLAG_TEMPORAL`). Positions are virtual: frames are read from the file
  when playback or analysis asks for them.
- **Series**: supplemental, time-dependent data belonging to a run, such as an EDR file. It is
  published under the run (`run/<name>/edr/...`) **on its own time axis**. It is not resampled
  onto the trajectory frames. Each series attribute uses the file's own sample times, and VIAMD
  maps a trajectory frame to the matching sample with `md_attribute_axis_map`.

The UI and scripting only see attributes:

- **System → Series** lists every temporal group under the run.
- The Timeline and Distributions windows plot any series.
- Scripts read them with `attr("edr/potential")`.

Supporting a new format therefore means *publishing the right attributes*. No viewer code has
to change.

## The GROMACS ↔ Amber correspondence

| Role | GROMACS | Amber | Loader flags |
|---|---|---|---|
| System (structure) | `.gro`, `.tpr`, `.pdb` | `.pdb` generated from the same topology and atom order | `System` |
| Run (trajectory) | `.xtc`, `.trr` → `md_xtc_system_publish_run` | `.nc`/`.netcdf` → `md_amber_netcdf_system_publish_run` | `Trajectory \| MM` |
| Series (energies) | `.edr` → `md_edr_system_supplement_from_file` | `mdout`, `mden` → `md_amber_energy_system_supplement_from_file` | `Supplemental \| Temporal \| MM` |
| Energy group | `run/<name>/edr/<term>` | `run/<name>/amber_energy/<term>` | |
| Script access | `attr("edr/potential")` | `attr("amber_energy/eptot")` | |

The Amber readers copy the GROMACS readers' contracts:

- The NetCDF reader publishes exactly the attributes XTC/TRR publish. Nothing downstream can
  tell which format a run came from.
- The energy reader follows EDR step for step:
  - an F64 time axis of its own in ps, and F64 values with units;
  - a probe-then-publish coverage check against the run;
  - replacing the previous group rather than merging with it;
  - the same slug and collision rules for attribute names;
  - a `source` string giving the file path;
  - temporal flags, so the same panels show it.

## Building

The trajectory reader is behind an mdlib CMake option, `OFF` by default:

```sh
# Linux (Debian/Ubuntu): sudo apt install libnetcdf-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DMD_ENABLE_AMBER_NETCDF=ON
cmake --build build -j
```

- `MD_ENABLE_AMBER_NETCDF=ON` does three things:
  - runs `find_package(netCDF CONFIG REQUIRED)`;
  - links `netCDF::netcdf`;
  - compiles [md_amber_netcdf.c](../ext/mdlib/src/md_amber_netcdf.c) and defines
    `MD_AMBER_NETCDF`.
- Off, nothing NetCDF-related is found, linked, compiled or advertised. `LoaderType_AMBER_NETCDF`
  does not exist in that build.
- With vcpkg, the mdlib manifest has an `amber-netcdf` feature pulling `netcdf-c`.
  [CheckVCPKG.cmake](../ext/mdlib/cmake/CheckVCPKG.cmake) enables it automatically when the
  option is set.
- The energy reader [md_amber_energy.c](../ext/mdlib/src/md_amber_energy.c) has no dependency
  and is always compiled.

## Trajectory reader (Amber NetCDF)

Files: [md_amber_netcdf.h](../ext/mdlib/src/md_amber_netcdf.h),
[md_amber_netcdf.c](../ext/mdlib/src/md_amber_netcdf.c).

```c
bool md_amber_netcdf_file_is_trajectory(str_t filename);
bool md_amber_netcdf_system_publish_run(md_system_t* sys, str_t filename, str_t run, uint32_t flags);
```

### Validation

A file is accepted only if it carries the Amber trajectory convention. Generic NetCDF files
and `AMBERRESTART` files are rejected, even with an `.nc` extension.

- **Format:** `NC_FORMAT_CLASSIC` or `NC_FORMAT_64BIT_OFFSET`.
- **Global attributes:** `Conventions` is `AMBER` and `ConventionVersion` is `1.0`.
- **Dimensions:** `frame`, `atom` and `spatial = 3`. An optional `spatial` label variable must
  read `xyz`.
- **`coordinates`:** float or double, shape `{frame, atom, spatial}`, units `angstrom`.
- **`time` (optional):** one value per frame, units `picosecond`, finite and nondecreasing.
- **`cell_lengths` and `cell_angles` (optional):**
  - both must be present, or both absent;
  - shapes `{frame, 3}`, units `angstrom` and `degree`;
  - every cell must be a valid triclinic cell.
- **`_FillValue` / `missing_value`:** these are honoured. A missing coordinate, time or cell
  value is an error, never silent data.
- **Atom count:** must equal the loaded system's (checked by `md_run_publish`).

### Published attributes

The reader fills an `md_run_desc_t` and calls `md_run_publish`, which is the same path the XTC
and TRR readers use:

| Path | Type / shape | Content |
|---|---|---|
| `run/<name>/time` | F64 `{F}`, temporal | ps if the file has `time`. Otherwise unitless frame ordinals, the existing convention for time-less trajectories. |
| `run/<name>/unitcell` | F32 `{F,3,3}` | Row vectors in Å, built from lengths/angles. **All zeros** for a nonperiodic file. |
| `run/<name>/atom/position` | virtual F32 `{F,N}` ×3 | Å, read on demand from the file |
| `run/<name>/source/path` | STR | Canonical path of the `.nc` |
| `run/<name>/source/offset`, `size` | I64 `{F}` | `-1` / `0`. NetCDF reads are hyperslabs, not byte ranges (same convention as H5MD). |

Details:

- **Units.** Amber coordinates are already in Å, so there is no ×10 as in XTC (nm).
- **Nonperiodic trajectories** still get a `unitcell` attribute, filled with zeros. Frame
  extraction requires the attribute to exist; a zero cell is VIAMD's existing "no box" value,
  so periodic handling is disabled exactly as for a GROMACS run without a box.
- **Streaming.** Only time and cells (O(F)) are resident. Coordinates are read with
  `nc_get_vara_float` by a virtual provider, either one full frame or a single atom slice.
  Each call opens and closes its own handle.
- **Thread safety.** NetCDF-C is not thread-safe, so every NetCDF call is serialized by a
  process-wide lock (`pthread_mutex` or Windows `SRWLOCK`).
- **Atomicity.** On any failure nothing is published, and owned state is freed.

## Energy reader (`mdout` and `mden`)

Files: [md_amber_energy.h](../ext/mdlib/src/md_amber_energy.h),
[md_amber_energy.c](../ext/mdlib/src/md_amber_energy.c).

```c
bool md_amber_energy_file_is_mdout(str_t filename);
bool md_amber_energy_file_is_mden(str_t filename);
bool md_amber_energy_file_is_energy(str_t filename);   // either
bool md_amber_energy_system_supplement_from_file(md_system_t* sys, str_t filename, str_t run);
```

### Where Amber writes energies

GROMACS has one binary energy file, `.edr`. Amber `sander`/`pmemd` have two text outputs,
written by the same run:

- **`mdout`** (`-o`, default name `mdout`): the human-readable log. Every `ntpr` steps it
  prints a record:

  ```text
   NSTEP =      500   TIME(PS) =  106001.000  TEMP(K) =   301.62  PRESS =     0.0
   Etot   =    -36614.6119  EKtot   =      8234.5537  EPtot      =    -44849.1657
   BOND   =       137.1327  ANGLE   =       909.8515  DIHED      =        29.1859
   1-4 NB =        62.1025  1-4 EEL =    -13601.9918  VDWAALS    =      6143.3880
   EELEC  =    -38528.8345  EHBOND  =         0.0000  RESTRAINT  =         0.0000
  ```

  The log also contains setup headers, input echoes and, at the end, `A V E R A G E S` and
  `R M S  F L U C T U A T I O N S` blocks that use the same `NAME = value` syntax.

- **`mden`** (`-e`, default name `mden`): written every `ntwe` steps when `ntwe > 0`. It starts
  with a header of `L0`…`L9` lines naming columns. Each record then repeats the same lines with
  numbers:

  ```text
  L0  Nsteps           time(ps)         Etot             EKinetic
  L1  Temp             T_solute         T_solv           Pres_scal_solu
  ...
  L9  AV_permMoment    AV_indMoment     AV_totMoment     Density          dV/dlambda
  L0    10000  0.1600000000E+03  -.1151381094E+05  0.2760976318E+04
  L1 0.3141275170E+03  0.3141275170E+03  -.1154520669E+11  0.1000000000E+01
  ...
  ```

The NetCDF trajectory convention has no energy variables, so energies always come from one of
these two files.

### Detection

- **`mdout`:** the first 1 MiB contains `NSTEP`, `TIME(PS)` and `Etot`, case-insensitively.
- **`mden`:** the file opens with an `L0` header line containing `Nsteps`, `time(ps)` and
  `Etot`. This is checked first, so an `mden` is never classified as `mdout`.

### Parsing

**`mdout`:**

- The line is tokenized into `NAME = value` pairs. Names may contain spaces (`1-4 NB`) and
  `=` may be attached to the name or the value.
- A record starts at `NSTEP`. Assignments before the first `NSTEP` (setup headers such as
  `ntx = 7`) are ignored.
- Parsing stops at the averages/RMS section, so those summary values never become samples.
- A record must have `NSTEP`, `TIME(PS)` and `Etot`. An incomplete record is an error and never
  reuses values from the previous one.

**`mden`:**

- Column names come from the file's own `L<k>` header, so other column sets are accepted.
- A record line whose value count differs from its header line is rejected, with the line
  number in the error.
- A new header in the middle of the file (an appended run) closes the open record.

**Both formats:**

- A term appearing twice in one record is an error.
- Time must be nondecreasing. A restarted run that goes back in time is rejected with an
  explicit message, as in EDR.
- A term missing from some records is omitted, with an info message. No values are invented.

### Term names, shared between the two formats

`mden` and `mdout` name the same quantities differently. Terms present in both are published
under the **`mdout` name**, so a script or saved plot works with either file:

| `mden` | published as | attribute slug |
|---|---|---|
| `Nsteps` | `NSTEP` | `nstep` |
| `time(ps)` | time axis | `time` |
| `Etot` | `Etot` | `etot` |
| `EKinetic` | `EKtot` | `ektot` |
| `E_pot` | `EPtot` | `eptot` |
| `Temp` | `TEMP(K)` | `temp_k` |
| `Pressure` | `PRESS` | `press` |
| `volume` | `VOLUME` | `volume` |
| `Density` | `Density` | `density` |
| `E_vdw`, `E_el`, `E_hbon` | `VDWAALS`, `EELEC`, `EHBOND` | `vdwaals`, `eelec`, `ehbond` |
| `E_bon`, `E_angle`, `E_dih` | `BOND`, `ANGLE`, `DIHED` | `bond`, `angle`, `dihed` |
| `E_14vdw`, `E_14el` | `1-4 NB`, `1-4 EEL` | `1_4_nb`, `1_4_eel` |
| `E_const`, `E_pol` | `RESTRAINT`, `EPOLZ` | `restraint`, `epolz` |
| `EKComTot`, `VIRIAL_tot` | `EKCMT`, `VIRIAL` | `ekcmt`, `virial` |
| `dV/dlambda` | `DV/DL` | `dv_dl` |

Terms only in `mden` keep their own name, for example `BoxX` → `boxx`, `T_solute` → `t_solute`,
`pres_X` → `pres_x`.

The slug is lowercase alphanumerics with `_` for any other run of characters, using the same
rules as EDR. If two names produce the same slug, the second gets a `_2`, `_3`, ... suffix.
`time` and `source` are reserved.

### Units

Amber writes no units in either file, so the reader assigns them by term:

| Terms | Unit |
|---|---|
| Energies (default for any other term) | kcal/mol |
| Time | ps |
| `TEMP(K)`, `T_solute`, `T_solv` | K |
| `PRESS`, `pres_X/Y/Z` | atm (1.01325 bar) |
| `VOLUME` | Å³ |
| `Density` | g/cm³ |
| `BoxX/Y/Z` | Å |
| `NSTEP`, pressure-scaling factors, dipole moments | none |

Units are `md_unit_t` values, as in EDR, so plots and scripts convert them. A GROMACS potential
in kJ/mol and an Amber `EPtot` in kcal/mol are therefore comparable.

### Published attributes

Under `run/<name>/amber_energy/`:

| Path | Type / shape | Content |
|---|---|---|
| `time` | F64 `{S}`, temporal | ps, the energy file's own sample times |
| `<term>` | F64 `{S}`, temporal | One scalar series per term, labelled with the Amber name |
| `source` | STR | Canonical path of the energy file |

`S` is the number of energy records. It is independent of the number of trajectory frames.
Energies are usually written more often than coordinates (`ntpr`/`ntwe < ntwx`), and every
sample is kept.

### Coverage check and replacement

Publication follows the same steps as `md_edr_system_supplement`:

1. Parse the whole file into temporary memory.
2. Create the energy time axis in a scratch attribute table, the "probe".
3. For every frame of `run/<name>/time`, check that `md_attribute_axis_map` finds an energy
   sample at that time.
   - If any frame has none, the load is refused with the frame index and its time. The usual
     cause is an energy file from another run or segment.
4. Remove the old `run/<name>/amber_energy` group: loading a second energy file **replaces**
   the first rather than merging two axes.
5. Publish `time`, the terms and `source`. If any step fails, the whole group is removed.

### Differences from EDR

- **No vector or tensor grouping.** EDR packs `Pres-XX`…`Pres-ZZ` into one 3×3 attribute.
  Amber has no such naming convention, so every term is a scalar, including `pres_X/Y/Z` and
  `BoxX/Y/Z`.
- **Units are assigned, not read.** EDR stores a unit string per term; Amber files do not.
- **F64 is preserved directly.** The generic `md_run_publish_series` stores F32 columns, so the
  reader publishes with `md_attributes_create` directly, like EDR, to keep double precision.

## VIAMD integration

All VIAMD-side changes are in the loader layer and in file detection. Playback, the Series
panel, the Timeline and scripting needed no Amber code.

### Loader types

[loader.h](../src/loader.h), [loader.cpp](../src/loader.cpp):

| Type | Name in the Loader menu | Extensions | Flags | Dispatch |
|---|---|---|---|---|
| `LoaderType_AMBER_NETCDF` (only with `MD_AMBER_NETCDF`) | Amber NetCDF Trajectory (nc) | `nc`, `netcdf` | `Trajectory \| MM` | `publish_run` → `md_amber_netcdf_system_publish_run` |
| `LoaderType_AMBER_ENERGY` | Amber Energy (mdout/mden) | `mdout`, `mden`; content-detected `.out`, `.log`, none | `Supplemental \| Temporal \| MM` | `load_supplemental` → `md_amber_energy_system_supplement_from_file` |

- The flags match XTC/TRR and EDR respectively, so roles, file-list entries and workspace
  records follow automatically:
  - `Trajectory` → `FileRole_Trajectory`;
  - `Supplemental | Temporal` → `FileRole_Series`.
- Loading a NetCDF trajectory *as a structure* is refused with the same message as XTC/TRR.
- The Dataset window labels an Amber energy file **Energies**, as it does an EDR file
  ([dataset.cpp](../src/components/dataset/dataset.cpp)).
- The success message gives the script path to use: `attr("amber_energy/<term>")`
  ([viamd.cpp](../src/viamd.cpp)).

### File detection (`loader::init`)

- **`.nc`:** recognized by extension, then confirmed with
  `md_amber_netcdf_file_is_trajectory`. A non-Amber `.nc` falls back to the Loader dialog.
- **Amber energy:** `.out`, `.log` and extensionless files are claimed only when
  `md_amber_energy_file_is_energy` recognizes the contents. Other formats share those
  extensions, so the extension alone is not enough.
- **Extension bug fixed:** `extract_ext` returns the text after the last `.` in the *whole
  path*. For a file named `mdout` inside `.../.config/` it returned `config/mdout`, so the
  content check never ran. `init` now discards an "extension" that contains a path separator.

### Open dialog and drag-and-drop

- **File → Open File…** always shows the *Load Dataset* dialog. The dialog used to choose a
  default loader from the extension only, so `.out` files had no default selected. It now calls
  `loader::init`, the same detection used for dropped files, so Amber energy output is
  pre-selected ([main.cpp](../src/main.cpp)).
- **Drag-and-drop** (`file_queue_push` in [viamd.cpp](../src/viamd.cpp)) falls back to content
  detection for unknown extensions. A dropped `mdout`/`mden`/`.out` therefore loads without a
  dialog.
- Dropped files keep their priority order: structure → trajectory → series. Dropping all three
  files at once loads them in the right order.

### Publishing the submodules

The synthetic Amber energy fixtures are tracked in mdlib itself; its separate `test_data`
submodule remains at an existing public revision. Publish the mdlib changes first, then update
and publish VIAMD's mdlib gitlink. A recursive clone fetches each submodule from its configured
URL, so the recorded commits must be available from those URLs.

This branch temporarily points VIAMD's mdlib URL at the fork. For upstream integration, first
make the mdlib changes available upstream, then restore the upstream URL and pin the commit
actually present there. A squash or rebase merge can give that commit a different hash.

## Using it

1. Load the structure, a PDB with **the same atom order** as the trajectory, solvent and ions
   included. If you only have a topology, generate it with cpptraj or MDTraj from the
   `.prmtop` and the first frame.
2. Load the trajectory `.nc`.
3. Load `mdout` or `mden`. With **File → Open File…**, the loader *Amber Energy (mdout/mden)*
   is pre-selected; dropped files load directly.
4. **System → Series → amber_energy** lists the terms. Double-click one to plot it on the
   Timeline, or drag it into a Timeline or Distributions subplot.
5. Scripts read the same series:

   ```text
   e  = attr("amber_energy/etot");     # kcal/mol, follows the current frame
   T  = attr("amber_energy/temp_k");
   ```

For the coverage check to pass, the energy output must contain a sample at every trajectory
frame time. In practice `ntwx` (coordinates) must be a multiple of `ntpr` (`mdout`) or `ntwe`
(`mden`), and the energy file must come from the same segment of the run.

## Tests

- **mdlib** ([test_amber_netcdf.c](../ext/mdlib/unittest/test_amber_netcdf.c)): NetCDF fixtures
  are generated at test time with NetCDF-C. The tests cover:
  - streamed coordinates, time and triclinic cells;
  - nonperiodic extraction clearing a previous cell;
  - missing time and cells, giving ordinals and no box;
  - wrong convention and decreasing time;
  - 64-bit-offset files, bad axis labels and fill values.
- **mdlib** ([test_amber_energy.c](../ext/mdlib/unittest/test_amber_energy.c)), using fixtures in
  [amber_energy_data](../ext/mdlib/unittest/amber_energy_data):
  - `mdout` detection and publication, including repeated setup headers;
  - refusal when the energies do not cover the run;
  - incomplete records;
  - `mden` detection, publication and name mapping;
  - `mden` lines that do not match their header.
- **VIAMD** ([test_loader.cpp](../test/test_loader.cpp)):
  - loader table consistency;
  - `.nc` content check;
  - `mdout`/`mden` detection and supplement through the loader;
  - extensionless energy files inside a dotted directory;
  - NetCDF trajectory dispatch through `publish_run`.

The readers were also checked against real files, outside the automated tests:

- **Trajectories:**
  - MDAnalysis `ace_tip3p.nc` (pmemd 16, periodic);
  - cpptraj `trpzip2.ff14SB.mbondi3.nc` (sander 22, nonperiodic);
  - a Zenodo protein trajectory (200 frames).
- **`mdout`:** a 50-frame excerpt of
  [Zenodo 14673608](https://doi.org/10.5281/zenodo.14673608), with its matching 491-record
  `pmemd` 18 `mdout`. This file exposed the setup-header bug.
- **`mden`:** an 8-frame `pmemd.cuda` run from the
  [CAR example](https://github.com/ZheLi-Lab/The_CAR_enhanced_adaptive_sampling_method), with
  its `mden`.
- The `mdout` and `mden` pairs were loaded in the VIAMD GUI and `Etot` was plotted.

## Limitations and future work

- **Native Amber setup.** `.prmtop`/`.parm7` and `.inpcrd`/`.rst7` would remove the PDB step.
  The loader must then keep Amber's explicit bonds instead of inferring them.
- **Atom order is not verified.** Only the atom count is checked against the structure.
- **Legacy `.mdcrd` trajectories.** These have no atom count, time or box metadata in the file.
  Reading them needs explicit user choices.
- **NetCDF-4, velocities/forces, ensembles** are not read.
- **Energy output variants:**
  - minimization `mdout`;
  - concatenated restarted runs, whose time goes backwards and are rejected today;
  - other producers' variants beyond those tested.
- **One file per run.** As with EDR, loading another energy file replaces the group. Splicing
  several segments is not supported.

## References

- [Amber NetCDF trajectory/restart convention](https://ambermd.org/netcdf/nctraj.xhtml)
- [cpptraj NetCDF implementation](https://github.com/Amber-MD/cpptraj/blob/master/src/Traj_AmberNetcdf.cpp),
  used as an interoperability reference
- [NetCDF-C documentation](https://docs.unidata.ucar.edu/netcdf-c/current/)
- Amber Reference Manual, `&cntrl` options `ntpr`, `ntwe`, `ntwx`, and the `mdout`/`mden` file
  descriptions
- GROMACS counterparts: [md_xtc.c](../ext/mdlib/src/md_xtc.c), [md_trr.c](../ext/mdlib/src/md_trr.c),
  [md_edr.c](../ext/mdlib/src/md_edr.c)
