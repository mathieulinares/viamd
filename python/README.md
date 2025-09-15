# VIAMD Python Bindings

This directory contains Python bindings for VIAMD, enabling integration with popular Python packages in the molecular dynamics community such as MDAnalysis and OpenMM.

## Features

- **Molecular Data Access**: Load and manipulate molecular structures using VIAMD's efficient C++ loaders
- **Trajectory Analysis**: Access trajectory data with NumPy integration for efficient processing
- **MDAnalysis Integration**: Convert VIAMD data to MDAnalysis Universe format for advanced analysis
- **OpenMM Integration**: Set up and run molecular dynamics simulations with real-time coordinate feedback
- **Memory Efficient**: Direct access to VIAMD's memory structures without unnecessary copying

## Building

### Prerequisites

- Python 3.8 or higher
- NumPy
- pybind11 (installed automatically during build)
- VIAMD dependencies (see main README)

### Build Instructions

1. Configure VIAMD with Python support:
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
```

2. Build VIAMD with Python bindings:
```bash
make -j$(nproc)
```

3. The Python module `pyviamd` will be built in the `python/` subdirectory.

### Installation

For development:
```bash
# From the build directory
export PYTHONPATH="$PWD/python:$PYTHONPATH"
```

For system installation:
```bash
make install
# or
cd python && pip install .
```

## Usage

### Basic Usage

```python
import pyviamd
import numpy as np

# Load a molecular structure
molecule = pyviamd.molecule.load_pdb("structure.pdb")
print(f"Loaded {molecule.n_atoms} atoms")

# Access coordinates as NumPy array
coords = molecule.atom.coordinates  # Shape: (n_atoms, 3)
masses = molecule.atom.masses       # Shape: (n_atoms,)

# Load trajectory
trajectory = pyviamd.trajectory.load_xtc("trajectory.xtc")
header = trajectory.get_header()
print(f"Trajectory has {header.num_frames} frames")

# Extract frame coordinates
frame_coords = trajectory.extract_coordinates(frame_idx=0, num_atoms=molecule.n_atoms)
```

### MDAnalysis Integration

```python
import pyviamd
import MDAnalysis as mda

# Load using VIAMD
molecule = pyviamd.molecule.load_pdb("structure.pdb")

# Convert to MDAnalysis Universe
coords = molecule.atom.coordinates
universe = mda.Universe.empty(n_atoms=len(coords), trajectory=True)
universe.atoms.positions = coords

# Use MDAnalysis analysis tools
from MDAnalysis.analysis import distances
dist = distances.distance_array(universe.atoms[:10].positions, 
                                universe.atoms[10:20].positions)
```

### OpenMM Integration

```python
import pyviamd
import openmm as mm
import openmm.app as app

# Load structure using VIAMD
molecule = pyviamd.molecule.load_pdb("structure.pdb")

# Set up OpenMM simulation
pdb = app.PDBFile("structure.pdb")
forcefield = app.ForceField('amber14-all.xml')
system = forcefield.createSystem(pdb.topology)

# Run simulation and update VIAMD coordinates
integrator = mm.LangevinMiddleIntegrator(300, 1, 0.002)
simulation = app.Simulation(pdb.topology, system, integrator)

for step in range(1000):
    simulation.step(1)
    if step % 100 == 0:
        state = simulation.context.getState(getPositions=True)
        positions = state.getPositions(asNumpy=True)
        molecule.set_coordinates(positions.value_in_unit(unit.angstrom))
```

## Examples

The `examples/` directory contains complete examples:

- `viamd_mdanalysis_example.py`: Integration with MDAnalysis for trajectory analysis
- `viamd_openmm_example.py`: Integration with OpenMM for molecular dynamics simulations

## API Reference

### Core Module (`pyviamd.core`)

- `log_info(message)`: Log info message to VIAMD logger
- `log_warning(message)`: Log warning message  
- `log_error(message)`: Log error message
- `get_version()`: Get VIAMD version string
- `get_heap_allocator_stats()`: Get memory usage statistics

### Molecule Module (`pyviamd.molecule`)

#### AtomData
- `coordinates`: NumPy array of atomic coordinates (N, 3)
- `masses`: NumPy array of atomic masses
- `radii`: NumPy array of atomic radii
- `elements`: NumPy array of atomic elements
- `residue_names`: List of residue names
- `chain_ids`: List of chain identifiers

#### Molecule
- `atom`: AtomData object
- `residue`: ResidueData object
- `chain`: ChainData object
- `n_atoms`: Number of atoms
- `n_residues`: Number of residues
- `n_chains`: Number of chains
- `set_coordinates(coords)`: Update atomic coordinates

#### Functions
- `load_pdb(filename)`: Load molecule from PDB file
- `load_gro(filename)`: Load molecule from GRO file  
- `load_xyz(filename)`: Load molecule from XYZ file

### Trajectory Module (`pyviamd.trajectory`)

#### TrajectoryHeader
- `num_frames`: Number of frames in trajectory
- `num_atoms`: Number of atoms per frame
- `frame_times`: NumPy array of frame timestamps

#### Trajectory
- `get_header()`: Get trajectory header information
- `get_frame_header(frame_idx)`: Get specific frame header
- `extract_coordinates(frame_idx, num_atoms)`: Extract coordinates as NumPy array

#### Functions
- `load_xtc(filename)`: Load trajectory from XTC file
- `load_trr(filename)`: Load trajectory from TRR file
- `extract_frame_data_for_mdanalysis(traj, frame_idx, num_atoms)`: Extract complete frame data

## Dependencies

### Required
- NumPy >= 1.19.0

### Optional
- MDAnalysis >= 2.0.0 (for advanced trajectory analysis)
- OpenMM >= 7.7.0 (for molecular dynamics simulations)
- matplotlib (for plotting examples)
- scipy (for additional analysis functions)

## Installation with Dependencies

```bash
# Basic installation
pip install numpy

# With MDAnalysis support
pip install numpy MDAnalysis

# With OpenMM support (conda recommended)
conda install -c conda-forge openmm

# Full installation with all optional dependencies
pip install numpy MDAnalysis matplotlib scipy
conda install -c conda-forge openmm
```

## Development

### Running Tests

```bash
cd python
python -m pytest tests/
```

### Code Style

```bash
# Format code
black src/ examples/

# Check style
flake8 src/ examples/
```

## Troubleshooting

### Import Error: No module named 'pyviamd'

Make sure the Python path includes the build directory:
```bash
export PYTHONPATH="/path/to/viamd/build/python:$PYTHONPATH"
```

### Symbol Errors

Ensure VIAMD was built with Python support:
```bash
cmake .. -DVIAMD_ENABLE_PYTHON=ON
```

### Memory Issues

Check memory usage:
```python
import pyviamd
stats = pyviamd.core.get_heap_allocator_stats()
print(stats)
```

## Contributing

When adding new Python bindings:

1. Add binding code to appropriate `src/*_bindings.cpp` file
2. Update the module interface in `src/pyviamd.cpp`
3. Add examples demonstrating the new functionality
4. Update this README with API documentation

## License

The VIAMD Python bindings follow the same license as the main VIAMD project.