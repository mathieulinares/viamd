#!/usr/bin/env python3
"""
OpenMM Dynamics GUI Integration Example for VIAMD

This example demonstrates how to use the new OpenMM dynamics GUI interface
integrated into VIAMD. The interface provides a complete visual workflow
for running molecular dynamics simulations with OpenMM.

GUI Interface Features:
1. Force Field Selection - Choose from AMBER, CHARMM, OPLS-AA
2. Water Model Options - TIP3P, TIP4P, SPC, SPC/E  
3. Simulation Protocols - Minimization, NVT/NPT equilibration, production
4. Real-time Monitoring - Progress, energy, temperature tracking
5. Visualization Integration - Automatic coordinate updates in VIAMD
6. Export Functionality - Multiple trajectory formats (XYZ, PDB, JSON)

Usage:
1. Load a molecular structure in VIAMD
2. Go to Windows -> OpenMM Dynamics to open the interface
3. Setup tab: Choose force field and water model
4. Protocol tab: Select simulation type and parameters  
5. Run simulation and monitor in real-time
6. Visualize results directly in VIAMD
7. Export trajectories as needed

The interface seamlessly bridges VIAMD's visualization capabilities with
OpenMM's molecular dynamics simulation engine.
"""

# This would be the underlying Python code that the GUI calls
import pyviamd
from pyviamd.dynamics import DynamicsRunner, ProtocolParameters, SimulationProtocol

def run_openmm_simulation_from_gui():
    """
    Example of how the GUI interface uses the Python bindings
    """
    
    # This would be called when user clicks "Initialize System"
    # The GUI passes the current VIAMD molecular system
    runner = DynamicsRunner()
    runner.initialize_from_viamd()
    
    # Force field setup (selected from GUI dropdown)
    force_field = "amber14"  # From GUI selection
    water_model = "tip3p"    # From GUI selection
    runner.setup_system(force_field, water_model=water_model)
    
    # Protocol parameters (configured in GUI)
    params = ProtocolParameters(
        temperature=300.0,      # From GUI input
        n_steps=10000,         # From GUI input
        time_step=0.002,       # From GUI input
        report_interval=100,   # From GUI input
        use_pbc=True,          # From GUI checkbox
        nonbonded_cutoff=1.0   # From GUI input
    )
    
    # Run protocol (selected from GUI)
    protocol = SimulationProtocol.MINIMIZATION  # From GUI selection
    
    # Start simulation with real-time callbacks for GUI updates
    def update_progress(progress):
        # GUI updates progress bar, energy plots, etc.
        print(f"Step {progress.current_step}/{progress.total_steps}")
        print(f"Temperature: {progress.current_temperature} K")
        print(f"Energy: {progress.potential_energy} kJ/mol")
    
    def update_coordinates(coords):
        # GUI updates VIAMD visualization in real-time
        print(f"Updating visualization with {len(coords)//3} atoms")
    
    runner.set_progress_callback(update_progress)
    runner.set_coordinate_callback(update_coordinates)
    
    # Run the simulation
    results = runner.run_protocol(protocol, params)
    
    # Export results (from GUI export tab)
    runner.export_results(results, "simulation_output", format="xyz")
    
    print("Simulation completed successfully!")

if __name__ == "__main__":
    print(__doc__)
    print("\nExample of GUI backend integration:")
    run_openmm_simulation_from_gui()