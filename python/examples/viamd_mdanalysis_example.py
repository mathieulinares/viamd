#!/usr/bin/env python3
"""
Example script demonstrating VIAMD Python bindings integration with MDAnalysis.

This script shows how to:
1. Load molecular data using VIAMD's native loaders
2. Convert data to MDAnalysis Universe format
3. Perform analysis using MDAnalysis
4. Feed results back to VIAMD for visualization

Requirements:
- pyviamd (compiled with VIAMD_ENABLE_PYTHON=ON)
- MDAnalysis
- numpy
"""

import numpy as np
try:
    import MDAnalysis as mda
    from MDAnalysis.lib.distances import distance_array
    HAS_MDANALYSIS = True
except ImportError:
    print("MDAnalysis not available. Install with: pip install MDAnalysis")
    HAS_MDANALYSIS = False

try:
    import pyviamd
    HAS_PYVIAMD = True
except ImportError:
    print("pyviamd not available. Build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    HAS_PYVIAMD = False


class VIAMDMDAnalysisInterface:
    """Interface class for VIAMD and MDAnalysis integration."""
    
    def __init__(self):
        self.viamd_molecule = None
        self.mda_universe = None
        
    def load_from_viamd(self, structure_file, trajectory_file=None):
        """Load molecular data using VIAMD and convert to MDAnalysis format."""
        if not HAS_PYVIAMD:
            raise RuntimeError("pyviamd not available")
            
        # Load structure using VIAMD
        if structure_file.endswith('.pdb'):
            self.viamd_molecule = pyviamd.molecule.load_pdb(structure_file)
        elif structure_file.endswith('.gro'):
            self.viamd_molecule = pyviamd.molecule.load_gro(structure_file)
        elif structure_file.endswith('.xyz'):
            self.viamd_molecule = pyviamd.molecule.load_xyz(structure_file)
        else:
            raise ValueError(f"Unsupported file format: {structure_file}")
            
        if self.viamd_molecule is None:
            raise RuntimeError(f"Failed to load molecule from {structure_file}")
            
        print(f"Loaded molecule with {self.viamd_molecule.n_atoms} atoms from VIAMD")
        
        # Convert to MDAnalysis if available
        if HAS_MDANALYSIS:
            self._create_mdanalysis_universe(trajectory_file)
            
    def _create_mdanalysis_universe(self, trajectory_file=None):
        """Create MDAnalysis Universe from VIAMD molecule data."""
        # Get coordinates from VIAMD
        coords = self.viamd_molecule.atom.coordinates
        n_atoms = coords.shape[0]
        
        # Get atom properties
        residue_names = self.viamd_molecule.atom.residue_names
        chain_ids = self.viamd_molecule.atom.chain_ids
        
        # Create minimal topology for MDAnalysis
        # This is a simplified approach - in practice, you'd want more complete topology
        atom_types = ['C'] * n_atoms  # Default to carbon
        
        # Create Universe from coordinate array
        # Note: This requires MDAnalysis 2.0+ for from_arrays functionality
        try:
            self.mda_universe = mda.Universe.empty(
                n_atoms=n_atoms,
                trajectory=True  # Enable trajectory
            )
            
            # Add basic attributes
            self.mda_universe.add_TopologyAttr('names', ['ATOM'] * n_atoms)
            self.mda_universe.add_TopologyAttr('types', atom_types)
            self.mda_universe.add_TopologyAttr('resnames', residue_names)
            
            # Set coordinates
            self.mda_universe.atoms.positions = coords
            
            print(f"Created MDAnalysis Universe with {len(self.mda_universe.atoms)} atoms")
            
        except Exception as e:
            print(f"Note: Advanced MDAnalysis integration requires MDAnalysis 2.0+: {e}")
            # Fallback: create a simple demonstration
            self.mda_universe = None
            
    def calculate_distances(self, atom_indices1, atom_indices2):
        """Calculate distances between sets of atoms using MDAnalysis."""
        if not HAS_MDANALYSIS or self.mda_universe is None:
            # Fallback using numpy
            coords = self.viamd_molecule.atom.coordinates
            pos1 = coords[atom_indices1]
            pos2 = coords[atom_indices2]
            return np.linalg.norm(pos1 - pos2, axis=1)
            
        # Use MDAnalysis distance calculation
        group1 = self.mda_universe.atoms[atom_indices1]
        group2 = self.mda_universe.atoms[atom_indices2]
        
        return distance_array(group1.positions, group2.positions, box=None)
        
    def analyze_trajectory(self, trajectory_file):
        """Analyze trajectory using VIAMD trajectory loader and MDAnalysis."""
        if not HAS_PYVIAMD:
            raise RuntimeError("pyviamd not available")
            
        # Load trajectory using VIAMD
        if trajectory_file.endswith('.xtc'):
            trajectory = pyviamd.trajectory.load_xtc(trajectory_file)
        elif trajectory_file.endswith('.trr'):
            trajectory = pyviamd.trajectory.load_trr(trajectory_file)
        else:
            raise ValueError(f"Unsupported trajectory format: {trajectory_file}")
            
        if trajectory is None:
            raise RuntimeError(f"Failed to load trajectory from {trajectory_file}")
            
        # Get trajectory information
        header = trajectory.get_header()
        if header is None:
            raise RuntimeError("Failed to get trajectory header")
            
        print(f"Trajectory: {header.num_frames} frames, {header.num_atoms} atoms")
        
        # Analyze a few frames
        n_frames_to_analyze = min(10, header.num_frames)
        results = []
        
        for frame_idx in range(n_frames_to_analyze):
            frame_data = pyviamd.trajectory.extract_frame_data_for_mdanalysis(
                trajectory, frame_idx, header.num_atoms
            )
            
            if 'coordinates' in frame_data:
                coords = frame_data['coordinates']
                # Update molecule coordinates in VIAMD
                self.viamd_molecule.set_coordinates(coords)
                
                # Perform simple analysis (center of mass)
                com = np.mean(coords, axis=0)
                
                results.append({
                    'frame': frame_idx,
                    'timestamp': frame_data.get('timestamp', 0.0),
                    'center_of_mass': com,
                    'max_distance_from_com': np.max(np.linalg.norm(coords - com, axis=1))
                })
                
        return results
        
    def export_analysis_to_viamd(self, analysis_results):
        """Export analysis results in a format suitable for VIAMD visualization."""
        # This would integrate with VIAMD's property system
        print("Analysis results summary:")
        for result in analysis_results:
            print(f"  Frame {result['frame']}: COM = {result['center_of_mass']}, "
                  f"Max dist = {result['max_distance_from_com']:.2f}")


def main():
    """Demonstration of VIAMD-MDAnalysis integration."""
    print("VIAMD-MDAnalysis Integration Demo")
    print("=" * 40)
    
    # Check requirements
    if not HAS_PYVIAMD:
        print("ERROR: pyviamd not available.")
        print("Build VIAMD with: cmake .. -DVIAMD_ENABLE_PYTHON=ON")
        return
        
    if not HAS_MDANALYSIS:
        print("WARNING: MDAnalysis not available. Limited functionality.")
        
    # Test core functionality
    print("\nTesting VIAMD core functionality:")
    pyviamd.core.log_info("VIAMD Python bindings initialized")
    
    version = pyviamd.core.get_version()
    print(f"Version: {version}")
    
    temp_size = pyviamd.core.get_temp_allocator_max_size()
    temp_pos = pyviamd.core.get_temp_allocator_position()
    print(f"Temp allocator: max_size={temp_size}, current_pos={temp_pos}")
    
    # Test with sample data (if available)
    print("\nFor full demonstration, provide structure and trajectory files:")
    print("  python viamd_mdanalysis_example.py structure.pdb trajectory.xtc")
    
    # Create interface instance
    interface = VIAMDMDAnalysisInterface()
    
    print("\nVIAMD-MDAnalysis interface ready for use!")


if __name__ == "__main__":
    main()