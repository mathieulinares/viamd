# Dockstring Integration in VIAMD

This document describes the integration of dockstring molecular docking capabilities into VIAMD.

## Overview

The dockstring integration allows users to perform molecular docking calculations directly within VIAMD. Users can input SMILES strings, select target proteins, and visualize docking results alongside their existing molecular dynamics data.

## Features

### Molecular Docking Interface
- **SMILES Input**: Text field for entering molecular SMILES strings
- **Target Selection**: Choose from available target proteins (default: DRD2)
- **Example Molecules**: Quick-access buttons for common molecules
- **Asynchronous Calculation**: Background docking with progress indication
- **Results Display**: Docking scores and molecular poses

### User Interface
- **Menu Access**: Available via "Docking → Dockstring" in the main menu
- **Dark Theme**: Consistent with VIAMD's visual design
- **Real-time Feedback**: Error messages and status updates
- **Example Library**: Pre-configured molecules for testing

## Usage

### 1. Access the Dockstring Interface
- Navigate to **Docking → Dockstring** in the main menu
- The dockstring window will open with the molecular input interface

### 2. Enter Molecular Data
- **SMILES String**: Enter a valid SMILES notation for your molecule
- **Target Protein**: Specify the target protein (e.g., DRD2, MAPK14, ROCK1)
- **Example Molecules**: Use provided buttons for quick testing

### 3. Perform Docking
- Click **"Dock Molecule"** to start the calculation
- Progress is shown in real-time
- Results display the docking score in kcal/mol

### 4. Load Results into VIAMD
- Click **"Load into VIAMD"** to visualize the docked pose
- The molecule appears in the main visualization window
- Full VIAMD functionality is available for analysis

## Technical Implementation

### Component Architecture
The dockstring integration follows VIAMD's component pattern:
- **Location**: `src/components/dockstring/dockstring.cpp`
- **Event Integration**: Uses VIAMD's event system
- **Memory Management**: VIAMD allocator compatibility
- **Task System**: Asynchronous background processing

### Dependencies
- **Python 3**: Required for dockstring execution
- **Dockstring Package**: `pip install dockstring`
- **RDKit**: Molecular informatics toolkit
- **OpenBabel**: Chemical toolbox for format conversion

### Build Configuration
Enable dockstring integration during build:
```bash
cmake -DVIAMD_ENABLE_DOCKSTRING=ON ..
make
```

## Installation

### Prerequisites
1. **Install Python Dependencies**:
   ```bash
   # Ubuntu/Debian
   sudo apt-get install python3-rdkit python3-openbabel openbabel
   pip3 install dockstring
   
   # Or use conda (recommended)
   conda install -c conda-forge dockstring
   ```

2. **Build VIAMD with Dockstring Support**:
   ```bash
   cd build
   cmake .. -DVIAMD_ENABLE_DOCKSTRING=ON
   make -j$(nproc)
   ```

### Verification
Test the installation:
```bash
python3 -c "import dockstring; print('Dockstring available')"
./viamd  # Check that Docking menu is available
```

## Workflow Example

1. **Load Target Protein**: Open a PDB file in VIAMD
2. **Open Dockstring**: Access via Docking → Dockstring menu
3. **Enter SMILES**: Input molecule (e.g., "CCO" for ethanol)
4. **Set Target**: Specify protein target (e.g., "DRD2")
5. **Run Docking**: Click "Dock Molecule" and wait for results
6. **Analyze Results**: View docking score and load pose into VIAMD
7. **Visualize**: Examine protein-ligand complex in VIAMD

## API Integration

### Event Handling
The component responds to:
- `ViamdInitialize`: Component setup and dependency checking
- `ViamdFrameTick`: UI updates and task monitoring
- `ViamdWindowDrawMenu`: Menu integration
- `ViamdShutdown`: Cleanup and resource deallocation

### Task System
Docking calculations use VIAMD's task system:
```cpp
// Create background task
docking_task = task_system::create_pool_task(
    STR_LIT("Dockstring Docking"), 
    [this]() { perform_docking(); }
);
task_system::enqueue_task(docking_task);
```

### Python Bridge
Communication with dockstring via generated Python scripts:
```python
from dockstring import load_target
target = load_target('DRD2')
score, result = target.dock('CCO')
# Save results for VIAMD processing
```

## Limitations and Future Enhancements

### Current Limitations
- Results currently displayed as scores only
- Limited to dockstring's available target proteins
- Python dependency required at runtime

### Future Enhancements
- **Direct Visualization**: Integrate docked poses into VIAMD's rendering
- **Custom Targets**: Support for user-provided protein targets
- **Batch Docking**: Multiple molecules against multiple targets
- **Result Analysis**: Integrated scoring and comparison tools
- **Export Options**: Save results in various formats

## Troubleshooting

### Common Issues
1. **"Dockstring not available"**: Install dockstring and dependencies
2. **Python not found**: Ensure Python 3 is in system PATH
3. **Docking failures**: Check SMILES validity and target availability
4. **Performance issues**: Consider using fewer CPU cores for docking

### Debugging
Enable verbose logging in VIAMD to see dockstring communication details.

## Contributing

To extend the dockstring integration:
1. Modify `src/components/dockstring/dockstring.cpp`
2. Update build system if adding new dependencies
3. Test with various molecules and targets
4. Update documentation for new features

## License

The dockstring integration follows VIAMD's licensing terms. The dockstring package itself is subject to its own license terms.