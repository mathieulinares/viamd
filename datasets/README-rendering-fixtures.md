# Rendering fixtures

## Main paired files

- `rendering_all_atom.gro`: 183,916 atoms.
- `rendering_martini_cg.gro`: 39,888 beads.
- `rendering_martini_cg.tpr`: the **bonded** Martini topology for the entire CG
  scene, not the former bondless generic-P5 container.
- `rendering_all_atom_all_lipids.gro`: preserves the full 7,449-atom AA panel,
  including all 17 original lipid examples.

The original isolated section contains one example each, in this exact order:
**CHL, POPC, DPPC, DOPC, POPE, DOPE, POPS, POPG, POPA, SPM, TOG, CER**.
The lipid centers occupy the same spatial slots in both files. Cholesterol is
separate on the left; the other examples retain their original positions in
the four-column AA grid. Slots for excluded lipids are left empty rather than
silently shifting the remaining examples.

Both main files now also contain a deposited SND3/SEC61/membrane example
on the right, plus four matched isolated ligands. The original isolated
examples retain their coordinates and order.

## Deposited simulated membrane-protein example

The previous SID1 complex in a constructed regular POPC patch has been
**replaced**, not randomized or minimized into a different artificial patch.
The new system comes from [Zenodo 16745188](https://zenodo.org/records/16745188),
Yang, Mukherjee, Langer, Hummer and McDowell, *SND3 is the membrane insertase
within a fungal multipass translocon*,
<https://doi.org/10.1101/2025.07.08.663624>. The deposit supplies both
CHARMM36m all-atom and Martini 3 simulations with molecular topologies.
Its CC-BY-4.0 license, attribution, archive checksum and retained source
checksums are documented in `martini3/rendering_deposited/PROVENANCE.md`
and `manifest.json`.

We use the **final AA replica-1 snapshot** and the **10-microsecond CG
snapshot**, not the initial INSANE membrane grid. Both include the same
six-chain, 1,246-residue protein assembly and a simulated, disordered
POPC/POPE/POPI bilayer. No new lipids, idealized tails, sidechains or
nucleic acids are constructed for this block.

| Deposited block | AA | Martini 3 |
|---|---:|---:|
| POPC copies | 612 | 1,442 |
| POPE copies | 432 | 1,037 |
| POPI copies | 156 | 375 |
| Total lipids | 1,200 | 2,854 |
| Protein + lipid atoms/sites | 177,191 | 38,255 |

The AA and CG simulations have **different membrane sizes and lipid counts**;
they are corresponding systems, not one-to-one lipid copies or atom-to-bead
mappings of one snapshot. This exception to the isolated lipid panel's exact
pairing was explicitly approved. **Neither deposited simulation contains
RNA or DNA**; the original isolated DNA/tRNA examples remain in both scenes.

Periodic molecules are made whole using the deposited molecular connectivity.
The AA assembly is rigidly fitted to the CG protein using all 1,246 CA/BB
pairs (backbone RMSD 0.896 nm), and the complete membrane moves with it.
Only periodic image choices and one rigid transform per representation
change; local conformations and lipid disorder are preserved. The displayed
membrane center is `(97, 25, 12)` nm in a `122 x 52 x 36` nm scene.
Water, ions and velocities are omitted for rendering; all protein chains and
all lipids are kept.

CG protein and lipid parameters are copied from the deposit without
reparameterization, with only molecule-type names prefixed `DEP_` to avoid
collisions with the isolated examples. The deposited Martini base force
field is byte-identical to the existing panel's base force field.
Retained snapshots, bonded parameters and the offline reproducible importer
are in `martini3/rendering_deposited/`. The old `rendering_combined/`
sources are historical and no longer consumed by the assembler.

## Four matched ligand examples

The same four neutral chemical species are displayed in the same order:

| AA/CG label | Ligand | AA atoms | CG sites | Center / nm |
|---|---|---:|---:|---|
| CAFF | Caffeine | 24 | 7 | 78, 3, 4 |
| CLPR | Chlorpropham | 26 | 5 | 84, 3, 4 |
| INDO | Indole | 16 | 5 | 90, 3, 4 |
| BZTA | Benzothiazole | 14 | 5 | 96, 3, 4 |

These span methylxanthine, flexible chlorinated carbamate, pyrrolic and
sulfur/nitrogen heteroaromatic chemistry. They are ligand/rendering examples,
not four approved pharmaceutical drugs or ligands bound to the membrane complex.
Corresponding AA structures, optimized CG coordinates, author mappings and
bonded Martini parameters come from the published official M3 small-molecule
library, pinned to `9a630c18934be270b4f77189722650f4397e4ef2`.
See `martini3/rendering_ligands/PROVENANCE.md` for licenses, citations and the
documented omission of one hydrogen in the author's indole mapping.
The original atomistic geometry and published CG parameters are preserved;
only rigid alignment/translation and collision-safe molecule names are used.

These are spatial rendering examples, **not an equilibrated simulation
mixture**. The new TPR has genuine molecule-specific bead types, masses,
charges, bonds, constraints, angles, dihedrals, exclusions and virtual sites
where the source models define them. No guessed distance-based bonds or
generic-P5 placeholders are used. Some Martini connections are constraints or
virtual-site constructions rather than entries in `[ bonds ]`; a renderer
must support these interaction classes as appropriate.

### Known VIAMD TPR rendering limitation

The user reports that the CG GRO renders, but the TPR does not work correctly
in their current VIAMD build. Its TPR loader creates bead types from topology
metadata without assigning the Martini backbone/sidechain roles assigned by
the GRO loader. Component classification then logs that amino-acid and
nucleotide backbones cannot be mapped. GROMACS validation and a successful
TPR-to-GRO round-trip do **not** establish correct rendering in that build.

Use `rendering_martini_cg.gro` for the working coordinate-only rendering path.
The TPR is retained with genuine bonded parameters and original bead names;
it is not rewritten with misleading atomistic labels to bypass the loader.
Application-code changes were explicitly declined for this fixture task.

## Molecular sources and limitations

The structures are ubiquitin (RCSB PDB 1UBQ), OmpA (1QJP), glycophorin A dimer
(1AFO, model 1), DNA (1BNA), and tRNA (1EHZ).

Proteins were rebuilt with `martinize2`/vermouth 0.15.0 using `martini3001`.
Secondary-structure assignments come from the source PDB HELIX/SHEET records.
OmpA has unresolved sequence gaps and is represented as four separate
fragments, without inventing peptide bonds across missing residues. The two
glycophorin A chains remain separate molecules. Generated ITP/GRO inputs and
secondary-structure strings are retained in `martini3/rendering_proteins/`.

RNA uses the model of Yangaliev and Ozkan, *Coarse-grained RNA model for the
Martini 3 force field*, Biophysical Journal (2025),
<https://doi.org/10.1016/j.bpj.2025.07.034>. The DNA model is the same authors'
**released alpha extension, explicitly not a peer-reviewed DNA model**.
Using this alpha model was explicitly approved for the rendering fixture.
Both are from <https://github.com/DanYev/Martini-3-DNA-RNA>, pinned to commit
`76f0c2e5de914927bb7d771175cd721559afa960`. Molecular ITPs, custom nonbonded
parameters and mapped GRO coordinates are retained in `martini3/rendering_nucleic/`.
The upstream README declares GPL-3.0; the license is included there.

The RNA model supports canonical bases, not the chemical modifications in
1EHZ. Its 14 modified residues are modeled as canonical parent bases;
`martini3/rendering_nucleic/rna_parent_bases.json` records every substitution.
This is not a parameterization of modified tRNA bases. Unparameterized
crystallographic manganese ions are not carried into the new CG scene.

Lipids use the published Martini 3 Lipidome and cholesterol parameters;
`martini3/rendering_lipids/PROVENANCE.md` documents the pinned sources,
attribution, residue-label normalization, bead counts and limitations.
Coordinates were constructed by fitting the published bonded geometry,
including virtual-site placement, not copied from the former display
approximations. The sources' molecular parameters were not replaced with
invented values.

Exact verified models were not available here for the AA cardiolipin, LPC,
glucocerebroside, galactocerebroside and ganglioside examples. Those five
remain in `rendering_all_atom_all_lipids.gro`, but are excluded from **both**
main paired files. In particular, published TOCL/GM3 models are not mislabeled
as chemically different AA examples.

The scene is unsolvated, has net charge -444 e, and is not neutralized or
equilibrated. DNA and RNA have their authors' elastic networks. The
zero-step steepest-descent TPR is for topology/rendering validation, not
production dynamics or a claim of force-field compatibility for a real
mixed biomolecular simulation.

## Rebuilding and validation

From the repository root:

```sh
# Optional: regenerate published lipid conformers from the retained sources.
node datasets/martini3/rendering_lipids/build.mjs
node datasets/martini3/rendering_ligands/build.mjs
# Reimport final simulated snapshots from the retained Zenodo sources.
# Node.js only; no membrane construction, remapping or Python dependencies.
node datasets/martini3/rendering_deposited/build.mjs

# Assemble retained molecular GRO/ITP inputs and regenerate the paired AA file.
node datasets/martini3/assemble_rendering.mjs

cd datasets/martini3
gmx grompp -f rendering_visualization.mdp \
  -c ../rendering_martini_cg.gro \
  -p rendering_visualization.top \
  -o ../rendering_martini_cg.tpr
cd ../..

node --test datasets/martini3/validate_rendering.test.mjs \
  datasets/martini3/rendering_lipids/build.test.mjs \
  datasets/martini3/rendering_ligands/build.test.mjs \
  datasets/martini3/rendering_deposited/build.test.mjs
```

The tests check published lipid parameter fidelity and checksums, exact AA/CG
lipid order and placement, bonds/constraints/angles/dihedrals/virtual sites
**inside the compiled TPR**, absence of isolated beads, and a TPR-to-GRO
label/coordinate round-trip. GROMACS 2026.1 preprocessing succeeds without
warning overrides. A zero-step GROMACS energy evaluation produces finite
bonded/nonbonded energies and forces; this is not an equilibration test.
The 18 tests also verify final-snapshot provenance, deposited protein/lipid
inventories, every imported coordinate modulo periodic images and rigid
alignment, molecular integrity, membrane roughness and distinct tail
conformations, and unchanged ligand parameters/source geometry.
`martini3/rendering_layout.json`
records section offsets and counts.
The zero-step sanity test additionally requires absolute potential energy
below 10 million kJ/mol and maximum force below 10 million kJ/mol/nm to reject
catastrophic overlaps; these loose bounds are not equilibration criteria.

`prepare_rendering_sources.mjs` can reconstruct temporary AA protein/DNA/RNA
PDB mapping inputs from the original AA archive. Its output directory must contain
the original 1UBQ, 1QJP and 1AFO PDB files for HELIX/SHEET annotations.
Final structure deliverables are GRO and TPR, not PDB.

## Atomistic lipid sources

The atomistic lipid display conformers were generated from these PubChem CIDs.
They are not simulation-prepared geometries or a combined membrane.

| Lipid | AA label | PubChem CID | In main paired files |
|---|---|---:|---|
| Cholesterol | CHL | 5997 | Yes |
| POPC | POPC | 5497103 | Yes |
| DPPC | DPPC | 452110 | Yes |
| DOPC | DOPC | 10350317 | Yes |
| POPE | POPE | 5283496 | Yes |
| DOPE | DOPE | 9546757 | Yes |
| POPS | POPS | 5283499 | Yes |
| POPG | POPG | 5283509 | Yes |
| POPA | POPA | 5283523 | Yes |
| Cardiolipin | CDL | 166177218 | No |
| Sphingomyelin | SPM | 9939941 | Yes |
| LPC | LPC | 460602 | No |
| Triolein | TOG | 5497163 | Yes |
| Ceramide | CER | 5283564 | Yes |
| Glucocerebroside | GLC | 20057358 | No |
| Galactocerebroside | GAL | 52949283 | No |
| Ganglioside | GNG | 163110884 | No |

## Separate ubiquitin-water example

`martini3/ubiquitin_water.tpr` is the older, separate Martini 3 ubiquitin/water
system, not the full rendering panel. Its minimization converged; minimized
coordinates are in `martini3/ubiquitin_water_minimized.gro`. Its unchanged
topology/settings are alongside it. Regenerate from `datasets/martini3` with:

```sh
gmx grompp -f minimization.mdp -c ubiquitin_water.gro \
  -p system.top -o ubiquitin_water.tpr
```
