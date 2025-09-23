#!/usr/bin/env python3
"""
Test script to validate dockstring integration with VIA MD.
This script replicates the docking functionality that the VIA MD component uses.
"""

import sys
import tempfile
import os

def test_dockstring_import():
    """Test if dockstring can be imported and basic functionality works."""
    try:
        import dockstring
        print("✓ Dockstring imported successfully")
        
        # Test listing targets
        targets = dockstring.list_all_target_names()
        print(f"✓ Found {len(targets)} available targets")
        print(f"  First 5 targets: {targets[:5]}")
        
        return True
    except ImportError as e:
        print(f"✗ Failed to import dockstring: {e}")
        return False
    except Exception as e:
        print(f"✗ Error with dockstring: {e}")
        return False

def test_basic_docking():
    """Test basic docking functionality."""
    try:
        from dockstring import load_target
        
        # Test with ethanol (CCO) against DRD2
        print("\nTesting basic docking...")
        target = load_target("DRD2")
        print("✓ Target DRD2 loaded successfully")
        
        smiles = "CCO"  # Ethanol
        print(f"✓ Docking {smiles} against DRD2...")
        
        score, result_data = target.dock(smiles)
        print(f"✓ Docking completed successfully")
        print(f"  Score: {score} kcal/mol")
        print(f"  Result keys: {list(result_data.keys())}")
        
        # Check if we have ligand data
        if 'ligand' in result_data:
            ligand = result_data['ligand']
            print(f"  Ligand conformers: {ligand.GetNumConformers()}")
            
            # Test PDB export (similar to what VIA MD does)
            from rdkit.Chem import MolToPDBBlock
            if ligand.GetNumConformers() > 0:
                pdb_block = MolToPDBBlock(ligand, confId=0)
                print(f"  PDB data length: {len(pdb_block)} characters")
                print("✓ PDB export successful")
        
        return True
    except Exception as e:
        print(f"✗ Docking test failed: {e}")
        return False

def test_viamd_script_simulation():
    """Test the exact script format that VIA MD generates."""
    try:
        print("\nTesting VIA MD script simulation...")
        
        # Create temporary files like VIA MD does
        with tempfile.NamedTemporaryFile(mode='w', suffix='.py', delete=False) as script_file:
            script_path = script_file.name
            
            with tempfile.NamedTemporaryFile(mode='w', suffix='.txt', delete=False) as output_file:
                output_path = output_file.name
        
        # Write the exact script that VIA MD generates
        with open(script_path, 'w') as f:
            f.write("""#!/usr/bin/env python3
import sys
try:
    from dockstring import load_target
    target = load_target('DRD2')
    score, result_data = target.dock('CC(=O)OC1=CC=CC=C1C(=O)O')
    with open('{}', 'w') as out:
        out.write(f'SCORE:{{score}}\\n')
        if 'ligand' in result_data:
            from rdkit.Chem import MolToPDBBlock
            ligand = result_data['ligand']
            if ligand.GetNumConformers() > 0:
                pdb_block = MolToPDBBlock(ligand, confId=0)
                out.write('PDB_DATA:\\n')
                out.write(pdb_block)
    print(f'Docking completed, score: {{score}}')
except Exception as e:
    print(f'Error: {{e}}', file=sys.stderr)
    sys.exit(1)
""".format(output_path))
        
        # Execute the script
        result = os.system(f"python3 {script_path}")
        
        if result == 0:
            print("✓ VIA MD script executed successfully")
            
            # Read results
            with open(output_path, 'r') as f:
                content = f.read()
                
            lines = content.split('\n')
            score_line = next((line for line in lines if line.startswith('SCORE:')), None)
            pdb_start = next((i for i, line in enumerate(lines) if line.startswith('PDB_DATA:')), None)
            
            if score_line:
                score = float(score_line.split(':')[1])
                print(f"  Extracted score: {score} kcal/mol")
            
            if pdb_start is not None:
                pdb_lines = len(lines) - pdb_start - 1
                print(f"  PDB data: {pdb_lines} lines")
                print("✓ Results parsing successful")
            
        else:
            print("✗ VIA MD script execution failed")
            return False
            
        # Cleanup
        os.unlink(script_path)
        os.unlink(output_path)
        
        return True
        
    except Exception as e:
        print(f"✗ VIA MD script simulation failed: {e}")
        return False

def main():
    """Run all tests."""
    print("=== VIA MD Dockstring Integration Test ===\n")
    
    tests = [
        ("Dockstring Import", test_dockstring_import),
        ("Basic Docking", test_basic_docking),
        ("VIA MD Script Simulation", test_viamd_script_simulation),
    ]
    
    results = []
    for test_name, test_func in tests:
        print(f"Running {test_name}...")
        try:
            result = test_func()
            results.append((test_name, result))
            if result:
                print(f"✓ {test_name} PASSED\n")
            else:
                print(f"✗ {test_name} FAILED\n")
        except Exception as e:
            print(f"✗ {test_name} FAILED with exception: {e}\n")
            results.append((test_name, False))
    
    # Summary
    print("=== Test Summary ===")
    passed = sum(1 for _, result in results if result)
    total = len(results)
    
    for test_name, result in results:
        status = "PASS" if result else "FAIL"
        print(f"  {test_name}: {status}")
    
    print(f"\nOverall: {passed}/{total} tests passed")
    
    if passed == total:
        print("🎉 All tests passed! VIA MD dockstring integration is ready.")
        return 0
    else:
        print("❌ Some tests failed. Check the output above for details.")
        return 1

if __name__ == "__main__":
    sys.exit(main())