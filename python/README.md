# VIAMD Python Bindings

This directory contains Python bindings for VIAMD, enabling integration with popular Python packages in the molecular dynamics community such as MDAnalysis and OpenMM.

## Features

### Core Python Bindings
- **Molecular Data Access**: Efficient loading and manipulation of molecular structures
- **Trajectory Analysis**: NumPy-based trajectory processing with zero-copy coordinate access
- **Memory Efficient**: Direct access to VIAMD's C++ memory structures
- **Multi-format Support**: PDB, GRO, XYZ, XTC, TRR, and other common formats

### OpenMM Integration
- **Complete MD Workflows**: Production-ready simulation protocols
- **DynamicsRunner**: High-level interface for MD simulations
- **Real-time Monitoring**: Performance metrics and system diagnostics
- **Multiple Protocols**: Minimization, NVT/NPT equilibration, production MD, heating/cooling
- **Force Field Support**: AMBER, CHARMM, and other popular force fields

### MDAnalysis Integration
- **Universe Creation**: Automatic MDAnalysis Universe generation from VIAMD data
- **C++ Bindings**: High-performance molecular analysis with C++ acceleration
- **Advanced Calculations**: RDF, RMSD, center of mass, radius of gyration
- **Seamless Workflows**: Bidirectional coordinate synchronization

### Event System
- **Custom Python Components**: Integrate with VIAMD's event-driven architecture
- **Real-time Analysis**: Event-driven molecular analysis pipelines
- **Event Filtering**: Subscription-based event processing
- **Custom Events**: Support for user-defined event types

### Advanced Analysis & Visualization
- **Statistical Analysis**: Rolling window statistics, geometry calculations
- **Machine Learning**: Feature extraction, clustering, dimensionality reduction
- **Visualization**: 3D rendering, real-time plotting, animation creation
- **Data Export**: Multiple output formats for external visualization tools

### Performance Features
- **Zero-copy Access**: Direct NumPy array access to molecular coordinates
- **Memory Management**: Configurable trajectory storage with automatic cleanup
- **Parallel Processing**: Multi-threaded analysis and simulation support
- **GPU Acceleration**: OpenMM CUDA support for high-performance simulations

## Building

## Building

### Prerequisites

- **Python 3.8+**: Required for Python bindings
- **NumPy ≥1.19.0**: Core numerical computing
- **CMake ≥3.15**: Build system
- **pybind11**: Python-C++ bindings (installed automatically)
- **VIAMD dependencies**: See main README for core VIAMD requirements

### Build Instructions

#### 1. Configure Build

```bash
# Create build directory
mkdir build && cd build

# Configure with Python support
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON

# Optional: Specify Python version
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON -DPYTHON_EXECUTABLE=/usr/bin/python3.10
```

#### 2. Build VIAMD with Python Bindings

```bash
# Build (adjust -j based on your CPU cores)
make -j$(nproc)

# The Python module will be built in: build/python/pyviamd/
```

#### 3. Set Up Python Environment

```bash
# Add to Python path (temporary)
export PYTHONPATH="$PWD/python:$PYTHONPATH"

# Or add to your shell profile (.bashrc, .zshrc)
echo 'export PYTHONPATH="/path/to/viamd/build/python:$PYTHONPATH"' >> ~/.bashrc
```

### Installation Options

#### Option 1: Development Installation

```bash
# From build directory
export PYTHONPATH="$PWD/python:$PYTHONPATH"

# Test installation
python -c "import pyviamd; print('Success!')"
```

#### Option 2: System Installation (CMake)

```bash
# Install VIAMD including Python bindings
make install

# Python module will be installed to system Python path
```

#### Option 3: pip Installation (Development)

```bash
# Navigate to python directory
cd python

# Install in development mode
pip install -e .

# Or regular installation
pip install .
```

### Installing Dependencies

#### Core Dependencies (Required)

```bash
# NumPy (required for all functionality)
pip install numpy>=1.19.0
```

#### OpenMM Integration (Optional)

```bash
# Install OpenMM via conda (recommended)
conda install -c conda-forge openmm

# For CUDA acceleration (if you have compatible GPU)
conda install -c conda-forge openmm cudatoolkit

# Test OpenMM installation
python -c "import openmm; print(f'OpenMM version: {openmm.version.version}')"
```

#### MDAnalysis Integration (Optional)

```bash
# Install MDAnalysis
pip install MDAnalysis>=2.0.0

# Test MDAnalysis installation
python -c "import MDAnalysis; print(f'MDAnalysis version: {MDAnalysis.__version__}')"
```

#### Full Scientific Stack (Optional)

```bash
# For complete functionality
pip install numpy MDAnalysis matplotlib scipy scikit-learn
conda install -c conda-forge openmm
```

### Platform-Specific Instructions

#### Ubuntu/Debian

```bash
# Install system dependencies
sudo apt-get update
sudo apt-get install python3-dev python3-pip cmake build-essential

# Install Python dependencies
pip3 install numpy pybind11

# Build VIAMD
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(nproc)
```

#### macOS

```bash
# Install dependencies via Homebrew
brew install python cmake

# Install Python dependencies
pip3 install numpy pybind11

# Build VIAMD
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(sysctl -n hw.ncpu)
```

#### Windows

```bash
# Using Visual Studio and conda
conda install cmake numpy pybind11

# Configure with Visual Studio
mkdir build && cd build
cmake .. -DVIAMD_ENABLE_PYTHON=ON -G "Visual Studio 16 2019"

# Build
cmake --build . --config Release
```

### Docker Installation

```dockerfile
# Example Dockerfile for VIAMD with Python
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    python3-dev python3-pip cmake build-essential git

RUN pip3 install numpy pybind11

COPY . /viamd
WORKDIR /viamd

RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON && \
    make -j$(nproc)

ENV PYTHONPATH="/viamd/build/python:$PYTHONPATH"
```

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

## Usage

### Quick Start

```python
# Import VIAMD Python bindings
import pyviamd
import numpy as np

# Load molecular structure
molecule = pyviamd.molecule.load_pdb("structure.pdb")
print(f"Loaded {molecule.n_atoms} atoms")

# Access coordinates and properties
coords = molecule.atom.coordinates  # Shape: (n_atoms, 3)
masses = molecule.atom.masses       # Shape: (n_atoms,)
elements = molecule.atom.elements   # Atomic elements

# Load trajectory
trajectory = pyviamd.trajectory.load_xtc("trajectory.xtc")
header = trajectory.get_header()
print(f"Trajectory has {header.num_frames} frames")

# Extract frame coordinates
frame_coords = trajectory.extract_coordinates(frame_idx=0, num_atoms=molecule.n_atoms)
```

### OpenMM Molecular Dynamics

#### Quick MD Simulation

```python
from pyviamd.dynamics import quick_md

# Complete MD simulation in one line
results = quick_md(
    structure_file="protein.pdb",
    n_steps=100000,
    temperature=300.0,
    output_dir="simulation_output"
)
```

#### Advanced MD Workflows

```python
from pyviamd.dynamics import DynamicsRunner, SimulationProtocol, ProtocolParameters

# Create dynamics runner
runner = DynamicsRunner(structure_file="protein.pdb")

# Setup OpenMM system
runner.setup_system(force_field="amber14", water_model="tip3p")

# Run complete production workflow
# 1. Energy minimization
minimize_params = ProtocolParameters(minimize_tolerance=10.0)
runner.run_protocol(SimulationProtocol.MINIMIZATION, minimize_params)

# 2. NVT equilibration
nvt_params = ProtocolParameters(temperature=300.0, n_steps=10000)
runner.run_protocol(SimulationProtocol.NVT_EQUILIBRATION, nvt_params)

# 3. NPT production
npt_params = ProtocolParameters(
    temperature=300.0, 
    pressure=1.0, 
    n_steps=100000,
    report_interval=1000
)
frames = runner.run_protocol(
    SimulationProtocol.PRODUCTION_NPT, 
    npt_params,
    real_time_analysis=True
)

# Export results
runner.export_results({"trajectory": frames}, "production_output")

# Access performance monitoring
performance = runner.monitoring_system.get_performance_summary()
print(f"Performance: {performance['average_ns_per_day']:.2f} ns/day")
```

#### Custom Simulation Protocols

```python
# Heating protocol (0K to 300K)
heating_params = ProtocolParameters(temperature=300.0, n_steps=10000)
heating_frames = runner.run_protocol(SimulationProtocol.HEATING, heating_params)

# Cooling protocol (300K to 100K) 
cooling_params = ProtocolParameters(temperature=100.0, n_steps=10000)
cooling_frames = runner.run_protocol(SimulationProtocol.COOLING, cooling_params)
```

### MDAnalysis Integration

#### Basic Integration

```python
import pyviamd.integrations.mdanalysis_integration as mda_integration
import MDAnalysis as mda

# Load using VIAMD
molecule = pyviamd.molecule.load_pdb("structure.pdb")

# Create MDAnalysis universe
universe = mda_integration.create_universe_from_viamd(molecule)

# Use MDAnalysis analysis tools
from MDAnalysis.analysis import distances, rms
dist = distances.distance_array(
    universe.atoms[:10].positions, 
    universe.atoms[10:20].positions
)
```

#### Advanced Analysis Workflows

```python
# C++ accelerated analysis
interface = mda_integration.VIAMDMDAnalysisInterface(molecule)

# High-performance calculations
center_of_mass = interface.calculate_center_of_mass()
radius_of_gyration = interface.calculate_radius_of_gyration()
atomic_masses = interface.get_atomic_masses()

# Trajectory analysis
trajectory_interface = mda_integration.MDAnalysisTrajectoryInterface()
trajectory_interface.load_trajectory("trajectory.xtc")

# Process trajectory frames efficiently
for frame_idx in range(trajectory_interface.get_frame_count()):
    coords = trajectory_interface.get_frame_coordinates(frame_idx)
    # Perform frame-specific analysis
```

### Event System Integration

#### Basic Event Handling

```python
from pyviamd.event import EventManager, EventType

# Create event manager
event_manager = EventManager()

# Define custom event handler
def my_analysis_callback(event_data):
    print(f"Received event: {event_data}")
    # Perform custom analysis
    return {"analysis_result": "completed"}

# Register handler and subscribe to events
event_manager.register_handler("trajectory_analysis", my_analysis_callback)
event_manager.subscribe_to_event("trajectory_analysis", EventType.ViamdFrameTick)

# Send and process events
event_manager.send_event(EventType.ViamdFrameTick, {"frame": 42})
event_manager.process_events()
```

#### Advanced Event Workflows

```python
from pyviamd.event import MolecularEventHandler, EventLogger

# Set up molecular event handler with filtering
mol_handler = MolecularEventHandler(molecule)
mol_handler.set_event_filter(["ViamdFrameTick", "ViamdSimulationStep"])

# Event logging
logger = EventLogger("simulation.log")
logger.start_logging()

# Register multiple handlers for orchestrated analysis
event_manager.register_handler("rmsd_analysis", rmsd_callback)
event_manager.register_handler("energy_analysis", energy_callback)
event_manager.register_handler("distance_analysis", distance_callback)
```

### Advanced Analysis & Machine Learning

#### Statistical Analysis

```python
import pyviamd.integrations.analysis_integration as analysis

# Load trajectory data
trajectory_data = analysis.load_trajectory_data("trajectory.xtc", molecule)

# Statistical calculations
rg_values = analysis.calculate_radius_of_gyration_trajectory(trajectory_data)
distance_stats = analysis.calculate_pairwise_distance_statistics(coords)

# Rolling window analysis
rolling_rmsd = analysis.calculate_rolling_rmsd(trajectory_data, window_size=100)
```

#### Machine Learning Integration

```python
import pyviamd.integrations.ml_integration as ml

# Feature extraction
features = ml.extract_molecular_features(
    molecule, 
    feature_types=["rdf", "geometry", "contacts"]
)

# Data preprocessing
features_scaled = ml.preprocess_features(features, method="standardize")
features_reduced = ml.reduce_dimensionality(features_scaled, method="pca", n_components=10)

# Clustering
cluster_labels = ml.cluster_conformations(features_reduced, method="kmeans", n_clusters=5)

# Model training (example with custom data)
model = ml.train_property_predictor(features, target_properties, model_type="random_forest")
predictions = ml.predict_properties(model, new_features)
```

#### Visualization

```python
import pyviamd.integrations.visualization_integration as viz

# Export for visualization
viz.export_trajectory(frames, "visualization.pdb", format="pdb")
viz.export_trajectory(frames, "data.xyz", format="xyz")

# Create visualizations
viz.create_animation(frames, "animation.gif", fps=10)
viz.plot_time_series(times, energies, "energy_plot.png")

# Real-time plotting
plotter = viz.RealTimePlotter()
plotter.add_plot("temperature", times, temperatures)
plotter.add_plot("energy", times, energies)
plotter.show()

# 3D molecular rendering
renderer = viz.MolecularRenderer()
renderer.render_structure(molecule, "structure.png", style="ball_and_stick")
```

### Integration Examples

#### VIAMD + OpenMM + MDAnalysis Workflow

```python
import pyviamd
from pyviamd.dynamics import DynamicsRunner
import pyviamd.integrations.mdanalysis_integration as mda_integration
import MDAnalysis as mda

# 1. Load structure with VIAMD
molecule = pyviamd.molecule.load_pdb("protein.pdb")

# 2. Run MD simulation with OpenMM
runner = DynamicsRunner(molecule=molecule)
runner.setup_system(force_field="amber14", water_model="tip3p")
frames = runner.run_production_md(n_steps=100000, temperature=300.0)

# 3. Analyze results with MDAnalysis
universe = mda_integration.create_universe_from_viamd(molecule)
# Update universe coordinates with simulation results
for i, frame in enumerate(frames):
    universe.trajectory[i] = frame.coordinates

# 4. Advanced analysis
from MDAnalysis.analysis import rms, contacts
rmsd_analysis = rms.RMSD(universe, reference=universe)
rmsd_analysis.run()

print(f"RMSD values: {rmsd_analysis.results.rmsd}")
```

#### Real-time Analysis Pipeline

```python
from pyviamd.dynamics import DynamicsRunner
from pyviamd.event import EventManager
import pyviamd.integrations.analysis_integration as analysis

# Set up event-driven analysis
event_manager = EventManager()

def real_time_analysis(event_data):
    coords = event_data.get("coordinates")
    if coords is not None:
        # Perform real-time calculations
        rg = analysis.calculate_radius_of_gyration(coords)
        com = analysis.calculate_center_of_mass(coords)
        return {"radius_of_gyration": rg, "center_of_mass": com}

# Register real-time analysis
event_manager.register_handler("real_time_analysis", real_time_analysis)

# Run simulation with real-time analysis
runner = DynamicsRunner(structure_file="protein.pdb")
runner.setup_system(force_field="amber14")
frames = runner.run_production_md(
    n_steps=100000, 
    temperature=300.0,
    real_time_analysis=True,
    event_manager=event_manager
)
```

## Examples

The `examples/` directory contains comprehensive examples demonstrating all functionality:

### Core Examples
- **`viamd_openmm_example.py`**: Basic OpenMM integration and simulation setup
- **`viamd_mdanalysis_example.py`**: MDAnalysis integration for trajectory analysis
- **`viamd_dynamics_example.py`**: Complete MD simulation workflows with DynamicsRunner

### Advanced Integration Examples
- **`advanced_dynamics_integration.py`**: Production MD workflows with all simulation protocols
- **`advanced_viamd_mdanalysis_example.py`**: Advanced MDAnalysis workflows with C++ acceleration
- **`advanced_visualization_example.py`**: Comprehensive visualization and export capabilities
- **`advanced_analysis_example.py`**: Statistical analysis and performance monitoring

### Specialized Examples
- **`event_system_example.py`**: Basic event handling and custom components
- **`advanced_event_system_example.py`**: Complex event-driven analysis pipelines
- **`ml_integration_example.py`**: Machine learning feature extraction and modeling

### Running Examples

```bash
# Navigate to examples directory
cd python/examples

# Run basic examples
python viamd_dynamics_example.py
python viamd_mdanalysis_example.py

# Run advanced examples (requires additional dependencies)
python advanced_dynamics_integration.py
python ml_integration_example.py
```

### Example Outputs

Each example generates comprehensive output demonstrating:
- Molecular data loading and processing
- Simulation execution and monitoring  
- Analysis results and statistics
- Exported data files and visualizations
- Performance metrics and diagnostics

## API Reference

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

### Dynamics Module (`pyviamd.dynamics`)

#### DynamicsRunner
- `__init__(structure_file=None, molecule=None)`: Initialize with structure file or molecule
- `setup_system(force_field, water_model=None, **kwargs)`: Setup OpenMM system
- `run_protocol(protocol, parameters, real_time_analysis=False)`: Run simulation protocol
- `run_production_md(n_steps, temperature, protocol="npt", **kwargs)`: Complete production workflow
- `export_results(results, output_dir)`: Export simulation results

#### SimulationProtocol (Enum)
- `MINIMIZATION`: Energy minimization
- `NVT_EQUILIBRATION`: NVT ensemble equilibration
- `NPT_EQUILIBRATION`: NPT ensemble equilibration
- `PRODUCTION_NVT`: Production NVT simulation
- `PRODUCTION_NPT`: Production NPT simulation
- `HEATING`: Temperature heating protocol
- `COOLING`: Temperature cooling protocol

#### ProtocolParameters
- `temperature`: Target temperature (K)
- `pressure`: Target pressure (bar, None for NVT)
- `time_step`: Integration time step (ps)
- `n_steps`: Number of simulation steps
- `report_interval`: Reporting frequency
- `minimize_tolerance`: Minimization tolerance
- `minimize_max_iterations`: Maximum minimization iterations
- `friction_coefficient`: Langevin friction (1/ps)
- `nonbonded_method`: Nonbonded method ("PME", "Ewald", "CutoffPeriodic")
- `nonbonded_cutoff`: Cutoff distance (nm)
- `constraints`: Bond constraints ("None", "HBonds", "AllBonds")
- `use_pbc`: Use periodic boundary conditions

#### TrajectoryManager
- `add_frame(frame_data)`: Add frame to trajectory
- `get_trajectory()`: Get complete trajectory data
- `get_time_series(property_name)`: Get time series for property
- `export_trajectory(filename, format)`: Export trajectory to file
- `register_analysis_callback(callback)`: Register real-time analysis callback

#### MonitoringSystem
- `get_performance_summary()`: Get simulation performance metrics
- `get_current_temperature()`: Get current system temperature
- `get_current_energy()`: Get current system energy
- `check_alerts()`: Check for system alerts
- `log_performance_data(data)`: Log performance information

#### Functions
- `quick_md(structure_file, n_steps, temperature, output_dir, **kwargs)`: One-line MD simulation
- `check_requirements()`: Check installation and dependencies

### Event Module (`pyviamd.event`)

#### EventManager
- `register_handler(name, callback)`: Register event handler
- `unregister_handler(name)`: Remove event handler
- `subscribe_to_event(handler_name, event_type)`: Subscribe to event type
- `unsubscribe_from_event(handler_name, event_type)`: Unsubscribe from event
- `send_event(event_type, payload=None)`: Send event
- `process_events()`: Process pending events

#### EventType (Enum)
- `ViamdFrameTick`: Frame update event
- `ViamdSimulationStep`: Simulation step event
- `ViamdAnalysisComplete`: Analysis completion event
- `ViamdUserEvent`: User-defined event

#### MolecularEventHandler
- `__init__(molecule)`: Initialize with molecule
- `set_event_filter(event_types)`: Set event type filter
- `handle_event(event_type, payload)`: Handle received event

#### EventLogger
- `__init__(log_file)`: Initialize with log file
- `start_logging()`: Start event logging
- `stop_logging()`: Stop event logging

### Integration Modules

#### OpenMM Integration (`pyviamd.integrations.openmm_integration`)
- `VIAMDInterface`: Interface for VIAMD-OpenMM data exchange
- `SimulationManager`: OpenMM simulation workflow manager
- `create_openmm_system(molecule, force_field)`: Create OpenMM system from VIAMD molecule
- `run_openmm_simulation(system, n_steps, temperature)`: Run OpenMM simulation

#### MDAnalysis Integration (`pyviamd.integrations.mdanalysis_integration`)
- `VIAMDMDAnalysisInterface`: C++ interface for MDAnalysis integration
- `VIAMDMDAnalysisSystem`: High-level MDAnalysis workflow manager
- `MDAnalysisTrajectoryInterface`: Trajectory processing interface
- `create_universe_from_viamd(molecule)`: Create MDAnalysis Universe

#### Analysis Integration (`pyviamd.integrations.analysis_integration`)
- `AdvancedAnalysisInterface`: C++ statistical analysis interface
- `AdvancedAnalysisSystem`: High-level analysis workflow manager
- `calculate_radius_of_gyration(coords)`: Calculate radius of gyration
- `calculate_pairwise_distances(coords)`: Calculate pairwise distances
- `calculate_rolling_rmsd(trajectory, window_size)`: Rolling RMSD calculation

#### Visualization Integration (`pyviamd.integrations.visualization_integration`)
- `VisualizationInterface`: C++ visualization interface
- `VisualizationSystem`: High-level visualization workflow manager
- `RealTimePlotter`: Real-time plotting capabilities
- `MolecularRenderer`: 3D molecular rendering
- `export_trajectory(frames, filename, format)`: Export trajectory data
- `create_animation(frames, filename, fps)`: Create trajectory animation

#### Machine Learning Integration (`pyviamd.integrations.ml_integration`)
- `MLInterface`: C++ machine learning interface
- `MLSystem`: High-level ML workflow manager
- `extract_molecular_features(molecule, feature_types)`: Extract molecular features
- `preprocess_features(features, method)`: Data preprocessing
- `reduce_dimensionality(features, method, n_components)`: Dimensionality reduction
- `cluster_conformations(features, method, n_clusters)`: Conformation clustering
- `train_property_predictor(features, targets, model_type)`: Train ML model

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

## Troubleshooting

### Common Issues

1. **Import Error**: "pyviamd not available"
   - Build VIAMD with `VIAMD_ENABLE_PYTHON=ON`
   - Add build directory to `PYTHONPATH`
   ```bash
   export PYTHONPATH="/path/to/viamd/build/python:$PYTHONPATH"
   ```

2. **OpenMM Error**: "OpenMM not available"
   - Install OpenMM: `conda install -c conda-forge openmm`
   - For CUDA support: `conda install -c conda-forge openmm cudatoolkit`

3. **Build Errors**: Missing pybind11
   - Install pybind11: `pip install pybind11`
   - Or use system package: `sudo apt-get install pybind11-dev`

4. **Performance Issues**:
   - Reduce `report_interval` for large simulations
   - Disable `real_time_analysis` for better performance
   - Use `max_frames` limit in TrajectoryManager

5. **Memory Issues**:
   ```python
   import pyviamd
   stats = pyviamd.core.get_heap_allocator_stats()
   print(stats)  # Check memory usage
   ```

### System Requirements

**Minimum:**
- Python 3.8+
- NumPy 1.19+
- 4GB RAM
- CMake 3.15+

**Recommended:**
- Python 3.10+
- NumPy 1.21+
- 16GB RAM (for large systems)
- CUDA-capable GPU (for OpenMM acceleration)

### Dependencies Summary

| Package | Version | Purpose | Installation |
|---------|---------|---------|-------------|
| NumPy | ≥1.19.0 | Core arrays | `pip install numpy` |
| OpenMM | ≥7.7.0 | MD simulations | `conda install -c conda-forge openmm` |
| MDAnalysis | ≥2.0.0 | Trajectory analysis | `pip install MDAnalysis` |
| matplotlib | latest | Visualization | `pip install matplotlib` |
| scikit-learn | latest | Machine learning | `pip install scikit-learn` |

### Testing Installation

```python
# Test basic installation
import pyviamd
print(f"VIAMD version: {pyviamd.core.get_version()}")

# Test OpenMM integration
from pyviamd.dynamics import check_requirements
print(check_requirements())

# Run comprehensive test
python test_dynamics_interface.py
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