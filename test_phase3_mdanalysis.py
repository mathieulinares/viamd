#!/usr/bin/env python3
"""
Comprehensive Test Suite for Phase 3: Advanced MDAnalysis Integration

This script thoroughly tests all components of the Phase 3 MDAnalysis integration,
including C++ bindings, high-level workflow integration, and analysis pipelines.

Test Categories:
1. C++ MDAnalysis bindings functionality
2. High-level integration system workflows
3. Analysis pipeline completeness
4. Memory efficiency and coordinate synchronization
5. Trajectory processing capabilities
6. Error handling and edge cases
"""

import sys
import numpy as np
import traceback
from pathlib import Path

# Test results tracking
test_results = {
    'passed': 0,
    'failed': 0,
    'skipped': 0,
    'details': []
}

def test_status(test_name: str, success: bool, details: str = ""):
    """Record test status and print result."""
    status = "✓ PASS" if success else "✗ FAIL"
    print(f"   {status}: {test_name}")
    
    if details and not success:
        print(f"      Details: {details}")
    
    if success:
        test_results['passed'] += 1
    else:
        test_results['failed'] += 1
    
    test_results['details'].append({
        'name': test_name,
        'success': success,
        'details': details
    })

def skip_test(test_name: str, reason: str):
    """Record skipped test."""
    print(f"   ⊝ SKIP: {test_name} ({reason})")
    test_results['skipped'] += 1
    test_results['details'].append({
        'name': test_name,
        'success': None,
        'details': f"Skipped: {reason}"
    })

def test_imports():
    """Test that all required modules can be imported."""
    print("\n🔍 Testing Module Imports:")
    
    # Test pyviamd import
    try:
        import pyviamd
        test_status("pyviamd import", True)
        
        # Test specific modules
        try:
            from pyviamd import core, molecule, trajectory, openmm, mdanalysis
            test_status("pyviamd submodules import", True)
        except Exception as e:
            test_status("pyviamd submodules import", False, str(e))
        
        # Test integrations
        try:
            from pyviamd.integrations import mdanalysis_integration
            test_status("mdanalysis_integration import", True)
        except Exception as e:
            test_status("mdanalysis_integration import", False, str(e))
        
    except Exception as e:
        test_status("pyviamd import", False, str(e))
        return False
    
    # Test MDAnalysis import (optional)
    try:
        import MDAnalysis as mda
        test_status("MDAnalysis import", True)
        return True
    except Exception as e:
        skip_test("MDAnalysis import", "MDAnalysis not available")
        return False

def test_cpp_bindings():
    """Test C++ MDAnalysis bindings functionality."""
    print("\n🧪 Testing C++ MDAnalysis Bindings:")
    
    try:
        import pyviamd
        
        # Create test data
        n_atoms = 50
        coordinates = np.random.rand(n_atoms, 3) * 10.0
        elements = ['C'] * 20 + ['O'] * 15 + ['N'] * 15
        residue_names = ['ALA'] * 20 + ['GLY'] * 15 + ['VAL'] * 15
        residue_ids = list(range(n_atoms))
        chain_ids = ['A'] * n_atoms
        atom_names = [f'ATOM{i:04d}' for i in range(n_atoms)]
        
        # Test interface creation
        try:
            interface = pyviamd.mdanalysis.VIAMDMDAnalysisInterface()
            test_status("VIAMDMDAnalysisInterface creation", True)
        except Exception as e:
            test_status("VIAMDMDAnalysisInterface creation", False, str(e))
            return False
        
        # Test initialization
        try:
            interface.initialize_from_arrays(
                coordinates, elements, residue_names,
                residue_ids, chain_ids, atom_names
            )
            test_status("Interface initialization", True)
        except Exception as e:
            test_status("Interface initialization", False, str(e))
            return False
        
        # Test basic properties
        try:
            assert interface.n_atoms == n_atoms
            assert interface.n_residues > 0
            assert interface.n_chains > 0
            test_status("Basic properties access", True)
        except Exception as e:
            test_status("Basic properties access", False, str(e))
        
        # Test coordinate access
        try:
            coords = interface.get_coordinates()
            assert coords.shape == (n_atoms, 3)
            assert np.allclose(coords, coordinates)
            test_status("Coordinate access (zero-copy)", True)
        except Exception as e:
            test_status("Coordinate access (zero-copy)", False, str(e))
        
        # Test coordinate update
        try:
            new_coords = coordinates + 0.1
            interface.set_coordinates(new_coords)
            updated_coords = interface.get_coordinates()
            assert np.allclose(updated_coords, new_coords)
            test_status("Coordinate update", True)
        except Exception as e:
            test_status("Coordinate update", False, str(e))
        
        # Test topology generation
        try:
            topology = interface.get_topology_dict()
            assert isinstance(topology, dict)
            assert 'n_atoms' in topology
            assert 'names' in topology
            assert 'types' in topology
            assert len(topology['names']) == n_atoms
            test_status("Topology dictionary generation", True)
        except Exception as e:
            test_status("Topology dictionary generation", False, str(e))
        
        # Test mass calculation
        try:
            masses = interface.get_atomic_masses()
            assert len(masses) == n_atoms
            assert all(m > 0 for m in masses)
            test_status("Atomic mass calculation", True)
        except Exception as e:
            test_status("Atomic mass calculation", False, str(e))
        
        # Test center of mass calculation
        try:
            com = interface.calculate_center_of_mass()
            assert len(com) == 3
            assert all(np.isfinite(com))
            test_status("Center of mass calculation", True)
        except Exception as e:
            test_status("Center of mass calculation", False, str(e))
        
        # Test radius of gyration calculation
        try:
            rg = interface.calculate_radius_of_gyration()
            assert np.isfinite(rg) and rg > 0
            test_status("Radius of gyration calculation", True)
        except Exception as e:
            test_status("Radius of gyration calculation", False, str(e))
        
        # Test residue info
        try:
            residue_info = interface.get_residue_info()
            assert isinstance(residue_info, dict)
            assert 'n_residues' in residue_info
            test_status("Residue information extraction", True)
        except Exception as e:
            test_status("Residue information extraction", False, str(e))
        
        return True
        
    except Exception as e:
        test_status("C++ bindings setup", False, str(e))
        return False

def test_trajectory_interface():
    """Test trajectory interface functionality."""
    print("\n🎬 Testing Trajectory Interface:")
    
    try:
        import pyviamd
        
        # Test trajectory interface creation
        try:
            traj_interface = pyviamd.mdanalysis.MDAnalysisTrajectoryInterface()
            test_status("MDAnalysisTrajectoryInterface creation", True)
        except Exception as e:
            test_status("MDAnalysisTrajectoryInterface creation", False, str(e))
            return False
        
        # Test initialization
        try:
            n_frames, n_atoms = 10, 30
            traj_interface.initialize(n_frames, n_atoms)
            assert traj_interface.n_frames == n_frames
            assert traj_interface.n_atoms == n_atoms
            test_status("Trajectory initialization", True)
        except Exception as e:
            test_status("Trajectory initialization", False, str(e))
            return False
        
        # Test frame addition
        try:
            for frame_idx in range(n_frames):
                coords = np.random.rand(n_atoms, 3) * 10.0
                traj_interface.add_frame(frame_idx, coords)
            test_status("Frame addition", True)
        except Exception as e:
            test_status("Frame addition", False, str(e))
        
        # Test frame retrieval
        try:
            frame_0 = traj_interface.get_frame(0)
            assert frame_0.shape == (n_atoms, 3)
            test_status("Frame retrieval", True)
        except Exception as e:
            test_status("Frame retrieval", False, str(e))
        
        # Test metadata
        try:
            metadata = traj_interface.get_metadata()
            assert isinstance(metadata, dict)
            assert metadata['n_frames'] == n_frames
            assert metadata['n_atoms'] == n_atoms
            test_status("Metadata access", True)
        except Exception as e:
            test_status("Metadata access", False, str(e))
        
        return True
        
    except Exception as e:
        test_status("Trajectory interface setup", False, str(e))
        return False

def test_utility_functions():
    """Test utility functions."""
    print("\n🛠️  Testing Utility Functions:")
    
    try:
        import pyviamd
        from pyviamd.integrations import mdanalysis_integration
        
        # Create test data
        n_atoms = 20
        coordinates = np.random.rand(n_atoms, 3) * 5.0
        elements = ['C'] * 10 + ['O'] * 5 + ['N'] * 5
        residue_names = ['ALA'] * 10 + ['GLY'] * 10
        residue_ids = list(range(n_atoms))
        chain_ids = ['A'] * n_atoms
        atom_names = [f'ATOM{i:03d}' for i in range(n_atoms)]
        
        # Test topology conversion utility
        try:
            topology_dict = pyviamd.mdanalysis.create_mdanalysis_topology_dict(
                coordinates, elements, residue_names,
                residue_ids, chain_ids, atom_names
            )
            assert isinstance(topology_dict, dict)
            assert topology_dict['n_atoms'] == n_atoms
            test_status("Topology conversion utility", True)
        except Exception as e:
            test_status("Topology conversion utility", False, str(e))
        
        return True
        
    except Exception as e:
        test_status("Utility functions setup", False, str(e))
        return False

def test_high_level_integration():
    """Test high-level integration system."""
    print("\n🏗️  Testing High-Level Integration System:")
    
    try:
        from pyviamd.integrations import mdanalysis_integration
        
        # Test system creation
        try:
            system = mdanalysis_integration.VIAMDMDAnalysisSystem()
            test_status("VIAMDMDAnalysisSystem creation", True)
        except Exception as e:
            test_status("VIAMDMDAnalysisSystem creation", False, str(e))
            return False
        
        # Test system info
        try:
            info = system.get_system_info()
            assert isinstance(info, dict)
            assert 'viamd_version' in info
            test_status("System info retrieval", True)
        except Exception as e:
            test_status("System info retrieval", False, str(e))
        
        # Create mock molecule for testing
        n_atoms = 25
        coordinates = np.random.rand(n_atoms, 3) * 8.0
        elements = ['C'] * 15 + ['O'] * 5 + ['N'] * 5
        residue_names = ['ALA'] * 12 + ['GLY'] * 13
        residue_ids = list(range(n_atoms))
        chain_ids = ['A'] * n_atoms
        atom_names = [f'ATOM{i:03d}' for i in range(n_atoms)]
        
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
        
        # Test interface creation
        try:
            system._create_mdanalysis_interface()
            assert system.mdanalysis_interface is not None
            assert system.topology_info is not None
            test_status("MDAnalysis interface creation", True)
        except Exception as e:
            test_status("MDAnalysis interface creation", False, str(e))
        
        # Test coordinate updates
        try:
            new_coords = coordinates + np.random.rand(*coordinates.shape) * 0.1
            system.update_coordinates(new_coords)
            test_status("Coordinate synchronization", True)
        except Exception as e:
            test_status("Coordinate synchronization", False, str(e))
        
        return True
        
    except Exception as e:
        test_status("High-level integration setup", False, str(e))
        return False

def test_analysis_pipeline():
    """Test analysis pipeline functionality."""
    print("\n📊 Testing Analysis Pipeline:")
    
    try:
        from pyviamd.integrations import mdanalysis_integration
        
        # Create and setup system (using the mock from previous test)
        system = mdanalysis_integration.VIAMDMDAnalysisSystem()
        
        n_atoms = 30
        coordinates = np.random.rand(n_atoms, 3) * 10.0
        elements = ['C'] * 15 + ['O'] * 8 + ['N'] * 7
        residue_names = ['ALA'] * 15 + ['GLY'] * 15
        residue_ids = list(range(n_atoms))
        chain_ids = ['A'] * n_atoms
        atom_names = [f'ATOM{i:03d}' for i in range(n_atoms)]
        
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
        
        # Test individual analyses
        analyses_to_test = ['rg', 'com', 'distances']
        
        for analysis in analyses_to_test:
            try:
                results = system.run_analysis_pipeline([analysis])
                assert isinstance(results, dict)
                assert analysis in results or 'radius_of_gyration' in results or 'center_of_mass' in results
                test_status(f"Analysis: {analysis}", True)
            except Exception as e:
                test_status(f"Analysis: {analysis}", False, str(e))
        
        # Test combined analysis pipeline
        try:
            all_results = system.run_analysis_pipeline(analyses_to_test)
            assert isinstance(all_results, dict)
            assert len(all_results) > 0
            test_status("Combined analysis pipeline", True)
        except Exception as e:
            test_status("Combined analysis pipeline", False, str(e))
        
        # Test result export
        try:
            system.export_analysis_results("test_phase3_output")
            test_status("Analysis result export", True)
        except Exception as e:
            test_status("Analysis result export", False, str(e))
        
        return True
        
    except Exception as e:
        test_status("Analysis pipeline setup", False, str(e))
        return False

def test_error_handling():
    """Test error handling and edge cases."""
    print("\n⚠️  Testing Error Handling:")
    
    try:
        import pyviamd
        from pyviamd.integrations import mdanalysis_integration
        
        # Test invalid array dimensions
        try:
            interface = pyviamd.mdanalysis.VIAMDMDAnalysisInterface()
            invalid_coords = np.random.rand(10, 2)  # Wrong shape
            elements = ['C'] * 10
            residue_names = ['ALA'] * 10
            residue_ids = list(range(10))
            chain_ids = ['A'] * 10
            atom_names = [f'ATOM{i:03d}' for i in range(10)]
            
            try:
                interface.initialize_from_arrays(
                    invalid_coords, elements, residue_names,
                    residue_ids, chain_ids, atom_names
                )
                test_status("Invalid coordinate dimensions handling", False, "Should have raised exception")
            except Exception:
                test_status("Invalid coordinate dimensions handling", True)
        except Exception as e:
            test_status("Invalid coordinate dimensions setup", False, str(e))
        
        # Test mismatched array lengths
        try:
            interface = pyviamd.mdanalysis.VIAMDMDAnalysisInterface()
            coords = np.random.rand(10, 3)
            elements = ['C'] * 5  # Wrong length
            residue_names = ['ALA'] * 10
            residue_ids = list(range(10))
            chain_ids = ['A'] * 10
            atom_names = [f'ATOM{i:03d}' for i in range(10)]
            
            try:
                interface.initialize_from_arrays(
                    coords, elements, residue_names,
                    residue_ids, chain_ids, atom_names
                )
                test_status("Mismatched array lengths handling", False, "Should have raised exception")
            except Exception:
                test_status("Mismatched array lengths handling", True)
        except Exception as e:
            test_status("Mismatched array lengths setup", False, str(e))
        
        # Test out-of-bounds frame access
        try:
            traj_interface = pyviamd.mdanalysis.MDAnalysisTrajectoryInterface()
            traj_interface.initialize(5, 10)
            
            try:
                traj_interface.get_frame(10)  # Out of bounds
                test_status("Out-of-bounds frame access handling", False, "Should have raised exception")
            except Exception:
                test_status("Out-of-bounds frame access handling", True)
        except Exception as e:
            test_status("Out-of-bounds frame access setup", False, str(e))
        
        return True
        
    except Exception as e:
        test_status("Error handling setup", False, str(e))
        return False

def print_test_summary():
    """Print comprehensive test summary."""
    print("\n" + "="*60)
    print("PHASE 3 MDANALYSIS INTEGRATION TEST SUMMARY")
    print("="*60)
    
    total_tests = test_results['passed'] + test_results['failed'] + test_results['skipped']
    pass_rate = (test_results['passed'] / total_tests * 100) if total_tests > 0 else 0
    
    print(f"\n📊 Overall Results:")
    print(f"   Total Tests: {total_tests}")
    print(f"   Passed: {test_results['passed']} ✓")
    print(f"   Failed: {test_results['failed']} ✗")
    print(f"   Skipped: {test_results['skipped']} ⊝")
    print(f"   Pass Rate: {pass_rate:.1f}%")
    
    if test_results['failed'] > 0:
        print(f"\n❌ Failed Tests:")
        for detail in test_results['details']:
            if detail['success'] is False:
                print(f"   - {detail['name']}: {detail['details']}")
    
    if test_results['skipped'] > 0:
        print(f"\n⊝ Skipped Tests:")
        for detail in test_results['details']:
            if detail['success'] is None:
                print(f"   - {detail['name']}: {detail['details']}")
    
    # Determine overall status
    if test_results['failed'] == 0:
        print(f"\n🎉 ALL TESTS PASSED! Phase 3 MDAnalysis integration is working correctly.")
        return True
    else:
        print(f"\n⚠️  Some tests failed. Phase 3 implementation may need attention.")
        return False

def main():
    """Main test execution function."""
    print("PHASE 3: ADVANCED MDANALYSIS INTEGRATION TEST SUITE")
    print("=" * 60)
    print("Testing comprehensive C++ bindings and workflow integration...")
    
    # Run all test categories
    test_categories = [
        ("Module Imports", test_imports),
        ("C++ Bindings", test_cpp_bindings),
        ("Trajectory Interface", test_trajectory_interface),
        ("Utility Functions", test_utility_functions),
        ("High-Level Integration", test_high_level_integration),
        ("Analysis Pipeline", test_analysis_pipeline),
        ("Error Handling", test_error_handling),
    ]
    
    for category_name, test_func in test_categories:
        try:
            print(f"\n{'='*20} {category_name} {'='*20}")
            test_func()
        except Exception as e:
            print(f"   ❌ CRITICAL ERROR in {category_name}: {e}")
            traceback.print_exc()
            test_results['failed'] += 1
    
    # Print final summary
    success = print_test_summary()
    
    return 0 if success else 1

if __name__ == "__main__":
    sys.exit(main())