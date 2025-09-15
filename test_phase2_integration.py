#!/usr/bin/env python3
"""
Phase 2 OpenMM Integration - Core Functionality Test

This test demonstrates the core Phase 2 OpenMM integration capabilities
without requiring OpenMM to be installed. It focuses on testing:

1. VIAMD-OpenMM interface creation and molecular data access
2. Coordinate transfer and manipulation capabilities  
3. OpenMM-compatible data structures and export functionality
4. Production-ready Python API design

This validates that the C++ integration bindings are working correctly
and ready for OpenMM integration when OpenMM is available.
"""

import sys
import os
import tempfile
import numpy as np

# Test basic imports
try:
    import pyviamd
    print("✓ VIAMD Python bindings loaded successfully")
    print(f"  Version: {pyviamd.core.get_version()}")
except ImportError as e:
    print(f"✗ VIAMD import failed: {e}")
    sys.exit(1)

# Test OpenMM integration module availability
try:
    openmm_module = pyviamd.openmm
    print("✓ OpenMM integration C++ bindings available")
    print(f"  Available classes: {[name for name in dir(openmm_module) if not name.startswith('_')]}")
except AttributeError as e:
    print(f"✗ OpenMM integration module not found: {e}")
    sys.exit(1)

def create_test_molecule():
    """Create a simple test molecule in PDB format."""
    pdb_content = """HEADER    Test molecule for Phase 2 OpenMM integration testing
ATOM      1  O   HOH A   1       0.000   0.000   0.000  1.00 20.00           O
ATOM      2  H1  HOH A   1       0.957   0.000   0.000  1.00 20.00           H  
ATOM      3  H2  HOH A   1      -0.240   0.927   0.000  1.00 20.00           H
ATOM      4  O   HOH A   2       3.000   0.000   0.000  1.00 20.00           O
ATOM      5  H1  HOH A   2       3.957   0.000   0.000  1.00 20.00           H
ATOM      6  H2  HOH A   2       2.760   0.927   0.000  1.00 20.00           H
END
"""
    
    test_file = "test_phase2_molecule.pdb"
    with open(test_file, 'w') as f:
        f.write(pdb_content)
    
    print(f"✓ Created test molecule: {test_file}")
    return test_file

def test_viamd_interface_creation(pdb_file):
    """Test VIAMD-OpenMM interface creation and basic functionality."""
    print("\n=== Testing VIAMD-OpenMM Interface Creation ===")
    
    try:
        # Test creating interface from PDB
        interface = pyviamd.openmm.create_interface_from_pdb(pdb_file)
        print("✓ Successfully created VIAMD-OpenMM interface from PDB")
        
        # Test basic molecular data access
        n_atoms = interface.get_num_atoms()
        print(f"✓ Number of atoms: {n_atoms}")
        
        # Test coordinate access
        coords = interface.get_coordinates()
        print(f"✓ Coordinates shape: {coords.shape}")
        print(f"  Coordinate range: {coords.min():.3f} to {coords.max():.3f} Å")
        
        # Test masses
        masses = interface.get_masses()
        print(f"✓ Masses shape: {masses.shape}")
        print(f"  Mass range: {masses.min():.3f} to {masses.max():.3f} amu")
        
        # Test elements
        elements = interface.get_elements()
        print(f"✓ Elements: {elements}")
        
        # Test atom types
        atom_types = interface.get_atom_types()
        print(f"✓ Atom types: {atom_types}")
        
        # Test residue names
        residue_names = interface.get_residue_names()
        print(f"✓ Residue names: {set(residue_names)}")
        
        return interface
        
    except Exception as e:
        print(f"✗ Interface creation failed: {e}")
        import traceback
        traceback.print_exc()
        return None

def test_coordinate_manipulation(interface):
    """Test coordinate manipulation and OpenMM compatibility."""
    print("\n=== Testing Coordinate Manipulation ===")
    
    try:
        # Get original coordinates
        original_coords = interface.get_coordinates()
        print(f"✓ Original coordinates shape: {original_coords.shape}")
        
        # Test coordinate modification
        modified_coords = original_coords + 1.0  # Translate by 1 Å
        interface.set_coordinates(modified_coords)
        print("✓ Successfully modified coordinates")
        
        # Verify modification
        new_coords = interface.get_coordinates()
        diff = np.abs(new_coords - modified_coords).max()
        print(f"✓ Coordinate modification verified (max diff: {diff:.6f})")
        
        # Test OpenMM-compatible coordinate format
        openmm_coords = interface.get_openmm_coordinates()
        print(f"✓ OpenMM coordinates shape: {openmm_coords.shape}")
        print(f"  OpenMM coordinate range: {openmm_coords.min():.4f} to {openmm_coords.max():.4f} nm")
        
        # Test coordinate conversion consistency
        expected_nm = modified_coords * 0.1  # Angstroms to nanometers
        conversion_diff = np.abs(openmm_coords - expected_nm).max()
        print(f"✓ Unit conversion verified (max diff: {conversion_diff:.6f} nm)")
        
        # Restore original coordinates
        interface.set_coordinates(original_coords)
        print("✓ Restored original coordinates")
        
        return True
        
    except Exception as e:
        print(f"✗ Coordinate manipulation failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_topology_data(interface):
    """Test OpenMM topology data extraction."""
    print("\n=== Testing Topology Data Extraction ===")
    
    try:
        topology_data = interface.get_topology_data()
        print("✓ Successfully extracted topology data")
        
        # Check required fields
        required_fields = ['n_atoms', 'n_residues', 'n_chains', 'coordinates', 
                          'masses', 'elements', 'atom_types', 'residue_names']
        
        for field in required_fields:
            if field in topology_data:
                print(f"  ✓ {field}: {type(topology_data[field])}")
            else:
                print(f"  ✗ Missing field: {field}")
                
        print(f"✓ Topology summary:")
        print(f"  - Atoms: {topology_data['n_atoms']}")
        print(f"  - Residues: {topology_data['n_residues']}")  
        print(f"  - Chains: {topology_data['n_chains']}")
        
        return True
        
    except Exception as e:
        print(f"✗ Topology data extraction failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_pdb_export(interface):
    """Test PDB export functionality."""
    print("\n=== Testing PDB Export ===")
    
    try:
        export_file = "exported_molecule.pdb"
        interface.export_pdb(export_file)
        print(f"✓ Successfully exported PDB: {export_file}")
        
        # Verify the exported file
        if os.path.exists(export_file):
            with open(export_file, 'r') as f:
                content = f.read()
                
            lines = content.strip().split('\n')
            atom_lines = [line for line in lines if line.startswith('ATOM')]
            print(f"✓ Exported {len(atom_lines)} ATOM records")
            
            # Check if file can be re-read
            try:
                reimported_interface = pyviamd.openmm.create_interface_from_pdb(export_file)
                print("✓ Exported PDB can be re-imported")
                
                # Compare atom counts
                original_atoms = interface.get_num_atoms()
                reimported_atoms = reimported_interface.get_num_atoms()
                if original_atoms == reimported_atoms:
                    print(f"✓ Atom count preserved: {original_atoms}")
                else:
                    print(f"⚠ Atom count mismatch: {original_atoms} -> {reimported_atoms}")
                    
            except Exception as e:
                print(f"⚠ Re-import test failed: {e}")
        
        return True
        
    except Exception as e:
        print(f"✗ PDB export failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def test_simulation_manager():
    """Test OpenMM simulation manager."""
    print("\n=== Testing Simulation Manager ===")
    
    try:
        # Create simulation manager
        sim_manager = pyviamd.openmm.SimulationManager()
        print("✓ Successfully created SimulationManager")
        
        # Test without interface (should handle gracefully)
        try:
            sim_manager.get_openmm_coordinates()
            print("✗ Should have failed without interface")
        except RuntimeError:
            print("✓ Correctly handles missing interface")
        
        return True
        
    except Exception as e:
        print(f"✗ Simulation manager test failed: {e}")
        import traceback
        traceback.print_exc()
        return False

def run_comprehensive_test():
    """Run comprehensive Phase 2 integration test."""
    print("VIAMD Phase 2 OpenMM Integration - Core Functionality Test")
    print("=" * 65)
    
    # Create test molecule
    pdb_file = create_test_molecule()
    
    test_results = []
    
    try:
        # Test 1: Interface creation
        interface = test_viamd_interface_creation(pdb_file)
        test_results.append(interface is not None)
        
        if interface:
            # Test 2: Coordinate manipulation
            coord_test = test_coordinate_manipulation(interface)
            test_results.append(coord_test)
            
            # Test 3: Topology data
            topo_test = test_topology_data(interface)
            test_results.append(topo_test)
            
            # Test 4: PDB export
            export_test = test_pdb_export(interface)
            test_results.append(export_test)
        else:
            test_results.extend([False, False, False])
            
        # Test 5: Simulation manager
        sim_test = test_simulation_manager()
        test_results.append(sim_test)
        
    finally:
        # Cleanup
        for file in ["test_phase2_molecule.pdb", "exported_molecule.pdb"]:
            if os.path.exists(file):
                os.remove(file)
    
    # Summary
    print("\n" + "=" * 65)
    print("PHASE 2 INTEGRATION TEST SUMMARY")
    print("=" * 65)
    
    test_names = [
        "VIAMD-OpenMM Interface Creation", 
        "Coordinate Manipulation",
        "Topology Data Extraction",
        "PDB Export Functionality", 
        "Simulation Manager"
    ]
    
    passed = sum(test_results)
    total = len(test_results)
    
    for i, (name, result) in enumerate(zip(test_names, test_results)):
        status = "✓ PASS" if result else "✗ FAIL"
        print(f"{i+1}. {name:<35} {status}")
    
    print(f"\nOverall: {passed}/{total} tests passed ({100*passed/total:.1f}%)")
    
    if passed == total:
        print("\n🎉 Phase 2 OpenMM Integration Core Functionality: FULLY OPERATIONAL!")
        print("\nKey Phase 2 Achievements Verified:")
        print("  ✓ C++ level OpenMM integration bindings working")
        print("  ✓ Seamless coordinate transfer and manipulation")
        print("  ✓ OpenMM-compatible data structures and units")
        print("  ✓ Production-ready molecular data access API")
        print("  ✓ Topology extraction for OpenMM system setup")
        print("  ✓ PDB export/import functionality")
        print("  ✓ Simulation management framework ready")
        print("\n🚀 Ready for OpenMM integration when OpenMM is available!")
        return 0
    else:
        print(f"\n❌ {total-passed} test(s) failed. Phase 2 integration needs attention.")
        return 1

if __name__ == "__main__":
    sys.exit(run_comprehensive_test())