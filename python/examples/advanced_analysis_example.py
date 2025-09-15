#!/usr/bin/env python3
"""
Advanced Analysis Example for VIAMD Phase 5

This example demonstrates the comprehensive molecular analysis capabilities
provided by VIAMD's Phase 5 implementation, including statistical analysis,
geometric calculations, and real-time data processing.

Features demonstrated:
- Advanced statistical analysis of molecular data
- Geometric property calculations (angles, dihedrals, radius of gyration)
- Real-time data processing and monitoring
- Performance profiling and optimization
- Data export for external tools

Usage:
    python advanced_analysis_example.py
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
    from pyviamd.integrations import analysis_integration
    print("✓ VIAMD Python bindings loaded successfully")
except ImportError as e:
    print(f"✗ Failed to import VIAMD Python bindings: {e}")
    print("Please build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    sys.exit(1)

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)


def generate_test_trajectory(n_frames=100, n_atoms=50):
    """Generate a test molecular trajectory for demonstration."""
    print(f"Generating test trajectory: {n_frames} frames, {n_atoms} atoms")
    
    trajectory = []
    
    # Create a simple protein-like structure that evolves over time
    for frame in range(n_frames):
        # Start with a random protein-like structure
        np.random.seed(42 + frame)  # Reproducible but evolving
        
        # Generate backbone coordinates (approximate protein structure)
        backbone_coords = []
        for i in range(n_atoms):
            # Helix-like structure with some flexibility
            angle = i * 100 * np.pi / 180  # 100 degrees per residue
            radius = 2.0 + 0.5 * np.sin(frame * 0.1)  # Breathing motion
            
            x = radius * np.cos(angle) + np.random.normal(0, 0.1)
            y = radius * np.sin(angle) + np.random.normal(0, 0.1)
            z = i * 1.5 + frame * 0.02 + np.random.normal(0, 0.1)  # Growth over time
            
            backbone_coords.append([x, y, z])
        
        trajectory.append(np.array(backbone_coords))
    
    return trajectory


def demonstrate_statistical_analysis():
    """Demonstrate advanced statistical analysis capabilities."""
    print("\n" + "="*60)
    print("STATISTICAL ANALYSIS DEMONSTRATION")
    print("="*60)
    
    # Generate test data
    np.random.seed(42)
    test_data = np.random.normal(loc=5.0, scale=2.0, size=1000)
    
    # Create statistical analyzer
    stats_analyzer = pyviamd.analysis.StatisticalAnalyzer()
    
    # Calculate basic statistics
    stats = stats_analyzer.calculate_statistics(test_data)
    print(f"📊 Basic Statistics:")
    print(f"   Mean: {stats.mean:.3f}")
    print(f"   Standard Deviation: {stats.std_dev:.3f}")
    print(f"   Min: {stats.min_val:.3f}")
    print(f"   Max: {stats.max_val:.3f}")
    print(f"   Median: {stats.median:.3f}")
    print(f"   Count: {stats.count}")
    
    # Rolling statistics
    window_size = 50
    rolling_stats = stats_analyzer.rolling_statistics(test_data, window_size)
    print(f"\n📈 Rolling Statistics (window size: {window_size}):")
    print(f"   Number of windows: {len(rolling_stats)}")
    print(f"   First window mean: {rolling_stats[0].mean:.3f}")
    print(f"   Last window mean: {rolling_stats[-1].mean:.3f}")
    
    # Correlation analysis
    test_data2 = test_data + np.random.normal(0, 1.0, len(test_data))  # Correlated data
    correlation = stats_analyzer.correlation(test_data, test_data2)
    print(f"\n🔗 Correlation Analysis:")
    print(f"   Correlation coefficient: {correlation:.3f}")
    
    # Autocorrelation
    max_lag = min(100, len(test_data) // 4)
    autocorr = stats_analyzer.autocorrelation(test_data, max_lag)
    print(f"\n🔄 Autocorrelation Analysis:")
    print(f"   Autocorr at lag 0: {autocorr[0]:.3f}")
    print(f"   Autocorr at lag 10: {autocorr[10]:.3f}")
    print(f"   Autocorr at lag 50: {autocorr[50]:.3f}")


def demonstrate_geometry_analysis():
    """Demonstrate molecular geometry analysis capabilities."""
    print("\n" + "="*60)
    print("GEOMETRY ANALYSIS DEMONSTRATION")
    print("="*60)
    
    # Generate test molecular structure
    n_atoms = 20
    np.random.seed(42)
    coordinates = np.random.uniform(-10, 10, (n_atoms, 3))
    
    # Create geometry analyzer
    geom_analyzer = pyviamd.analysis.GeometryAnalyzer()
    
    # Distance matrix calculation
    print("🔍 Calculating distance matrix...")
    dist_matrix = geom_analyzer.distance_matrix(coordinates)
    print(f"   Distance matrix shape: {dist_matrix.shape}")
    print(f"   Average distance: {np.mean(dist_matrix[dist_matrix > 0]):.3f} Å")
    print(f"   Min distance: {np.min(dist_matrix[dist_matrix > 0]):.3f} Å")
    print(f"   Max distance: {np.max(dist_matrix):.3f} Å")
    
    # Angle calculations
    if n_atoms >= 3:
        print("\n📐 Calculating bond angles...")
        angles = []
        for i in range(min(5, n_atoms - 2)):  # Calculate first 5 angles
            angle = geom_analyzer.calculate_angle(
                coordinates[i], coordinates[i + 1], coordinates[i + 2]
            )
            angles.append(angle)
            print(f"   Angle {i+1}: {angle:.2f}°")
        
        print(f"   Average angle: {np.mean(angles):.2f}°")
    
    # Dihedral calculations
    if n_atoms >= 4:
        print("\n🔄 Calculating dihedral angles...")
        dihedrals = []
        for i in range(min(5, n_atoms - 3)):  # Calculate first 5 dihedrals
            dihedral = geom_analyzer.calculate_dihedral(
                coordinates[i], coordinates[i + 1], 
                coordinates[i + 2], coordinates[i + 3]
            )
            dihedrals.append(dihedral)
            print(f"   Dihedral {i+1}: {dihedral:.2f}°")
        
        print(f"   Average dihedral: {np.mean(dihedrals):.2f}°")
    
    # Radius of gyration
    masses = np.ones(n_atoms)  # Assume uniform masses
    rg = geom_analyzer.radius_of_gyration(coordinates, masses)
    print(f"\n⚪ Radius of gyration: {rg:.3f} Å")


def demonstrate_real_time_processing():
    """Demonstrate real-time molecular data processing."""
    print("\n" + "="*60)
    print("REAL-TIME PROCESSING DEMONSTRATION")
    print("="*60)
    
    # Create real-time processor
    processor = pyviamd.analysis.RealTimeProcessor()
    
    # Data collection for demonstration
    processed_frames = []
    
    def analysis_callback(frame_data):
        """Callback function for real-time analysis."""
        frame_id = frame_data.get('frame', 0)
        energy = frame_data.get('energy', 0.0)
        
        # Simulate some analysis
        analysis_result = {
            'frame': frame_id,
            'energy': energy,
            'analysis_time': time.time(),
            'status': 'processed'
        }
        
        processed_frames.append(analysis_result)
        
        if frame_id % 10 == 0:
            print(f"   📊 Processed frame {frame_id}, Energy: {energy:.2f}")
    
    # Register callback
    processor.add_callback("analysis", analysis_callback)
    
    # Simulate real-time data stream
    print("🔄 Simulating real-time data processing...")
    
    n_frames = 50
    for frame_id in range(n_frames):
        # Simulate frame data
        frame_data = {
            'frame': frame_id,
            'timestamp': time.time(),
            'energy': -100 + 20 * np.sin(frame_id * 0.1) + np.random.normal(0, 2),
            'temperature': 300 + 10 * np.random.normal(),
            'coordinates': np.random.uniform(-5, 5, (20, 3))
        }
        
        # Process frame
        processor.process_frame(frame_data)
        
        # Small delay to simulate real-time
        time.sleep(0.01)
    
    print(f"✓ Processed {len(processed_frames)} frames")
    
    # Get buffered data
    buffered_data = processor.get_buffered_data()
    print(f"📁 Buffered data keys: {list(buffered_data.keys())}")
    
    # Get buffer information
    buffer_keys = processor.get_buffer_keys()
    print(f"🔑 Buffer keys: {buffer_keys}")


def demonstrate_performance_monitoring():
    """Demonstrate performance monitoring and profiling."""
    print("\n" + "="*60)
    print("PERFORMANCE MONITORING DEMONSTRATION")
    print("="*60)
    
    # Create performance monitor
    monitor = pyviamd.analysis.PerformanceMonitor()
    
    # Simulate various operations with timing
    operations = [
        ('matrix_calculation', lambda: np.random.rand(1000, 1000) @ np.random.rand(1000, 1000)),
        ('data_processing', lambda: np.fft.fft(np.random.rand(10000))),
        ('statistical_analysis', lambda: np.std(np.random.rand(100000))),
        ('geometry_calculation', lambda: np.linalg.norm(np.random.rand(1000, 3), axis=1)),
    ]
    
    print("⏱️  Running performance tests...")
    
    # Run each operation multiple times
    for op_name, operation in operations:
        print(f"   Testing {op_name}...")
        
        for i in range(10):  # Run 10 times for statistics
            monitor.start_timer(op_name)
            result = operation()
            duration = monitor.stop_timer(op_name)
            
            if i == 0:  # Print first timing
                print(f"      First run: {duration:.2f} ms")
    
    # Get timing statistics
    print("\n📈 Performance Statistics:")
    
    for op_name, _ in operations:
        stats = monitor.get_timing_stats(op_name)
        print(f"   {op_name}:")
        print(f"      Mean: {stats.mean:.2f} ms")
        print(f"      Std Dev: {stats.std_dev:.2f} ms")
        print(f"      Min: {stats.min_val:.2f} ms")
        print(f"      Max: {stats.max_val:.2f} ms")
        print(f"      Count: {stats.count}")
    
    # Get all timings
    all_timings = monitor.get_all_timings()
    print(f"\n📊 Total operations monitored: {len(all_timings)}")
    
    # Get operation names
    operation_names = monitor.get_operation_names()
    print(f"🏷️  Monitored operations: {operation_names}")


def demonstrate_analysis_workflow():
    """Demonstrate the comprehensive analysis workflow."""
    print("\n" + "="*60)
    print("COMPREHENSIVE ANALYSIS WORKFLOW DEMONSTRATION")
    print("="*60)
    
    # Generate test trajectory
    trajectory = generate_test_trajectory(n_frames=20, n_atoms=15)
    
    # Create analysis workflow
    workflow = analysis_integration.AnalysisWorkflow()
    
    # Configure analyzers
    print("🔧 Configuring analysis workflow...")
    
    workflow.add_analyzer("rdf", "rdf", r_max=15.0, n_bins=50)
    workflow.add_analyzer("geometry", "geometry", 
                         radius_of_gyration=True, 
                         center_of_mass=True,
                         calculate_angles=True)
    workflow.add_analyzer("statistics", "statistics", per_dimension=True)
    
    print(f"   Added analyzers: {workflow.get_analyzer_list()}")
    
    # Process trajectory
    print("\n🏃 Processing molecular trajectory...")
    results = workflow.process_trajectory(trajectory)
    
    # Display results summary
    print(f"\n📊 Analysis Results Summary:")
    print(f"   Processed frames: {results['trajectory_info']['n_frames']}")
    print(f"   Processing time: {results['trajectory_info']['processing_time']:.2f} ms")
    print(f"   Analyzers used: {list(results['analyzers'].keys())}")
    
    # Show some specific results
    if 'frames' in results and results['frames']:
        first_frame = results['frames'][0]
        print(f"\n🔍 First Frame Analysis:")
        
        if 'geometry' in first_frame:
            geom_results = first_frame['geometry']
            if 'radius_of_gyration' in geom_results:
                print(f"   Radius of gyration: {geom_results['radius_of_gyration']:.3f} Å")
            if 'center_of_mass' in geom_results:
                com = geom_results['center_of_mass']
                print(f"   Center of mass: ({com[0]:.2f}, {com[1]:.2f}, {com[2]:.2f}) Å")
        
        if 'rdf' in first_frame:
            rdf_results = first_frame['rdf']
            print(f"   RDF bins: {rdf_results['n_bins']}")
            print(f"   RDF r_max: {rdf_results['r_max']} Å")
            print(f"   Atom pairs analyzed: {rdf_results['n_pairs']}")
    
    # Performance summary
    if 'performance' in results:
        perf = results['performance']
        print(f"\n⚡ Performance Summary:")
        print(f"   Total operations: {perf['total_operations']}")
        print(f"   Operation types: {len(perf['operation_types'])}")
    
    # Export results
    print("\n💾 Exporting results...")
    
    export_formats = ['json', 'csv']
    
    for fmt in export_formats:
        filename = f"analysis_results.{fmt}"
        success = workflow.export_results(filename, fmt)
        if success:
            print(f"   ✓ Exported to {filename}")
            
            # Check file size
            if os.path.exists(filename):
                size = os.path.getsize(filename)
                print(f"     File size: {size} bytes")
        else:
            print(f"   ✗ Failed to export to {filename}")


def main():
    """Main demonstration function."""
    print("VIAMD Phase 5: Advanced Analysis and Visualization Demo")
    print("=" * 60)
    print("This example demonstrates the advanced analysis capabilities")
    print("introduced in VIAMD Phase 5 implementation.")
    print()
    
    try:
        # Test basic C++ bindings availability
        print("🧪 Testing VIAMD analysis bindings...")
        
        # Test statistical analyzer
        stats = pyviamd.analysis.StatisticalAnalyzer()
        test_data = np.array([1.0, 2.0, 3.0, 4.0, 5.0])
        result = stats.calculate_statistics(test_data)
        print(f"   ✓ Statistical analyzer working (mean: {result.mean})")
        
        # Test geometry analyzer
        geom = pyviamd.analysis.GeometryAnalyzer()
        coords = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]])
        dist_matrix = geom.distance_matrix(coords)
        print(f"   ✓ Geometry analyzer working (matrix shape: {dist_matrix.shape})")
        
        # Test real-time processor
        processor = pyviamd.analysis.RealTimeProcessor()
        print(f"   ✓ Real-time processor available")
        
        # Test performance monitor
        monitor = pyviamd.analysis.PerformanceMonitor()
        print(f"   ✓ Performance monitor available")
        
        print("\n🎉 All Phase 5 analysis bindings loaded successfully!")
        
        # Run demonstrations
        demonstrate_statistical_analysis()
        demonstrate_geometry_analysis()
        demonstrate_real_time_processing()
        demonstrate_performance_monitoring()
        demonstrate_analysis_workflow()
        
        print("\n" + "="*60)
        print("✅ PHASE 5 ANALYSIS DEMONSTRATION COMPLETED SUCCESSFULLY")
        print("="*60)
        print()
        print("Phase 5 provides comprehensive molecular analysis capabilities:")
        print("• Advanced statistical analysis with rolling windows and correlations")
        print("• Geometric property calculations (angles, dihedrals, radius of gyration)")
        print("• Real-time data processing with customizable callbacks")
        print("• Performance monitoring and profiling tools")
        print("• Comprehensive analysis workflows with multiple analyzers")
        print("• Data export in multiple formats (JSON, CSV, HDF5)")
        print()
        print("These tools enable sophisticated molecular analysis workflows")
        print("with seamless integration into Python data science ecosystems.")
        
    except Exception as e:
        print(f"\n❌ Error during demonstration: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())