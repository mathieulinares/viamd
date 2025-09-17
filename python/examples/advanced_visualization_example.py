#!/usr/bin/env python3
"""
Advanced Visualization Example for VIAMD Phase 5

This example demonstrates the comprehensive visualization capabilities
provided by VIAMD's Phase 5 implementation, including 3D molecular
visualization, real-time plotting, and data export for external tools.

Features demonstrated:
- 3D molecular structure visualization
- Real-time data plotting and monitoring
- Color mapping and molecular rendering
- Animation creation for trajectory data
- Data export for external visualization tools
- Plot generation and customization

Usage:
    python advanced_visualization_example.py
"""

import sys
import os
import numpy as np
import logging
import time

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

try:
    import pyviamd
    from pyviamd.integrations import visualization_integration
    print("✓ VIAMD Python bindings loaded successfully")
except ImportError as e:
    print(f"✗ Failed to import VIAMD Python bindings: {e}")
    print("Please build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    sys.exit(1)

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)


def generate_test_molecule():
    """Generate a test molecular structure for visualization."""
    print("🧬 Generating test molecular structure...")
    
    # Create a small protein-like structure
    n_atoms = 30
    atom_names = []
    coordinates = []
    
    # Generate backbone atoms (CA, C, N pattern)
    for i in range(n_atoms):
        if i % 3 == 0:
            atom_names.append('C')  # Carbon alpha
        elif i % 3 == 1:
            atom_names.append('C')  # Carbonyl carbon
        else:
            atom_names.append('N')  # Nitrogen
        
        # Create helix-like structure
        angle = i * 100 * np.pi / 180  # 100 degrees per residue
        radius = 3.0
        
        x = radius * np.cos(angle)
        y = radius * np.sin(angle)
        z = i * 1.5
        
        coordinates.append([x, y, z])
    
    # Add some side chain atoms
    for i in range(10):
        atom_names.append('O')  # Oxygen
        # Random positions around backbone
        base_coord = coordinates[i * 2] if i * 2 < len(coordinates) else coordinates[-1]
        x = base_coord[0] + np.random.uniform(-2, 2)
        y = base_coord[1] + np.random.uniform(-2, 2)
        z = base_coord[2] + np.random.uniform(-1, 1)
        coordinates.append([x, y, z])
    
    coordinates = np.array(coordinates)
    
    print(f"   Generated molecule with {len(atom_names)} atoms")
    print(f"   Atom types: {set(atom_names)}")
    print(f"   Coordinate range: {coordinates.min():.2f} to {coordinates.max():.2f} Å")
    
    return coordinates, atom_names


def generate_test_trajectory(n_frames=20):
    """Generate a test trajectory for animation."""
    print(f"🎬 Generating test trajectory with {n_frames} frames...")
    
    base_coords, atom_names = generate_test_molecule()
    trajectory = []
    
    for frame in range(n_frames):
        # Add some motion to the molecule
        frame_coords = base_coords.copy()
        
        # Breathing motion
        breathing_factor = 1.0 + 0.2 * np.sin(frame * 0.3)
        frame_coords *= breathing_factor
        
        # Random thermal motion
        thermal_motion = np.random.normal(0, 0.1, frame_coords.shape)
        frame_coords += thermal_motion
        
        # Rotation around z-axis
        angle = frame * 0.1
        cos_a, sin_a = np.cos(angle), np.sin(angle)
        rotation_matrix = np.array([
            [cos_a, -sin_a, 0],
            [sin_a, cos_a, 0],
            [0, 0, 1]
        ])
        
        frame_coords = frame_coords @ rotation_matrix.T
        
        trajectory.append(frame_coords)
    
    print(f"   Generated trajectory with {len(trajectory)} frames")
    
    return trajectory, atom_names


def demonstrate_data_export():
    """Demonstrate data export capabilities."""
    print("\n" + "="*60)
    print("DATA EXPORT DEMONSTRATION")
    print("="*60)
    
    # Generate test data
    coordinates, atom_names = generate_test_molecule()
    
    # Create data exporter
    exporter = pyviamd.visualization.DataExporter()
    
    # Export coordinates in different formats
    formats = ['xyz', 'pdb']
    
    for fmt in formats:
        filename = f"test_molecule.{fmt}"
        print(f"💾 Exporting to {fmt.upper()} format...")
        
        success = exporter.export_coordinates(coordinates, atom_names, filename, fmt)
        
        if success:
            print(f"   ✓ Successfully exported to {filename}")
            
            # Check file size
            if os.path.exists(filename):
                size = os.path.getsize(filename)
                print(f"     File size: {size} bytes")
                
                # Show first few lines
                with open(filename, 'r') as f:
                    lines = f.readlines()[:5]
                    print("     First few lines:")
                    for line in lines:
                        print(f"       {line.strip()}")
        else:
            print(f"   ✗ Failed to export to {filename}")
    
    # Export trajectory as JSON
    trajectory, _ = generate_test_trajectory(5)  # Small trajectory for demo
    
    print(f"\n🎬 Exporting trajectory to JSON...")
    success = exporter.export_trajectory_json(trajectory, atom_names, "test_trajectory.json")
    
    if success:
        print(f"   ✓ Successfully exported trajectory")
        if os.path.exists("test_trajectory.json"):
            size = os.path.getsize("test_trajectory.json")
            print(f"     File size: {size} bytes")
    
    # Export analysis data as CSV
    analysis_data = {
        'time': np.arange(100),
        'energy': -100 + 20 * np.sin(np.arange(100) * 0.1),
        'temperature': 300 + 10 * np.random.normal(size=100),
        'rg': 5.0 + np.random.normal(0, 0.5, 100)
    }
    
    print(f"\n📊 Exporting analysis data to CSV...")
    success = exporter.export_analysis_csv(analysis_data, "analysis_data.csv")
    
    if success:
        print(f"   ✓ Successfully exported analysis data")
        if os.path.exists("analysis_data.csv"):
            size = os.path.getsize("analysis_data.csv")
            print(f"     File size: {size} bytes")
    
    # Prepare data for matplotlib
    print(f"\n📈 Preparing data for matplotlib...")
    matplotlib_data = exporter.prepare_matplotlib_data(analysis_data)
    print(f"   ✓ Prepared data keys: {list(matplotlib_data.keys())}")
    
    for key, values in matplotlib_data.items():
        print(f"     {key}: {len(values)} values")


def demonstrate_real_time_plotting():
    """Demonstrate real-time plotting capabilities."""
    print("\n" + "="*60)
    print("REAL-TIME PLOTTING DEMONSTRATION")
    print("="*60)
    
    # Create real-time plotter
    plotter = pyviamd.visualization.RealTimePlotter(max_points=100)
    
    # Simulate real-time data
    print("📊 Simulating real-time data streams...")
    
    data_collected = {}
    
    def collect_data(data_list):
        """Callback to collect data instead of plotting (for demo)."""
        nonlocal data_collected
        # Store the last few points for demonstration
        if len(data_list) > 0:
            data_collected['last_values'] = list(data_list)[-10:]  # Last 10 values
    
    # Register callbacks for different data series
    series_names = ['energy', 'temperature', 'pressure', 'volume']
    
    for series in series_names:
        plotter.register_plot_callback(series, collect_data)
    
    # Generate and add real-time data
    n_points = 50
    
    for i in range(n_points):
        # Generate simulated data
        energy = -100 + 20 * np.sin(i * 0.1) + np.random.normal(0, 2)
        temperature = 300 + 10 * np.sin(i * 0.05) + np.random.normal(0, 5)
        pressure = 1.0 + 0.2 * np.sin(i * 0.15) + np.random.normal(0, 0.1)
        volume = 1000 + 50 * np.sin(i * 0.08) + np.random.normal(0, 10)
        
        # Add data points
        plotter.add_data_point('energy', energy)
        plotter.add_data_point('temperature', temperature)
        plotter.add_data_point('pressure', pressure)
        plotter.add_data_point('volume', volume)
        
        if i % 10 == 0:
            print(f"   📈 Added data point {i}: Energy={energy:.2f}, Temp={temperature:.1f}")
    
    # Get data from series
    print(f"\n📊 Real-time data collection results:")
    
    for series in series_names:
        data = plotter.get_series_data(series)
        if data:
            print(f"   {series}: {len(data)} points")
            print(f"     Range: {min(data):.2f} to {max(data):.2f}")
            print(f"     Mean: {np.mean(data):.2f}")
    
    # Get all series names
    all_series = plotter.get_series_names()
    print(f"\n🏷️  Active series: {all_series}")
    
    # Get all data
    all_data = plotter.get_all_data()
    print(f"📁 Total data collected: {len(all_data)} series")
    
    # Clear specific series
    plotter.clear_series('energy')
    remaining_series = plotter.get_series_names()
    print(f"🧹 After clearing 'energy': {remaining_series}")


def demonstrate_color_mapping():
    """Demonstrate color mapping utilities."""
    print("\n" + "="*60)
    print("COLOR MAPPING DEMONSTRATION")
    print("="*60)
    
    # Create color mapper
    color_mapper = pyviamd.visualization.ColorMapper()
    
    # Test element colors
    test_elements = ['H', 'C', 'N', 'O', 'S', 'P', 'Fe', 'Cu', 'Unknown']
    
    print("🎨 Element-based color mapping (CPK convention):")
    element_colors = color_mapper.element_colors(test_elements)
    
    for i, element in enumerate(test_elements):
        r, g, b = element_colors[i]
        hex_color = f"#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}"
        print(f"   {element:8s}: RGB({r:.2f}, {g:.2f}, {b:.2f}) = {hex_color}")
    
    # Test scalar to color mapping
    print(f"\n🌈 Scalar-to-color mapping:")
    
    # Generate test scalar values
    scalar_values = np.linspace(0, 1, 10)
    
    colormaps = ['viridis', 'plasma', 'hot', 'cool']
    
    for colormap in colormaps:
        print(f"\n   {colormap.title()} colormap:")
        colors = color_mapper.scalar_to_rgb(scalar_values, colormap, 0.0, 1.0)
        
        for i, value in enumerate(scalar_values):
            r, g, b = colors[i]
            hex_color = f"#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}"
            print(f"     Value {value:.1f}: {hex_color}")


def demonstrate_mesh_generation():
    """Demonstrate mesh generation utilities."""
    print("\n" + "="*60)
    print("MESH GENERATION DEMONSTRATION")
    print("="*60)
    
    # Create mesh generator
    mesh_gen = pyviamd.visualization.MeshGenerator()
    
    # Generate sphere mesh
    print("⚪ Generating sphere mesh...")
    sphere_mesh = mesh_gen.generate_sphere_mesh(radius=2.0, resolution=10)
    
    print(f"   Vertices: {sphere_mesh['n_vertices']}")
    print(f"   Faces: {sphere_mesh['n_faces']}")
    print(f"   Vertex array shape: {sphere_mesh['vertices'].shape}")
    print(f"   Face array shape: {sphere_mesh['faces'].shape}")
    
    # Show some vertex coordinates
    vertices = sphere_mesh['vertices']
    print(f"   Sample vertices:")
    for i in range(min(5, len(vertices) // 3)):
        idx = i * 3
        x, y, z = vertices[idx], vertices[idx + 1], vertices[idx + 2]
        print(f"     Vertex {i}: ({x:.2f}, {y:.2f}, {z:.2f})")
    
    # Generate cylinder mesh
    print(f"\n🔵 Generating cylinder mesh...")
    start_pos = np.array([0.0, 0.0, 0.0])
    end_pos = np.array([3.0, 2.0, 1.0])
    
    cylinder_mesh = mesh_gen.generate_cylinder_mesh(
        start_pos, end_pos, radius=0.5, resolution=8
    )
    
    print(f"   Vertices: {cylinder_mesh['n_vertices']}")
    print(f"   Faces: {cylinder_mesh['n_faces']}")
    print(f"   Vertex array shape: {cylinder_mesh['vertices'].shape}")
    print(f"   Face array shape: {cylinder_mesh['faces'].shape}")
    
    # Calculate cylinder properties
    vertices = cylinder_mesh['vertices']
    min_coords = np.min(vertices.reshape(-1, 3), axis=0)
    max_coords = np.max(vertices.reshape(-1, 3), axis=0)
    
    print(f"   Bounding box:")
    print(f"     Min: ({min_coords[0]:.2f}, {min_coords[1]:.2f}, {min_coords[2]:.2f})")
    print(f"     Max: ({max_coords[0]:.2f}, {max_coords[1]:.2f}, {max_coords[2]:.2f})")


def demonstrate_visualization_manager():
    """Demonstrate the high-level visualization manager."""
    print("\n" + "="*60)
    print("VISUALIZATION MANAGER DEMONSTRATION")
    print("="*60)
    
    # Check if plotting libraries are available
    try:
        import matplotlib.pyplot as plt
        has_matplotlib = True
        print("✓ Matplotlib available for plotting")
    except ImportError:
        has_matplotlib = False
        print("⚠️  Matplotlib not available - some features will be skipped")
    
    try:
        import plotly.graph_objects as go
        has_plotly = True
        print("✓ Plotly available for 3D visualization")
    except ImportError:
        has_plotly = False
        print("⚠️  Plotly not available - 3D visualization will be skipped")
    
    # Create visualization manager
    viz_manager = visualization_integration.VisualizationManager()
    
    # Setup real-time plotting
    if has_matplotlib:
        print(f"\n📊 Setting up real-time plotting...")
        plot_names = ['energy', 'temperature', 'pressure']
        viz_manager.setup_real_time_plotting(plot_names)
        
        # Simulate some data updates
        for i in range(20):
            data = {
                'energy': -100 + 20 * np.sin(i * 0.1) + np.random.normal(0, 2),
                'temperature': 300 + 10 * np.sin(i * 0.05) + np.random.normal(0, 5),
                'pressure': 1.0 + 0.2 * np.sin(i * 0.15) + np.random.normal(0, 0.1)
            }
            viz_manager.update_plots(data)
        
        print(f"   ✓ Updated plots with 20 data points")
        
        # Get current plot data
        plot_data = viz_manager.get_plot_data()
        print(f"   📈 Current plot data: {list(plot_data.keys())}")
    
    # Create molecular visualization
    coordinates, atom_names = generate_test_molecule()
    
    if has_plotly:
        print(f"\n🧬 Creating 3D molecular visualization...")
        
        # Generate some bonds (connect nearby atoms)
        bonds = []
        for i in range(len(coordinates)):
            for j in range(i + 1, min(i + 3, len(coordinates))):  # Connect to next 2 atoms
                bonds.append((i, j))
        
        viz_data = viz_manager.create_molecular_visualization(
            coordinates, atom_names, bonds,
            title="Test Molecule Structure",
            atom_size=10,
            bond_width=6
        )
        
        if viz_data:
            print(f"   ✓ Created 3D visualization")
            print(f"     Atoms: {viz_data['n_atoms']}")
            print(f"     Bonds: {viz_data['n_bonds']}")
            print(f"     Type: {viz_data['type']}")
    
    # Create property plots
    if has_matplotlib:
        print(f"\n📈 Creating property plots...")
        
        # Generate test data
        x_data = np.linspace(0, 10, 100)
        plot_data = {
            'sine_wave': np.sin(x_data),
            'cosine_wave': np.cos(x_data),
            'exponential': np.exp(-x_data / 5)
        }
        
        # Line plot
        line_plot = viz_manager.create_property_plot(
            plot_data, 'line',
            title="Test Property Plot",
            xlabel="Time (ns)",
            ylabel="Value"
        )
        
        if line_plot:
            print(f"   ✓ Created line plot")
            print(f"     Type: {line_plot['type']}")
            print(f"     Series: {line_plot['series_count']}")
        
        # Histogram
        hist_data = {'random_data': np.random.normal(0, 1, 1000)}
        histogram = viz_manager.create_property_plot(
            hist_data, 'histogram',
            title="Random Data Distribution",
            bins=50
        )
        
        if histogram:
            print(f"   ✓ Created histogram")
            print(f"     Type: {histogram['type']}")
    
    # Export data
    print(f"\n💾 Testing data export...")
    
    export_data = {
        'coordinates': coordinates,
        'atom_names': atom_names
    }
    
    export_formats = ['xyz', 'pdb', 'json']
    
    for fmt in export_formats:
        filename = f"viz_export.{fmt}"
        success = viz_manager.export_data(export_data, filename, fmt)
        
        if success:
            print(f"   ✓ Exported to {filename}")
        else:
            print(f"   ✗ Failed to export to {filename}")
    
    # Test animation creation
    if has_matplotlib:
        print(f"\n🎬 Creating trajectory animation...")
        
        trajectory, _ = generate_test_trajectory(10)  # Small trajectory
        
        animation_data = viz_manager.create_animation(
            trajectory, atom_names,
            title="Molecular Dynamics Trajectory",
            interval=200  # ms between frames
        )
        
        if animation_data:
            print(f"   ✓ Created animation")
            print(f"     Frames: {animation_data['n_frames']}")
            print(f"     Atoms: {animation_data['n_atoms']}")
            print(f"     Type: {animation_data['type']}")
    
    # Clear plots
    viz_manager.clear_plots()
    print(f"🧹 Cleared all plots")


def main():
    """Main demonstration function."""
    print("VIAMD Phase 5: Advanced Visualization Demo")
    print("=" * 60)
    print("This example demonstrates the advanced visualization capabilities")
    print("introduced in VIAMD Phase 5 implementation.")
    print()
    
    try:
        # Test basic C++ bindings availability
        print("🧪 Testing VIAMD visualization bindings...")
        
        # Test data exporter
        exporter = pyviamd.visualization.DataExporter()
        print(f"   ✓ Data exporter available")
        
        # Test real-time plotter
        plotter = pyviamd.visualization.RealTimePlotter()
        print(f"   ✓ Real-time plotter available")
        
        # Test color mapper
        mapper = pyviamd.visualization.ColorMapper()
        test_colors = mapper.element_colors(['H', 'C', 'N', 'O'])
        print(f"   ✓ Color mapper working (got {len(test_colors)} colors)")
        
        # Test mesh generator
        mesh_gen = pyviamd.visualization.MeshGenerator()
        sphere = mesh_gen.generate_sphere_mesh(1.0, 10)
        print(f"   ✓ Mesh generator working (sphere has {sphere['n_vertices']} vertices)")
        
        print("\n🎉 All Phase 5 visualization bindings loaded successfully!")
        
        # Run demonstrations
        demonstrate_data_export()
        demonstrate_real_time_plotting()
        demonstrate_color_mapping()
        demonstrate_mesh_generation()
        demonstrate_visualization_manager()
        
        print("\n" + "="*60)
        print("✅ PHASE 5 VISUALIZATION DEMONSTRATION COMPLETED SUCCESSFULLY")
        print("="*60)
        print()
        print("Phase 5 provides comprehensive visualization capabilities:")
        print("• Data export in multiple formats (XYZ, PDB, JSON, CSV)")
        print("• Real-time plotting with customizable callbacks")
        print("• Advanced color mapping with multiple color schemes")
        print("• 3D mesh generation for molecular surfaces")
        print("• High-level visualization management")
        print("• Animation creation for trajectory data")
        print("• Integration with popular plotting libraries")
        print()
        print("These tools enable sophisticated molecular visualization workflows")
        print("with seamless integration into Python visualization ecosystems.")
        
    except Exception as e:
        print(f"\n❌ Error during demonstration: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())