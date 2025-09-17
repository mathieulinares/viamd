#!/usr/bin/env python3
"""
Comprehensive Test Suite for VIAMD Phase 5

This test suite validates the advanced analysis and visualization bindings
implemented in VIAMD Phase 5, including statistical analysis, geometry
calculations, visualization tools, and machine learning integration.

Test Categories:
- Analysis bindings (statistical analysis, geometry calculations)
- Visualization bindings (data export, color mapping, mesh generation)
- Machine learning bindings (feature extraction, preprocessing, clustering)
- Integration workflows (high-level API functionality)
- Performance and reliability tests

Usage:
    python test_phase5_comprehensive.py
"""

import sys
import os
import numpy as np
import time
import tempfile
import logging

# Add parent directory to path for imports
sys.path.insert(0, os.path.dirname(os.path.dirname(__file__)))

try:
    import pyviamd
    from pyviamd.integrations import analysis_integration, visualization_integration, ml_integration
    print("✓ All VIAMD Phase 5 modules loaded successfully")
except ImportError as e:
    print(f"✗ Failed to import VIAMD Phase 5 modules: {e}")
    print("Please build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    sys.exit(1)

# Configure logging
logging.basicConfig(level=logging.WARNING)  # Reduce log noise during tests


class Phase5TestSuite:
    """Comprehensive test suite for VIAMD Phase 5 functionality."""
    
    def __init__(self):
        self.tests_passed = 0
        self.tests_failed = 0
        self.test_results = []
        
    def run_test(self, test_name, test_func):
        """Run a single test and record results."""
        print(f"🧪 Testing {test_name}...")
        
        try:
            start_time = time.time()
            result = test_func()
            end_time = time.time()
            
            if result:
                print(f"   ✅ PASSED ({end_time - start_time:.3f}s)")
                self.tests_passed += 1
                self.test_results.append((test_name, "PASSED", end_time - start_time))
            else:
                print(f"   ❌ FAILED")
                self.tests_failed += 1
                self.test_results.append((test_name, "FAILED", end_time - start_time))
                
        except Exception as e:
            print(f"   ❌ ERROR: {e}")
            self.tests_failed += 1
            self.test_results.append((test_name, f"ERROR: {e}", 0))
    
    def test_statistical_analyzer(self):
        """Test statistical analysis functionality."""
        stats = pyviamd.analysis.StatisticalAnalyzer()
        
        # Test basic statistics
        test_data = np.array([1.0, 2.0, 3.0, 4.0, 5.0])
        result = stats.calculate_statistics(test_data)
        
        if not (abs(result.mean - 3.0) < 1e-6):
            return False
        if not (abs(result.min_val - 1.0) < 1e-6):
            return False
        if not (abs(result.max_val - 5.0) < 1e-6):
            return False
        if not (result.count == 5):
            return False
        
        # Test rolling statistics
        rolling_stats = stats.rolling_statistics(test_data, 3)
        if len(rolling_stats) != 3:  # 5 - 3 + 1 = 3 windows
            return False
        
        # Test correlation
        data1 = np.array([1.0, 2.0, 3.0, 4.0, 5.0])
        data2 = np.array([2.0, 4.0, 6.0, 8.0, 10.0])  # Perfect correlation
        corr = stats.correlation(data1, data2)
        if not (abs(corr - 1.0) < 1e-6):
            return False
        
        # Test autocorrelation
        autocorr = stats.autocorrelation(test_data, 3)
        if len(autocorr) != 4:  # 0 to 3 lags
            return False
        if not (abs(autocorr[0] - 1.0) < 1e-6):  # Lag 0 should be 1.0
            return False
        
        return True
    
    def test_geometry_analyzer(self):
        """Test geometry analysis functionality."""
        geom = pyviamd.analysis.GeometryAnalyzer()
        
        # Test distance matrix
        coords = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]])
        dist_matrix = geom.distance_matrix(coords)
        
        if dist_matrix.shape != (3, 3):
            return False
        if not (abs(dist_matrix[0, 1] - 1.0) < 1e-6):  # Distance should be 1.0
            return False
        if not (abs(dist_matrix[0, 2] - 1.0) < 1e-6):  # Distance should be 1.0
            return False
        
        # Test angle calculation
        atom1 = np.array([1.0, 0.0, 0.0])
        atom2 = np.array([0.0, 0.0, 0.0])
        atom3 = np.array([0.0, 1.0, 0.0])
        angle = geom.calculate_angle(atom1, atom2, atom3)
        
        if not (abs(angle - 90.0) < 1e-6):  # Should be 90 degrees
            return False
        
        # Test dihedral calculation
        atom4 = np.array([0.0, 0.0, 1.0])
        dihedral = geom.calculate_dihedral(atom1, atom2, atom3, atom4)
        
        # Should be a valid dihedral angle
        if not (-180.0 <= dihedral <= 180.0):
            return False
        
        # Test radius of gyration
        masses = np.ones(3)
        rg = geom.radius_of_gyration(coords, masses)
        
        if not (rg > 0):  # Should be positive
            return False
        
        return True
    
    def test_real_time_processor(self):
        """Test real-time processing functionality."""
        processor = pyviamd.analysis.RealTimeProcessor()
        
        # Test callback registration and execution
        callback_called = [False]
        processed_data = []
        
        def test_callback(frame_data):
            callback_called[0] = True
            processed_data.append(frame_data.get('frame', -1))
        
        processor.add_callback("test_callback", test_callback)
        
        # Process test frame
        test_frame = {'frame': 42, 'data': 'test'}
        processor.process_frame(test_frame)
        
        if not callback_called[0]:
            return False
        if len(processed_data) != 1 or processed_data[0] != 42:
            return False
        
        # Test buffer management
        buffered = processor.get_buffered_data()
        if 'frame' not in buffered or buffered['frame'] != 42:
            return False
        
        buffer_keys = processor.get_buffer_keys()
        if 'frame' not in buffer_keys or 'data' not in buffer_keys:
            return False
        
        # Test buffer clearing
        processor.clear_buffers()
        cleared_keys = processor.get_buffer_keys()
        if len(cleared_keys) != 0:
            return False
        
        return True
    
    def test_performance_monitor(self):
        """Test performance monitoring functionality."""
        monitor = pyviamd.analysis.PerformanceMonitor()
        
        # Test timer functionality
        monitor.start_timer("test_operation")
        time.sleep(0.01)  # Small delay
        duration = monitor.stop_timer("test_operation")
        
        if not (duration > 5.0):  # Should be at least 5ms
            return False
        
        # Test multiple measurements
        for i in range(5):
            monitor.start_timer("repeated_op")
            time.sleep(0.001)
            monitor.stop_timer("repeated_op")
        
        # Get timing statistics
        stats = monitor.get_timing_stats("repeated_op")
        if stats.count != 5:
            return False
        if not (stats.mean > 0):
            return False
        
        # Test getting all timings
        all_timings = monitor.get_all_timings()
        if "test_operation" not in all_timings:
            return False
        if "repeated_op" not in all_timings:
            return False
        
        # Test operation names
        operation_names = monitor.get_operation_names()
        if "test_operation" not in operation_names:
            return False
        
        # Test clearing
        monitor.clear_timings()
        cleared_names = monitor.get_operation_names()
        if len(cleared_names) != 0:
            return False
        
        return True
    
    def test_data_exporter(self):
        """Test data export functionality."""
        exporter = pyviamd.visualization.DataExporter()
        
        # Test coordinate export
        coords = np.array([[0, 0, 0], [1, 1, 1], [2, 2, 2]])
        atom_names = ["C", "N", "O"]
        
        with tempfile.NamedTemporaryFile(suffix=".xyz", delete=False) as f:
            xyz_file = f.name
        
        try:
            success = exporter.export_coordinates(coords, atom_names, xyz_file, "xyz")
            if not success:
                return False
            
            # Check file exists and has content
            if not os.path.exists(xyz_file):
                return False
            
            with open(xyz_file, 'r') as f:
                lines = f.readlines()
                if len(lines) < 2:  # Should have at least header + 1 atom
                    return False
                if not lines[0].strip() == "3":  # First line should be atom count
                    return False
        finally:
            if os.path.exists(xyz_file):
                os.unlink(xyz_file)
        
        # Test PDB export
        with tempfile.NamedTemporaryFile(suffix=".pdb", delete=False) as f:
            pdb_file = f.name
        
        try:
            success = exporter.export_coordinates(coords, atom_names, pdb_file, "pdb")
            if not success:
                return False
            
            if not os.path.exists(pdb_file):
                return False
            
            with open(pdb_file, 'r') as f:
                content = f.read()
                if "ATOM" not in content:
                    return False
        finally:
            if os.path.exists(pdb_file):
                os.unlink(pdb_file)
        
        # Test CSV export
        test_data = {
            'x': np.array([1.0, 2.0, 3.0]),
            'y': np.array([4.0, 5.0, 6.0])
        }
        
        with tempfile.NamedTemporaryFile(suffix=".csv", delete=False) as f:
            csv_file = f.name
        
        try:
            success = exporter.export_analysis_csv(test_data, csv_file)
            if not success:
                return False
            
            if not os.path.exists(csv_file):
                return False
        finally:
            if os.path.exists(csv_file):
                os.unlink(csv_file)
        
        return True
    
    def test_color_mapper(self):
        """Test color mapping functionality."""
        mapper = pyviamd.visualization.ColorMapper()
        
        # Test element colors
        elements = ["H", "C", "N", "O"]
        colors = mapper.element_colors(elements)
        
        if colors.shape != (4, 3):
            return False
        
        # Check color values are in valid range
        if not np.all((colors >= 0) & (colors <= 1)):
            return False
        
        # Test scalar to RGB mapping
        scalar_values = np.array([0.0, 0.5, 1.0])
        rgb_colors = mapper.scalar_to_rgb(scalar_values, "viridis", 0.0, 1.0)
        
        if rgb_colors.shape != (3, 3):
            return False
        
        # Check RGB values are in valid range
        if not np.all((rgb_colors >= 0) & (rgb_colors <= 1)):
            return False
        
        # Test different colormaps
        for colormap in ["plasma", "hot", "cool"]:
            rgb_colors = mapper.scalar_to_rgb(scalar_values, colormap, 0.0, 1.0)
            if rgb_colors.shape != (3, 3):
                return False
        
        return True
    
    def test_mesh_generator(self):
        """Test mesh generation functionality."""
        mesh_gen = pyviamd.visualization.MeshGenerator()
        
        # Test sphere mesh generation
        sphere = mesh_gen.generate_sphere_mesh(radius=1.0, resolution=8)
        
        required_keys = ['vertices', 'faces', 'n_vertices', 'n_faces']
        for key in required_keys:
            if key not in sphere:
                return False
        
        vertices = sphere['vertices']
        faces = sphere['faces']
        
        if len(vertices) != sphere['n_vertices'] * 3:  # 3 coords per vertex
            return False
        if len(faces) != sphere['n_faces'] * 3:  # 3 vertices per face
            return False
        
        # Test cylinder mesh generation
        start_pos = np.array([0.0, 0.0, 0.0])
        end_pos = np.array([1.0, 0.0, 0.0])
        
        cylinder = mesh_gen.generate_cylinder_mesh(start_pos, end_pos, radius=0.5, resolution=6)
        
        for key in required_keys:
            if key not in cylinder:
                return False
        
        # Check that cylinder has reasonable number of vertices
        if cylinder['n_vertices'] < 6:  # Should have at least resolution vertices
            return False
        
        return True
    
    def test_real_time_plotter(self):
        """Test real-time plotting functionality."""
        plotter = pyviamd.visualization.RealTimePlotter(max_points=10)
        
        # Test data addition
        test_data = [1.0, 2.0, 3.0, 4.0, 5.0]
        for value in test_data:
            plotter.add_data_point("test_series", value)
        
        # Get data back
        retrieved_data = plotter.get_series_data("test_series")
        if len(retrieved_data) != len(test_data):
            return False
        
        # Check values match
        for i, value in enumerate(test_data):
            if abs(retrieved_data[i] - value) > 1e-6:
                return False
        
        # Test series management
        series_names = plotter.get_series_names()
        if "test_series" not in series_names:
            return False
        
        # Test clearing
        plotter.clear_series("test_series")
        cleared_data = plotter.get_series_data("test_series")
        if len(cleared_data) != 0:
            return False
        
        # Test max points limit
        for i in range(15):  # Add more than max_points
            plotter.add_data_point("limited_series", float(i))
        
        limited_data = plotter.get_series_data("limited_series")
        if len(limited_data) > 10:  # Should be capped at max_points
            return False
        
        return True
    
    def test_feature_extractor(self):
        """Test machine learning feature extraction."""
        extractor = pyviamd.ml.FeatureExtractor()
        
        # Create simple test molecule
        coords = np.array([
            [0, 0, 0],
            [1, 0, 0],
            [0, 1, 0],
            [1, 1, 0],
            [0.5, 0.5, 1]
        ])
        
        # Test RDF features
        rdf_features = extractor.extract_rdf_features(coords, r_max=5.0, n_bins=20)
        if rdf_features.shape != (20,):
            return False
        if not np.all(rdf_features >= 0):  # RDF should be non-negative
            return False
        
        # Test angular features
        angular_features = extractor.extract_angular_features(coords, n_bins=90)
        if angular_features.shape != (90,):
            return False
        if not np.all(angular_features >= 0):  # Counts should be non-negative
            return False
        
        # Test contact features
        contact_features = extractor.extract_contact_features(coords, cutoff=2.0)
        if contact_features.shape != (5, 5):
            return False
        if not np.all((contact_features == 0) | (contact_features == 1)):  # Should be binary
            return False
        
        # Test structural descriptors
        descriptors = extractor.extract_structural_descriptors(coords)
        required_keys = ['center_of_mass', 'radius_of_gyration', 'n_atoms']
        for key in required_keys:
            if key not in descriptors:
                return False
        
        if descriptors['n_atoms'] != 5:
            return False
        
        return True
    
    def test_data_preprocessor(self):
        """Test data preprocessing functionality."""
        preprocessor = pyviamd.ml.DataPreprocessor()
        
        # Create test data with known statistics
        test_data = np.array([
            [1.0, 10.0],
            [2.0, 20.0],
            [3.0, 30.0],
            [4.0, 40.0],
            [5.0, 50.0]
        ])
        
        # Test standardization
        std_result = preprocessor.standardize(test_data)
        std_data = std_result['data']
        
        # Check that standardized data has zero mean and unit variance
        means = np.mean(std_data, axis=0)
        stds = np.std(std_data, axis=0)
        
        if not np.allclose(means, 0.0, atol=1e-10):
            return False
        if not np.allclose(stds, 1.0, atol=1e-10):
            return False
        
        # Test normalization
        norm_result = preprocessor.normalize_minmax(test_data)
        norm_data = norm_result['data']
        
        # Check that normalized data is in [0, 1] range
        if not np.all((norm_data >= 0) & (norm_data <= 1)):
            return False
        
        # Check min and max values
        mins = np.min(norm_data, axis=0)
        maxs = np.max(norm_data, axis=0)
        
        if not np.allclose(mins, 0.0):
            return False
        if not np.allclose(maxs, 1.0):
            return False
        
        return True
    
    def test_dimensionality_reducer(self):
        """Test dimensionality reduction functionality."""
        reducer = pyviamd.ml.DimensionalityReducer()
        
        # Create test data
        np.random.seed(42)
        test_data = np.random.normal(0, 1, (50, 10))
        
        # Test PCA
        pca_result = reducer.pca(test_data, n_components=3)
        
        required_keys = ['centered_data', 'mean', 'covariance']
        for key in required_keys:
            if key not in pca_result:
                return False
        
        centered_data = pca_result['centered_data']
        if centered_data.shape != test_data.shape:
            return False
        
        # Test k-means clustering
        kmeans_result = reducer.kmeans(test_data, k=3, max_iters=10)
        
        required_keys = ['labels', 'centroids', 'n_clusters']
        for key in required_keys:
            if key not in kmeans_result:
                return False
        
        labels = kmeans_result['labels']
        centroids = kmeans_result['centroids']
        
        if len(labels) != test_data.shape[0]:
            return False
        if centroids.shape != (3, test_data.shape[1]):
            return False
        
        # Check that all labels are valid cluster IDs
        if not np.all((labels >= 0) & (labels < 3)):
            return False
        
        return True
    
    def test_analysis_workflow(self):
        """Test high-level analysis workflow integration."""
        # Generate small test trajectory
        n_frames, n_atoms = 5, 10
        trajectory = []
        
        for frame in range(n_frames):
            coords = np.random.uniform(-5, 5, (n_atoms, 3))
            trajectory.append(coords)
        
        # Create and configure workflow
        workflow = analysis_integration.AnalysisWorkflow()
        workflow.add_analyzer("test_rdf", "rdf", r_max=8.0, n_bins=20)
        workflow.add_analyzer("test_geom", "geometry", radius_of_gyration=True)
        workflow.add_analyzer("test_stats", "statistics", per_dimension=True)
        
        # Process trajectory
        results = workflow.process_trajectory(trajectory)
        
        # Check results structure
        required_keys = ['trajectory_info', 'analyzers', 'frames', 'statistics', 'performance']
        for key in required_keys:
            if key not in results:
                return False
        
        if results['trajectory_info']['n_frames'] != n_frames:
            return False
        
        if len(results['frames']) != n_frames:
            return False
        
        # Check that analyzers ran
        first_frame = results['frames'][0]
        if 'test_rdf' not in first_frame:
            return False
        if 'test_geom' not in first_frame:
            return False
        if 'test_stats' not in first_frame:
            return False
        
        return True
    
    def test_ml_pipeline(self):
        """Test machine learning pipeline integration."""
        # Generate test conformations
        n_conformations, n_atoms = 10, 8
        conformations = []
        
        for i in range(n_conformations):
            coords = np.random.uniform(-3, 3, (n_atoms, 3))
            conformations.append(coords)
        
        # Create and configure pipeline
        pipeline = ml_integration.MLPipeline()
        pipeline.add_feature_extractor("rdf", "rdf", r_max=5.0, n_bins=10)
        pipeline.add_feature_extractor("struct", "structural")
        pipeline.add_preprocessor("standardize", "standardize")
        
        # Extract and process features
        features = pipeline.extract_features(conformations)
        processed_features = pipeline.preprocess_features(features)
        combined_features = pipeline.combine_features(processed_features)
        
        # Check feature extraction worked
        if 'rdf' not in features:
            return False
        if 'struct' not in features:
            return False
        
        if features['rdf'].shape[0] != n_conformations:
            return False
        
        # Check preprocessing worked
        if combined_features.shape[0] != n_conformations:
            return False
        
        # Test clustering
        clustering_result = pipeline.cluster_data(combined_features, n_clusters=2)
        
        if 'labels' not in clustering_result:
            return False
        if len(clustering_result['labels']) != n_conformations:
            return False
        
        return True
    
    def run_all_tests(self):
        """Run all tests in the test suite."""
        print("🚀 Starting VIAMD Phase 5 Comprehensive Test Suite")
        print("=" * 60)
        
        # Analysis tests
        print("\n📊 Testing Analysis Bindings:")
        self.run_test("Statistical Analyzer", self.test_statistical_analyzer)
        self.run_test("Geometry Analyzer", self.test_geometry_analyzer)
        self.run_test("Real-time Processor", self.test_real_time_processor)
        self.run_test("Performance Monitor", self.test_performance_monitor)
        
        # Visualization tests
        print("\n🎨 Testing Visualization Bindings:")
        self.run_test("Data Exporter", self.test_data_exporter)
        self.run_test("Color Mapper", self.test_color_mapper)
        self.run_test("Mesh Generator", self.test_mesh_generator)
        self.run_test("Real-time Plotter", self.test_real_time_plotter)
        
        # Machine learning tests
        print("\n🤖 Testing Machine Learning Bindings:")
        self.run_test("Feature Extractor", self.test_feature_extractor)
        self.run_test("Data Preprocessor", self.test_data_preprocessor)
        self.run_test("Dimensionality Reducer", self.test_dimensionality_reducer)
        
        # Integration tests
        print("\n🔗 Testing High-level Integrations:")
        self.run_test("Analysis Workflow", self.test_analysis_workflow)
        self.run_test("ML Pipeline", self.test_ml_pipeline)
        
        # Summary
        print("\n" + "=" * 60)
        print("📋 TEST SUMMARY")
        print("=" * 60)
        
        total_tests = self.tests_passed + self.tests_failed
        success_rate = (self.tests_passed / total_tests * 100) if total_tests > 0 else 0
        
        print(f"Total tests: {total_tests}")
        print(f"Passed: {self.tests_passed}")
        print(f"Failed: {self.tests_failed}")
        print(f"Success rate: {success_rate:.1f}%")
        
        if self.tests_failed > 0:
            print(f"\n❌ FAILED TESTS:")
            for test_name, status, duration in self.test_results:
                if status != "PASSED":
                    print(f"   {test_name}: {status}")
        
        print(f"\n⚡ PERFORMANCE SUMMARY:")
        total_time = sum(result[2] for result in self.test_results if isinstance(result[2], (int, float)))
        print(f"Total execution time: {total_time:.3f} seconds")
        
        fastest_test = min(self.test_results, key=lambda x: x[2] if isinstance(x[2], (int, float)) else float('inf'))
        slowest_test = max(self.test_results, key=lambda x: x[2] if isinstance(x[2], (int, float)) else 0)
        
        print(f"Fastest test: {fastest_test[0]} ({fastest_test[2]:.3f}s)")
        print(f"Slowest test: {slowest_test[0]} ({slowest_test[2]:.3f}s)")
        
        if self.tests_failed == 0:
            print(f"\n🎉 ALL TESTS PASSED! Phase 5 implementation is working correctly.")
            return True
        else:
            print(f"\n⚠️  Some tests failed. Please review the implementation.")
            return False


def main():
    """Main test function."""
    print("VIAMD Phase 5: Comprehensive Test Suite")
    print("This test suite validates all Phase 5 functionality")
    print("including analysis, visualization, and ML bindings.")
    print()
    
    # Create and run test suite
    test_suite = Phase5TestSuite()
    success = test_suite.run_all_tests()
    
    if success:
        print("\n✅ Phase 5 implementation validated successfully!")
        return 0
    else:
        print("\n❌ Phase 5 implementation has issues that need attention.")
        return 1


if __name__ == "__main__":
    sys.exit(main())