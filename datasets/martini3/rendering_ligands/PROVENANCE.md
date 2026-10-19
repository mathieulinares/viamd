# Four isolated, exactly paired Martini 3 ligands

These are rendering/preprocessing fixtures, not equilibrated simulation systems.
The same neutral chemical species appear in this order in both GRO files:

| Slot | Residue | Identity | Formula | AA atoms (including H) | CG sites | Center / nm |
|---|---|---|---|---:|---:|---|
| 1 | CAFF | Caffeine | C8H10N4O2 | 24 | 7 | 78, 3, 4 |
| 2 | CLPR | Chlorpropham | C10H12ClNO2 | 26 | 5 | 84, 3, 4 |
| 3 | INDO | Indole | C8H7N | 16 | 5 | 90, 3, 4 |
| 4 | BZTA | Benzothiazole | C7H5NS | 14 | 5 | 96, 3, 4 |

Caffeine is a drug ligand; chlorpropham is a carbamate herbicide; indole is a
biological signaling ligand; benzothiazole is a sulfur/nitrogen heteroaromatic
bioactive scaffold. These are **not four approved pharmaceutical drugs**.
The selection spans methylxanthine, halogenated flexible carbamate, pyrrolic
heteroaromatic and thiazole chemistries without inventing unavailable drug models.

## Authoritative sources and license

All AA PDBs, OPLS-AA topologies, CG GROs, Martini 3 ITPs, SMILES and author mapping
files are verbatim files from the official
[M3-Small-Molecules repository](https://github.com/Martini-Force-Field-Initiative/M3-Small-Molecules),
pinned to commit `9a630c18934be270b4f77189722650f4397e4ef2`.
`sources.json` records the individual immutable URLs, original paths and SHA-256
hashes. `SHA256SUMS` independently lists the vendored file hashes.
Original documentation is preserved in `upstream/README.md`,
`upstream/models_README.md` and `upstream/LIBRARY.md`.

Copyright 2021 University of Groningen. Upstream is Apache-2.0; the complete
license is preserved in `upstream/LICENSE`. The original model headers and all
references remain in both the source ITPs and `selected_ligands.itp`.

References:

* Alessandri et al., *Martini 3 Coarse-Grained Force Field: Small Molecules*,
  **Advanced Theory and Simulations** 5 (2022), 2100391,
  <https://doi.org/10.1002/adts.202100391>.
* Souza et al., *Martini 3: a general purpose force field for coarse-grained
  molecular dynamics*, **Nature Methods** 18 (2021), 382–388,
  <https://doi.org/10.1038/s41592-021-01098-3>.
* Caffeine model's additional reference: Souza et al., *Protein–ligand binding
  with the coarse-grained Martini model*, **Nature Communications** 11 (2020),
  3714, <https://doi.org/10.1038/s41467-020-17437-5>.

## Construction, mapping and fidelity

`ligands_aa.gro` contains the **published corresponding LigParGen atomistic
structures**, including every hydrogen in source PDB order. No SMILES embedding,
protonation change, chemical substitution or guessed structure is performed.
The published AA ITPs validate atom names/order, composition and neutral charge;
they are attribution/identity evidence, **not included in the CG topology**.

`ligands_cg.gro` uses the **published optimized CG coordinates**, not guessed
centroids or generated beads. Only translation is applied to CG structures.
All beads retain published order/names, including massless virtual sites.
AA structures undergo a rigid rotation using frames defined by author mapping
groups B0/B1/B2 and corresponding CG sites 1/2/3; this aligns display orientation
without distorting atomistic geometry. Each AA and CG residue is independently
translated to the same arithmetic centroid (all AA atoms / all CG sites).
GRO rounding introduces at most 0.0005 nm per coordinate; centroids match within
that precision. The simulation box is 102 × 8 × 8 nm.

`manifest.json` explicitly records author AA indices for every CG bead.
For indole, the author's **refined NDX** is used: repeated indices express the
author's weighting and shared atoms are intentional. It omits AA hydrogen 11
(H0A) but covers every heavy atom; the older reverse `.map` also has outdated
hydrogen numbering. Both original files are retained unchanged and the omission
is recorded in the manifest. **Neither file is used to synthesize CG geometry**:
the published CG GRO avoids silently "repairing" the author mapping.

`selected_ligands.itp` preserves the published bead types, charges, masses,
bonds, constraints, angles, improper dihedrals, exclusions, virtual sites and
`FLEXIBLE` conditionals. The **only topology modification** is safely renaming
`[ moleculetype ]` names to `LIG_CAFF`, `LIG_CLPR`, `LIG_INDO`, `LIG_BZTA` to avoid
collisions with other libraries; residue names remain unchanged.

## Rebuild and validate

Requires Node.js 18+; GROMACS is optional for the compile test. No Python,
RDKit, npm packages, network access or AA force-field installation is required
for the normal offline rebuild.

```sh
node datasets/martini3/rendering_ligands/build.mjs
node --test datasets/martini3/rendering_ligands/build.test.mjs
# Optional: re-download every source from the pinned URLs, checking locked hashes:
node datasets/martini3/rendering_ligands/build.mjs --fetch
```

The tests verify locked source hashes, reproducibility, chemical composition,
atom/bead order, source geometry preservation, pair centroids, and complete
topology fidelity. If `gmx` is installed they run **warning-free `grompp` without
`-maxwarn`**, using `../martini_v3.0.0.itp`, and inspect the resulting TPR for
22 sites and all four molecule identities. `validation.log` records the result;
transient compile outputs are removed. No minimization/dynamics is asserted.

## Parent-panel integration contract

* Append `ligands_aa.gro` atom records to the AA panel and `ligands_cg.gro`
  atom records to the CG panel, renumbering GRO residue/atom serials as needed.
* After the base Martini 3 force-field include and before `[ system ]`, include
  `martini3/rendering_ligands/includes.itp` (relative to the parent topology).
* Append the rows in `molecules.top` **inside** the parent's `[ molecules ]`,
  at the same position as the CG GRO records.
* `ligands.top` is a standalone compile harness only; do not include it inside
  the parent topology, because it already includes the base force field.
* To move slots, change the center expression in `build.mjs`, rebuild and update
  the documented centers and corresponding tests together.
