# VIAMD OpenMM Dynamics Interface

A comprehensive Python interface for running molecular dynamics simulations with OpenMM directly from VIAMD.

## Overview

The VIAMD OpenMM Dynamics Interface provides a high-level, production-ready framework for molecular dynamics simulations that seamlessly integrates VIAMD's molecular data structures with OpenMM's simulation engine. It offers:

- **Easy-to-use API**: Simple interface for complex MD workflows
- **Production protocols**: Predefined simulation protocols for common scenarios
- **Real-time monitoring**: Performance monitoring and simulation diagnostics
- **Integrated analysis**: Automatic trajectory analysis and visualization
- **Flexible workflows**: Support for custom simulation protocols

## Quick Start

### Basic Usage

```python
from pyviamd.dynamics import DynamicsRunner

# Load structure and create dynamics runner
runner = DynamicsRunner(structure_file="protein.pdb")

# Run complete MD workflow
results = runner.run_production_md(
    n_steps=100000,
    temperature=300.0,
    protocol="npt_equilibration",
    real_time_analysis=True
)

# Export results
runner.export_results(results, output_dir="md_output")
```

### Quick MD Simulation

```python
from pyviamd.dynamics import quick_md

# One-line MD simulation
results = quick_md(
    structure_file="structure.pdb",
    n_steps=50000,
    temperature=300.0,
    output_dir="quick_output"
)
```

## Features

### 1. DynamicsRunner Class

The main interface for running MD simulations:

```python
from pyviamd.dynamics import DynamicsRunner, ProtocolParameters

# Initialize with structure
runner = DynamicsRunner(structure_file="protein.pdb")

# Or with VIAMD molecule object
mol = pyviamd.molecule.load_pdb("protein.pdb")
runner = DynamicsRunner(molecule=mol)

# Setup simulation system
runner.setup_system(
    force_field="amber14",
    water_model="tip3p"
)
```

### 2. Simulation Protocols

Pre-defined protocols for common MD workflows:

```python
from pyviamd.dynamics import SimulationProtocol, ProtocolParameters

# Energy minimization
params = ProtocolParameters(
    minimize_tolerance=10.0,
    minimize_max_iterations=1000
)
frames = runner.run_protocol(SimulationProtocol.MINIMIZATION, params)

# NVT equilibration
params = ProtocolParameters(
    temperature=300.0,
    n_steps=10000,
    time_step=0.002
)
frames = runner.run_protocol(SimulationProtocol.NVT_EQUILIBRATION, params)

# NPT production
params = ProtocolParameters(
    temperature=300.0,
    pressure=1.0,
    n_steps=100000
)
frames = runner.run_protocol(SimulationProtocol.PRODUCTION_NPT, params)
```

### 3. Advanced Protocols

Special protocols for complex scenarios:

```python
# Heating protocol (0K to target temperature)
heating_frames = runner.run_protocol(
    SimulationProtocol.HEATING,
    ProtocolParameters(temperature=300.0, n_steps=10000)
)

# Cooling protocol (target to lower temperature)
cooling_frames = runner.run_protocol(
    SimulationProtocol.COOLING,
    ProtocolParameters(temperature=300.0, n_steps=10000)
)
```

### 4. Real-time Monitoring

Built-in performance monitoring and diagnostics:

```python
# Access monitoring data
performance = runner.monitoring_system.get_performance_summary()
print(f"Performance: {performance['average_ns_per_day']:.2f} ns/day")
print(f"Current temperature: {performance['current_temperature']:.1f} K")

# Check for alerts
if runner.monitoring_system.alerts:
    print("System alerts:")
    for alert in runner.monitoring_system.alerts:
        print(f"  - {alert}")
```

### 5. Trajectory Analysis

Comprehensive trajectory management and analysis:

```python
# Access trajectory data
trajectory = runner.trajectory_manager

# Get time series data
times, energies = trajectory.get_time_series("potential_energy")
times, temps = trajectory.get_time_series("temperature")

# Export trajectory
trajectory.export_trajectory("output.xyz", format="xyz")
trajectory.export_trajectory("analysis.json", format="json")

# Register custom analysis callbacks
def custom_analysis(frame):
    # Your custom analysis here
    return {"custom_property": some_calculation(frame)}

trajectory.register_analysis_callback(custom_analysis)
```

## Protocol Parameters

Customize simulation parameters using `ProtocolParameters`:

```python
from pyviamd.dynamics import ProtocolParameters

params = ProtocolParameters(
    temperature=300.0,           # Target temperature (K)
    pressure=1.0,               # Pressure for NPT (bar)
    time_step=0.002,            # Integration time step (ps)
    friction_coefficient=1.0,    # Langevin friction (1/ps)
    n_steps=100000,             # Number of simulation steps
    report_interval=1000,       # Frequency of data collection
    nonbonded_method="PME",     # Nonbonded calculation method
    nonbonded_cutoff=1.0,       # Cutoff distance (nm)
    constraints="HBonds",       # Bond constraints
    use_pbc=True                # Periodic boundary conditions
)
```

## Force Fields

Supported force fields and water models:

```python
# Common force field combinations
runner.setup_system(force_field="amber14", water_model="tip3p")
runner.setup_system(force_field="charmm36", water_model="tip3p")
runner.setup_system(force_field="amber99sb", water_model="spce")
```

## Complete Workflow Example

```python
from pyviamd.dynamics import DynamicsRunner, SimulationProtocol, ProtocolParameters

# 1. Initialize system
runner = DynamicsRunner(structure_file="protein.pdb")

# 2. Setup OpenMM system
runner.setup_system(force_field="amber14", water_model="tip3p")

# 3. Energy minimization
minimize_params = ProtocolParameters(
    minimize_tolerance=10.0,
    minimize_max_iterations=1000
)
runner.run_protocol(SimulationProtocol.MINIMIZATION, minimize_params)

# 4. NVT equilibration
nvt_params = ProtocolParameters(
    temperature=300.0,
    n_steps=10000,
    pressure=None  # NVT
)
runner.run_protocol(SimulationProtocol.NVT_EQUILIBRATION, nvt_params)

# 5. NPT equilibration
npt_params = ProtocolParameters(
    temperature=300.0,
    pressure=1.0,
    n_steps=10000
)
runner.run_protocol(SimulationProtocol.NPT_EQUILIBRATION, npt_params)

# 6. Production simulation
prod_params = ProtocolParameters(
    temperature=300.0,
    pressure=1.0,
    n_steps=100000,
    report_interval=1000
)
frames = runner.run_protocol(
    SimulationProtocol.PRODUCTION_NPT, 
    prod_params, 
    real_time_analysis=True
)

# 7. Analysis and export
runner.export_results({"trajectory_data": frames}, "production_output")
```

## Performance Optimization

### Memory Management

```python
# Limit trajectory frames in memory
from pyviamd.dynamics import TrajectoryManager

trajectory = TrajectoryManager(max_frames=1000)  # Keep only last 1000 frames
```

### Reporting Frequency

```python
# Adjust reporting frequency based on simulation length
n_steps = 1000000
report_interval = max(1, n_steps // 1000)  # Aim for ~1000 data points

params = ProtocolParameters(
    n_steps=n_steps,
    report_interval=report_interval
)
```

### Real-time Analysis

```python
# Enable/disable real-time coordinate updates
frames = runner.run_protocol(
    protocol, 
    params, 
    real_time_analysis=False  # Disable for better performance
)
```

## Integration with VIAMD

The dynamics interface seamlessly integrates with other VIAMD components:

### With VIAMD Analysis

```python
# Use VIAMD analysis tools on simulation results
import pyviamd.analysis as analysis

# Get final coordinates
final_coords = runner.viamd_interface.get_coordinates()

# Perform VIAMD analysis
rg = analysis.radius_of_gyration(final_coords)
distances = analysis.pairwise_distances(final_coords)
```

### With VIAMD Visualization

```python
# Visualize results with VIAMD visualization tools
import pyviamd.visualization as viz

# Export for visualization
viz.export_trajectory(frames, "visualization.pdb")
viz.create_animation(frames, "animation.gif")
```

### With Event System

```python
# Integrate with VIAMD event system
import pyviamd.event as events

# Register MD events
event_manager = events.EventManager()
event_manager.register_handler("md_complete", my_analysis_callback)

# Trigger events during simulation
# (automatically handled by dynamics interface)
```

## Requirements

- **pyviamd**: VIAMD Python bindings (compiled with `VIAMD_ENABLE_PYTHON=ON`)
- **OpenMM**: ≥ 7.7.0 (`conda install -c conda-forge openmm`)
- **NumPy**: ≥ 1.19.0 (`pip install numpy`)
- **Matplotlib**: Optional, for plotting (`pip install matplotlib`)

## Installation

1. **Build VIAMD with Python support**:
   ```bash
   mkdir build && cd build
   cmake .. -DVIAMD_ENABLE_PYTHON=ON -DCMAKE_BUILD_TYPE=Release
   make -j$(nproc)
   export PYTHONPATH="$PWD/lib:$PYTHONPATH"
   ```

2. **Install OpenMM**:
   ```bash
   conda install -c conda-forge openmm
   ```

3. **Test installation**:
   ```python
   from pyviamd.dynamics import check_requirements
   print(check_requirements())
   ```

## Troubleshooting

### Common Issues

1. **Import Error**: "pyviamd not available"
   - Build VIAMD with `VIAMD_ENABLE_PYTHON=ON`
   - Add build directory to `PYTHONPATH`

2. **OpenMM Error**: "OpenMM not available"
   - Install OpenMM: `conda install -c conda-forge openmm`

3. **Force Field Error**: "Failed to create force field"
   - Check force field names (use `amber14`, `charmm36`, etc.)
   - Ensure force field XML files are available

4. **Performance Issues**:
   - Reduce `report_interval` for large simulations
   - Disable `real_time_analysis` for better performance
   - Use `max_frames` limit in TrajectoryManager

### Getting Help

- Check the example scripts in `python/examples/`
- Run the test suite: `python test_dynamics_interface.py`
- Review the API documentation in the source code

## Examples

See the following example scripts:

- `python/examples/viamd_dynamics_example.py`: Comprehensive demonstration
- `test_dynamics_interface.py`: Test suite and validation

## API Reference

### Core Classes

- `DynamicsRunner`: Main interface for MD simulations
- `SimulationProtocol`: Enum defining simulation protocols
- `ProtocolParameters`: Dataclass for simulation parameters
- `TrajectoryManager`: Trajectory data management
- `MonitoringSystem`: Performance monitoring and diagnostics

### Convenience Functions

- `quick_md()`: One-line MD simulation
- `check_requirements()`: Verify installation

### Integration Modules

The dynamics interface integrates with existing VIAMD integration modules:

- `pyviamd.integrations.openmm_integration`: Advanced OpenMM workflows
- `pyviamd.integrations.analysis_integration`: Molecular analysis tools
- `pyviamd.integrations.visualization_integration`: Visualization utilities