# Martini 3 DNA/RNA Coarse-Graining Tools

Standalone scripts to convert all-atom DNA and RNA structures to coarse-grained
Martini 3 representations (`.itp` topology + `.pdb` structure) for GROMACS.

> **Note:** The DNA force field is in **alpha** — a publication is being prepared.
> It's based on the RNA version with updated backbone parameters to match B-DNA and sidechain parameters for thymine.

## Quick Start

```bash
# DNA
python martinize_dna_alpha.py -f input.pdb -os cg.pdb -ot cg.itp

# RNA
python martinize_rna_v3.0.0.py -f input.pdb -os cg.pdb -ot cg.itp
```

View all options:
```bash
python martinize_dna_alpha.py --help
python martinize_rna_v3.0.0.py --help
```

## Python API

```python
from martinize_dna_alpha import martinize_dna
from martinize_rna_v3_0_0 import martinize_rna

# DNA
pdb, itp = martinize_dna("input.pdb", output_structure="cg.pdb", output_topology="cg.itp")

# RNA
pdb, itp = martinize_rna("input.pdb", output_structure="cg.pdb", output_topology="cg.itp")
```

## Dependencies

- Python ≥ 3.9
- NumPy

## Citation

> Yangaliev D, Ozkan SB. Coarse-grained DNA model for the Martini 3 force field.
> *Biophys J*. 2025 Aug 5:S0006-3495(25)00483-7.
> doi: [10.1016/j.bpj.2025.07.034](https://doi.org/10.1016/j.bpj.2025.07.034)

[![DOI](https://zenodo.org/badge/944535799.svg)](https://doi.org/10.5281/zenodo.19207978)

## Other Martini 3 related work

Automated Ligand Parametrization:
 - https://github.com/DanYev/SMartini

## License

GNU General Public License v3.0

