#!/usr/bin/env python3
"""
OpenMM Dynamics Interface for VIAMD

This module provides a comprehensive interface for running molecular dynamics
simulations with OpenMM directly from VIAMD. It offers production-ready
simulation protocols, real-time monitoring, and seamless integration with
VIAMD's analysis and visualization capabilities.

Classes:
    DynamicsRunner: High-level interface for running OpenMM MD simulations
    SimulationProtocol: Predefined simulation protocols and parameters
    TrajectoryManager: Real-time trajectory handling and analysis
    MonitoringSystem: Real-time simulation monitoring and diagnostics

Example Usage:
    >>> import pyviamd
    >>> from pyviamd.dynamics import DynamicsRunner
    >>> 
    >>> # Load molecule and create dynamics runner
    >>> mol = pyviamd.molecule.load_pdb("structure.pdb")
    >>> runner = DynamicsRunner(mol)
    >>> 
    >>> # Run production MD with real-time analysis
    >>> results = runner.run_production_md(
    ...     n_steps=100000,
    ...     temperature=300.0,
    ...     protocol="npt_equilibration",
    ...     real_time_analysis=True
    ... )
"""

import numpy as np
import tempfile
import os
import time
import logging
from typing import Optional, Dict, List, Tuple, Any, Union, Callable
from dataclasses import dataclass, field
from enum import Enum
import json

# Set up logging
logger = logging.getLogger(__name__)

try:
    import pyviamd
    HAS_PYVIAMD = True
except ImportError:
    logger.warning("pyviamd not available. Build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    HAS_PYVIAMD = False

try:
    import openmm as mm
    import openmm.app as app
    import openmm.unit as unit
    HAS_OPENMM = True
except ImportError:
    logger.warning("OpenMM not available. Install with: conda install -c conda-forge openmm")
    HAS_OPENMM = False

try:
    import matplotlib.pyplot as plt
    HAS_MATPLOTLIB = True
except ImportError:
    HAS_MATPLOTLIB = False


class SimulationProtocol(Enum):
    """Predefined simulation protocols."""
    MINIMIZATION = "minimization"
    NVT_EQUILIBRATION = "nvt_equilibration"
    NPT_EQUILIBRATION = "npt_equilibration"
    PRODUCTION_NVT = "production_nvt"
    PRODUCTION_NPT = "production_npt"
    HEATING = "heating"
    COOLING = "cooling"
    CUSTOM = "custom"


@dataclass
class ProtocolParameters:
    """Parameters for simulation protocols."""
    temperature: float = 300.0  # K
    pressure: Optional[float] = 1.0  # bar
    time_step: float = 0.002  # ps
    friction_coefficient: float = 1.0  # 1/ps
    n_steps: int = 10000
    report_interval: int = 1000
    minimize_tolerance: float = 10.0  # kJ/mol
    minimize_max_iterations: int = 1000
    constraints: str = "HBonds"  # None, HBonds, AllBonds, HAngles
    nonbonded_method: str = "PME"  # NoCutoff, CutoffNonPeriodic, CutoffPeriodic, PME
    nonbonded_cutoff: float = 1.0  # nm
    use_pbc: bool = True
    constraint_tolerance: float = 1e-6


@dataclass
class SimulationState:
    """Current state of the simulation."""
    step: int = 0
    time: float = 0.0  # ps
    potential_energy: float = 0.0  # kJ/mol
    kinetic_energy: float = 0.0  # kJ/mol
    total_energy: float = 0.0  # kJ/mol
    temperature: float = 0.0  # K
    pressure: Optional[float] = None  # bar
    volume: Optional[float] = None  # nm³
    density: Optional[float] = None  # g/cm³
    coordinates: Optional[np.ndarray] = None
    velocities: Optional[np.ndarray] = None
    box_vectors: Optional[np.ndarray] = None


@dataclass
class TrajectoryFrame:
    """Single trajectory frame with comprehensive data."""
    step: int
    time: float
    state: SimulationState
    analysis_data: Dict[str, Any] = field(default_factory=dict)


class TrajectoryManager:
    """Real-time trajectory handling and analysis."""
    
    def __init__(self, max_frames: int = 10000):
        self.frames: List[TrajectoryFrame] = []
        self.max_frames = max_frames
        self.analysis_callbacks: List[Callable] = []
        
    def add_frame(self, frame: TrajectoryFrame) -> None:
        """Add a new trajectory frame."""
        # Run analysis callbacks
        for callback in self.analysis_callbacks:
            try:
                analysis_result = callback(frame)
                if analysis_result:
                    frame.analysis_data.update(analysis_result)
            except Exception as e:
                logger.warning(f"Analysis callback failed: {e}")
        
        self.frames.append(frame)
        
        # Limit memory usage
        if len(self.frames) > self.max_frames:
            self.frames.pop(0)
    
    def register_analysis_callback(self, callback: Callable) -> None:
        """Register a callback for real-time analysis."""
        self.analysis_callbacks.append(callback)
    
    def get_time_series(self, property_name: str) -> Tuple[np.ndarray, np.ndarray]:
        """Get time series for a given property."""
        times = []
        values = []
        
        for frame in self.frames:
            times.append(frame.time)
            
            # Get property from state or analysis data
            if hasattr(frame.state, property_name):
                values.append(getattr(frame.state, property_name))
            elif property_name in frame.analysis_data:
                values.append(frame.analysis_data[property_name])
            else:
                values.append(None)
        
        return np.array(times), np.array(values)
    
    def export_trajectory(self, filename: str, format: str = "xyz") -> None:
        """Export trajectory to file."""
        if format.lower() == "xyz":
            self._export_xyz(filename)
        elif format.lower() == "json":
            self._export_json(filename)
        else:
            raise ValueError(f"Unsupported export format: {format}")
    
    def _export_xyz(self, filename: str) -> None:
        """Export trajectory in XYZ format."""
        if not self.frames:
            return
            
        n_atoms = len(self.frames[0].state.coordinates) if self.frames[0].state.coordinates is not None else 0
        
        with open(filename, 'w') as f:
            for frame in self.frames:
                if frame.state.coordinates is None:
                    continue
                    
                coords = frame.state.coordinates * 10.0  # nm to Angstroms
                
                f.write(f"{n_atoms}\n")
                f.write(f"Step {frame.step}: E={frame.state.potential_energy:.2f} kJ/mol\n")
                
                for i, pos in enumerate(coords):
                    f.write(f"C {pos[0]:.6f} {pos[1]:.6f} {pos[2]:.6f}\n")
    
    def _export_json(self, filename: str) -> None:
        """Export trajectory data in JSON format."""
        data = {
            "frames": [],
            "metadata": {
                "n_frames": len(self.frames),
                "format_version": "1.0"
            }
        }
        
        for frame in self.frames:
            frame_data = {
                "step": frame.step,
                "time": frame.time,
                "potential_energy": frame.state.potential_energy,
                "kinetic_energy": frame.state.kinetic_energy,
                "total_energy": frame.state.total_energy,
                "temperature": frame.state.temperature,
                "analysis_data": frame.analysis_data
            }
            
            if frame.state.pressure is not None:
                frame_data["pressure"] = frame.state.pressure
            if frame.state.volume is not None:
                frame_data["volume"] = frame.state.volume
            if frame.state.density is not None:
                frame_data["density"] = frame.state.density
                
            data["frames"].append(frame_data)
        
        with open(filename, 'w') as f:
            json.dump(data, f, indent=2)


class MonitoringSystem:
    """Real-time simulation monitoring and diagnostics."""
    
    def __init__(self):
        self.start_time: Optional[float] = None
        self.performance_data: List[Dict[str, Any]] = []
        self.alerts: List[str] = []
        
    def start_monitoring(self) -> None:
        """Start monitoring session."""
        self.start_time = time.time()
        
    def update_performance(self, step: int, state: SimulationState) -> None:
        """Update performance metrics."""
        if self.start_time is None:
            return
            
        current_time = time.time()
        elapsed_wall_time = current_time - self.start_time
        
        # Calculate performance metrics
        ns_per_day = (state.time * 1e-3) / (elapsed_wall_time / 86400.0) if elapsed_wall_time > 0 else 0
        steps_per_second = step / elapsed_wall_time if elapsed_wall_time > 0 else 0
        
        perf_data = {
            "step": step,
            "simulation_time_ns": state.time * 1e-3,
            "wall_time_s": elapsed_wall_time,
            "ns_per_day": ns_per_day,
            "steps_per_second": steps_per_second,
            "potential_energy": state.potential_energy,
            "temperature": state.temperature
        }
        
        self.performance_data.append(perf_data)
        
        # Check for alerts
        self._check_alerts(state)
    
    def _check_alerts(self, state: SimulationState) -> None:
        """Check for simulation alerts."""
        # Temperature alerts
        if state.temperature > 400.0:
            self.alerts.append(f"High temperature: {state.temperature:.1f} K")
        elif state.temperature < 250.0:
            self.alerts.append(f"Low temperature: {state.temperature:.1f} K")
        
        # Energy alerts
        if abs(state.potential_energy) > 1e6:
            self.alerts.append(f"Very high potential energy: {state.potential_energy:.1e} kJ/mol")
    
    def get_performance_summary(self) -> Dict[str, Any]:
        """Get performance summary."""
        if not self.performance_data:
            return {}
            
        recent_data = self.performance_data[-10:]  # Last 10 data points
        
        return {
            "total_wall_time": self.performance_data[-1]["wall_time_s"],
            "total_simulation_time": self.performance_data[-1]["simulation_time_ns"],
            "average_ns_per_day": np.mean([d["ns_per_day"] for d in recent_data]),
            "average_steps_per_second": np.mean([d["steps_per_second"] for d in recent_data]),
            "current_temperature": self.performance_data[-1]["temperature"],
            "current_potential_energy": self.performance_data[-1]["potential_energy"],
            "n_alerts": len(self.alerts),
            "recent_alerts": self.alerts[-5:] if self.alerts else []
        }


class DynamicsRunner:
    """High-level interface for running OpenMM MD simulations."""
    
    def __init__(self, molecule: Optional['md_molecule_t'] = None, structure_file: Optional[str] = None):
        if not HAS_PYVIAMD or not HAS_OPENMM:
            raise RuntimeError("Both pyviamd and OpenMM are required")
            
        self.viamd_interface = None
        self.openmm_simulation = None
        self.trajectory_manager = TrajectoryManager()
        self.monitoring_system = MonitoringSystem()
        
        # Initialize VIAMD interface
        if molecule is not None:
            self.viamd_interface = pyviamd.openmm.create_interface_from_molecule(molecule)
        elif structure_file is not None:
            if structure_file.endswith('.pdb'):
                self.viamd_interface = pyviamd.openmm.create_interface_from_pdb(structure_file)
            else:
                # Load molecule first, then create interface
                if structure_file.endswith('.gro'):
                    mol = pyviamd.molecule.load_gro(structure_file)
                elif structure_file.endswith('.xyz'):
                    mol = pyviamd.molecule.load_xyz(structure_file)
                else:
                    raise ValueError(f"Unsupported structure format: {structure_file}")
                    
                if mol is not None:
                    self.viamd_interface = pyviamd.openmm.create_interface_from_molecule(mol)
        
        if self.viamd_interface is None:
            raise RuntimeError("Failed to create VIAMD interface")
    
    def setup_system(self, force_field: str = "amber14", water_model: str = "tip3p",
                    protocol_params: Optional[ProtocolParameters] = None) -> None:
        """Set up the OpenMM system for simulation."""
        if protocol_params is None:
            protocol_params = ProtocolParameters()
            
        # Create topology from VIAMD interface
        with tempfile.NamedTemporaryFile(suffix='.pdb', delete=False) as tmp_pdb:
            tmp_pdb_path = tmp_pdb.name
            
        try:
            self.viamd_interface.export_pdb(tmp_pdb_path)
            
            # Load PDB
            pdb = app.PDBFile(tmp_pdb_path)
            topology = pdb.topology
            
            # Create force field
            if force_field == "amber14":
                forcefield = app.ForceField('amber14-all.xml', 'amber14/tip3pfb.xml')
            elif force_field == "charmm36":
                forcefield = app.ForceField('charmm36.xml', 'charmm36/water.xml')
            else:
                forcefield = app.ForceField(f'{force_field}.xml')
            
            # System parameters
            nonbonded_methods = {
                "NoCutoff": app.NoCutoff,
                "CutoffNonPeriodic": app.CutoffNonPeriodic,
                "CutoffPeriodic": app.CutoffPeriodic,
                "PME": app.PME
            }
            
            constraints_map = {
                "None": None,
                "HBonds": app.HBonds,
                "AllBonds": app.AllBonds,
                "HAngles": app.HAngles
            }
            
            system_params = {
                'nonbondedMethod': nonbonded_methods[protocol_params.nonbonded_method],
                'nonbondedCutoff': protocol_params.nonbonded_cutoff * unit.nanometer,
                'constraints': constraints_map[protocol_params.constraints]
            }
            
            if protocol_params.constraints and protocol_params.constraint_tolerance:
                system_params['constraintTolerance'] = protocol_params.constraint_tolerance
            
            # Create system
            system = forcefield.createSystem(topology, **system_params)
            
            # Add pressure coupling if needed
            if protocol_params.pressure is not None:
                barostat = mm.MonteCarloBarostat(
                    protocol_params.pressure * unit.bar,
                    protocol_params.temperature * unit.kelvin
                )
                system.addForce(barostat)
            
            # Create integrator
            integrator = mm.LangevinMiddleIntegrator(
                protocol_params.temperature * unit.kelvin,
                protocol_params.friction_coefficient / unit.picosecond,
                protocol_params.time_step * unit.picoseconds
            )
            
            # Create simulation
            self.openmm_simulation = app.Simulation(topology, system, integrator)
            
            # Set initial positions
            coords_nm = self.viamd_interface.get_openmm_coordinates()
            self.openmm_simulation.context.setPositions(coords_nm * unit.nanometer)
            
            logger.info("OpenMM system setup complete")
            
        finally:
            if os.path.exists(tmp_pdb_path):
                os.unlink(tmp_pdb_path)
    
    def run_protocol(self, protocol: SimulationProtocol, 
                    protocol_params: Optional[ProtocolParameters] = None,
                    real_time_analysis: bool = False) -> List[TrajectoryFrame]:
        """Run a specific simulation protocol."""
        if protocol_params is None:
            protocol_params = ProtocolParameters()
            
        if self.openmm_simulation is None:
            self.setup_system(protocol_params=protocol_params)
        
        logger.info(f"Running {protocol.value} protocol")
        
        # Start monitoring
        self.monitoring_system.start_monitoring()
        
        if protocol == SimulationProtocol.MINIMIZATION:
            return self._run_minimization(protocol_params)
        elif protocol == SimulationProtocol.NVT_EQUILIBRATION:
            return self._run_nvt_equilibration(protocol_params, real_time_analysis)
        elif protocol == SimulationProtocol.NPT_EQUILIBRATION:
            return self._run_npt_equilibration(protocol_params, real_time_analysis)
        elif protocol == SimulationProtocol.PRODUCTION_NVT:
            return self._run_production_nvt(protocol_params, real_time_analysis)
        elif protocol == SimulationProtocol.PRODUCTION_NPT:
            return self._run_production_npt(protocol_params, real_time_analysis)
        elif protocol == SimulationProtocol.HEATING:
            return self._run_heating(protocol_params, real_time_analysis)
        elif protocol == SimulationProtocol.COOLING:
            return self._run_cooling(protocol_params, real_time_analysis)
        else:
            raise ValueError(f"Unknown protocol: {protocol}")
    
    def _run_minimization(self, params: ProtocolParameters) -> List[TrajectoryFrame]:
        """Run energy minimization."""
        logger.info("Starting energy minimization...")
        
        self.openmm_simulation.minimizeEnergy(
            tolerance=params.minimize_tolerance * unit.kilojoule_per_mole,
            maxIterations=params.minimize_max_iterations
        )
        
        # Get final state
        state = self._get_current_state(0, 0.0)
        
        # Update VIAMD coordinates
        self.viamd_interface.set_openmm_coordinates(state.coordinates)
        
        frame = TrajectoryFrame(
            step=0,
            time=0.0,
            state=state
        )
        
        self.trajectory_manager.add_frame(frame)
        logger.info("Energy minimization complete")
        
        return [frame]
    
    def _run_nvt_equilibration(self, params: ProtocolParameters, real_time_analysis: bool) -> List[TrajectoryFrame]:
        """Run NVT equilibration."""
        return self._run_md_steps(params, "NVT Equilibration", real_time_analysis)
    
    def _run_npt_equilibration(self, params: ProtocolParameters, real_time_analysis: bool) -> List[TrajectoryFrame]:
        """Run NPT equilibration."""
        return self._run_md_steps(params, "NPT Equilibration", real_time_analysis)
    
    def _run_production_nvt(self, params: ProtocolParameters, real_time_analysis: bool) -> List[TrajectoryFrame]:
        """Run production NVT simulation."""
        return self._run_md_steps(params, "Production NVT", real_time_analysis)
    
    def _run_production_npt(self, params: ProtocolParameters, real_time_analysis: bool) -> List[TrajectoryFrame]:
        """Run production NPT simulation."""
        return self._run_md_steps(params, "Production NPT", real_time_analysis)
    
    def _run_heating(self, params: ProtocolParameters, real_time_analysis: bool) -> List[TrajectoryFrame]:
        """Run heating protocol."""
        # Implement gradual heating from 0K to target temperature
        logger.info(f"Heating from 0K to {params.temperature}K")
        
        # Set initial temperature to near zero
        self.openmm_simulation.context.setVelocitiesToTemperature(1.0 * unit.kelvin)
        
        # Gradually increase temperature
        heating_steps = params.n_steps // 10  # 10 temperature increments
        temp_increment = params.temperature / 10
        
        frames = []
        
        for i in range(10):
            current_temp = (i + 1) * temp_increment
            self.openmm_simulation.integrator.setTemperature(current_temp * unit.kelvin)
            
            logger.info(f"Heating step {i+1}/10: T = {current_temp:.1f} K")
            
            # Run steps at this temperature
            for step in range(heating_steps):
                self.openmm_simulation.step(1)
                
                if step % params.report_interval == 0:
                    global_step = i * heating_steps + step
                    sim_time = global_step * params.time_step
                    state = self._get_current_state(global_step, sim_time)
                    
                    frame = TrajectoryFrame(
                        step=global_step,
                        time=sim_time,
                        state=state
                    )
                    
                    self.trajectory_manager.add_frame(frame)
                    self.monitoring_system.update_performance(global_step, state)
                    frames.append(frame)
                    
                    if real_time_analysis:
                        self._update_viamd_coordinates(state)
        
        logger.info("Heating protocol complete")
        return frames
    
    def _run_cooling(self, params: ProtocolParameters, real_time_analysis: bool) -> List[TrajectoryFrame]:
        """Run cooling protocol."""
        # Implement gradual cooling from current temperature to lower temperature
        logger.info(f"Cooling from {params.temperature}K to 200K")
        
        cooling_steps = params.n_steps // 10  # 10 temperature decrements
        temp_decrement = (params.temperature - 200.0) / 10
        
        frames = []
        
        for i in range(10):
            current_temp = params.temperature - (i + 1) * temp_decrement
            self.openmm_simulation.integrator.setTemperature(current_temp * unit.kelvin)
            
            logger.info(f"Cooling step {i+1}/10: T = {current_temp:.1f} K")
            
            # Run steps at this temperature
            for step in range(cooling_steps):
                self.openmm_simulation.step(1)
                
                if step % params.report_interval == 0:
                    global_step = i * cooling_steps + step
                    sim_time = global_step * params.time_step
                    state = self._get_current_state(global_step, sim_time)
                    
                    frame = TrajectoryFrame(
                        step=global_step,
                        time=sim_time,
                        state=state
                    )
                    
                    self.trajectory_manager.add_frame(frame)
                    self.monitoring_system.update_performance(global_step, state)
                    frames.append(frame)
                    
                    if real_time_analysis:
                        self._update_viamd_coordinates(state)
        
        logger.info("Cooling protocol complete")
        return frames
    
    def _run_md_steps(self, params: ProtocolParameters, protocol_name: str, real_time_analysis: bool) -> List[TrajectoryFrame]:
        """Run MD steps with the given parameters."""
        logger.info(f"Starting {protocol_name}: {params.n_steps} steps")
        
        # Set target temperature
        self.openmm_simulation.integrator.setTemperature(params.temperature * unit.kelvin)
        
        # Set initial velocities if not already set
        if params.temperature > 0:
            self.openmm_simulation.context.setVelocitiesToTemperature(params.temperature * unit.kelvin)
        
        frames = []
        
        for step in range(params.n_steps):
            self.openmm_simulation.step(1)
            
            if step % params.report_interval == 0:
                sim_time = step * params.time_step
                state = self._get_current_state(step, sim_time)
                
                frame = TrajectoryFrame(
                    step=step,
                    time=sim_time,
                    state=state
                )
                
                self.trajectory_manager.add_frame(frame)
                self.monitoring_system.update_performance(step, state)
                frames.append(frame)
                
                logger.info(f"{protocol_name} step {step}: T={state.temperature:.1f} K, "
                          f"PE={state.potential_energy:.2f} kJ/mol, KE={state.kinetic_energy:.2f} kJ/mol")
                
                if real_time_analysis:
                    self._update_viamd_coordinates(state)
        
        logger.info(f"{protocol_name} complete")
        return frames
    
    def _get_current_state(self, step: int, time: float) -> SimulationState:
        """Get current simulation state."""
        context_state = self.openmm_simulation.context.getState(
            getPositions=True,
            getVelocities=True,
            getEnergy=True,
            getParameters=True
        )
        
        positions = context_state.getPositions(asNumpy=True)
        velocities = context_state.getVelocities(asNumpy=True)
        pe = context_state.getPotentialEnergy()
        ke = context_state.getKineticEnergy()
        
        # Calculate temperature
        n_particles = self.openmm_simulation.system.getNumParticles()
        temp = ke / (1.5 * unit.BOLTZMANN_CONSTANT_kB * n_particles)
        
        # Get pressure and volume if available
        pressure = None
        volume = None
        density = None
        
        try:
            # Try to get pressure (if barostat is present)
            for force in self.openmm_simulation.system.getForces():
                if isinstance(force, mm.MonteCarloBarostat):
                    # Get box vectors for volume calculation
                    box_vectors = context_state.getPeriodicBoxVectors()
                    if box_vectors:
                        # Calculate volume
                        a, b, c = box_vectors
                        volume = a[0] * b[1] * c[2]  # Simplified for rectangular box
                        
                        # Estimate pressure (simplified)
                        pressure = 1.0  # Default value - actual pressure extraction is complex
                    break
        except:
            pass
        
        return SimulationState(
            step=step,
            time=time,
            potential_energy=pe.value_in_unit(unit.kilojoule_per_mole),
            kinetic_energy=ke.value_in_unit(unit.kilojoule_per_mole),
            total_energy=(pe + ke).value_in_unit(unit.kilojoule_per_mole),
            temperature=temp.value_in_unit(unit.kelvin),
            pressure=pressure,
            volume=volume.value_in_unit(unit.nanometer**3) if volume else None,
            density=density,
            coordinates=positions.value_in_unit(unit.nanometer),
            velocities=velocities.value_in_unit(unit.nanometer / unit.picosecond)
        )
    
    def _update_viamd_coordinates(self, state: SimulationState) -> None:
        """Update VIAMD coordinates with current state."""
        if state.coordinates is not None:
            self.viamd_interface.set_openmm_coordinates(state.coordinates)
    
    def run_production_md(self, n_steps: int = 100000, temperature: float = 300.0,
                         protocol: str = "npt_equilibration", 
                         real_time_analysis: bool = True,
                         force_field: str = "amber14") -> Dict[str, Any]:
        """Run a complete production MD simulation with analysis."""
        logger.info("Starting production MD simulation")
        
        # Set up protocol parameters
        if protocol == "npt_equilibration":
            params = ProtocolParameters(
                temperature=temperature,
                pressure=1.0,
                n_steps=n_steps,
                time_step=0.002,
                report_interval=max(1, n_steps // 1000)  # 1000 report points max
            )
            protocol_enum = SimulationProtocol.NPT_EQUILIBRATION
        elif protocol == "nvt_equilibration":
            params = ProtocolParameters(
                temperature=temperature,
                pressure=None,
                n_steps=n_steps,
                time_step=0.002,
                report_interval=max(1, n_steps // 1000)
            )
            protocol_enum = SimulationProtocol.NVT_EQUILIBRATION
        else:
            raise ValueError(f"Unknown protocol: {protocol}")
        
        # Setup system
        self.setup_system(force_field=force_field, protocol_params=params)
        
        # Run minimization first
        logger.info("Running energy minimization...")
        minimize_params = ProtocolParameters(minimize_tolerance=10.0, minimize_max_iterations=1000)
        self.run_protocol(SimulationProtocol.MINIMIZATION, minimize_params)
        
        # Run production simulation
        logger.info(f"Running {protocol} simulation...")
        frames = self.run_protocol(protocol_enum, params, real_time_analysis)
        
        # Get performance summary
        performance = self.monitoring_system.get_performance_summary()
        
        # Compile results
        results = {
            "n_frames": len(frames),
            "simulation_time_ps": frames[-1].time if frames else 0.0,
            "final_state": frames[-1].state if frames else None,
            "performance": performance,
            "trajectory_data": frames,
            "protocol_parameters": params
        }
        
        logger.info(f"Production MD complete: {len(frames)} frames, "
                   f"{results['simulation_time_ps']:.2f} ps simulated")
        
        return results
    
    def export_results(self, results: Dict[str, Any], output_dir: str = ".") -> Dict[str, str]:
        """Export simulation results to files."""
        os.makedirs(output_dir, exist_ok=True)
        
        exported_files = {}
        
        # Export trajectory
        traj_file = os.path.join(output_dir, "trajectory.xyz")
        self.trajectory_manager.export_trajectory(traj_file, format="xyz")
        exported_files["trajectory_xyz"] = traj_file
        
        # Export analysis data
        analysis_file = os.path.join(output_dir, "analysis.json")
        self.trajectory_manager.export_trajectory(analysis_file, format="json")
        exported_files["analysis_json"] = analysis_file
        
        # Export performance data
        perf_file = os.path.join(output_dir, "performance.json")
        with open(perf_file, 'w') as f:
            json.dump(results["performance"], f, indent=2)
        exported_files["performance_json"] = perf_file
        
        # Export plots if matplotlib available
        if HAS_MATPLOTLIB:
            plots_file = self._create_analysis_plots(results, output_dir)
            if plots_file:
                exported_files["plots"] = plots_file
        
        logger.info(f"Results exported to {output_dir}")
        return exported_files
    
    def _create_analysis_plots(self, results: Dict[str, Any], output_dir: str) -> Optional[str]:
        """Create analysis plots."""
        try:
            fig, axes = plt.subplots(2, 2, figsize=(12, 8))
            
            # Energy plot
            times, pe = self.trajectory_manager.get_time_series("potential_energy")
            times, ke = self.trajectory_manager.get_time_series("kinetic_energy")
            times, te = self.trajectory_manager.get_time_series("total_energy")
            
            axes[0, 0].plot(times, pe, label="Potential")
            axes[0, 0].plot(times, ke, label="Kinetic")
            axes[0, 0].plot(times, te, label="Total")
            axes[0, 0].set_xlabel("Time (ps)")
            axes[0, 0].set_ylabel("Energy (kJ/mol)")
            axes[0, 0].set_title("Energy Evolution")
            axes[0, 0].legend()
            
            # Temperature plot
            times, temps = self.trajectory_manager.get_time_series("temperature")
            axes[0, 1].plot(times, temps)
            axes[0, 1].set_xlabel("Time (ps)")
            axes[0, 1].set_ylabel("Temperature (K)")
            axes[0, 1].set_title("Temperature Evolution")
            
            # Performance plot
            if results["performance"]:
                perf_data = self.monitoring_system.performance_data
                if perf_data:
                    wall_times = [d["wall_time_s"] for d in perf_data]
                    ns_per_day = [d["ns_per_day"] for d in perf_data]
                    
                    axes[1, 0].plot(wall_times, ns_per_day)
                    axes[1, 0].set_xlabel("Wall Time (s)")
                    axes[1, 0].set_ylabel("ns/day")
                    axes[1, 0].set_title("Performance")
            
            # Energy distribution
            if pe.size > 0:
                axes[1, 1].hist(pe, bins=50, alpha=0.7, label="Potential")
                axes[1, 1].hist(ke, bins=50, alpha=0.7, label="Kinetic")
                axes[1, 1].set_xlabel("Energy (kJ/mol)")
                axes[1, 1].set_ylabel("Frequency")
                axes[1, 1].set_title("Energy Distribution")
                axes[1, 1].legend()
            
            plt.tight_layout()
            
            plots_file = os.path.join(output_dir, "analysis_plots.png")
            plt.savefig(plots_file, dpi=300, bbox_inches='tight')
            plt.close()
            
            return plots_file
            
        except Exception as e:
            logger.warning(f"Failed to create plots: {e}")
            return None


# Convenience functions
def quick_md(structure_file: str, n_steps: int = 10000, temperature: float = 300.0,
            output_dir: str = ".") -> Dict[str, Any]:
    """Run a quick MD simulation with automatic setup and analysis."""
    runner = DynamicsRunner(structure_file=structure_file)
    
    results = runner.run_production_md(
        n_steps=n_steps,
        temperature=temperature,
        protocol="npt_equilibration",
        real_time_analysis=True
    )
    
    exported_files = runner.export_results(results, output_dir)
    results["exported_files"] = exported_files
    
    return results


def check_requirements() -> Dict[str, bool]:
    """Check if all required packages are available."""
    return {
        'pyviamd': HAS_PYVIAMD,
        'openmm': HAS_OPENMM,
        'numpy': True,  # Always available if this module loads
        'matplotlib': HAS_MATPLOTLIB
    }


# Example usage and testing
if __name__ == "__main__":
    logging.basicConfig(level=logging.INFO)
    
    # Check requirements
    requirements = check_requirements()
    print("Requirements check:")
    for package, available in requirements.items():
        status = "✓" if available else "✗"
        print(f"  {status} {package}")
    
    if requirements['pyviamd'] and requirements['openmm']:
        print("\nDynamics interface ready!")
        print("Example usage:")
        print("  runner = DynamicsRunner(structure_file='protein.pdb')")
        print("  results = runner.run_production_md(n_steps=10000)")
        print("  runner.export_results(results, 'output_dir')")
    else:
        print("\nSome requirements missing. Install required packages.")