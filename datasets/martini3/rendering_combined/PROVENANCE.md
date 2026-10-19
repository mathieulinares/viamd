# SID1/dsRNA in a constructed paired POPC bilayer

**Superseded:** these are historical source/build artifacts. The main scenes
and TPR no longer include this constructed membrane. They now use the final
simulated SND3/SEC61 snapshots from Zenodo 16745188; see
[`../rendering_deposited/PROVENANCE.md`](../rendering_deposited/PROVENANCE.md).
Running this legacy builder does not update the main scenes.

## Sources

- RCSB PDB **8XC1**, downloaded from
  <https://files.rcsb.org/download/8XC1.pdb>, 2026-10-16.
  Gong et al., *Structural basis for double-stranded RNA recognition by SID1*,
  Nucleic Acids Research (2024), <https://doi.org/10.1093/nar/gkae395>.
  `orientation.json` locks the retained PDB SHA-256 and the exact coordinate
  transform. The source's protein chains A/B and RNA chains C/D/E/F are used.
  Each RNA chain has 30 resolved nucleotides; no unresolved terminal
  nucleotides are fabricated.
- UniProt **Q9GZC8**, predicted transmembrane-helix ranges from
  <https://rest.uniprot.org/uniprotkb/Q9GZC8.json?fields=accession,ft_transmem>.
  The retained JSON gives the exact ranges used. Source protein author
  residue numbers match the UniProt positions in these modeled helices.
- RCSB Chemical Component Dictionary **ASP, LYS, LEU**, retained CIF files
  downloaded from `https://files.rcsb.org/ligands/download/<ID>.cif`.
  Only missing sidechain atoms are added; observed atoms are not moved by
  reconstruction. The ideal component is rigidly fitted to observed
  N/CA/C/CB atoms. This repairs unresolved geometry, not missing sequence.
- POPC chemistry: PubChem **CID 5497103**, original SDF retained as
  `upstream/popc.sdf`. The atomistic template is the same full-chain,
  explicit-hydrogen POPC conformer in `../../rendering_all_atom_all_lipids.gro`.
  Single-bond acyl torsions are extended to trans; cis double bonds are
  preserved. The published Martini template/parameters are taken unchanged
  from `../rendering_lipids/`; see that directory's provenance and licenses.
- Proteins: martinize2/vermouth **0.15.0**, `martini3001`. Secondary structure
  comes from HELIX/SHEET records in 8XC1. Six molecular ITPs correspond to
  the three contiguous resolved fragments in each of the two SID1 chains.
- RNA: Yangaliev and Ozkan, *Coarse-grained RNA model for the Martini 3
  force field*, Biophys. J. (2025), <https://doi.org/10.1016/j.bpj.2025.07.034>.
  Mapper and parameter source:
  <https://github.com/DanYev/Martini-3-DNA-RNA>,
  commit `76f0c2e5de914927bb7d771175cd721559afa960`. Retained sources and
  GPL-3.0 license are in `../rendering_nucleic/`. Four canonical RNA strands
  are merged into one topology with the authors' elastic network; they are
  not joined by invented covalent bonds.

## Geometry and scope

The biological protein/RNA pose is retained by a single rigid transform.
The 22 modeled transmembrane helices define the orientation: the dominant
axis of their direction dyadics becomes z, their mean center defines the
membrane midplane, and the extracellular domain points toward positive z.
The panel center is `(90,23,9)` nm. `orientation.json` records this transform
and every reconstructed sidechain atom.

The POPC membrane is **newly constructed, not a deposited biological membrane**.
Upper and lower leaflets use an 18-by-18 candidate grid, 0.85 nm spacing,
with a half-grid stagger. Lipid full-atom/full-bead centroids share exactly
the same sites in AA/CG. Copies that clash in either representation are
excluded from both. Thresholds: 0.22 nm AA heavy atom/protein-RNA separation,
0.45 nm CG lipid/protein-RNA separation, 0.15 nm inter-lipid AA heavy-atom
separation, and 0.35 nm inter-lipid CG separation before GRO rounding.
There are **228 paired POPC copies**, at least 80 in each leaflet.
This open, unrelaxed finite patch is suitable for rendering tests, not
production membrane simulations. There is no water, salt or equilibration.
AA and CG lipid conformers are independently constructed representations
of POPC, not a claimed bead-by-bead mapping of membrane AA coordinates.

Glycans, zinc and individually resolved bound lipids are omitted from both
paired complex representations. The artificial membrane is POPC-only.
The standalone `complex_aa.gro` contains protein/RNA only; the main scene
adds `membrane_aa.gro`/`membrane_cg.gro` alongside the mapped complex.

## Reproduction

Ordinary assembly uses retained GRO and ITP inputs and requires no mapper:

```sh
node datasets/martini3/assemble_rendering.mjs
```

To regenerate only the paired membrane, use a Python environment with NumPy,
SciPy and RDKit:

```sh
python datasets/martini3/rendering_combined/build.py
```

For the complete protein/RNA remapping, additionally provide martinize2 and
the retained RNA mapper. Use an explicitly resolved Python interpreter for
both the driver and `--python` when working outside an activated environment:

```sh
python datasets/martini3/rendering_combined/build.py --prepare \
  --work /tmp/viamd-sid1-mapping \
  --martinize /absolute/path/to/martinize2 \
  --rna-mapper "$PWD/datasets/martini3/rendering_nucleic/upstream/martinize_rna_v3.0.0.py" \
  --python /absolute/path/to/python
```

GROMACS preprocessing of the full panel succeeds without warning overrides.
A zero-step energy/force evaluation is finite. This checks topology/geometry
sanity, not suitability for dynamics or minimization convergence.
