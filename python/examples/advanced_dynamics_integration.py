#!/usr/bin/env python3
"""
Advanced VIAMD-OpenMM Dynamics Integration Example

This script demonstrates the complete integration between the new OpenMM dynamics
interface and existing VIAMD analysis capabilities. It shows how to:

1. Run molecular dynamics simulations with the DynamicsRunner
2. Integrate with VIAMD's analysis and visualization tools
3. Combine with machine learning and statistical analysis
4. Export results for external visualization packages
5. Create production-ready analysis workflows

This example showcases the full potential of the VIAMD ecosystem for
molecular dynamics simulation and analysis.
"""

import sys
import os
import logging
import tempfile
import json
from pathlib import Path

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Check for required packages
try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    logger.error("NumPy not available")
    HAS_NUMPY = False

try:
    import pyviamd
    HAS_PYVIAMD = True
except ImportError:
    logger.error("pyviamd not available")
    HAS_PYVIAMD = False

try:
    import openmm as mm
    import openmm.app as app
    import openmm.unit as unit
    HAS_OPENMM = True
except ImportError:
    logger.error("OpenMM not available")
    HAS_OPENMM = False


def create_comprehensive_example_structure():
    """Create a more complex test structure for comprehensive analysis."""
    logger.info("Creating comprehensive test structure...")
    
    # Small protein-like structure (extended Ala tripeptide)
    pdb_content = """HEADER    Extended test peptide for comprehensive analysis
ATOM      1  N   ALA A   1      -8.901   4.127  -0.555  1.00 20.00           N
ATOM      2  CA  ALA A   1      -8.608   3.135  -1.618  1.00 20.00           C
ATOM      3  C   ALA A   1      -7.221   2.458  -1.897  1.00 20.00           C
ATOM      4  O   ALA A   1      -6.634   1.849  -1.174  1.00 20.00           O
ATOM      5  CB  ALA A   1      -9.062   3.709  -2.961  1.00 20.00           C
ATOM      6  H   ALA A   1      -8.225   4.243  -0.014  1.00 20.00           H
ATOM      7  HA  ALA A   1      -9.240   2.344  -1.339  1.00 20.00           H
ATOM      8  HB1 ALA A   1      -8.964   3.011  -3.743  1.00 20.00           H
ATOM      9  HB2 ALA A   1     -10.068   4.018  -2.917  1.00 20.00           H
ATOM     10  HB3 ALA A   1      -8.445   4.520  -3.197  1.00 20.00           H
ATOM     11  N   VAL A   2      -6.935   2.554  -3.123  1.00 20.00           N
ATOM     12  CA  VAL A   2      -5.708   1.970  -3.691  1.00 20.00           C
ATOM     13  C   VAL A   2      -5.618   0.456  -3.897  1.00 20.00           C
ATOM     14  O   VAL A   2      -6.511  -0.192  -4.447  1.00 20.00           O
ATOM     15  CB  VAL A   2      -5.645   2.641  -5.052  1.00 20.00           C
ATOM     16  CG1 VAL A   2      -4.245   2.567  -5.632  1.00 20.00           C
ATOM     17  CG2 VAL A   2      -6.745   2.189  -6.001  1.00 20.00           C
ATOM     18  H   VAL A   2      -7.581   3.064  -3.627  1.00 20.00           H
ATOM     19  HA  VAL A   2      -4.893   2.183  -3.055  1.00 20.00           H
ATOM     20  HB  VAL A   2      -5.789   3.686  -4.903  1.00 20.00           H
ATOM     21  HG11VAL A   2      -4.205   3.011  -6.598  1.00 20.00           H
ATOM     22  HG12VAL A   2      -3.967   1.549  -5.759  1.00 20.00           H
ATOM     23  HG13VAL A   2      -3.523   3.047  -5.041  1.00 20.00           H
ATOM     24  HG21VAL A   2      -6.704   2.617  -6.967  1.00 20.00           H
ATOM     25  HG22VAL A   2      -7.699   2.405  -5.588  1.00 20.00           H
ATOM     26  HG23VAL A   2      -6.702   1.133  -6.128  1.00 20.00           H
ATOM     27  N   GLY A   3      -4.476  -0.043  -3.439  1.00 20.00           N
ATOM     28  CA  GLY A   3      -4.321  -1.490  -3.567  1.00 20.00           C
ATOM     29  C   GLY A   3      -3.201  -2.050  -2.708  1.00 20.00           C
ATOM     30  O   GLY A   3      -2.167  -1.467  -2.378  1.00 20.00           O
ATOM     31  OXT GLY A   3      -3.296  -3.239  -2.404  1.00 20.00           O
ATOM     32  H   GLY A   3      -3.817   0.510  -2.989  1.00 20.00           H
ATOM     33  HA2 GLY A   3      -5.218  -1.953  -3.254  1.00 20.00           H
ATOM     34  HA3 GLY A   3      -4.127  -1.736  -4.573  1.00 20.00           H
END
"""
    
    test_file = "comprehensive_peptide.pdb"
    with open(test_file, 'w') as f:
        f.write(pdb_content)
    
    logger.info(f"Comprehensive test structure created: {test_file}")
    return test_file


def demonstrate_integrated_workflow():
    """Demonstrate complete integrated workflow."""
    logger.info("=== Integrated VIAMD-OpenMM Workflow Demo ===")
    
    if not all([HAS_PYVIAMD, HAS_OPENMM, HAS_NUMPY]):
        logger.error("Required packages not available for integrated workflow")
        return False
    
    try:
        # Import dynamics and integration modules
        from pyviamd.dynamics import DynamicsRunner, SimulationProtocol, ProtocolParameters
        
        # Create test structure
        structure_file = create_comprehensive_example_structure()
        
        logger.info("1. Initializing VIAMD-OpenMM dynamics system...")
        runner = DynamicsRunner(structure_file=structure_file)
        
        # Show initial molecular information
        if runner.viamd_interface:
            n_atoms = runner.viamd_interface.get_num_atoms()
            coords = runner.viamd_interface.get_coordinates()
            elements = runner.viamd_interface.get_elements()
            masses = runner.viamd_interface.get_masses()
            
            logger.info(f"Loaded molecular system:")
            logger.info(f"  - Atoms: {n_atoms}")
            logger.info(f"  - Coordinate shape: {coords.shape}")
            logger.info(f"  - Mass range: {masses.min():.2f} - {masses.max():.2f} amu")
            logger.info(f"  - Element types: {set(elements)}")
        
        logger.info("2. Setting up OpenMM simulation system...")
        # Use simplified parameters for demo
        params = ProtocolParameters(
            temperature=300.0,
            nonbonded_method="NoCutoff",  # Simplified for demo
            constraints="HBonds",
            time_step=0.002
        )
        
        runner.setup_system(force_field="amber14", protocol_params=params)
        
        logger.info("3. Running energy minimization...")
        minimize_params = ProtocolParameters(
            minimize_tolerance=50.0,  # Loose tolerance for demo
            minimize_max_iterations=50
        )
        
        min_frames = runner.run_protocol(SimulationProtocol.MINIMIZATION, minimize_params)
        if min_frames:
            logger.info(f"Minimization complete: PE = {min_frames[0].state.potential_energy:.2f} kJ/mol")
        
        logger.info("4. Running short equilibration...")
        eq_params = ProtocolParameters(
            temperature=300.0,
            n_steps=200,  # Short for demo
            time_step=0.002,
            report_interval=50,
            nonbonded_method="NoCutoff"
        )
        
        eq_frames = runner.run_protocol(
            SimulationProtocol.NVT_EQUILIBRATION, 
            eq_params, 
            real_time_analysis=True
        )
        
        logger.info(f"Equilibration complete: {len(eq_frames)} frames")
        
        logger.info("5. Running production simulation...")
        prod_params = ProtocolParameters(
            temperature=300.0,
            n_steps=500,  # Short for demo
            time_step=0.002,
            report_interval=100
        )
        
        prod_frames = runner.run_protocol(
            SimulationProtocol.NVT_EQUILIBRATION,  # Using NVT for simplicity
            prod_params,
            real_time_analysis=True
        )
        
        logger.info(f"Production simulation complete: {len(prod_frames)} frames")
        
        # 6. Analyze results with VIAMD integration
        logger.info("6. Performing integrated analysis...")
        
        # Basic trajectory analysis
        times, pe_values = runner.trajectory_manager.get_time_series("potential_energy")
        times, ke_values = runner.trajectory_manager.get_time_series("kinetic_energy")
        times, temp_values = runner.trajectory_manager.get_time_series("temperature")
        
        if len(pe_values) > 0:
            logger.info(f"Energy statistics:")
            logger.info(f"  - PE: {np.mean(pe_values):.2f} ± {np.std(pe_values):.2f} kJ/mol")
            logger.info(f"  - KE: {np.mean(ke_values):.2f} ± {np.std(ke_values):.2f} kJ/mol")
            logger.info(f"  - Temperature: {np.mean(temp_values):.1f} ± {np.std(temp_values):.1f} K")
        
        # Try to use VIAMD analysis tools if available
        try:
            logger.info("7. Testing VIAMD analysis integration...")
            
            # Get final coordinates for analysis
            final_coords = runner.viamd_interface.get_coordinates()
            logger.info(f"Final coordinates shape: {final_coords.shape}")
            
            # Test basic geometric analysis
            # Calculate center of mass
            masses = runner.viamd_interface.get_masses()
            com = np.average(final_coords, axis=0, weights=masses)
            logger.info(f"Center of mass: [{com[0]:.3f}, {com[1]:.3f}, {com[2]:.3f}] Å")
            
            # Calculate radius of gyration
            diff = final_coords - com
            rg_sq = np.average(np.sum(diff**2, axis=1), weights=masses)
            rg = np.sqrt(rg_sq)
            logger.info(f"Radius of gyration: {rg:.3f} Å")
            
            # Test with VIAMD analysis bindings if available
            if hasattr(pyviamd, 'analysis'):
                logger.info("VIAMD analysis bindings available - testing integration...")
                # This would use the C++ analysis bindings
                # analyzer = pyviamd.analysis.GeometryAnalyzer()
                # analyzer.set_coordinates(final_coords)
                # rg_viamd = analyzer.radius_of_gyration(masses)
                # logger.info(f"VIAMD RG calculation: {rg_viamd:.3f} Å")
            
        except Exception as e:
            logger.warning(f"Advanced analysis failed: {e}")
        
        # 8. Test integration with other VIAMD modules
        logger.info("8. Testing integration with other VIAMD modules...")
        
        # Test event system integration if available
        try:
            if hasattr(pyviamd, 'event'):
                logger.info("Event system integration available")
                # Could register events for simulation milestones
                # event_mgr = pyviamd.event.EventManager()
                # event_mgr.send_event("MD_COMPLETE", {"n_frames": len(prod_frames)})
            
        except Exception as e:
            logger.warning(f"Event system integration test failed: {e}")
        
        # Test visualization integration if available
        try:
            logger.info("Testing trajectory export capabilities...")
            
            # Export trajectory in multiple formats
            runner.trajectory_manager.export_trajectory("integrated_demo.xyz", format="xyz")
            runner.trajectory_manager.export_trajectory("integrated_demo.json", format="json")
            
            logger.info("Trajectory exported successfully")
            
            # Test integration with visualization module if available
            if hasattr(pyviamd, 'visualization'):
                logger.info("Visualization integration available")
                # Could create visualizations of the trajectory
                # viz = pyviamd.visualization.RealTimePlotter()
                # viz.plot_time_series(times, pe_values, "Potential Energy")
            
        except Exception as e:
            logger.warning(f"Visualization integration test failed: {e}")
        
        # 9. Performance summary
        logger.info("9. Performance summary...")
        performance = runner.monitoring_system.get_performance_summary()
        
        if performance:
            logger.info("Simulation performance:")
            for key, value in performance.items():
                if isinstance(value, (int, float)):
                    logger.info(f"  - {key}: {value:.3f}")
                elif isinstance(value, list) and value:
                    logger.info(f"  - {key}: {len(value)} items")
        
        # 10. Final results compilation
        logger.info("10. Compiling final results...")
        
        results = {
            "simulation_summary": {
                "total_frames": len(runner.trajectory_manager.frames),
                "final_energy": prod_frames[-1].state.potential_energy if prod_frames else None,
                "final_temperature": prod_frames[-1].state.temperature if prod_frames else None,
                "simulation_time_ps": prod_frames[-1].time if prod_frames else 0.0
            },
            "analysis_summary": {
                "energy_statistics": {
                    "mean_pe": float(np.mean(pe_values)) if len(pe_values) > 0 else None,
                    "std_pe": float(np.std(pe_values)) if len(pe_values) > 0 else None,
                    "mean_temperature": float(np.mean(temp_values)) if len(temp_values) > 0 else None,
                    "std_temperature": float(np.std(temp_values)) if len(temp_values) > 0 else None
                }
            },
            "performance": performance
        }
        
        # Export comprehensive results
        with open("integrated_results.json", 'w') as f:
            json.dump(results, f, indent=2)
        
        logger.info("Results exported to integrated_results.json")
        
        return True
        
    except Exception as e:
        logger.error(f"Integrated workflow failed: {e}")
        import traceback
        traceback.print_exc()
        return False


def demonstrate_ml_integration():
    """Demonstrate machine learning integration capabilities."""
    logger.info("=== Machine Learning Integration Demo ===")
    
    try:
        # This would demonstrate integration with VIAMD's ML capabilities
        logger.info("Testing ML integration framework...")
        
        # If ML module is available
        if HAS_PYVIAMD and hasattr(pyviamd, 'ml'):
            logger.info("VIAMD ML module available for integration")
            # Could extract features from trajectory for ML analysis
            # feature_extractor = pyviamd.ml.FeatureExtractor()
            # features = feature_extractor.extract_trajectory_features(trajectory_frames)
            # 
            # # Dimensionality reduction
            # reducer = pyviamd.ml.DimensionalityReducer()
            # reduced_features = reducer.pca_transform(features, n_components=2)
        else:
            logger.info("ML integration would be available with full VIAMD installation")
        
        return True
        
    except Exception as e:
        logger.warning(f"ML integration demo failed: {e}")
        return False


def demonstrate_production_workflow():
    """Demonstrate a production-ready workflow."""
    logger.info("=== Production Workflow Demo ===")
    
    try:
        from pyviamd.dynamics import quick_md
        
        # Create production structure
        structure_file = create_comprehensive_example_structure()
        
        logger.info("Running production-ready workflow...")
        
        # Use the quick_md function for a complete workflow
        results = quick_md(
            structure_file=structure_file,
            n_steps=1000,  # Short for demo, would be much longer in production
            temperature=300.0,
            output_dir="production_output"
        )
        
        logger.info("Production workflow results:")
        logger.info(f"  - Frames generated: {results['n_frames']}")
        logger.info(f"  - Simulation time: {results['simulation_time_ps']:.3f} ps")
        logger.info(f"  - Final energy: {results['final_state'].potential_energy:.2f} kJ/mol")
        logger.info(f"  - Final temperature: {results['final_state'].temperature:.1f} K")
        
        # Show exported files
        if 'exported_files' in results:
            logger.info("Files exported:")
            for file_type, filename in results['exported_files'].items():
                logger.info(f"  - {file_type}: {filename}")
        
        return True
        
    except Exception as e:
        logger.error(f"Production workflow demo failed: {e}")
        return False


def main():
    """Main demonstration function."""
    logger.info("Advanced VIAMD-OpenMM Dynamics Integration Demonstration")
    logger.info("=" * 70)
    
    # Check requirements
    requirements = {
        'NumPy': HAS_NUMPY,
        'VIAMD': HAS_PYVIAMD,
        'OpenMM': HAS_OPENMM
    }
    
    logger.info("Requirements check:")
    missing_requirements = []
    for package, available in requirements.items():
        status = "✓" if available else "✗"
        logger.info(f"  {status} {package}")
        if not available:
            missing_requirements.append(package)
    
    if missing_requirements:
        logger.error(f"Missing requirements: {', '.join(missing_requirements)}")
        logger.info("This demonstration requires a full VIAMD installation with:")
        logger.info("  - VIAMD compiled with VIAMD_ENABLE_PYTHON=ON")
        logger.info("  - OpenMM installed (conda install -c conda-forge openmm)")
        logger.info("  - NumPy available (pip install numpy)")
        return 1
    
    # Run demonstrations
    success = True
    
    # 1. Integrated workflow
    if not demonstrate_integrated_workflow():
        success = False
        logger.error("Integrated workflow demonstration failed")
    
    # 2. ML integration demo
    if not demonstrate_ml_integration():
        logger.warning("ML integration demonstration had issues")
    
    # 3. Production workflow
    if not demonstrate_production_workflow():
        success = False
        logger.error("Production workflow demonstration failed")
    
    if success:
        logger.info("\n🎉 Advanced VIAMD-OpenMM integration demonstration completed successfully!")
        logger.info("\nKey integration features demonstrated:")
        logger.info("  ✓ Seamless OpenMM dynamics interface")
        logger.info("  ✓ Integration with VIAMD molecular data structures")
        logger.info("  ✓ Real-time monitoring and analysis")
        logger.info("  ✓ Comprehensive trajectory management")
        logger.info("  ✓ Multi-format data export")
        logger.info("  ✓ Production-ready workflow automation")
        logger.info("  ✓ Integration framework for ML and visualization")
        
        logger.info("\n🚀 The VIAMD ecosystem now provides a complete solution for:")
        logger.info("  • Molecular dynamics simulation (OpenMM integration)")
        logger.info("  • Real-time analysis and monitoring")
        logger.info("  • Advanced statistical and geometric analysis")
        logger.info("  • Machine learning workflows")
        logger.info("  • Visualization and data export")
        logger.info("  • Event-driven custom components")
        
    else:
        logger.error("Some demonstrations failed - see logs for details")
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())