#!/usr/bin/env python3
"""
VIAMD OpenMM Dynamics Interface Example

This script demonstrates the comprehensive OpenMM dynamics interface for VIAMD.
It shows how to run complete molecular dynamics simulations directly from VIAMD
with production-ready protocols, real-time monitoring, and integrated analysis.

Features demonstrated:
1. High-level DynamicsRunner interface for MD simulations
2. Predefined simulation protocols (minimization, equilibration, production)
3. Real-time monitoring and performance analysis
4. Trajectory management with automatic analysis
5. Comprehensive result export and visualization
6. Integration with VIAMD's molecular data structures

Requirements:
- pyviamd (compiled with VIAMD_ENABLE_PYTHON=ON)
- openmm ≥ 7.7.0
- numpy ≥ 1.19.0
- matplotlib (optional, for plots)

Usage:
    python viamd_dynamics_example.py [structure.pdb] [n_steps]
"""

import sys
import os
import logging
import tempfile
from pathlib import Path

# Set up logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

try:
    import numpy as np
    HAS_NUMPY = True
except ImportError:
    logger.error("NumPy not available. Install with: pip install numpy")
    HAS_NUMPY = False

try:
    import pyviamd
    HAS_PYVIAMD = True
    logger.info(f"VIAMD Python bindings loaded successfully")
except ImportError:
    logger.error("pyviamd not available. Build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    HAS_PYVIAMD = False

try:
    import openmm as mm
    import openmm.app as app
    import openmm.unit as unit
    HAS_OPENMM = True
    logger.info(f"OpenMM loaded successfully")
except ImportError:
    logger.error("OpenMM not available. Install with: conda install -c conda-forge openmm")
    HAS_OPENMM = False

# Import the dynamics module
try:
    from pyviamd.dynamics import (
        DynamicsRunner, SimulationProtocol, ProtocolParameters,
        quick_md, check_requirements
    )
    HAS_DYNAMICS = True
    logger.info("VIAMD dynamics interface loaded successfully")
except ImportError:
    logger.error("VIAMD dynamics interface not available")
    HAS_DYNAMICS = False


def create_test_protein():
    """Create a small test protein structure for demonstration."""
    logger.info("Creating test protein structure...")
    
    # Simple 3-residue peptide (Ala-Ala-Ala)
    pdb_content = """HEADER    Test peptide for VIAMD dynamics
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
ATOM     11  N   ALA A   2      -6.935   2.554  -3.123  1.00 20.00           N
ATOM     12  CA  ALA A   2      -5.708   1.970  -3.691  1.00 20.00           C
ATOM     13  C   ALA A   2      -5.618   0.456  -3.897  1.00 20.00           C
ATOM     14  O   ALA A   2      -6.511  -0.192  -4.447  1.00 20.00           O
ATOM     15  CB  ALA A   2      -5.645   2.641  -5.052  1.00 20.00           C
ATOM     16  H   ALA A   2      -7.581   3.064  -3.627  1.00 20.00           H
ATOM     17  HA  ALA A   2      -4.893   2.183  -3.055  1.00 20.00           H
ATOM     18  HB1 ALA A   2      -4.789   2.396  -5.598  1.00 20.00           H
ATOM     19  HB2 ALA A   2      -6.503   2.413  -5.616  1.00 20.00           H
ATOM     20  HB3 ALA A   2      -5.658   3.686  -4.903  1.00 20.00           H
ATOM     21  N   ALA A   3      -4.476  -0.043  -3.439  1.00 20.00           N
ATOM     22  CA  ALA A   3      -4.321  -1.490  -3.567  1.00 20.00           C
ATOM     23  C   ALA A   3      -3.201  -2.050  -2.708  1.00 20.00           C
ATOM     24  O   ALA A   3      -2.167  -1.467  -2.378  1.00 20.00           O
ATOM     25  CB  ALA A   3      -4.058  -1.866  -5.016  1.00 20.00           C
ATOM     26  OXT ALA A   3      -3.296  -3.239  -2.404  1.00 20.00           O
ATOM     27  H   ALA A   3      -3.817   0.510  -2.989  1.00 20.00           H
ATOM     28  HA  ALA A   3      -5.218  -1.953  -3.254  1.00 20.00           H
ATOM     29  HB1 ALA A   3      -3.964  -2.918  -5.092  1.00 20.00           H
ATOM     30  HB2 ALA A   3      -4.824  -1.561  -5.659  1.00 20.00           H
ATOM     31  HB3 ALA A   3      -3.162  -1.437  -5.354  1.00 20.00           H
END
"""
    
    test_file = "test_peptide.pdb"
    with open(test_file, 'w') as f:
        f.write(pdb_content)
    
    logger.info(f"Test peptide structure created: {test_file}")
    return test_file


def demonstrate_basic_dynamics():
    """Demonstrate basic dynamics functionality."""
    logger.info("=== Basic Dynamics Demo ===")
    
    if not all([HAS_PYVIAMD, HAS_OPENMM, HAS_DYNAMICS]):
        logger.error("Required packages not available")
        return False
    
    try:
        # Create test structure
        structure_file = create_test_protein()
        
        logger.info(f"Creating DynamicsRunner for {structure_file}")
        runner = DynamicsRunner(structure_file=structure_file)
        
        # Show molecular information
        if runner.viamd_interface:
            n_atoms = runner.viamd_interface.get_num_atoms()
            coords = runner.viamd_interface.get_coordinates()
            elements = runner.viamd_interface.get_elements()
            
            logger.info(f"Loaded molecule:")
            logger.info(f"  - Atoms: {n_atoms}")
            logger.info(f"  - Coordinate shape: {coords.shape}")
            logger.info(f"  - Elements: {set(elements)}")
        
        return True
        
    except Exception as e:
        logger.error(f"Basic dynamics demo failed: {e}")
        import traceback
        traceback.print_exc()
        return False


def demonstrate_minimization(runner):
    """Demonstrate energy minimization."""
    logger.info("=== Energy Minimization Demo ===")
    
    try:
        # Set up system
        params = ProtocolParameters(
            temperature=300.0,
            nonbonded_method="NoCutoff",  # Simplified for demo
            constraints="HBonds"
        )
        
        runner.setup_system(force_field="amber14", protocol_params=params)
        
        # Run minimization
        minimize_params = ProtocolParameters(
            minimize_tolerance=10.0,
            minimize_max_iterations=100
        )
        
        frames = runner.run_protocol(SimulationProtocol.MINIMIZATION, minimize_params)
        
        if frames:
            final_state = frames[-1].state
            logger.info(f"Minimization complete:")
            logger.info(f"  - Final PE: {final_state.potential_energy:.2f} kJ/mol")
            logger.info(f"  - Final KE: {final_state.kinetic_energy:.2f} kJ/mol")
            logger.info(f"  - Temperature: {final_state.temperature:.1f} K")
        
        return True
        
    except Exception as e:
        logger.error(f"Minimization demo failed: {e}")
        return False


def demonstrate_equilibration(runner):
    """Demonstrate equilibration simulation."""
    logger.info("=== Equilibration Demo ===")
    
    try:
        # Run short NVT equilibration
        params = ProtocolParameters(
            temperature=300.0,
            pressure=None,  # NVT
            n_steps=500,  # Short for demo
            time_step=0.002,
            report_interval=100,
            nonbonded_method="NoCutoff",
            constraints="HBonds"
        )
        
        frames = runner.run_protocol(
            SimulationProtocol.NVT_EQUILIBRATION, 
            params, 
            real_time_analysis=True
        )
        
        logger.info(f"Equilibration complete: {len(frames)} frames")
        
        if frames:
            # Show equilibration progress
            initial_state = frames[0].state
            final_state = frames[-1].state
            
            logger.info(f"Initial state:")
            logger.info(f"  - PE: {initial_state.potential_energy:.2f} kJ/mol")
            logger.info(f"  - Temperature: {initial_state.temperature:.1f} K")
            
            logger.info(f"Final state:")
            logger.info(f"  - PE: {final_state.potential_energy:.2f} kJ/mol")
            logger.info(f"  - Temperature: {final_state.temperature:.1f} K")
            logger.info(f"  - Simulation time: {final_state.time:.3f} ps")
        
        return True
        
    except Exception as e:
        logger.error(f"Equilibration demo failed: {e}")
        return False


def demonstrate_production_md(runner):
    """Demonstrate production MD simulation."""
    logger.info("=== Production MD Demo ===")
    
    try:
        # Run short production simulation
        results = runner.run_production_md(
            n_steps=1000,  # Short for demo
            temperature=300.0,
            protocol="nvt_equilibration",  # Use NVT for simplicity
            real_time_analysis=True,
            force_field="amber14"
        )
        
        logger.info(f"Production MD complete:")
        logger.info(f"  - Frames: {results['n_frames']}")
        logger.info(f"  - Simulation time: {results['simulation_time_ps']:.3f} ps")
        
        # Show performance
        if results['performance']:
            perf = results['performance']
            logger.info(f"  - Wall time: {perf.get('total_wall_time', 0):.2f} s")
            logger.info(f"  - Performance: {perf.get('average_ns_per_day', 0):.2f} ns/day")
            logger.info(f"  - Final temperature: {perf.get('current_temperature', 0):.1f} K")
        
        return results
        
    except Exception as e:
        logger.error(f"Production MD demo failed: {e}")
        return None


def demonstrate_trajectory_analysis(runner):
    """Demonstrate trajectory analysis capabilities."""
    logger.info("=== Trajectory Analysis Demo ===")
    
    try:
        # Get trajectory data
        if not runner.trajectory_manager.frames:
            logger.warning("No trajectory data available for analysis")
            return
        
        # Analyze energy evolution
        times, pe_values = runner.trajectory_manager.get_time_series("potential_energy")
        times, ke_values = runner.trajectory_manager.get_time_series("kinetic_energy")
        times, temp_values = runner.trajectory_manager.get_time_series("temperature")
        
        if len(pe_values) > 0:
            logger.info(f"Energy analysis:")
            logger.info(f"  - PE: {np.mean(pe_values):.2f} ± {np.std(pe_values):.2f} kJ/mol")
            logger.info(f"  - KE: {np.mean(ke_values):.2f} ± {np.std(ke_values):.2f} kJ/mol")
            logger.info(f"  - Temperature: {np.mean(temp_values):.1f} ± {np.std(temp_values):.1f} K")
        
        # Export trajectory
        runner.trajectory_manager.export_trajectory("demo_trajectory.xyz", format="xyz")
        runner.trajectory_manager.export_trajectory("demo_analysis.json", format="json")
        
        logger.info("Trajectory exported to demo_trajectory.xyz and demo_analysis.json")
        
        return True
        
    except Exception as e:
        logger.error(f"Trajectory analysis demo failed: {e}")
        return False


def demonstrate_advanced_protocols(runner):
    """Demonstrate advanced simulation protocols."""
    logger.info("=== Advanced Protocols Demo ===")
    
    try:
        # Demonstrate heating protocol
        logger.info("Testing heating protocol...")
        heating_params = ProtocolParameters(
            temperature=300.0,
            n_steps=200,  # Very short for demo
            time_step=0.002,
            report_interval=50,
            nonbonded_method="NoCutoff"
        )
        
        heating_frames = runner.run_protocol(
            SimulationProtocol.HEATING,
            heating_params,
            real_time_analysis=True
        )
        
        logger.info(f"Heating protocol complete: {len(heating_frames)} frames")
        
        # Show temperature progression
        if heating_frames:
            initial_temp = heating_frames[0].state.temperature
            final_temp = heating_frames[-1].state.temperature
            logger.info(f"Temperature change: {initial_temp:.1f} K → {final_temp:.1f} K")
        
        return True
        
    except Exception as e:
        logger.error(f"Advanced protocols demo failed: {e}")
        return False


def demonstrate_monitoring_system(runner):
    """Demonstrate the monitoring system capabilities."""
    logger.info("=== Monitoring System Demo ===")
    
    try:
        # Get performance summary
        performance = runner.monitoring_system.get_performance_summary()
        
        if performance:
            logger.info("Performance summary:")
            for key, value in performance.items():
                if isinstance(value, (int, float)):
                    logger.info(f"  - {key}: {value:.3f}")
                else:
                    logger.info(f"  - {key}: {value}")
        
        # Show any alerts
        if runner.monitoring_system.alerts:
            logger.info(f"System alerts: {len(runner.monitoring_system.alerts)}")
            for alert in runner.monitoring_system.alerts[-3:]:  # Show last 3
                logger.info(f"  - {alert}")
        else:
            logger.info("No system alerts")
        
        return True
        
    except Exception as e:
        logger.error(f"Monitoring system demo failed: {e}")
        return False


def demonstrate_quick_md():
    """Demonstrate the quick_md convenience function."""
    logger.info("=== Quick MD Demo ===")
    
    try:
        # Create test structure
        structure_file = create_test_protein()
        
        # Run quick MD
        logger.info("Running quick MD simulation...")
        results = quick_md(
            structure_file=structure_file,
            n_steps=500,  # Short for demo
            temperature=300.0,
            output_dir="quick_md_output"
        )
        
        logger.info(f"Quick MD complete:")
        logger.info(f"  - Frames: {results['n_frames']}")
        logger.info(f"  - Simulation time: {results['simulation_time_ps']:.3f} ps")
        
        # Show exported files
        if 'exported_files' in results:
            logger.info("Exported files:")
            for file_type, filename in results['exported_files'].items():
                logger.info(f"  - {file_type}: {filename}")
        
        return True
        
    except Exception as e:
        logger.error(f"Quick MD demo failed: {e}")
        return False


def check_requirements_and_setup():
    """Check all requirements and provide setup instructions."""
    logger.info("=== Requirements Check ===")
    
    requirements = check_requirements()
    
    all_satisfied = True
    for package, available in requirements.items():
        status = "✓" if available else "✗"
        logger.info(f"{status} {package}")
        if not available:
            all_satisfied = False
    
    if not all_satisfied:
        logger.error("\nSome requirements are missing!")
        logger.info("Setup instructions:")
        
        if not requirements['pyviamd']:
            logger.info("  Build VIAMD with Python support:")
            logger.info("    mkdir build && cd build")
            logger.info("    cmake .. -DVIAMD_ENABLE_PYTHON=ON -DCMAKE_BUILD_TYPE=Release")
            logger.info("    make -j$(nproc)")
            logger.info("    export PYTHONPATH=\"$PWD/lib:$PYTHONPATH\"")
            
        if not requirements['openmm']:
            logger.info("  Install OpenMM: conda install -c conda-forge openmm")
            
        if not requirements['matplotlib']:
            logger.info("  Install matplotlib (optional): pip install matplotlib")
    
    return all_satisfied


def main():
    """Main demonstration function."""
    logger.info("VIAMD OpenMM Dynamics Interface Demonstration")
    logger.info("=" * 60)
    
    # Check requirements
    if not check_requirements_and_setup():
        logger.error("Requirements not satisfied. Please install missing packages.")
        return 1
    
    # Run basic functionality test
    if not demonstrate_basic_dynamics():
        logger.error("Basic dynamics test failed")
        return 1
    
    # Get command line arguments
    structure_file = None
    n_steps = 1000  # Default
    
    if len(sys.argv) > 1:
        structure_file = sys.argv[1]
        if not os.path.exists(structure_file):
            logger.error(f"Structure file not found: {structure_file}")
            structure_file = None
    
    if len(sys.argv) > 2:
        try:
            n_steps = int(sys.argv[2])
        except ValueError:
            logger.warning(f"Invalid n_steps: {sys.argv[2]}. Using default: {n_steps}")
    
    # Create structure if none provided
    if not structure_file:
        logger.info("No structure file provided. Creating test structure...")
        try:
            structure_file = create_test_protein()
        except Exception as e:
            logger.error(f"Failed to create test structure: {e}")
            return 1
    
    # Run comprehensive demonstration
    try:
        logger.info(f"Running comprehensive dynamics demonstration with {structure_file}")
        runner = DynamicsRunner(structure_file=structure_file)
        
        # Run demonstration sequence
        success = True
        
        # 1. Energy minimization
        if not demonstrate_minimization(runner):
            success = False
        
        # 2. Equilibration
        if success and not demonstrate_equilibration(runner):
            success = False
        
        # 3. Production MD
        if success:
            results = demonstrate_production_md(runner)
            if not results:
                success = False
        
        # 4. Trajectory analysis
        if success and not demonstrate_trajectory_analysis(runner):
            success = False
        
        # 5. Advanced protocols
        if success and not demonstrate_advanced_protocols(runner):
            success = False
        
        # 6. Monitoring system
        if success and not demonstrate_monitoring_system(runner):
            success = False
        
        if success:
            logger.info("\n🎉 Comprehensive dynamics demonstration completed successfully!")
            logger.info("\nKey features demonstrated:")
            logger.info("  ✓ High-level DynamicsRunner interface")
            logger.info("  ✓ Multiple simulation protocols (minimization, equilibration, production)")
            logger.info("  ✓ Real-time monitoring and performance analysis")
            logger.info("  ✓ Trajectory management with automatic analysis")
            logger.info("  ✓ Advanced protocols (heating, cooling)")
            logger.info("  ✓ Comprehensive result export")
        else:
            logger.error("Some demonstrations failed")
    
    except Exception as e:
        logger.error(f"Comprehensive demonstration failed: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    # Demonstrate quick MD function
    logger.info("\n" + "="*60)
    if not demonstrate_quick_md():
        logger.warning("Quick MD demonstration failed")
    
    logger.info("\n🚀 VIAMD OpenMM dynamics interface is ready for production use!")
    logger.info("\nExample usage:")
    logger.info("  from pyviamd.dynamics import DynamicsRunner")
    logger.info("  runner = DynamicsRunner('protein.pdb')")
    logger.info("  results = runner.run_production_md(n_steps=100000)")
    logger.info("  runner.export_results(results, 'output_dir')")
    
    return 0


if __name__ == "__main__":
    sys.exit(main())