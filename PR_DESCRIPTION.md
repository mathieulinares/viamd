# Amber support: NetCDF trajectories and `mdout`/`mden` energies

Adds Amber trajectory and energy support to VIAMD, using the same pipeline as GROMACS: an Amber
NetCDF trajectory loads like an XTC/TRR, and Amber energy output loads like an EDR. No Amber-specific
UI was needed: playback, analysis, **System → Series**, Timeline/Distributions and `attr()` scripting
work unchanged.

Full implementation details: [docs/amber-support.md](docs/amber-support.md).

## What's new

- **Amber NetCDF trajectories** (`.nc`, `.netcdf`)
  - Reads classic and 64-bit-offset files.
  - The file must declare the Amber convention; other NetCDF files are rejected.
  - Coordinates are streamed on demand. Time is in ps, and periodic cells may be triclinic or
    absent.
  - Publishes the same `run/<name>/...` attributes as XTC/TRR, through `md_run_publish`.
  - Optional at build time: `-DMD_ENABLE_AMBER_NETCDF=ON` requires NetCDF-C. vcpkg users get it
    through the `amber-netcdf` feature.
- **Amber energies**, from both `mdout` (the text log, `ntpr`) and `mden` (the energy file,
  `ntwe`)
  - Published like EDR under `run/<name>/amber_energy/`. The energy file keeps its own F64 time
    axis and is not resampled to the frames.
  - Values are F64 with units; energies are in kcal/mol.
  - A `source` attribute records the file path.
  - Before publishing, the reader checks that every trajectory frame has a matching energy
    sample.
  - Terms that `mden` shares with `mdout` use the `mdout` names, so
    `attr("amber_energy/etot")` works with either file.
- **Detection**
  - Amber energy output is recognized from its contents: `.out`, `.log`, and the extensionless
    `mdout`/`mden` files.
  - **File → Open File…** now pre-selects the detected loader, and dropped files load
    directly.
  - Fixed a bug that made VIAMD treat part of a dotted directory name as the extension of an
    extensionless file.

## Changes

**mdlib** (`ext/mdlib`, `amber` branch):

- New readers: `src/md_amber_netcdf.{c,h}` and `src/md_amber_energy.{c,h}`.
- Build changes: CMake option, vcpkg feature and `CheckVCPKG.cmake`.
- New tests: `unittest/test_amber_netcdf.c` and `unittest/test_amber_energy.c`.
- New synthetic fixtures in `unittest/amber_energy_data/`, tracked with mdlib.

**VIAMD:**

- `src/loader.{h,cpp}`: adds `LoaderType_AMBER_NETCDF` and `LoaderType_AMBER_ENERGY`, plus
  content detection and the extension fix.
- `src/main.cpp`: the Open dialog uses the same detection as drag-and-drop.
- `src/viamd.cpp`: drop priority uses content detection; the success message shows the
  `attr()` path to use.
- `src/components/dataset/dataset.cpp`: Amber energy files are shown as **Energies**.
- `test/test_loader.cpp` and `test/test_file_list.cpp`: new loader tests.
- `docs/amber-support.md`: new documentation.
- `.gitmodules`: temporarily points mdlib to the published `mathieulinares/mdlib` fork. Restore
  the upstream URL after the mdlib changes are available there.

Publish mdlib before committing VIAMD's updated gitlink. For upstream integration, merge mdlib
first, restore the upstream URL, and pin the resulting upstream commit (which may have a new
hash after a squash or rebase merge).

## Testing

- The full `md_unittest` (855 tests) and `viamd_test` (120 tests) suites passed before moving
  the Amber fixtures into mdlib.
- After the move: all 5 Amber energy tests and all 13 VIAMD loader tests pass.
- Real files checked by hand in the GUI:
  - `mdout`: Zenodo 14673608, `pmemd` 18, with its trajectory.
  - `mden`: the CAR example, `pmemd.cuda`, with its trajectory.
- Real trajectories also checked: MDAnalysis `ace_tip3p`, periodic, and cpptraj `trpzip2`,
  nonperiodic.

## Known limitations

- The structure must come from a separately loaded PDB with the trajectory's atom order. Native
  `.prmtop`/`.rst7` support is future work.
- Not supported yet: NetCDF-4, legacy `.mdcrd`, minimization output, and restarted runs
  concatenated in one energy file.

Details: [docs/amber-support.md](docs/amber-support.md#limitations-and-future-work).
