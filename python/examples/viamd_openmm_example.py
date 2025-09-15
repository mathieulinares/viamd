#!/usr/bin/env python3
"""
Example script demonstrating VIAMD Python bindings integration with OpenMM.

This script shows how to:
1. Load molecular data using VIAMD
2. Set up OpenMM simulation
3. Run dynamics and feed coordinates back to VIAMD
4. Visualize results

Requirements:
- pyviamd (compiled with VIAMD_ENABLE_PYTHON=ON)
- openmm
- numpy
"""

import numpy as np
try:
    import openmm as mm
    import openmm.app as app
    import openmm.unit as unit
    HAS_OPENMM = True
except ImportError:
    print("OpenMM not available. Install with: conda install -c conda-forge openmm")
    HAS_OPENMM = False

try:
    import pyviamd
    HAS_PYVIAMD = True
except ImportError:
    print("pyviamd not available. Build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    HAS_PYVIAMD = False


class VIAMDOpenMMInterface:
    """Interface class for VIAMD and OpenMM integration."""
    
    def __init__(self):
        self.viamd_molecule = None
        self.openmm_system = None
        self.openmm_simulation = None
        self.trajectory_coords = []
        
    def load_molecule_from_viamd(self, structure_file):
        """Load molecular structure using VIAMD."""
        if not HAS_PYVIAMD:
            raise RuntimeError("pyviamd not available")
            
        # Load structure using VIAMD
        if structure_file.endswith('.pdb'):
            self.viamd_molecule = pyviamd.molecule.load_pdb(structure_file)
        else:
            raise ValueError(f"Unsupported file format for OpenMM: {structure_file}")
            
        if self.viamd_molecule is None:
            raise RuntimeError(f"Failed to load molecule from {structure_file}")
            
        print(f"Loaded molecule with {self.viamd_molecule.n_atoms} atoms from VIAMD")
        return True
        
    def setup_openmm_simulation(self, structure_file, force_field='amber14-all.xml'):
        """Set up OpenMM simulation from VIAMD molecule data."""
        if not HAS_OPENMM:
            raise RuntimeError("OpenMM not available")
            
        # Load PDB for OpenMM (OpenMM needs topology information)
        pdb = app.PDBFile(structure_file)
        
        # Create force field
        forcefield = app.ForceField(force_field, 'amber14/tip3pfb.xml')
        
        # Create system
        self.openmm_system = forcefield.createSystem(
            pdb.topology,
            nonbondedMethod=app.PME,
            nonbondedCutoff=1*unit.nanometer,
            constraints=app.HBonds
        )
        
        # Set up integrator
        integrator = mm.LangevinMiddleIntegrator(
            300*unit.kelvin,    # Temperature
            1/unit.picosecond,  # Friction coefficient
            0.004*unit.picoseconds  # Step size
        )
        
        # Create simulation
        self.openmm_simulation = app.Simulation(pdb.topology, self.openmm_system, integrator)
        
        # Set initial positions from VIAMD if available
        if self.viamd_molecule is not None:
            coords = self.viamd_molecule.atom.coordinates
            # Convert to OpenMM units (nm)
            coords_nm = coords * 0.1  # Assuming VIAMD coordinates are in Angstroms
            self.openmm_simulation.context.setPositions(coords_nm * unit.nanometer)
        else:
            self.openmm_simulation.context.setPositions(pdb.positions)
            
        # Minimize energy
        print("Minimizing energy...")
        self.openmm_simulation.minimizeEnergy()
        
        # Set initial velocities
        self.openmm_simulation.context.setVelocitiesToTemperature(300*unit.kelvin)
        
        print("OpenMM simulation setup complete")
        
    def run_dynamics_with_viamd_feedback(self, n_steps=1000, report_interval=100):
        """Run OpenMM dynamics and feed coordinates back to VIAMD."""
        if not self.openmm_simulation:
            raise RuntimeError("OpenMM simulation not set up")
            
        if not self.viamd_molecule:
            raise RuntimeError("VIAMD molecule not loaded")
            
        print(f"Running {n_steps} steps of dynamics...")
        
        # Clear previous trajectory
        self.trajectory_coords = []
        
        for step in range(n_steps):
            # Run one step
            self.openmm_simulation.step(1)
            
            if step % report_interval == 0:
                # Get current state
                state = self.openmm_simulation.context.getState(getPositions=True, getEnergy=True)
                
                # Extract coordinates
                positions = state.getPositions(asNumpy=True)
                coords_angstrom = positions.value_in_unit(unit.angstrom)
                
                # Update VIAMD molecule coordinates
                self.viamd_molecule.set_coordinates(coords_angstrom)
                
                # Store for trajectory
                self.trajectory_coords.append(coords_angstrom.copy())
                
                # Get energy
                potential_energy = state.getPotentialEnergy()
                kinetic_energy = state.getKineticEnergy()
                
                print(f"Step {step}: PE = {potential_energy:.2f}, KE = {kinetic_energy:.2f}")
                
        print(f"Dynamics complete. Collected {len(self.trajectory_coords)} frames")
        
    def export_trajectory_to_viamd_format(self, output_file):
        """Export the computed trajectory in a format VIAMD can read."""
        if not self.trajectory_coords:
            raise RuntimeError("No trajectory data available")
            
        # Simple XYZ format export
        n_atoms = len(self.trajectory_coords[0])
        
        with open(output_file, 'w') as f:
            for frame_idx, coords in enumerate(self.trajectory_coords):
                f.write(f"{n_atoms}\\n")
                f.write(f"Frame {frame_idx}\\n")
                
                for atom_idx in range(n_atoms):
                    x, y, z = coords[atom_idx]
                    f.write(f"C {x:.6f} {y:.6f} {z:.6f}\\n")
                    
        print(f"Trajectory exported to {output_file}")
        
    def analyze_dynamics(self):
        """Perform basic analysis of the dynamics trajectory."""
        if not self.trajectory_coords:
            return {}
            
        coords_array = np.array(self.trajectory_coords)
        n_frames, n_atoms, _ = coords_array.shape
        
        # Calculate center of mass for each frame
        center_of_mass = np.mean(coords_array, axis=1)
        
        # Calculate RMSD from first frame
        first_frame = coords_array[0]
        rmsd_values = []
        
        for frame in coords_array:
            diff = frame - first_frame
            rmsd = np.sqrt(np.mean(np.sum(diff**2, axis=1)))
            rmsd_values.append(rmsd)
            
        # Calculate radius of gyration
        rg_values = []
        for frame in coords_array:
            com = np.mean(frame, axis=0)
            distances = np.linalg.norm(frame - com, axis=1)
            rg = np.sqrt(np.mean(distances**2))
            rg_values.append(rg)
            
        analysis_results = {
            'n_frames': n_frames,
            'n_atoms': n_atoms,
            'center_of_mass_trajectory': center_of_mass,
            'rmsd_from_initial': rmsd_values,
            'radius_of_gyration': rg_values,
            'final_rmsd': rmsd_values[-1] if rmsd_values else 0.0,
            'average_rg': np.mean(rg_values) if rg_values else 0.0
        }
        
        return analysis_results


def main():
    """Demonstration of VIAMD-OpenMM integration."""
    print("VIAMD-OpenMM Integration Demo")
    print("=" * 40)
    
    # Check requirements
    if not HAS_PYVIAMD:
        print("ERROR: pyviamd not available.")
        print("Build VIAMD with: cmake .. -DVIAMD_ENABLE_PYTHON=ON")
        return
        
    if not HAS_OPENMM:
        print("ERROR: OpenMM not available.")
        print("Install with: conda install -c conda-forge openmm")
        return
        
    # Test core functionality
    print("\\nTesting VIAMD core functionality:")
    pyviamd.core.log_info("VIAMD-OpenMM integration initialized")
    
    version = pyviamd.core.get_version()
    print(f"VIAMD Version: {version}")
    
    # Test with sample data (if available)
    print("\\nFor full demonstration, provide a PDB file:")
    print("  python viamd_openmm_example.py structure.pdb")
    
    # Create interface instance
    interface = VIAMDOpenMMInterface()
    
    print("\\nVIAMD-OpenMM interface ready!")
    print("\\nExample workflow:")
    print("  1. interface.load_molecule_from_viamd('structure.pdb')")
    print("  2. interface.setup_openmm_simulation('structure.pdb')")
    print("  3. interface.run_dynamics_with_viamd_feedback(n_steps=1000)")
    print("  4. results = interface.analyze_dynamics()")
    print("  5. interface.export_trajectory_to_viamd_format('trajectory.xyz')")


if __name__ == "__main__":
    main()