# Molden Support in VIAMD

## Overview
VIAMD now supports loading and visualizing molecular structures from Molden format files (.molden, .mold). The implementation follows the same workflow as VeloxChem file support, providing seamless integration with VIAMD's visualization capabilities.

## Features

### Phase 3 Implementation
- ✅ Load molecular geometry from Molden files
- ✅ Parse [ATOMS] section (Angstrom and Atomic Units)
- ✅ Automatic bond inference
- ✅ Stereochemistry analysis
- ✅ CPK coloring
- ✅ Multiple rendering modes (CPK, ball-and-stick, licorice)
- ✅ File dialog support
- ✅ Drag-and-drop support

### Supported Format
Currently supported:
- `[Atoms]` or `[Atoms] (Angs)` - Atomic positions in Ångström
- `[Atoms] (AU)` - Atomic positions in Atomic Units (Bohr)

Format: `Label  AtomicNumber  Index  X  Y  Z`

## Usage

### Loading Files
1. Via File Menu: `File` → `Load Molecule` → Select `.molden` file
2. Via Drag-and-Drop: Drag `.molden` file onto VIAMD window
3. Via Command Line: `./viamd molecule.molden`

### Test Files
- `datasets/test/water.molden` - H2O
- `datasets/test/ethane.molden` - C2H6

## Build
Enabled by default. To disable: `cmake -DVIAMD_ENABLE_MOLDEN=OFF ..`

## Technical Details
- Backend: `ext/mdlib/src/md_molden.{h,c}` 
- Frontend: `src/components/molden/molden.cpp`
- Follows VeloxChem component architecture
- Auto bond inference and stereochemistry analysis

## References
- Molden format: http://www.cmbi.ru.nl/molden/molden_format.html
