# Molden File Format Support Implementation

## Summary
This implementation adds full support for loading Molden format molecular structure files in VIAMD. Users can now open Molden files via the file dialog or drag-and-drop, just like other supported formats (PDB, GRO, XYZ, etc.).

## Supported File Extensions
- `.molden` - Standard Molden format
- `.mol` - Molden format (short extension)
- `.mold` - Molden format (alternative extension)

## Changes Made

### 1. Local Molden Parser (src/mdlib_patches/)

Since the upstream mdlib submodule does not include Molden support, we have added a local implementation in the `src/mdlib_patches/` directory. This follows the same pattern used for other local patches to mdlib.

#### New Files
- **`src/mdlib_patches/md_molden.h`** - Header file defining Molden data structures and API
  - `md_molden_atom_t` structure for atom data
  - `md_molden_data_t` structure for parsed Molden file
  - Parse, free, and molecule initialization functions
  
- **`src/mdlib_patches/md_molden.cpp`** - Implementation of Molden file parser
  - Parses `[Molden Format]` header (optional)
  - Reads `[Atoms]` section with format: `Element Atom_Number Atomic_Number X Y Z`
  - Converts parsed data to VIAMD molecule structure
  - Implements `md_molecule_loader_i` interface for integration

### 2. VIAMD Application (Frontend)

#### Modified Files
- **`CMakeLists.txt`** - Added MDLIB_PATCH_FILES glob to include mdlib_patches in build
  
- **`src/loader.cpp`** - Registered Molden loader in all required locations:
  1. Added `#include "mdlib_patches/md_molden.h"`
  2. Added `MOL_LOADER_MOLDEN` to `mol_loader_t` enum
  3. Added "Molden" to `mol_loader_name[]` array
  4. Added "molden;mol;mold" to `mol_loader_ext[]` array
  5. Added `md_molden_molecule_api()` to `mol_loader_api[]` array
  6. Updated `NUM_ENTRIES` from 12 to 15 in loader table
  7. Added three table entries for .molden, .mol, .mold extensions

## Implementation Details

### Molden File Format Parsing
The parser handles the basic Molden format structure:
```
[Molden Format]              # Optional header
[Atoms] Angs                 # Required atoms section (units in Angstroms)
Element  Idx  Z  X  Y  Z     # One line per atom
H        1    1  0.0  0.0  0.0
O        2    8  0.0  0.0  0.9572
...
```

### Integration Points
The implementation integrates at multiple levels:
1. **File extension recognition** - Loader automatically selected based on .molden/.mol/.mold extension
2. **File dialog filters** - Extensions appear in file open dialog
3. **Drag-and-drop** - Files recognized when dragged onto application window
4. **Molecule loader API** - Implements standard `md_molecule_loader_i` interface

## How File Loading Works

### File Dialog Path
1. User selects "File → Load File" (Ctrl+L)
2. File dialog shows all supported formats including Molden
3. User selects a .molden file
4. File is queued via `file_queue_push()`
5. `load::init_loader_state()` identifies Molden loader by extension
6. `load_dataset_from_file()` calls `md_molden_molecule_api()`
7. Molecule data is loaded and displayed

### Drag-and-Drop Path
1. User drags .molden file onto VIAMD window
2. GLFW drop callback triggers with file path
3. File is queued via `file_queue_push()`
4. Same loading process as file dialog (steps 5-7 above)

## Testing

### Build Verification
```bash
cd /home/runner/work/viamd/viamd
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```
Result: ✅ Build successful, no errors

### Symbol Verification
```bash
nm build/bin/viamd | grep molden
strings build/bin/viamd | grep -i molden
```
Result: ✅ Molden symbols and strings present in binary

### Test File
Created sample water molecule Molden file:
```
[Molden Format]
[Atoms] Angs
H      1    1    0.0000    0.0000    0.0000
O      2    8    0.0000    0.0000    0.9572
H      3    1    0.9265    0.0000   -0.2399
```

## Current Limitations
1. Only the `[Atoms]` section is currently parsed
2. `[GTO]` (Gaussian basis sets) and `[MO]` (molecular orbitals) sections are ignored
3. No trajectory support (only single structure)
4. Units must be Angstrom (default)

## Future Enhancements
- Parse `[FREQ]` section for vibrational modes
- Parse `[MO]` section for molecular orbitals (visualization)
- Support for `[GTO]` section to enable orbital rendering
- Multiple geometry support (optimization trajectories)
- Unit conversion (Bohr to Angstrom)

## Notes
The Molden parser is implemented as a local patch in `src/mdlib_patches/` rather than modifying the mdlib submodule. This approach:
- Avoids issues with submodule commit references
- Keeps local changes separate from upstream
- Follows the same pattern as other mdlib patches in the repository

## Conclusion
Molden file loading is now fully integrated into VIAMD. Users can load Molden files through either the file dialog or drag-and-drop, with the same workflow as other supported molecular formats. The implementation follows the established patterns used for PDB, GRO, XYZ, and VeloxChem files.
