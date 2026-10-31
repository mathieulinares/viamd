# Amber topology, trajectory, and energy support

Adds Amber support to VIAMD using the same pipeline as GROMACS: an Amber NetCDF trajectory loads
like an XTC/TRR, Amber `mdout`/`mden` energies load like an EDR, and a native Amber topology plus
coordinates loads like a system from TPR. The existing PDB-based workflow remains supported.
Playback, analysis, **System → Series**, Timeline/Distributions and `attr()` scripting use the
existing VIAMD pipeline.

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
  - Restarted runs concatenated in one file are published as separate `segment_N` series,
    retaining original timestamps rather than applying a guessed offset.
  - Terms that `mden` shares with `mdout` use the `mdout` names, so
    `attr("amber_energy/etot")` works with either file.
- **Native Amber systems**, from `.prmtop`/`.parm7` plus `.inpcrd`/`.rst7`, a classic
  `AMBERRESTART`, or the first frame of an Amber NetCDF trajectory.
  - Imports atom/residue metadata, charges, masses and explicit bonds.
  - When the trajectory supplies initial coordinates, VIAMD also opens it as the run.
  - A separately loaded PDB remains supported without topology; a matching `.prmtop` can also
    be applied to that structure before opening the trajectory. Atom/residue order and names
    are checked before applying topology bonds.
- **Detection**
  - Amber energy output is recognized from its contents: `.out`, `.log`, and the extensionless
    `mdout`/`mden` files.
  - **File → Open File…** now pre-selects the detected loader, and dropped files load
    directly.
  - Fixed a bug that made VIAMD treat part of a dotted directory name as the extension of an
    extensionless file.

## Changes

**mdlib** (`ext/mdlib`, `amber` branch):

- New readers: `src/md_amber_netcdf.{c,h}`, `src/md_amber_energy.{c,h}` and
  `src/md_amber_prmtop.{c,h}`.
- Build changes: CMake option, vcpkg feature and `CheckVCPKG.cmake`.
- New tests: Amber NetCDF, topology/restart loading and energy parsing/segmentation.
- New synthetic fixtures in `unittest/amber_energy_data/`, tracked with mdlib.

**VIAMD:**

- `src/loader.{h,cpp}`: adds Amber NetCDF, energy and topology loaders, plus content detection
  and the extension fix. A topology supplements an existing structure, or builds a system when
  supplied with separate coordinates.
- `src/main.cpp`: the Open dialog requests Amber coordinates for a new topology-based system,
  and explains the PDB-supplement path when a structure is already loaded; drag-and-drop can
  preselect a sole matching restart or trajectory.
- `src/viamd.cpp` and `src/file_list.cpp`: topology-coordinate dependencies are recorded in
  workspaces, a NetCDF coordinate trajectory is also opened as a run, and dropped structures,
  topology supplements, trajectories and energy files are ordered for loading. Applying topology
  refreshes chemistry, backbone data and representations while retaining an open trajectory.
- `src/components/dataset/dataset.cpp`: Amber energy files are shown as **Energies**; type
  lists and saved appearance overrides are refreshed when a topology supplements a PDB.
  Workspace overrides also distinguish types with identical names and force-field identifiers.
- Shapespace and Density Volume invalidate topology-dependent cached data and glyphs.
- `test/test_loader.cpp` and `test/test_file_list.cpp`: new loader tests, including topology
  application to a matching PDB, rejection of mismatched atom names and workspace load order.
- `docs/amber-support.md`: new documentation.
- `datasets/amber/`: a compact, single-system NMA example containing the PDB, topology, restart,
  trajectory, `mdout`, `.out` alias and an `mden` companion. See its README for provenance and
  which files were converted or derived.
- `.gitmodules`: temporarily points mdlib to the published `mathieulinares/mdlib` fork. Restore
  the upstream URL after the mdlib changes are available there.

Publish mdlib before committing VIAMD's updated gitlink. For upstream integration, merge mdlib
first, restore the upstream URL, and pin the resulting upstream commit (which may have a new
hash after a squash or rebase merge).

## Testing

- GCC 13 Release builds of VIAMD and both test targets succeed with Amber NetCDF enabled and
  disabled. The focused Amber reader, loader and workspace-file tests pass in both builds
  (51 enabled, 40 disabled).
- Automated coverage includes `.prmtop` with ASCII and classic NetCDF restart coordinates,
  applying topology to a matching PDB, workspace input order, mismatched atom names, and
  restarted energy segments. The NMA PDB, topology, trajectory and all three energy files are
  imported together, and values are compared at every trajectory frame.
- Manual GUI validation is still needed for a protein topology applied after a trajectory is
  already open, including backbone cartoons, Shapespace and Density Volume.
- vcpkg manifest-feature selection was checked in CMake for automatic and explicit toolchains,
  a custom manifest, disabled manifest mode, and an environment-supplied non-vcpkg toolchain.
- Real files checked by hand in the GUI during the earlier trajectory/energy work:
  - `mdout`: Zenodo 14673608, `pmemd` 18, with its trajectory.
  - `mden`: the CAR example, `pmemd.cuda`, with its trajectory.
- Real trajectories also checked: MDAnalysis `ace_tip3p`, periodic, and cpptraj `trpzip2`,
  nonperiodic.

## Known limitations

- PDB remains supported, but its atom order (like the order of topology coordinates) must match
  the trajectory; only atom count is checked automatically.
- CHAMBER topologies and nonstandard Amber topology variants are not supported.
- Not supported yet: NetCDF-4, legacy `.mdcrd`, minimization output, and trajectory
  velocities/forces.

Details: [docs/amber-support.md](docs/amber-support.md#limitations-and-future-work).
