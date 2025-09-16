# VIAMD
Visual Interactive Analysis of Molecular Dynamics

VIAMD is an interactive analysis tool for molecular dynamics (MD) written in C/C++. VIAMD is developed at the PDC Center for High Performance Computing (KTH, Stockholm). It exposes a rudementary script language that is used to declare operations which are performed over the frames of the trajectory.
The results can then be viewed in the different windows exposed in the application.

**NEW**: VIAMD now includes comprehensive **Python bindings** that provide seamless integration with popular molecular dynamics packages like OpenMM and MDAnalysis, enabling advanced analysis workflows and direct MD simulation capabilities from Python. 
<p align="center">
<img src="https://github.com/scanberg/viamd/assets/38646069/5651ef62-28bc-4f41-8234-75cf9ba85612" alt="This is an overview of the viamd software" width="800"/>
</p>

## Status
[![Windows (MSVC 19)](https://github.com/scanberg/viamd/actions/workflows/windows.yml/badge.svg?branch=master)](https://github.com/scanberg/viamd/actions/workflows/windows.yml)
[![Ubuntu 22.04 (GCC 11)](https://github.com/scanberg/viamd/actions/workflows/ubuntu22.yml/badge.svg)](https://github.com/scanberg/viamd/actions/workflows/ubuntu22.yml)
[![Ubuntu 24.04 (GCC 13)](https://github.com/scanberg/viamd/actions/workflows/ubuntu24.yml/badge.svg)](https://github.com/scanberg/viamd/actions/workflows/ubuntu24.yml)
[![MacOS (Clang)](https://github.com/scanberg/viamd/actions/workflows/macos.yml/badge.svg)](https://github.com/scanberg/viamd/actions/workflows/macos.yml)

## Running VIAMD 

### Quick Installation

**For detailed installation instructions, see [INSTALL.md](INSTALL.md)**

#### GUI Application

For windows, we recommend to use the latest binary available on the [release page](https://github.com/scanberg/viamd/releases/).

For Ubuntu and MacOs, to [build](https://github.com/scanberg/viamd/wiki/0.-Building) VIAMD on your machine, you can follow the procedure described in details in the wiki for [Linux](https://github.com/scanberg/viamd/wiki/0.-Building#linux) and [MacOS](https://github.com/scanberg/viamd/wiki/0.-Building#mac).

#### Python Interface (NEW)

VIAMD includes comprehensive Python bindings that enable integration with the Python scientific ecosystem:

```bash
# Quick Installation
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(nproc)

# Set up Python environment
export PYTHONPATH="$PWD/python:$PYTHONPATH"

# Install Python dependencies
pip install numpy
conda install -c conda-forge openmm  # For MD simulations
pip install MDAnalysis              # For trajectory analysis
```

#### Quick Start Example

```python
from pyviamd.dynamics import quick_md

# Run a complete MD simulation in one line
results = quick_md(
    structure_file="protein.pdb",
    n_steps=50000,
    temperature=300.0,
    output_dir="md_output"
)
```

**See the [Installation Guide](INSTALL.md) for comprehensive setup instructions and the [Python documentation](python/README.md) for detailed usage information.**

## Features

### Core VIAMD Application
- **Interactive 3D Visualization**: Real-time molecular structure and trajectory visualization
- **Trajectory Analysis**: Comprehensive analysis tools for MD simulation data
- **Scripting Language**: Custom scripting for automated analysis workflows
- **High Performance**: Optimized C++ backend for large-scale molecular systems

### Python Integration (NEW)
- **Molecular Data Access**: Direct access to VIAMD's efficient molecular data structures from Python
- **OpenMM Integration**: Run complete MD simulations with real-time monitoring and analysis
- **MDAnalysis Compatibility**: Seamless integration with MDAnalysis for advanced trajectory analysis
- **Event System**: Custom Python components can participate in VIAMD's event-driven architecture
- **Advanced Analysis**: Statistical analysis, geometry calculations, and machine learning integration
- **Visualization**: Multi-format export and real-time plotting capabilities

### Key Python Capabilities
- **One-line MD Simulations**: `quick_md()` function for rapid simulation setup
- **Production Workflows**: Complete MD pipelines with minimization, equilibration, and production phases
- **Real-time Monitoring**: Performance metrics, temperature/energy alerts, and optimization insights
- **Memory Efficient**: Zero-copy NumPy array access to molecular coordinates
- **Multiple Formats**: Support for PDB, XYZ, XTC, TRR, and other common formats
## Documentation

### Core VIAMD
Documentation about VIAMD is available on the github [wiki](https://github.com/scanberg/viamd/wiki). The two first chapters relate to the [visual](https://github.com/scanberg/viamd/wiki/1.-Visual) and [analysis](https://github.com/scanberg/viamd/wiki/2.-Analysis) features respectively, where we highlight the interactive part of software. The third chapter focus on the VIAMD [language](https://github.com/scanberg/viamd/wiki/3.-Language) used for scripting and the fourth chapter propose a serie of [tutorial](https://github.com/scanberg/viamd/wiki/4.-Tutorials) (under construction). 

A series of videos is available on [youtube](https://youtube.com/playlist?list=PLNx9MpJY8ffr9CeK7WefdOnuGRw_E5rSj&si=VatBHEwiL7jWyhPK).

### Python Bindings
- **[Python API Documentation](python/README.md)**: Comprehensive guide to the Python interface
- **[OpenMM Dynamics Guide](python/DYNAMICS_GUIDE.md)**: Detailed guide for running MD simulations
- **[Examples](python/examples/)**: Complete example scripts for all Python functionality

### Python Examples
- **Basic Usage**: Load structures, access coordinates, basic analysis
- **OpenMM Integration**: Complete MD simulation workflows
- **MDAnalysis Integration**: Advanced trajectory analysis pipelines
- **Event System**: Custom Python components and real-time analysis
- **Machine Learning**: Feature extraction and molecular property prediction
- **Visualization**: 3D rendering, animations, and data export

## Python Integration

VIAMD now includes comprehensive Python bindings that bridge the gap between VIAMD's high-performance C++ molecular data structures and Python's rich scientific ecosystem. This integration enables seamless workflows with popular packages like OpenMM, MDAnalysis, NumPy, and scikit-learn.

### Installation

```bash
# 1. Build VIAMD with Python support
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release -DVIAMD_ENABLE_PYTHON=ON
make -j$(nproc)

# 2. Set up Python environment
export PYTHONPATH="$PWD/python:$PYTHONPATH"

# 3. Install dependencies
pip install numpy                     # Required
conda install -c conda-forge openmm   # For MD simulations
pip install MDAnalysis               # For trajectory analysis
pip install matplotlib scipy         # For visualization and analysis
```

### Core Capabilities

#### 1. Molecular Data Access
```python
import pyviamd

# Load molecular structures
molecule = pyviamd.molecule.load_pdb("protein.pdb")
coords = molecule.atom.coordinates  # NumPy array access
masses = molecule.atom.masses

# Load trajectories
trajectory = pyviamd.trajectory.load_xtc("trajectory.xtc")
frame_coords = trajectory.extract_coordinates(frame_idx=0)
```

#### 2. OpenMM Integration & MD Simulations
```python
from pyviamd.dynamics import DynamicsRunner, quick_md

# Quick MD simulation
results = quick_md(
    structure_file="protein.pdb",
    n_steps=100000,
    temperature=300.0,
    output_dir="simulation_output"
)

# Advanced workflows
runner = DynamicsRunner(structure_file="protein.pdb")
runner.setup_system(force_field="amber14", water_model="tip3p")
frames = runner.run_production_md(n_steps=100000, temperature=300.0)
```

#### 3. MDAnalysis Integration
```python
import pyviamd.integrations.mdanalysis_integration as mda_integration

# Create MDAnalysis universe from VIAMD data
universe = mda_integration.create_universe_from_viamd(molecule)

# Run advanced analysis
from MDAnalysis.analysis import rms, contacts
rmsd_analysis = rms.RMSD(universe, reference=universe)
rmsd_analysis.run()
```

#### 4. Real-time Analysis & Events
```python
from pyviamd.event import EventManager

# Set up event-driven analysis
event_manager = EventManager()
event_manager.register_handler("trajectory_analysis", my_analysis_function)
event_manager.subscribe_to_event("trajectory_analysis", "ViamdFrameTick")
```

#### 5. Advanced Analysis & Machine Learning
```python
import pyviamd.integrations.analysis_integration as analysis
import pyviamd.integrations.ml_integration as ml

# Statistical analysis
rg_values = analysis.calculate_radius_of_gyration_trajectory(trajectory_data)
distance_stats = analysis.calculate_pairwise_distance_statistics(coords)

# Machine learning features
features = ml.extract_molecular_features(molecule, feature_types=["rdf", "geometry"])
features_scaled = ml.preprocess_features(features, method="standardize")
```

### Production Workflows

The Python interface supports complete production workflows:

```python
from pyviamd.dynamics import DynamicsRunner, SimulationProtocol, ProtocolParameters

# Complete production MD workflow
runner = DynamicsRunner(structure_file="system.pdb")
runner.setup_system(force_field="amber14", water_model="tip3p")

# 1. Energy minimization
runner.run_protocol(SimulationProtocol.MINIMIZATION, 
                   ProtocolParameters(minimize_tolerance=10.0))

# 2. NVT equilibration
runner.run_protocol(SimulationProtocol.NVT_EQUILIBRATION,
                   ProtocolParameters(temperature=300.0, n_steps=10000))

# 3. NPT equilibration
runner.run_protocol(SimulationProtocol.NPT_EQUILIBRATION,
                   ProtocolParameters(temperature=300.0, pressure=1.0, n_steps=10000))

# 4. Production simulation
frames = runner.run_protocol(SimulationProtocol.PRODUCTION_NPT,
                           ProtocolParameters(temperature=300.0, pressure=1.0, 
                                            n_steps=1000000, report_interval=1000))

# 5. Export results
runner.export_results({"trajectory": frames}, "production_output")
```

### Performance & Integration

- **High Performance**: Zero-copy NumPy array access to VIAMD's molecular data
- **Memory Efficient**: Configurable trajectory storage and automatic memory management
- **Real-time Monitoring**: Performance metrics (ns/day), system alerts, and diagnostics
- **Multi-format Export**: XYZ, JSON, PDB formats for external visualization tools
- **Event-driven Architecture**: Custom Python components integrate seamlessly with VIAMD

## Update
If you want to stay informed about the latest update of VIAMD, please register your email address to the [form](https://forms.gle/fAxuWob8nMLcrS5h9). 

## Citations:
* General Framework:
  * R Skånberg, I Hotz, A Ynnerman, M Linares, VIAMD: a Software for Visual Interactive Analysis of Molecular Dynamics, J. Chem. Inf. Model. 2023, 63, 23, 7382–7391 https://doi.org/10.1021/acs.jcim.3c01033
  * R Skånberg, C König, P Norman, M Linares, D Jönsson, I Hotz, A Ynnerman, VIA-MD: Visual Interactive Analysis of Molecular Dynamics, 2018, Eurographics Proceedings, p. 19–27

* Specific tool:
  * Selection tool: Robin Skånberg, Mathieu Linares, Martin Falk, Ingrid Hotz, Anders Ynnerman, MolFind-Integrated Multi-Selection Schemes for Complex Molecular Structures, 2019, The Eurographics Association, p. 17-21​
  * Shape Space and Spatial Distribution Function: Robin Skånberg, Martin Falk, Mathieu Linares, Anders Ynnerman, Ingrid Hotz, Tracking Internal Frames of Reference for Consistent Molecular Distribution Functions, 2021, IEEE Transactions on Visualization and Computer Graphics, 28 (9), 3126-3137​

## Financial Support
VIAMD has received constant financial support since 2018 from the Swedish e-Research center ([SeRC](https://e-science.se/)) and the [Wallenberg Foundation](https://www.wallenberg.org/en)

VIAMD is supported by [InfraVis](https://infravis.se/) for specific projets:
- Parser for LAMMPS file (2301-5217 / 140 hours)
- Interactice analysis of [VeloxChem](https://veloxchem.org/docs/intro.html) file (interactive analysis of orbitals and spectra plotting) (600 hours) 

<p align="center">
<img src="https://github.com/scanberg/viamd/assets/38646069/e7245119-3ec4-4b84-9056-7197b3d1448b"  height="75" >
<img src="https://github.com/scanberg/viamd/assets/38646069/f1c8493f-9519-4458-87c6-2d57a4071ad7"  height="75" >
<img src="https://github.com/scanberg/viamd/assets/38646069/cfc3feed-728f-45c2-a7db-c3c0707acbb1"  height="75" >
</p>

## Acknowledgements

https://github.com/glfw/glfw

https://github.com/dougbinks/enkiTS

https://github.com/ocornut/imgui

https://github.com/epezent/implot

https://github.com/BalazsJako/ImGuiColorTextEdit

https://github.com/skaslev/gl3w

https://github.com/max0x7ba/atomic_queue

https://github.com/mlabbe/nativefiledialog

https://github.com/nothings/stb

#
<p align="center">
<img src="https://github.com/user-attachments/assets/39b69b10-88a1-43a7-9d69-68513ac4e632"  width="150" alt="This is the VIAMD logo" >
</p>



