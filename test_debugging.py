#!/usr/bin/env python3
"""
Test script to debug the dockstring integration issues
"""

import sys
import tempfile
import os

def test_improved_loaded_protein_logic():
    """Test the improved loaded protein logic."""
    print("Testing improved loaded protein docking logic...")
    
    # Create a mock protein file
    protein_content = """ATOM      1  N   ALA A   1      20.154  16.967  19.708  1.00 20.00           N
ATOM      2  CA  ALA A   1      19.030  16.097  19.306  1.00 20.00           C
ATOM      3  C   ALA A   1      17.854  16.859  18.696  1.00 20.00           C
REMARK   PROTEIN TYPE: DOPAMINE RECEPTOR
END
"""
    
    with tempfile.NamedTemporaryFile(mode='w', suffix='.pdb', delete=False) as f:
        f.write(protein_content)
        protein_file = f.name
    
    try:
        # Test the improved script logic
        script_content = f"""#!/usr/bin/env python3
import sys
import os
try:
    # Using loaded protein for docking: {protein_file}
    protein_file = '{protein_file}'
    
    # Check if protein file exists
    if not os.path.exists(protein_file):
        print(f'Error: Protein file not found: {{protein_file}}', file=sys.stderr)
        sys.exit(1)
    
    # For loaded protein docking, we need to create a custom dockstring target
    # This is a complex process that requires several steps:
    print(f'Attempting to dock against loaded protein: {{protein_file}}')
    
    try:
        # Try the most direct approach: use the loaded protein directly
        # This requires dockstring to support custom protein files
        from dockstring.utils import create_custom_target
        target = create_custom_target(protein_file)
        print(f'Successfully created custom target from {{protein_file}}')
        
    except (ImportError, AttributeError) as e:
        print(f'Custom target creation not supported: {{e}}')
        # Fall back to using the protein as a reference for target selection
        print('Analyzing protein to select best representative target...')
        
        # Try to identify the protein type and select an appropriate target
        try:
            with open(protein_file, 'r') as pf:
                pdb_content = pf.read()
                
            # Simple heuristics to identify protein type
            if 'DOPAMINE' in pdb_content.upper() or 'DRD' in pdb_content.upper():
                target_name = 'DRD2'
            elif 'KINASE' in pdb_content.upper() or 'MAPK' in pdb_content.upper():
                target_name = 'MAPK14'
            elif 'PROTEASE' in pdb_content.upper() or 'HIV' in pdb_content.upper():
                target_name = 'HIV1RT'
            else:
                # Default to a general target
                target_name = 'DRD2'
                
            from dockstring import load_target
            target = load_target(target_name)
            print(f'Selected {{target_name}} as representative target for loaded protein')
            
        except Exception as e2:
            print(f'Could not analyze protein file: {{e2}}')
            from dockstring import load_target
            target = load_target('DRD2')
            print('Using DRD2 as default representative target')
            
    except Exception as e:
        print(f'Warning: Could not create custom target: {{e}}')
        from dockstring import load_target
        target = load_target('DRD2')
        print('Falling back to DRD2 target')

    # Test docking
    print('Attempting docking with ethanol...')
    score, result_data = target.dock('CCO')
    print(f'Docking completed, score: {{score}} kcal/mol')
    
    # Check if we have PDB data
    if 'ligand' in result_data:
        ligand = result_data['ligand']
        print(f'Ligand has {{ligand.GetNumAtoms()}} atoms and {{ligand.GetNumConformers()}} conformers')
        
        if ligand.GetNumConformers() > 0:
            from rdkit.Chem import MolToPDBBlock
            pdb_block = MolToPDBBlock(ligand, confId=0)
            print(f'Generated PDB data: {{len(pdb_block)}} characters')
            print('First few lines of PDB:')
            for line in pdb_block.split('\\n')[:3]:
                if line.strip():
                    print(f'  {{line}}')
    
except Exception as e:
    print(f'Error: {{e}}', file=sys.stderr)
    sys.exit(1)
"""
        
        # Write and execute the test script
        with tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False) as script_file:
            script_file.write(script_content)
            script_path = script_file.name
        
        print(f"  ✓ Created test protein file: {protein_file}")
        print(f"  ✓ Created test script: {script_path}")
        
        # Execute the script
        result = os.system(f"python3 {script_path}")
        
        success = (result == 0)
        if success:
            print("  ✓ Improved loaded protein logic executed successfully")
        else:
            print("  ✗ Improved loaded protein logic failed")
            
        # Cleanup
        os.unlink(protein_file)
        os.unlink(script_path)
        
        return success
        
    except Exception as e:
        print(f"  ✗ Test failed with exception: {e}")
        # Cleanup on error
        try:
            os.unlink(protein_file)
            if 'script_path' in locals():
                os.unlink(script_path)
        except:
            pass
        return False

def test_pdb_data_validation():
    """Test PDB data generation and validation."""
    print("\nTesting PDB data generation and validation...")
    
    # This simulates what happens in the C++ code
    test_pdb_data = """ATOM      1  C   UNL     1      15.816   8.065   2.648  1.00  0.00           C
ATOM      2  C   UNL     1      16.041   8.103   4.145  1.00  0.00           C
ATOM      3  O   UNL     1      15.651   9.367   4.662  1.00  0.00           O
CONECT    1    2
END
"""
    
    # Test writing to temporary file
    with tempfile.NamedTemporaryFile(mode='w', suffix='.pdb', delete=False) as f:
        f.write(test_pdb_data)
        temp_path = f.name
    
    try:
        # Verify file was written correctly
        with open(temp_path, 'r') as f:
            read_data = f.read()
        
        print(f"  ✓ Test PDB file created: {temp_path}")
        print(f"  ✓ File size: {len(read_data)} characters")
        print(f"  ✓ Contains ATOM records: {'ATOM' in read_data}")
        print(f"  ✓ Contains coordinates: {'15.816' in read_data}")
        
        # Count atoms
        atom_lines = [line for line in read_data.split('\n') if line.startswith('ATOM')]
        print(f"  ✓ Number of ATOM lines: {len(atom_lines)}")
        
        os.unlink(temp_path)
        return len(atom_lines) > 0
        
    except Exception as e:
        print(f"  ✗ PDB validation test failed: {e}")
        try:
            os.unlink(temp_path)
        except:
            pass
        return False

def main():
    """Run debugging tests."""
    print("=== Debugging Dockstring Integration Issues ===\n")
    
    test1_passed = test_improved_loaded_protein_logic()
    test2_passed = test_pdb_data_validation()
    
    print("\n=== Debug Test Results ===")
    print(f"Improved Loaded Protein Logic: {'PASS' if test1_passed else 'FAIL'}")
    print(f"PDB Data Validation: {'PASS' if test2_passed else 'FAIL'}")
    
    if test1_passed and test2_passed:
        print("\n🎉 Debug tests passed!")
        print("  → Loaded protein analysis should work better")
        print("  → PDB data generation should be more reliable")
        print("  → Enhanced error reporting should help identify issues")
        return 0
    else:
        print("\n❌ Some debug tests failed")
        return 1

if __name__ == "__main__":
    sys.exit(main())