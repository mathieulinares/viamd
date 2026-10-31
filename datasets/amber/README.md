# Amber NMA smoke-test dataset

This is a small, single-system Amber example for exercising VIAMD's Amber
topology, coordinate, trajectory, and energy loaders together.

## Files

- `nma.pdb`: initial coordinates in the same 12-atom order as the topology and trajectory.
- `nma.prmtop`: Amber topology for two residues (12 atoms).
- `nma.inpcrd`: matching nonperiodic restart coordinates.
- `nma.nc`: 20-frame, 5 ps Amber NetCDF trajectory, sampled every 0.25 ps.
- `nma.mdout`: Amber energy output from the same NMA simulation.
- `nma.out`: byte-for-byte copy of `nma.mdout`, for testing `.out` content detection.
- `nma.mden`: an `mden`-format table reconstructed from the matching `mdout` samples
  at the trajectory frame times. It is included to exercise both supported energy
  readers; it was not emitted by the original run.
- `nma.mdin`: the simulation input associated with the source run.
- `AmberClassic-LICENSE.txt`: the license supplied with the source project.

The `.nc` trajectory was converted from the source run's legacy Amber trajectory,
which is not included. The PDB was generated from `nma.inpcrd` and `nma.prmtop`;
its atom names, residue grouping, and atom order follow the topology.

## Using the example

For the existing PDB-based workflow, load `nma.pdb`, then `nma.nc`, and import
either `nma.mdout`, `nma.out`, or `nma.mden` as the run's energy series.

For native topology support, load `nma.prmtop` and choose `nma.inpcrd` as its
coordinates. Alternatively, choose `nma.nc`; VIAMD uses its first frame as the
initial state and opens the trajectory as the run.

The PDB-first route is also supported: load `nma.pdb`, apply `nma.prmtop` to add
Amber topology metadata, then open `nma.nc`. VIAMD checks the atom and residue
names and boundaries before applying the topology; the loaded coordinates are kept.
The PDB, topology, trajectory and energy file can also be dropped together.

## Provenance

The topology, restart, input, legacy trajectory, and energy output originate
from the AmberClassic NMA test at
[`test/paramfit/write_qminput_nma/mdcrd_calc`](https://github.com/Amber-MD/AmberClassic/tree/76b00b2e9d7dc41ab1ce7c7aa2fe52b3608ec002/test/paramfit/write_qminput_nma/mdcrd_calc)
at commit `76b00b2e9d7dc41ab1ce7c7aa2fe52b3608ec002`. AmberClassic's license is
included in this directory.
