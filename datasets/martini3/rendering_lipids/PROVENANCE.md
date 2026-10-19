# Published Martini 3 lipid rendering inputs

**Coverage: 12 of the requested 17 examples, 142 beads. CDL, LPC, GLC
(glucocerebroside), GAL (galactocerebroside), and GNG are unsupported here.**
They have no placeholder atoms, no fabricated topology, and no entries in
`molecules.top`. Their original grid positions remain empty. This is deliberately
not advertised as a complete 17-lipid fixture. Published TOCL and GM3 were
initially selected as class representatives, but were removed from the active
fixture after the requirement was clarified to prohibit chemically nonmatching
substitutes for the actual AA residues.

## Molecules and AA correspondences

The GRO residue numbers are the original 1-based example indices. Residue names
match the AA labels; GROMACS molecule names retain the published identifiers.
These are class-level correspondences, not an atom-to-bead mapping of the AA
coordinates. Generic classes are made chemically explicit below.

| Slot | AA label | Published molecule | Beads | Representative |
|---:|---|---|---:|---|
| 1 | CHL | CHOL | 9 | Cholesterol |
| 2 | POPC | POPC | 12 | 16:0/18:1 PC |
| 3 | DPPC | DPPC | 12 | 16:0/16:0 PC |
| 4 | DOPC | DOPC | 12 | 18:1/18:1 PC |
| 5 | POPE | POPE | 12 | 16:0/18:1 PE |
| 6 | DOPE | DOPE | 12 | 18:1/18:1 PE |
| 7 | POPS | POPS | 12 | 16:0/18:1 PS |
| 8 | POPG | POPG | 12 | 16:0/18:1 PG |
| 9 | POPA | POPA | 11 | 16:0/18:1 PA |
| 10 | CDL | **not provided** | 0 | AA cardiolipin is not tetraoleoyl cardiolipin |
| 11 | SPM | PSM | 12 | Palmitoyl sphingomyelin |
| 12 | LPC | **not provided** | 0 | Lysophosphatidylcholine |
| 13 | TOG | TO | 15 | Triolein |
| 14 | CER | PCER | 11 | Palmitoyl ceramide |
| 15 | GLC | **not provided** | 0 | Glucocerebroside |
| 16 | GAL | **not provided** | 0 | Galactocerebroside |
| 17 | GNG | **not provided** | 0 | AA oxygenation differs from GM3 |

`manifest.json` supplies the same order, source URLs, charges, topology-record
counts, placement centers, and geometric fitting residuals in machine-readable
form. The downloaded GM3 topology remains as a documented alternative, not an
active molecule or an unspecified common ganglioside surrogate.

## Pinned sources and attribution

Downloaded 2026-10-16. `upstream/` contains unmodified source files;
`SHA256SUMS` records their SHA-256 checksums.

### Lipidome parameters

Repository:
<https://github.com/Martini-Force-Field-Initiative/M3-Lipid-Parameters>

Commit: **2121527045696345069deb4d3df754724888a4f9**

Download pattern:
`https://raw.githubusercontent.com/Martini-Force-Field-Initiative/M3-Lipid-Parameters/2121527045696345069deb4d3df754724888a4f9/ITPs/<filename>`

Files: `martini_v3.0.0_ffbonded_v2.itp`,
`martini_v3.0.0_phospholipids_{PC,PE,PS,PG,PA,CL,SM}_v2.itp`,
`martini_v3.0.0_triglycerides_v2.itp`,
`martini_v3.0.0_ceramides_v2.itp`, and
`martini_v3.0.0_sterols_v1.itp`.
The repository README and Apache-2.0 license are retained alongside them.

Cite K. B. Pedersen et al., **The Martini 3 Lipidome: Expanded and Refined
Parameters Improve Lipid Phase Behavior**, *ACS Central Science* (2025),
<https://doi.org/10.1021/acscentsci.5c00755>.
The published release explicitly states that the open-beta parameters and final
publication parameters are identical.

The cholesterol file contains the published nine-bead, virtual-site model
originally developed by L. Borges-Araújo et al.,
**Martini 3 Coarse-Grained Force Field for Cholesterol**, *JCTC* (2023),
<https://doi.org/10.1021/acs.jctc.3c00547>.
Its upstream header still cites the earlier preprint; the journal DOI above
supersedes that citation. The primary sterol repository was also inspected at
commit `421efdaecfe54be5efd2076f9c7c706e79746ecb`:
<https://github.com/Martini-Force-Field-Initiative/M3-Sterol-Parameters>.
The vendored copy is from the Apache-licensed Lipidome distribution.

### GM3 carbohydrate-publication model

F. Grünewald, M. H. Punt, E. E. Jefferys, P. A. Vainikka, M. König,
V. Virtanen, T. A. Meyer, W. Pezeshkian, A. J. Gormley, M. Karonen,
M. S. P. Sansom, P. C. T. Souza, and S. J. Marrink,
**Martini 3 Coarse-Grained Force Field for Carbohydrates**, *JCTC* **18**
(2022), 7555–7569, <https://doi.org/10.1021/acs.jctc.2c00757>.

The article describes GM3 and Gb3. The current official force-field collection
does not contain its GM3 topology, so `gm3_final.itp` is obtained from the
CCPBioSim aiida-gromacs tutorial's explicitly cited copy:

<https://raw.githubusercontent.com/CCPBioSim/aiida-gromacs/fcc507a98e0b4a005c73ea8055287c001f432cd2/examples/PTH2R_coarse-grained_files/gromacs/gm3_final.itp>

Mirror commit: **fcc507a98e0b4a005c73ea8055287c001f432cd2**.
The original topology header, author/publication citations, and the mirror's MIT
license (copyright James Gebbie-Rayet) are retained. This is a secondary mirror
of the published model, not a newly parameterized glycolipid. Its source header
specifies original Martini 3 tails and the carbohydrate model's aggregation-
optimized linker/mapping; those differences from the newer Lipidome sphingolipid
mapping have **not** been "harmonized" by inventing replacement parameters.

All active molecules use the existing `../martini_v3.0.0.itp` base force field:
P. C. T. Souza et al., **Martini 3: a general purpose force field for
coarse-grained molecular dynamics**, *Nature Methods* (2021),
<https://doi.org/10.1038/s41592-021-01098-3>.
Every selected bead type exists in that file. Newer Lipidome bonded macros are
required, but no base nonbonded parameters are replaced.

## Explicitly unsupported species and nearest legitimate alternatives

- **CDL:** the actual AA residue contains C81 H141 O17 P2. Published TOCL
  represents tetraoleoyl cardiolipin (neutral formula C81H150O17P2; PubChem
  CID 46931152, <https://pubchem.ncbi.nlm.nih.gov/compound/46931152>).
  Protonation cannot account for nine missing hydrogens. The pinned Lipidome
  collection offers TOCL and TMCL, not a topology matching this AA composition.
  Neither is silently relabeled CDL. The TOCL source is retained only as a
  legitimate, chemically different alternative.
- **GNG:** the actual AA residue contains C59 H108 N2 O22. The located
  published GM3 model has the conventional N-acetylneuraminic-acid GM3
  carbohydrate headgroup; the AA oxygenation is not established to match this
  model (e.g. a hydroxylated ceramide or N-glycolyl sialic acid requires a
  distinct mapping). GM1/GM2 also have different headgroups. Until the exact
  AA ganglioside identity and matching published topology are established,
  no relabeled GM3 substitute is included.
- **LPC:** no published lysophosphatidylcholine model was found in the pinned
  official Martini 3 lipid collection. Its published generator explicitly marks
  lyso linkers as unsupported. The older official `LPPC` is a **two-tail**
  phosphatidylcholine (its header specifies C12:0/16:0), not lyso-PC.
  Third-party files named `martini_v3.0_PPC_02.itp` were rejected because their
  own headers say generated according to Martini **2.0**, untested.
  A legitimate Martini 2 PPC/lyso-PC model is a different force-field option,
  not an approved Martini 3 substitution.
- **GLC / GAL:** the official Martini 3 carbohydrate collection's `GLC` and
  `GAL` are standalone glucose and galactose, **not** glucocerebroside or
  galactocerebroside. No directly published, license-attributed Martini 3
  monoglycosylceramide topology for either was located. Published Martini 3
  **GM3 / Gb3** are related glycosphingolipids but have different headgroups;
  **PCER** is an available ceramide without a sugar. None is substituted into
  these two slots. Martini 2 glycosphingolipid models are another legitimate
  but force-field-incompatible alternative.

Search scope included the pinned official M3 lipid, sterol, and force-field
repositories, their published generator and carbohydrate parameter collection,
the carbohydrate paper/SI, and public topology repositories. These statements
describe the inputs located and validated here, not proof that no parameter
set exists anywhere.
The official `martini-forcefields` collection was inspected at commit
`47aa24712f1c51ba6db1c6b8edf92d0e4443a079`; the Lipidome generator's
`tools/lipid-itp-generator-Martini3-01.py` at the Lipidome commit above contains
the explicit `lyso linker not supported yet` messages.

## Coordinates, adaptations, and reproduction

Coordinates are **generated idealized templates**, not published experimental
or equilibrated membrane coordinates. `build.mjs` embeds each full published
bond/constraint/angle/dihedral graph deterministically with finite-difference
L-BFGS. All original parameters, constraints, exclusions, dihedral restraints,
and virtual-site definitions are retained in the selected topology.
Cholesterol uses its original `virtual_sites3` type-4 construction; GM3 uses
its original equal-weight `virtual_sitesn` construction. GRO coordinates
include virtual beads in the original atom order.

Every bead is placed; the arithmetic full-bead centroid is translated to the
arithmetic full-atom centroid of its matching residue in
`datasets/rendering_all_atom_all_lipids.gro`. Thus cholesterol remains separate on the left
and the 16 ordered lipid slots retain the AA fixture's **4 columns × 4 rows**,
not a five-column grid. Unsupported positions remain empty. Exact unrounded
AA centroids, AA residue identifiers/counts, and the AA source SHA-256 are
recorded in `manifest.json`. The AA box is copied verbatim. GRO rounding is
balanced over each molecule, placing its centroid within `0.0005 / bead_count`
nm of the exact AA centroid, the closest representable value at GRO precision.
Intermediate lipid residue indices retain the unsupported gaps. The final
main paired files contain only the 12 available entries; all 17 original AA
examples are preserved in `datasets/rendering_all_atom_all_lipids.gro`.

`selected_lipids.itp` extracts the selected molecule blocks and changes only
the `[atoms]` residue-label column to the AA labels. Molecule names, atom
names/types/order, charges, masses, connectivity, and all parameters remain
unchanged. Original complete source files are preserved, including their
headers/citations. This residue-label adaptation and coordinate generation are
the only model modifications.

Active examples fit their unrounded bond/constraint/angle targets to numerical
precision (maximum bond/constraint fitting error below 0.00001 nm).
GRO rounding to 0.001 nm introduces additional sub-0.002 nm distance error.
These isolated ideal templates are **not equilibrated simulation systems**.

From the repository root:

```sh
node datasets/martini3/rendering_lipids/build.mjs
node --test datasets/martini3/rendering_lipids/build.test.mjs
cd datasets/martini3/rendering_lipids
gmx grompp -f validation.mdp -c lipids_available.gro \
  -p lipids_available.top -o validation.tpr -po validated.mdp
gmx mdrun -s validation.tpr -deffnm geometry_check -nt 1 -nb cpu -nsteps 0
```

Validation used GROMACS 2026.1. Preprocessing succeeds **without `-maxwarn`**,
with 12 molecules and 142 atoms. Four explanatory notes include the expected
net charge of **−3 e** (no counterions were requested), zero-step minimization,
and the preprocessing-only cut-off settings. A zero-step CPU evaluation also
succeeds with finite potential energy, confirming
constructible constraints/virtual sites. This is an energy sanity check, not
energy minimization to convergence. Logs are retained.

## Including in a larger system

After the existing Martini 3.0.0 base include, and **before any molecule
definitions**, include:

```cpp
#include "rendering_lipids/includes.itp"
```

This inserts the Lipidome bonded definitions first, as upstream requires, then
the 12 selected molecules. Do not also include the original full lipid ITPs
defining those same molecules.

After your `[system]` section, `molecules.top` provides its own `[molecules]`
header for a standalone system. For a larger fixture with an existing
`[molecules]` section, append only its 12 molecule rows in the coordinate order
listed above, without repeating the section header. `lipids_available.top`
is a complete standalone example. Append all 142 GRO atom records in this same
order, renumbering global atom/residue identifiers as needed. Do not fill the
five unsupported slots with the old approximate CG molecules.
