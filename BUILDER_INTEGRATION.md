# Molecule Builder Integration in VIAMD

This document describes the molecule builder component integration in VIAMD with a lightweight implementation.

## Overview

VIAMD now includes a lightweight molecule builder component that allows users to create and visualize molecules from SMILES strings without external dependencies. This enables on-demand molecular structure generation and seamless integration with VIAMD's visualization capabilities.

## Features

### Molecule Building Capabilities
- **SMILES parsing** with custom lightweight parser for common chemical notation
- **3D structure generation** using simple geometric rules and standard bond lengths/angles
- **Hydrogen addition** based on valence rules for complete molecular representation
- **Real-time molecule generation** with immediate visualization
- **No external dependencies** - no need for RDKit or other chemistry libraries

### User Interface Features
- **Menu integration** accessible via "Builder → Molecule Builder"
- **Interactive GUI** with dark theme matching VIAMD's aesthetic
- **Example molecule library** with quick-access buttons for common structures
- **Real-time feedback** with error reporting and molecule statistics
- **Seamless VIAMD integration** for direct loading into visualization pipeline

### Built-in Example Molecules
- **Water (H₂O)**: `O`
- **Methane (CH₄)**: `C`
- **Ethanol**: `CCO`
- **Benzene**: `c1ccccc1`
- **Caffeine**: `CN1C=NC2=C1C(=O)N(C(=O)N2C)C`
- **Aspirin**: `CC(=O)OC1=CC=CC=C1C(=O)O`
- **Glucose**: `C([C@@H]1[C@H]([C@@H]([C@H]([C@H](O1)O)O)O)O)O`

## Installation

### Prerequisites
- C++20 compatible compiler
- CMake 3.20 or higher
- No external chemistry libraries required

### Building with Molecule Builder (Default)
```bash
# Clone and build VIAMD (molecule builder enabled by default)
mkdir build && cd build
cmake .. -DVIAMD_ENABLE_BUILDER=ON
make -j$(nproc)
```

### Disabling Molecule Builder
```bash
# Build without molecule builder if not needed
cmake .. -DVIAMD_ENABLE_BUILDER=OFF
```

## No External Dependencies Required

**VIAMD's molecule builder now uses a lightweight, built-in implementation** that:
- **Zero external dependencies** - no need for RDKit, Boost, or other chemistry libraries
- **Faster build times** - eliminates dependency compilation and linking
- **Simplified installation** - works out-of-the-box on any system that can build VIAMD
- **Smaller binary size** - no heavy chemistry library linking
- **Same functionality** - supports the common SMILES patterns needed for molecular visualization

**No additional packages need to be installed** - the builder is fully self-contained.

### Building VIAMD with Molecule Builder

#### Standard Build (Builder enabled by default)
```bash
mkdir build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

#### Custom Build Options
```bash
# Enable/disable builder module
cmake .. -DVIAMD_ENABLE_BUILDER=ON   # (default: ON)

# Disable builder if not needed
cmake .. -DVIAMD_ENABLE_BUILDER=OFF

# Complete build example
cmake .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DVIAMD_ENABLE_BUILDER=ON
make -j$(nproc)
```
  -DCMAKE_BUILD_TYPE=Release \
  -DVIAMD_ENABLE_BUILDER=ON \
  -DVIAMD_ENABLE_RDKIT=ON
```

### CMake Configuration Options

| Option | Default | Description |
|--------|---------|-------------|
| `VIAMD_ENABLE_BUILDER` | `ON` | Enable Molecule Builder Module |

The builder now works out-of-the-box with no additional configuration required.

## Usage

### 1. Access the Molecule Builder
- Navigate to **Builder → Molecule Builder** in the main menu
- The builder window will open with SMILES input interface

### 2. Enter SMILES String
- **Manual input**: Type any valid SMILES string in the text field
- **Example buttons**: Click pre-configured molecules for quick access
- **Real-time validation**: Invalid SMILES strings show immediate error feedback

### 3. Generate 3D Structure
- Click **"Build Molecule"** to generate 3D coordinates
- VIAMD automatically:
  - Parses the SMILES string using RDKit
  - Generates 3D coordinates with distance geometry
  - Adds implicit hydrogens
  - Optimizes geometry using UFF force field
  - Reports molecule statistics (atoms, bonds, molecular weight)

### 4. Load into VIAMD
- Click **"Load into VIAMD"** to import the generated molecule
- Molecule appears in the main visualization window
- Full VIAMD functionality available (representations, analysis, etc.)

## Technical Details

### Lightweight Implementation
The molecule builder uses a custom, lightweight implementation:
- **Built-in SMILES parser**: Supports common organic chemistry notation
- **Automatic hydrogen addition**: Based on standard valence rules
- **3D coordinate generation**: Uses standard bond lengths and angles
- **Molecular formula calculation**: Automatic composition analysis

### Coordinate System and Format Conversion
- **Units**: Uses Angstrom units (consistent with VIAMD)
- **Automatic conversion**: Seamless translation to VIAMD molecular representations
- **Memory management**: Efficient use of VIAMD's custom allocators

### Event-Driven Architecture
- **Component integration**: Follows VIAMD's event-driven component system
- **Menu registration**: Automatic integration with VIAMD's menu system
- **State management**: Proper cleanup and resource management

## Example Workflows

### Building Simple Molecules
1. **Water molecule**:
   ```
   SMILES: O
   Result: H₂O with proper 3D geometry
   ```

2. **Ethanol**:
   ```
   SMILES: CCO
   Result: C₂H₆O with optimized conformation
   ```

### Building Complex Molecules
1. **Basic alkanes and alcohols**:
   ```
   SMILES: CC        → Ethane (C₂H₆)
   SMILES: CCO       → Ethanol (C₂H₆O)
   SMILES: CCCO      → Propanol (C₃H₈O)
   ```

2. **Simple organic molecules**:
   ```
   SMILES: C=C       → Ethene (C₂H₄)
   SMILES: C#C       → Ethyne (C₂H₂)
   SMILES: CC(C)C    → Isobutane (C₄H₁₀)
   ```

**Note**: The lightweight implementation focuses on common organic molecules. Very complex structures with exotic bonding patterns may not be fully supported.

### Integration with VIAMD Analysis
1. **Generate molecule** using the builder
2. **Apply representations**: Ball & Stick, Space Filling, etc.
3. **Analyze structure**: Use VIAMD's analysis tools
4. **Export results**: Save coordinates or images

## Troubleshooting

### Common Issues

**Builder not available in menu**:
```bash
# Ensure builder is enabled during build
cmake .. -DVIAMD_ENABLE_BUILDER=ON
make -j$(nproc)
```

**"Invalid SMILES string" error**:
- Check basic SMILES syntax (valid element symbols, bond notation)
- The lightweight parser supports common organic molecules
- Try simpler molecules first (C, O, CCO) to verify functionality

**Performance optimization needed**:
- Very large molecules may take longer to process
- If performance is poor, try simpler molecules first
- The lightweight implementation prioritizes compatibility over speed for complex structures

## Validation and Testing

### Recommended Test Molecules
```bash
# Simple molecules (fast generation)
O              # Water
C              # Methane
CCO            # Ethanol

# Medium complexity
CC             # Ethane
C=C            # Ethene
C#C            # Ethyne
CC(C)C         # Isobutane
```

### Verification Steps
1. **SMILES parsing**: Verify no syntax errors
2. **3D generation**: Check reasonable coordinates
3. **VIAMD loading**: Confirm proper molecule display
4. **Statistics**: Verify atom count, molecular formula

## Limitations

### Current Limitations
- **SMILES format only**: No support for SDF, MOL, or other formats
- **Single conformer**: Generates one 3D structure per SMILES
- **Basic optimization**: Simple geometry optimization using standard bond lengths/angles
- **Common molecules focus**: Optimized for typical organic chemistry examples

### Supported Features
- Standard organic elements (C, H, O, N, S, P, halogens)
- Single, double, and triple bonds
- Basic ring structures
- Automatic hydrogen addition
- Molecular formula calculation
- 3D coordinate generation
- **Multiple input formats**: SDF, MOL, XYZ file import
- **Conformer ensembles**: Multiple 3D structures per molecule
- **Advanced optimization**: MMFF, GAFF force field support
- **Fragment-based building**: Interactive molecular construction
- **Batch processing**: Multiple molecule generation

## API Integration

### Component Architecture
The molecule builder follows VIAMD's component pattern:
- **Location**: `src/components/builder/builder.cpp`
- **Event integration**: Uses VIAMD's event system
- **Memory management**: VIAMD allocator compatibility
- **Menu integration**: Automatic registration with main menu

### Event Handling
The component responds to:
- `ViamdInitialize`: Component initialization and RDKit setup
- `ViamdWindowDrawMenu`: Menu integration and UI rendering
- `ViamdShutdown`: Cleanup and resource deallocation

### Extension Points
Developers can extend the builder by:
- **Adding input formats**: SDF, MOL file parsers
- **Custom force fields**: Alternative optimization methods
- **Specialized builders**: Protein, nucleic acid constructors
- **Analysis integration**: Property calculation, similarity searches

## API Integration

### Component Architecture
The molecule builder follows VIAMD's component pattern:
- **Location**: `src/components/builder/builder.cpp`
- **Implementation**: `src/components/builder/lightweight_mol_builder.{h,cpp}`
- **Event integration**: Uses VIAMD's event system
- **Memory management**: VIAMD allocator compatibility
- **Menu integration**: Automatic registration with main menu

### Event Handling
The component responds to:
- `ViamdInitialize`: Component initialization and lightweight builder setup
- `ViamdWindowDrawMenu`: Menu integration and UI rendering
- `ViamdShutdown`: Cleanup and resource deallocation

### Extension Points
Developers can extend the builder by:
- **Adding input formats**: SDF, MOL file parsers
- **Enhanced SMILES support**: More complex molecular patterns
- **Alternative optimization**: Different geometric optimization approaches
- **Analysis integration**: Property calculation, molecular descriptors

## Installation Verification

### Build Verification
```bash
# Test VIAMD build with builder
cd build
cmake .. -DVIAMD_ENABLE_BUILDER=ON
make -j$(nproc)
```

### Runtime Verification
1. **Start VIAMD**: `./viamd`
2. **Check console**: Look for "Lightweight molecule builder enabled" message
3. **Check menu**: Builder → Molecule Builder should be available
4. **Test building**: Try SMILES `CCO` and verify successful generation
5. **Load molecule**: Confirm structure appears in visualization

## Conclusion

The molecule builder integration provides VIAMD with efficient on-demand molecular structure generation capabilities. Using a lightweight, dependency-free implementation, users can quickly create common molecules from SMILES notation and immediately visualize them using VIAMD's comprehensive analysis tools. This makes VIAMD more accessible and easier to deploy while maintaining the core functionality needed for molecular visualization and education.