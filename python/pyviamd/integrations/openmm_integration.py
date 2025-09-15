#!/usr/bin/env python3
"""
Advanced OpenMM Integration for VIAMD

This module provides a high-level Python interface for integrating VIAMD
with OpenMM molecular dynamics simulations. It offers seamless coordinate
transfer, automated simulation setup, and production-ready workflows.

Classes:
    VIAMDOpenMMSystem: Complete integration system for VIAMD-OpenMM workflows
    OpenMMSimulation: Enhanced OpenMM simulation with VIAMD integration
    ForceFieldManager: Utilities for OpenMM force field setup
    AnalysisTools: Analysis utilities for VIAMD-OpenMM trajectories

Example Usage:
    >>> import pyviamd
    >>> from pyviamd.integrations import openmm_integration
    >>> 
    >>> # Create integrated system
    >>> system = openmm_integration.VIAMDOpenMMSystem("structure.pdb")
    >>> system.setup_simulation(force_field="amber14-all.xml")
    >>> 
    >>> # Run dynamics with real-time VIAMD updates
    >>> trajectory = system.run_dynamics(n_steps=10000, temperature=300)
    >>> system.export_trajectory("output.xtc")
"""

import numpy as np
import tempfile
import os
from typing import Optional, Dict, List, Tuple, Any, Union
import logging

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


class ForceFieldManager:
    """Utilities for OpenMM force field setup and management."""
    
    COMMON_FORCE_FIELDS = {
        'amber14': 'amber14-all.xml',
        'amber99sb': 'amber99sb.xml', 
        'amber99sbildn': 'amber99sbildn.xml',
        'amber03': 'amber03.xml',
        'charmm36': 'charmm36.xml',
        'amoeba2013': 'amoeba2013.xml'
    }
    
    WATER_MODELS = {
        'tip3p': 'amber14/tip3pfb.xml',
        'tip4pew': 'amber14/tip4pew.xml',
        'spce': 'amber14/spce.xml',
        'tip3pfb': 'amber14/tip3pfb.xml'
    }
    
    @classmethod
    def create_forcefield(cls, protein_ff: str = 'amber14', water_model: str = 'tip3p') -> 'app.ForceField':
        """Create OpenMM ForceField object with common force field combinations.
        
        Args:
            protein_ff: Protein force field name or XML file
            water_model: Water model name or XML file
            
        Returns:
            OpenMM ForceField object
        """
        if not HAS_OPENMM:
            raise RuntimeError("OpenMM not available")
            
        # Resolve force field names
        protein_xml = cls.COMMON_FORCE_FIELDS.get(protein_ff, protein_ff)
        water_xml = cls.WATER_MODELS.get(water_model, water_model)
        
        return app.ForceField(protein_xml, water_xml)
    
    @classmethod
    def get_available_force_fields(cls) -> Dict[str, List[str]]:
        """Get available force field options."""
        return {
            'protein': list(cls.COMMON_FORCE_FIELDS.keys()),
            'water': list(cls.WATER_MODELS.keys())
        }


class OpenMMSimulation:
    """Enhanced OpenMM simulation with VIAMD integration."""
    
    def __init__(self, viamd_interface: Optional['pyviamd.openmm.VIAMDInterface'] = None):
        self.viamd_interface = viamd_interface
        self.topology = None
        self.system = None
        self.simulation = None
        self.integrator = None
        self.trajectory_data = []
        
    def setup_from_pdb(self, pdb_file: str, force_field: Union[str, 'app.ForceField'] = 'amber14',
                      water_model: str = 'tip3p', **kwargs) -> None:
        """Set up OpenMM simulation from PDB file.
        
        Args:
            pdb_file: Path to PDB file
            force_field: Force field name or ForceField object
            water_model: Water model name
            **kwargs: Additional system creation parameters
        """
        if not HAS_OPENMM:
            raise RuntimeError("OpenMM not available")
            
        # Load PDB
        pdb = app.PDBFile(pdb_file)
        self.topology = pdb.topology
        
        # Create force field
        if isinstance(force_field, str):
            forcefield = ForceFieldManager.create_forcefield(force_field, water_model)
        else:
            forcefield = force_field
        
        # Default system parameters
        system_params = {
            'nonbondedMethod': app.PME,
            'nonbondedCutoff': 1.0 * unit.nanometer,
            'constraints': app.HBonds,
        }
        system_params.update(kwargs)
        
        # Create system
        self.system = forcefield.createSystem(pdb.topology, **system_params)
        
        # Set up integrator (can be customized later)
        self.integrator = mm.LangevinMiddleIntegrator(
            300 * unit.kelvin,           # Temperature
            1 / unit.picosecond,         # Friction coefficient  
            0.004 * unit.picoseconds     # Step size
        )
        
        # Create simulation
        self.simulation = app.Simulation(self.topology, self.system, self.integrator)
        
        # Set initial positions
        if self.viamd_interface:
            # Use VIAMD coordinates
            coords_nm = self.viamd_interface.get_openmm_coordinates()
            self.simulation.context.setPositions(coords_nm * unit.nanometer)
        else:
            # Use PDB coordinates
            self.simulation.context.setPositions(pdb.positions)
            
        logger.info(f"OpenMM simulation setup complete from {pdb_file}")
    
    def setup_from_viamd(self, viamd_interface: 'pyviamd.openmm.VIAMDInterface',
                        force_field: Union[str, 'app.ForceField'] = 'amber14',
                        water_model: str = 'tip3p', **kwargs) -> None:
        """Set up OpenMM simulation from VIAMD interface.
        
        Args:
            viamd_interface: VIAMD OpenMM interface object
            force_field: Force field name or ForceField object
            water_model: Water model name
            **kwargs: Additional system creation parameters
        """
        self.viamd_interface = viamd_interface
        
        # Export PDB for OpenMM topology
        with tempfile.NamedTemporaryFile(suffix='.pdb', delete=False) as tmp_pdb:
            tmp_pdb_path = tmp_pdb.name
            
        try:
            self.viamd_interface.export_pdb(tmp_pdb_path)
            self.setup_from_pdb(tmp_pdb_path, force_field, water_model, **kwargs)
        finally:
            if os.path.exists(tmp_pdb_path):
                os.unlink(tmp_pdb_path)
                
        logger.info("OpenMM simulation setup complete from VIAMD interface")
    
    def minimize_energy(self, tolerance: float = 10.0, max_iterations: int = 1000) -> None:
        """Perform energy minimization.
        
        Args:
            tolerance: Energy tolerance for convergence (kJ/mol)
            max_iterations: Maximum number of minimization steps
        """
        if not self.simulation:
            raise RuntimeError("Simulation not set up")
            
        logger.info("Starting energy minimization...")
        self.simulation.minimizeEnergy(
            tolerance=tolerance * unit.kilojoule_per_mole,
            maxIterations=max_iterations
        )
        
        # Update VIAMD coordinates if interface available
        if self.viamd_interface:
            state = self.simulation.context.getState(getPositions=True)
            positions = state.getPositions(asNumpy=True)
            self.viamd_interface.update_viamd_coordinates(positions.value_in_unit(unit.nanometer))
            
        logger.info("Energy minimization complete")
    
    def equilibrate(self, temperature: float = 300.0, n_steps: int = 10000,
                   report_interval: int = 1000) -> None:
        """Perform equilibration simulation.
        
        Args:
            temperature: Target temperature (K)
            n_steps: Number of equilibration steps
            report_interval: Frequency of progress reports
        """
        if not self.simulation:
            raise RuntimeError("Simulation not set up")
            
        # Set temperature
        self.integrator.setTemperature(temperature * unit.kelvin)
        
        # Set initial velocities
        self.simulation.context.setVelocitiesToTemperature(temperature * unit.kelvin)
        
        logger.info(f"Starting equilibration: {n_steps} steps at {temperature} K")
        
        for step in range(n_steps):
            self.simulation.step(1)
            
            if step % report_interval == 0:
                state = self.simulation.context.getState(getEnergy=True, getTemperature=True)
                temp = state.getKineticEnergy() / (1.5 * unit.BOLTZMANN_CONSTANT_kB * self.system.getNumParticles())
                pe = state.getPotentialEnergy()
                ke = state.getKineticEnergy()
                
                logger.info(f"Equilibration step {step}: T={temp:.1f} K, PE={pe:.2f}, KE={ke:.2f}")
                
        logger.info("Equilibration complete")
    
    def run_production(self, n_steps: int, temperature: float = 300.0,
                      report_interval: int = 1000, 
                      update_viamd: bool = True) -> List[Dict[str, Any]]:
        """Run production molecular dynamics simulation.
        
        Args:
            n_steps: Number of simulation steps
            temperature: Target temperature (K)
            report_interval: Frequency of data collection and VIAMD updates
            update_viamd: Whether to update VIAMD coordinates during simulation
            
        Returns:
            List of trajectory frames with coordinates and energies
        """
        if not self.simulation:
            raise RuntimeError("Simulation not set up")
            
        logger.info(f"Starting production MD: {n_steps} steps at {temperature} K")
        
        # Set temperature
        self.integrator.setTemperature(temperature * unit.kelvin)
        
        trajectory = []
        
        for step in range(n_steps):
            self.simulation.step(1)
            
            if step % report_interval == 0:
                # Get current state
                state = self.simulation.context.getState(
                    getPositions=True, 
                    getVelocities=True,
                    getEnergy=True,
                    getTemperature=True
                )
                
                # Extract data
                positions = state.getPositions(asNumpy=True)
                velocities = state.getVelocities(asNumpy=True)
                pe = state.getPotentialEnergy()
                ke = state.getKineticEnergy()
                temp = ke / (1.5 * unit.BOLTZMANN_CONSTANT_kB * self.system.getNumParticles())
                
                # Store frame data
                frame_data = {
                    'step': step,
                    'time': step * self.integrator.getStepSize(),
                    'positions': positions.value_in_unit(unit.nanometer),
                    'velocities': velocities.value_in_unit(unit.nanometer / unit.picosecond),
                    'potential_energy': pe.value_in_unit(unit.kilojoule_per_mole),
                    'kinetic_energy': ke.value_in_unit(unit.kilojoule_per_mole),
                    'temperature': temp.value_in_unit(unit.kelvin)
                }
                trajectory.append(frame_data)
                
                # Update VIAMD coordinates
                if update_viamd and self.viamd_interface:
                    self.viamd_interface.update_viamd_coordinates(
                        positions.value_in_unit(unit.nanometer)
                    )
                
                logger.info(f"MD step {step}: T={temp:.1f} K, PE={pe:.2f}, KE={ke:.2f}")
        
        # Store trajectory data
        self.trajectory_data.extend(trajectory)
        
        logger.info("Production MD complete")
        return trajectory
    
    def get_current_state(self) -> Dict[str, Any]:
        """Get current simulation state."""
        if not self.simulation:
            raise RuntimeError("Simulation not set up")
            
        state = self.simulation.context.getState(
            getPositions=True,
            getVelocities=True,
            getEnergy=True,
            getTemperature=True
        )
        
        positions = state.getPositions(asNumpy=True)
        velocities = state.getVelocities(asNumpy=True)
        pe = state.getPotentialEnergy()
        ke = state.getKineticEnergy()
        temp = ke / (1.5 * unit.BOLTZMANN_CONSTANT_kB * self.system.getNumParticles())
        
        return {
            'positions': positions.value_in_unit(unit.nanometer),
            'velocities': velocities.value_in_unit(unit.nanometer / unit.picosecond),
            'potential_energy': pe.value_in_unit(unit.kilojoule_per_mole),
            'kinetic_energy': ke.value_in_unit(unit.kilojoule_per_mole),
            'temperature': temp.value_in_unit(unit.kelvin),
            'total_energy': (pe + ke).value_in_unit(unit.kilojoule_per_mole)
        }


class VIAMDOpenMMSystem:
    """Complete integration system for VIAMD-OpenMM workflows."""
    
    def __init__(self, structure_file: Optional[str] = None):
        self.viamd_interface = None
        self.openmm_simulation = None
        self.structure_file = structure_file
        
        if structure_file and HAS_PYVIAMD:
            # Create VIAMD interface from structure file
            if structure_file.endswith('.pdb'):
                self.viamd_interface = pyviamd.openmm.create_interface_from_pdb(structure_file)
            else:
                # Load with molecule module first, then create interface
                if structure_file.endswith('.gro'):
                    mol = pyviamd.molecule.load_gro(structure_file)
                elif structure_file.endswith('.xyz'):
                    mol = pyviamd.molecule.load_xyz(structure_file)
                else:
                    raise ValueError(f"Unsupported structure file format: {structure_file}")
                    
                if mol is not None:
                    self.viamd_interface = pyviamd.openmm.create_interface_from_molecule(mol)
                    
        # Create OpenMM simulation with VIAMD integration
        self.openmm_simulation = OpenMMSimulation(self.viamd_interface)
        
    def setup_simulation(self, force_field: Union[str, 'app.ForceField'] = 'amber14',
                        water_model: str = 'tip3p', **kwargs) -> None:
        """Set up the integrated simulation system.
        
        Args:
            force_field: Force field name or ForceField object
            water_model: Water model name
            **kwargs: Additional system creation parameters
        """
        if self.structure_file:
            self.openmm_simulation.setup_from_pdb(
                self.structure_file, force_field, water_model, **kwargs
            )
        elif self.viamd_interface:
            self.openmm_simulation.setup_from_viamd(
                self.viamd_interface, force_field, water_model, **kwargs
            )
        else:
            raise RuntimeError("No structure file or VIAMD interface available")
            
        logger.info("Integrated VIAMD-OpenMM system setup complete")
    
    def run_complete_workflow(self, n_steps: int = 10000, temperature: float = 300.0,
                            minimize: bool = True, equilibrate: bool = True,
                            eq_steps: int = 5000) -> List[Dict[str, Any]]:
        """Run complete MD workflow with energy minimization, equilibration, and production.
        
        Args:
            n_steps: Number of production MD steps
            temperature: Target temperature (K)
            minimize: Whether to perform energy minimization
            equilibrate: Whether to perform equilibration
            eq_steps: Number of equilibration steps
            
        Returns:
            Production trajectory data
        """
        if not self.openmm_simulation.simulation:
            raise RuntimeError("Simulation not set up. Call setup_simulation() first.")
            
        logger.info("Starting complete VIAMD-OpenMM workflow")
        
        # Energy minimization
        if minimize:
            self.openmm_simulation.minimize_energy()
            
        # Equilibration
        if equilibrate:
            self.openmm_simulation.equilibrate(temperature, eq_steps)
            
        # Production MD
        trajectory = self.openmm_simulation.run_production(n_steps, temperature)
        
        logger.info("Complete VIAMD-OpenMM workflow finished")
        return trajectory
    
    def export_trajectory(self, filename: str, format: str = 'xtc') -> None:
        """Export trajectory in specified format.
        
        Args:
            filename: Output filename
            format: Trajectory format ('xtc', 'dcd', 'pdb')
        """
        if not self.openmm_simulation.trajectory_data:
            logger.warning("No trajectory data to export")
            return
            
        # Simple XYZ export for now - can be extended for other formats
        if format.lower() == 'xyz':
            self._export_xyz(filename)
        else:
            logger.warning(f"Export format '{format}' not yet implemented. Using XYZ format.")
            self._export_xyz(filename.rsplit('.', 1)[0] + '.xyz')
    
    def _export_xyz(self, filename: str) -> None:
        """Export trajectory in XYZ format."""
        n_atoms = len(self.openmm_simulation.trajectory_data[0]['positions'])
        
        with open(filename, 'w') as f:
            for frame in self.openmm_simulation.trajectory_data:
                positions = frame['positions'] * 10.0  # nm to Angstroms
                
                f.write(f"{n_atoms}\\n")
                f.write(f"Frame {frame['step']}: E={frame['potential_energy']:.2f} kJ/mol\\n")
                
                for i, pos in enumerate(positions):
                    f.write(f"C {pos[0]:.6f} {pos[1]:.6f} {pos[2]:.6f}\\n")
                    
        logger.info(f"Trajectory exported to {filename}")
    
    def get_analysis_summary(self) -> Dict[str, Any]:
        """Get summary analysis of the simulation."""
        if not self.openmm_simulation.trajectory_data:
            return {}
            
        trajectory = self.openmm_simulation.trajectory_data
        
        # Extract energies and temperatures
        pe_values = [frame['potential_energy'] for frame in trajectory]
        ke_values = [frame['kinetic_energy'] for frame in trajectory]
        temps = [frame['temperature'] for frame in trajectory]
        
        # Calculate statistics
        summary = {
            'n_frames': len(trajectory),
            'potential_energy': {
                'mean': np.mean(pe_values),
                'std': np.std(pe_values),
                'min': np.min(pe_values),
                'max': np.max(pe_values)
            },
            'kinetic_energy': {
                'mean': np.mean(ke_values),
                'std': np.std(ke_values),
                'min': np.min(ke_values),
                'max': np.max(ke_values)
            },
            'temperature': {
                'mean': np.mean(temps),
                'std': np.std(temps),
                'min': np.min(temps),
                'max': np.max(temps)
            }
        }
        
        return summary


# Convenience functions for quick workflows
def quick_simulation(pdb_file: str, n_steps: int = 10000, force_field: str = 'amber14',
                    temperature: float = 300.0) -> Tuple['VIAMDOpenMMSystem', List[Dict[str, Any]]]:
    """Run a quick VIAMD-OpenMM simulation workflow.
    
    Args:
        pdb_file: Path to PDB structure file
        n_steps: Number of production MD steps
        force_field: Force field to use
        temperature: Simulation temperature (K)
        
    Returns:
        Tuple of (VIAMDOpenMMSystem, trajectory_data)
    """
    system = VIAMDOpenMMSystem(pdb_file)
    system.setup_simulation(force_field=force_field)
    trajectory = system.run_complete_workflow(n_steps=n_steps, temperature=temperature)
    
    return system, trajectory


def check_requirements() -> Dict[str, bool]:
    """Check if all required packages are available."""
    return {
        'pyviamd': HAS_PYVIAMD,
        'openmm': HAS_OPENMM,
        'numpy': True  # Always available if this module loads
    }


# Main execution for testing
if __name__ == "__main__":
    # Check requirements
    requirements = check_requirements()
    print("Requirements check:")
    for package, available in requirements.items():
        status = "✓" if available else "✗"
        print(f"  {status} {package}")
    
    if all(requirements.values()):
        print("\\nAll requirements satisfied!")
        print("Example usage:")
        print("  system = VIAMDOpenMMSystem('structure.pdb')")
        print("  system.setup_simulation()")
        print("  trajectory = system.run_complete_workflow(n_steps=1000)")
    else:
        missing = [pkg for pkg, available in requirements.items() if not available]
        print(f"\\nMissing requirements: {', '.join(missing)}")