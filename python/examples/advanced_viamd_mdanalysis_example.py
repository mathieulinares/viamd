#!/usr/bin/env python3
"""
Advanced VIAMD-MDAnalysis Integration Example

This script demonstrates the comprehensive Phase 3 MDAnalysis integration
with C++ level bindings and production-ready analysis workflows.

Features demonstrated:
1. Advanced VIAMDMDAnalysisSystem with C++ bindings
2. Seamless Universe creation with proper topology handling
3. Comprehensive analysis pipeline with multiple analysis types
4. Efficient coordinate synchronization between VIAMD and MDAnalysis
5. Real-time analysis result visualization integration
6. Memory-efficient trajectory processing

Requirements:
- pyviamd (compiled with VIAMD_ENABLE_PYTHON=ON)
- MDAnalysis >= 2.0.0
- numpy

Usage:
    python advanced_viamd_mdanalysis_example.py [structure.pdb] [trajectory.xtc]
"""

import sys
import numpy as np
from pathlib import Path

try:
    import MDAnalysis as mda
    HAS_MDANALYSIS = True
    print(f"✓ MDAnalysis {mda.__version__} available")
except ImportError:
    HAS_MDANALYSIS = False
    print("✗ MDAnalysis not available. Install with: pip install MDAnalysis>=2.0.0")

try:
    import pyviamd
    from pyviamd.integrations import mdanalysis_integration
    HAS_PYVIAMD = True
    print(f"✓ pyviamd {pyviamd.__version__} available")
except ImportError:
    HAS_PYVIAMD = False
    print("✗ pyviamd not available. Build VIAMD with VIAMD_ENABLE_PYTHON=ON")


def demonstrate_advanced_integration():
    """Demonstrate advanced VIAMD-MDAnalysis integration capabilities."""
    
    print("\n" + "="*60)
    print("ADVANCED VIAMD-MDANALYSIS INTEGRATION DEMO (Phase 3)")
    print("="*60)
    
    if not HAS_PYVIAMD or not HAS_MDANALYSIS:
        print("\n❌ Missing required dependencies. Cannot run demo.")
        return False
    
    # Test core functionality
    print("\n🔧 Testing VIAMD Core Functionality:")
    pyviamd.core.log_info("VIAMD-MDAnalysis advanced integration initialized")
    
    version = pyviamd.core.get_version()
    print(f"   VIAMD Version: {version}")
    
    temp_size = pyviamd.core.get_temp_allocator_max_size()
    temp_pos = pyviamd.core.get_temp_allocator_position()
    print(f"   Memory Allocator: max_size={temp_size}, current_pos={temp_pos}")
    
    # Test C++ MDAnalysis bindings
    print("\n🧪 Testing C++ MDAnalysis Bindings:")
    try:
        # Create sample molecular data
        n_atoms = 100
        coordinates = np.random.rand(n_atoms, 3) * 10.0  # Random coordinates in 10Å box
        elements = ['C'] * 50 + ['O'] * 30 + ['N'] * 20  # Mixed elements
        residue_names = ['ALA'] * 30 + ['GLY'] * 30 + ['VAL'] * 40
        residue_ids = list(range(n_atoms))
        chain_ids = ['A'] * n_atoms
        atom_names = [f'ATOM{i:04d}' for i in range(n_atoms)]
        
        # Test C++ interface
        interface = pyviamd.mdanalysis.VIAMDMDAnalysisInterface()
        interface.initialize_from_arrays(
            coordinates, elements, residue_names,
            residue_ids, chain_ids, atom_names
        )
        
        print(f"   ✓ Created C++ interface with {interface.n_atoms} atoms")
        print(f"   ✓ Topology: {interface.n_residues} residues, {interface.n_chains} chains")
        
        # Test topology conversion
        topology = interface.get_topology_dict()
        print(f"   ✓ Generated topology dictionary with {len(topology)} properties")
        
        # Test calculations
        com = interface.calculate_center_of_mass()
        rg = interface.calculate_radius_of_gyration()
        print(f"   ✓ Center of mass: [{com[0]:.2f}, {com[1]:.2f}, {com[2]:.2f}]")
        print(f"   ✓ Radius of gyration: {rg:.2f} Å")
        
    except Exception as e:
        print(f"   ❌ C++ bindings test failed: {e}")
        return False
    
    # Test high-level integration system
    print("\n🏗️  Testing High-Level Integration System:")
    try:
        # Create integration system without file (using synthetic data)
        system = mdanalysis_integration.VIAMDMDAnalysisSystem()
        
        # Create synthetic VIAMD molecule
        print("   Creating synthetic molecular data...")
        
        # This would normally be loaded from file, but we'll simulate it
        # by creating a mock molecule with the required attributes
        class MockMolecule:
            def __init__(self):
                self.n_atoms = n_atoms
                self.n_residues = len(set(residue_names))
                
                class MockAtom:
                    def __init__(self):
                        self.coordinates = coordinates
                        self.elements = elements
                        self.residue_names = residue_names
                        self.residue_ids = residue_ids
                        self.chain_ids = chain_ids
                        self.names = atom_names
                
                self.atom = MockAtom()
            
            def set_coordinates(self, new_coords):
                self.atom.coordinates = new_coords
        
        system.viamd_molecule = MockMolecule()
        system._create_mdanalysis_interface()
        
        print(f"   ✓ Created integration system with {system.viamd_molecule.n_atoms} atoms")
        
        # Create MDAnalysis Universe
        universe = system.create_universe()
        print(f"   ✓ Created MDAnalysis Universe with {len(universe.atoms)} atoms")
        
        # Test coordinate synchronization
        new_coords = coordinates + np.random.rand(*coordinates.shape) * 0.1
        system.update_coordinates(new_coords)
        print("   ✓ Coordinate synchronization successful")
        
    except Exception as e:
        print(f"   ❌ High-level integration test failed: {e}")
        return False
    
    # Test analysis pipeline
    print("\n📊 Testing Analysis Pipeline:")
    try:
        analyses = ['rg', 'com', 'distances']
        results = system.run_analysis_pipeline(analyses)
        
        print(f"   ✓ Completed {len(results)} analyses:")
        for analysis_name, result in results.items():
            if isinstance(result, dict):
                print(f"      - {analysis_name}: {len(result)} properties")
            else:
                print(f"      - {analysis_name}: {type(result).__name__}")
        
        # Export results
        system.export_analysis_results("test_output")
        print("   ✓ Exported analysis results")
        
    except Exception as e:
        print(f"   ❌ Analysis pipeline test failed: {e}")
        return False
    
    # Test trajectory interface
    print("\n🎬 Testing Trajectory Interface:")
    try:
        traj_interface = pyviamd.mdanalysis.MDAnalysisTrajectoryInterface()
        n_frames = 10
        traj_interface.initialize(n_frames, n_atoms)
        
        # Add sample frames
        for frame_idx in range(n_frames):
            frame_coords = coordinates + np.random.rand(*coordinates.shape) * 0.5
            traj_interface.add_frame(frame_idx, frame_coords)
        
        print(f"   ✓ Created trajectory interface with {traj_interface.n_frames} frames")
        
        # Test frame retrieval
        frame_0 = traj_interface.get_frame(0)
        print(f"   ✓ Retrieved frame 0 with shape {frame_0.shape}")
        
        metadata = traj_interface.get_metadata()
        print(f"   ✓ Trajectory metadata: {metadata['n_frames']} frames, "
              f"{metadata['total_time']:.1f} time units")
        
    except Exception as e:
        print(f"   ❌ Trajectory interface test failed: {e}")
        return False
    
    # Test utility functions
    print("\n🛠️  Testing Utility Functions:")
    try:
        # Test topology conversion utility
        topology_dict = mdanalysis_integration.convert_viamd_to_mdanalysis_topology(
            system.viamd_molecule
        )
        print(f"   ✓ Converted topology: {topology_dict['n_atoms']} atoms")
        
        # Test system info
        system_info = system.get_system_info()
        print(f"   ✓ System info: {len(system_info)} properties")
        print(f"      - VIAMD version: {system_info['viamd_version']}")
        print(f"      - MDAnalysis version: {system_info['mdanalysis_version']}")
        
    except Exception as e:
        print(f"   ❌ Utility functions test failed: {e}")
        return False
    
    print("\n🎉 ALL TESTS PASSED! Advanced MDAnalysis integration is working correctly.")
    return True


def demonstrate_with_files(structure_file: str, trajectory_file: str = None):
    """Demonstrate integration with actual molecular files."""
    
    print(f"\n📁 Testing with real molecular files:")
    print(f"   Structure: {structure_file}")
    if trajectory_file:
        print(f"   Trajectory: {trajectory_file}")
    
    try:
        # Create system with real files
        system = mdanalysis_integration.VIAMDMDAnalysisSystem(structure_file)
        print(f"   ✓ Loaded structure with {system.viamd_molecule.n_atoms} atoms")
        
        # Create Universe
        universe = system.create_universe(trajectory_file)
        print(f"   ✓ Created MDAnalysis Universe")
        
        # Run comprehensive analysis
        analyses = ['rg', 'com', 'rmsd']
        if trajectory_file:
            analyses.append('rdf')
        
        results = system.run_analysis_pipeline(analyses, trajectory_file)
        print(f"   ✓ Completed analysis pipeline with {len(results)} analyses")
        
        # Display results
        for analysis_name, result in results.items():
            if analysis_name == 'radius_of_gyration':
                print(f"      - Radius of gyration: {result['radius_of_gyration']:.3f} Å")
            elif analysis_name == 'center_of_mass':
                com = result['center_of_mass']
                print(f"      - Center of mass: [{com[0]:.2f}, {com[1]:.2f}, {com[2]:.2f}] Å")
            elif analysis_name == 'rmsd':
                print(f"      - RMSD: {result['rmsd']:.3f} Å")
        
        # Export results
        system.export_analysis_results(f"real_data_{Path(structure_file).stem}")
        print("   ✓ Exported analysis results")
        
        # Load trajectory if provided
        if trajectory_file:
            system.load_trajectory(trajectory_file)
            print("   ✓ Loaded and processed trajectory")
        
        return True
        
    except Exception as e:
        print(f"   ❌ Real file test failed: {e}")
        return False


def main():
    """Main demonstration function."""
    
    # Always run the synthetic data demonstration
    success = demonstrate_advanced_integration()
    
    if not success:
        print("\n❌ Core functionality tests failed. Cannot proceed with file tests.")
        return 1
    
    # Check for command line arguments for real file testing
    if len(sys.argv) >= 2:
        structure_file = sys.argv[1]
        trajectory_file = sys.argv[2] if len(sys.argv) >= 3 else None
        
        if Path(structure_file).exists():
            file_success = demonstrate_with_files(structure_file, trajectory_file)
            if not file_success:
                print("\n⚠️  File-based tests failed, but core functionality works.")
        else:
            print(f"\n⚠️  Structure file not found: {structure_file}")
            print("   Skipping file-based tests.")
    else:
        print(f"\n💡 To test with real molecular files, run:")
        print(f"   python {Path(__file__).name} structure.pdb [trajectory.xtc]")
    
    print(f"\n✨ Phase 3 MDAnalysis Integration Demo Complete!")
    print(f"   Advanced C++ bindings and workflow integration successfully implemented.")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())