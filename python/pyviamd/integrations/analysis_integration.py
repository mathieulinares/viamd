"""
Advanced Analysis and Visualization Integration for VIAMD

This module provides high-level Python APIs for advanced molecular analysis
and visualization capabilities, building upon VIAMD's efficient C++ core.

Classes:
    AnalysisWorkflow: Comprehensive molecular analysis orchestration
    VisualizationManager: Advanced visualization and plotting coordination
    MLPipeline: Machine learning workflow management
    DataProcessor: Real-time data processing and analysis
    PerformanceProfiler: System performance monitoring and optimization

Example Usage:
    >>> import pyviamd
    >>> from pyviamd.integrations import analysis_integration
    >>> 
    >>> # Create comprehensive analysis workflow
    >>> workflow = analysis_integration.AnalysisWorkflow()
    >>> workflow.add_analyzer("rdf", r_max=12.0, n_bins=200)
    >>> workflow.add_analyzer("geometry", calculate_angles=True)
    >>> 
    >>> # Process molecular data
    >>> results = workflow.process_trajectory(trajectory_data)
    >>> workflow.export_results("analysis_output.h5")
"""

import numpy as np
import tempfile
import os
from typing import Optional, Dict, List, Tuple, Any, Union, Callable
import logging
import time
import json

# Set up logging
logger = logging.getLogger(__name__)

try:
    import pyviamd
    HAS_PYVIAMD = True
except ImportError:
    logger.warning("pyviamd not available. Build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    HAS_PYVIAMD = False

try:
    import matplotlib.pyplot as plt
    import matplotlib.animation as animation
    HAS_MATPLOTLIB = True
except ImportError:
    logger.warning("Matplotlib not available. Install with: pip install matplotlib")
    HAS_MATPLOTLIB = False

try:
    import h5py
    HAS_HDF5 = True
except ImportError:
    logger.warning("h5py not available. Install with: pip install h5py")
    HAS_HDF5 = False

try:
    import pandas as pd
    HAS_PANDAS = True
except ImportError:
    logger.warning("Pandas not available. Install with: pip install pandas")
    HAS_PANDAS = False


class AnalysisWorkflow:
    """
    Comprehensive molecular analysis workflow orchestration.
    
    This class provides a high-level interface for managing complex
    analysis workflows that combine multiple analysis tools and
    coordinate real-time processing of molecular dynamics data.
    """
    
    def __init__(self):
        if not HAS_PYVIAMD:
            raise ImportError("pyviamd module not available")
        
        self.analyzers = {}
        self.results = {}
        self.performance_monitor = pyviamd.analysis.PerformanceMonitor()
        self.real_time_processor = pyviamd.analysis.RealTimeProcessor()
        self.config = {
            'save_intermediate': True,
            'parallel_processing': True,
            'memory_efficient': True
        }
        
        # Statistical analyzer instance
        self.stats = pyviamd.analysis.StatisticalAnalyzer()
        self.geometry = pyviamd.analysis.GeometryAnalyzer()
        
    def add_analyzer(self, name: str, analyzer_type: str, **kwargs) -> None:
        """
        Add an analyzer to the workflow.
        
        Args:
            name: Unique name for the analyzer
            analyzer_type: Type of analysis ('rdf', 'geometry', 'statistics', 'custom')
            **kwargs: Configuration parameters for the analyzer
        """
        if name in self.analyzers:
            raise ValueError(f"Analyzer '{name}' already exists")
        
        analyzer_config = {
            'type': analyzer_type,
            'config': kwargs,
            'enabled': True
        }
        
        self.analyzers[name] = analyzer_config
        logger.info(f"Added {analyzer_type} analyzer '{name}' with config: {kwargs}")
    
    def remove_analyzer(self, name: str) -> None:
        """Remove an analyzer from the workflow."""
        if name in self.analyzers:
            del self.analyzers[name]
            logger.info(f"Removed analyzer '{name}'")
    
    def configure_analyzer(self, name: str, **kwargs) -> None:
        """Update configuration for an existing analyzer."""
        if name not in self.analyzers:
            raise ValueError(f"Analyzer '{name}' not found")
        
        self.analyzers[name]['config'].update(kwargs)
        logger.info(f"Updated configuration for analyzer '{name}': {kwargs}")
    
    def enable_analyzer(self, name: str, enabled: bool = True) -> None:
        """Enable or disable an analyzer."""
        if name not in self.analyzers:
            raise ValueError(f"Analyzer '{name}' not found")
        
        self.analyzers[name]['enabled'] = enabled
        logger.info(f"{'Enabled' if enabled else 'Disabled'} analyzer '{name}'")
    
    def process_frame(self, coordinates: np.ndarray, frame_id: int = 0, **metadata) -> Dict[str, Any]:
        """
        Process a single molecular frame.
        
        Args:
            coordinates: Atomic coordinates (Nx3 array)
            frame_id: Frame identifier
            **metadata: Additional frame metadata
            
        Returns:
            Dictionary containing analysis results for this frame
        """
        if not HAS_PYVIAMD:
            raise RuntimeError("pyviamd not available")
        
        self.performance_monitor.start_timer(f"frame_{frame_id}")
        
        frame_results = {
            'frame_id': frame_id,
            'timestamp': time.time(),
            'metadata': metadata
        }
        
        # Process with each enabled analyzer
        for name, analyzer in self.analyzers.items():
            if not analyzer['enabled']:
                continue
            
            analyzer_timer = f"{name}_frame_{frame_id}"
            self.performance_monitor.start_timer(analyzer_timer)
            
            try:
                result = self._run_analyzer(name, analyzer, coordinates, frame_id, metadata)
                frame_results[name] = result
            except Exception as e:
                logger.error(f"Error in analyzer '{name}' for frame {frame_id}: {e}")
                frame_results[name] = {'error': str(e)}
            finally:
                self.performance_monitor.stop_timer(analyzer_timer)
        
        # Store results
        if self.config.get('save_intermediate', True):
            if 'frames' not in self.results:
                self.results['frames'] = []
            self.results['frames'].append(frame_results)
        
        # Process with real-time processor
        self.real_time_processor.process_frame(frame_results)
        
        self.performance_monitor.stop_timer(f"frame_{frame_id}")
        
        return frame_results
    
    def process_trajectory(self, trajectory: List[np.ndarray], **kwargs) -> Dict[str, Any]:
        """
        Process an entire molecular trajectory.
        
        Args:
            trajectory: List of coordinate arrays
            **kwargs: Additional processing options
            
        Returns:
            Complete analysis results for the trajectory
        """
        logger.info(f"Processing trajectory with {len(trajectory)} frames")
        
        self.performance_monitor.start_timer("trajectory_processing")
        
        # Initialize results storage
        self.results = {
            'trajectory_info': {
                'n_frames': len(trajectory),
                'processing_time': None,
                'config': self.config.copy()
            },
            'analyzers': {name: config.copy() for name, config in self.analyzers.items()},
            'frames': []
        }
        
        # Process each frame
        for frame_id, coordinates in enumerate(trajectory):
            frame_results = self.process_frame(coordinates, frame_id)
            
            if frame_id % max(1, len(trajectory) // 10) == 0:
                progress = (frame_id + 1) / len(trajectory) * 100
                logger.info(f"Processing progress: {progress:.1f}%")
        
        # Calculate aggregate statistics
        self.results['statistics'] = self._calculate_trajectory_statistics()
        
        # Performance metrics
        total_time = self.performance_monitor.stop_timer("trajectory_processing")
        self.results['trajectory_info']['processing_time'] = total_time
        self.results['performance'] = self._get_performance_summary()
        
        logger.info(f"Trajectory processing completed in {total_time:.2f} ms")
        
        return self.results
    
    def _run_analyzer(self, name: str, analyzer: Dict, coordinates: np.ndarray, 
                     frame_id: int, metadata: Dict) -> Dict[str, Any]:
        """Run a specific analyzer on frame data."""
        analyzer_type = analyzer['type']
        config = analyzer['config']
        
        if analyzer_type == 'rdf':
            return self._run_rdf_analysis(coordinates, config)
        elif analyzer_type == 'geometry':
            return self._run_geometry_analysis(coordinates, config)
        elif analyzer_type == 'statistics':
            return self._run_statistical_analysis(coordinates, config)
        elif analyzer_type == 'custom':
            return self._run_custom_analysis(coordinates, config, frame_id, metadata)
        else:
            raise ValueError(f"Unknown analyzer type: {analyzer_type}")
    
    def _run_rdf_analysis(self, coordinates: np.ndarray, config: Dict) -> Dict[str, Any]:
        """Run radial distribution function analysis."""
        r_max = config.get('r_max', 10.0)
        n_bins = config.get('n_bins', 100)
        
        # Calculate distance matrix
        dist_matrix = self.geometry.distance_matrix(coordinates)
        
        # Extract unique distances (upper triangle)
        n_atoms = coordinates.shape[0]
        distances = []
        for i in range(n_atoms):
            for j in range(i + 1, n_atoms):
                distances.append(dist_matrix[i, j])
        
        # Create histogram
        hist, bins = np.histogram(distances, bins=n_bins, range=(0, r_max))
        bin_centers = (bins[:-1] + bins[1:]) / 2
        
        return {
            'histogram': hist.tolist(),
            'bin_centers': bin_centers.tolist(),
            'r_max': r_max,
            'n_bins': n_bins,
            'n_pairs': len(distances)
        }
    
    def _run_geometry_analysis(self, coordinates: np.ndarray, config: Dict) -> Dict[str, Any]:
        """Run molecular geometry analysis."""
        results = {}
        
        # Basic geometric properties
        if config.get('radius_of_gyration', True):
            # Assume uniform masses for simplicity
            masses = np.ones(coordinates.shape[0])
            rg = self.geometry.radius_of_gyration(coordinates, masses)
            results['radius_of_gyration'] = float(rg)
        
        if config.get('center_of_mass', True):
            com = np.mean(coordinates, axis=0)
            results['center_of_mass'] = com.tolist()
        
        if config.get('calculate_angles', False) and coordinates.shape[0] >= 3:
            # Calculate sample angles
            angles = []
            for i in range(min(10, coordinates.shape[0] - 2)):  # Sample first 10 angles
                angle = self.geometry.calculate_angle(
                    coordinates[i], coordinates[i + 1], coordinates[i + 2]
                )
                angles.append(float(angle))
            results['sample_angles'] = angles
        
        if config.get('calculate_dihedrals', False) and coordinates.shape[0] >= 4:
            # Calculate sample dihedrals
            dihedrals = []
            for i in range(min(10, coordinates.shape[0] - 3)):  # Sample first 10 dihedrals
                dihedral = self.geometry.calculate_dihedral(
                    coordinates[i], coordinates[i + 1], 
                    coordinates[i + 2], coordinates[i + 3]
                )
                dihedrals.append(float(dihedral))
            results['sample_dihedrals'] = dihedrals
        
        return results
    
    def _run_statistical_analysis(self, coordinates: np.ndarray, config: Dict) -> Dict[str, Any]:
        """Run statistical analysis on coordinate data."""
        results = {}
        
        # Flatten coordinates for statistical analysis
        flat_coords = coordinates.flatten()
        stats = self.stats.calculate_statistics(flat_coords)
        
        results['coordinate_statistics'] = {
            'mean': stats.mean,
            'std_dev': stats.std_dev,
            'min_val': stats.min_val,
            'max_val': stats.max_val,
            'median': stats.median,
            'count': stats.count
        }
        
        # Per-dimension statistics
        if config.get('per_dimension', True):
            dim_stats = {}
            for dim, dim_name in enumerate(['x', 'y', 'z']):
                dim_data = coordinates[:, dim]
                dim_stat = self.stats.calculate_statistics(dim_data)
                dim_stats[dim_name] = {
                    'mean': dim_stat.mean,
                    'std_dev': dim_stat.std_dev,
                    'min_val': dim_stat.min_val,
                    'max_val': dim_stat.max_val
                }
            results['dimension_statistics'] = dim_stats
        
        return results
    
    def _run_custom_analysis(self, coordinates: np.ndarray, config: Dict, 
                           frame_id: int, metadata: Dict) -> Dict[str, Any]:
        """Run custom user-defined analysis."""
        callback = config.get('callback')
        if callback and callable(callback):
            return callback(coordinates, frame_id, metadata, config)
        else:
            return {'error': 'No valid callback provided for custom analysis'}
    
    def _calculate_trajectory_statistics(self) -> Dict[str, Any]:
        """Calculate aggregate statistics across the trajectory."""
        if 'frames' not in self.results:
            return {}
        
        stats = {}
        
        # Collect data for each analyzer
        for analyzer_name in self.analyzers.keys():
            analyzer_data = []
            for frame in self.results['frames']:
                if analyzer_name in frame and 'error' not in frame[analyzer_name]:
                    analyzer_data.append(frame[analyzer_name])
            
            if analyzer_data:
                stats[analyzer_name] = self._analyze_time_series(analyzer_data)
        
        return stats
    
    def _analyze_time_series(self, data_series: List[Dict]) -> Dict[str, Any]:
        """Analyze time series data from an analyzer."""
        time_series_stats = {}
        
        # Extract numeric fields and calculate statistics
        if data_series:
            first_item = data_series[0]
            for key, value in first_item.items():
                if isinstance(value, (int, float)):
                    values = [item.get(key, 0) for item in data_series if key in item]
                    if values:
                        values_array = np.array(values)
                        stats = self.stats.calculate_statistics(values_array)
                        time_series_stats[f"{key}_stats"] = {
                            'mean': stats.mean,
                            'std_dev': stats.std_dev,
                            'min_val': stats.min_val,
                            'max_val': stats.max_val,
                            'trend': self._calculate_trend(values_array)
                        }
        
        return time_series_stats
    
    def _calculate_trend(self, values: np.ndarray) -> str:
        """Calculate the trend of a time series."""
        if len(values) < 2:
            return "insufficient_data"
        
        # Simple linear trend calculation
        x = np.arange(len(values))
        slope = np.polyfit(x, values, 1)[0]
        
        if abs(slope) < 1e-6:
            return "stable"
        elif slope > 0:
            return "increasing"
        else:
            return "decreasing"
    
    def _get_performance_summary(self) -> Dict[str, Any]:
        """Get performance monitoring summary."""
        all_timings = self.performance_monitor.get_all_timings()
        
        summary = {
            'total_operations': len(all_timings),
            'operation_types': list(all_timings.keys()),
            'timing_statistics': {}
        }
        
        for operation, timings in all_timings.items():
            if timings:
                timing_array = np.array(timings)
                summary['timing_statistics'][operation] = {
                    'mean_time_ms': float(np.mean(timing_array)),
                    'std_time_ms': float(np.std(timing_array)),
                    'min_time_ms': float(np.min(timing_array)),
                    'max_time_ms': float(np.max(timing_array)),
                    'total_time_ms': float(np.sum(timing_array)),
                    'call_count': len(timings)
                }
        
        return summary
    
    def export_results(self, filename: str, format: str = 'auto') -> bool:
        """
        Export analysis results to file.
        
        Args:
            filename: Output filename
            format: Export format ('json', 'hdf5', 'csv', 'auto')
            
        Returns:
            True if export successful
        """
        if not self.results:
            logger.warning("No results to export")
            return False
        
        # Auto-detect format from extension
        if format == 'auto':
            if filename.endswith('.json'):
                format = 'json'
            elif filename.endswith(('.h5', '.hdf5')):
                format = 'hdf5'
            elif filename.endswith('.csv'):
                format = 'csv'
            else:
                format = 'json'  # Default
        
        try:
            if format == 'json':
                return self._export_json(filename)
            elif format == 'hdf5':
                return self._export_hdf5(filename)
            elif format == 'csv':
                return self._export_csv(filename)
            else:
                logger.error(f"Unsupported export format: {format}")
                return False
        except Exception as e:
            logger.error(f"Export failed: {e}")
            return False
    
    def _export_json(self, filename: str) -> bool:
        """Export results as JSON."""
        with open(filename, 'w') as f:
            json.dump(self.results, f, indent=2, default=str)
        logger.info(f"Results exported to {filename} (JSON format)")
        return True
    
    def _export_hdf5(self, filename: str) -> bool:
        """Export results as HDF5."""
        if not HAS_HDF5:
            logger.error("h5py not available for HDF5 export")
            return False
        
        with h5py.File(filename, 'w') as f:
            self._write_dict_to_hdf5(f, self.results)
        
        logger.info(f"Results exported to {filename} (HDF5 format)")
        return True
    
    def _write_dict_to_hdf5(self, group, data_dict):
        """Recursively write dictionary to HDF5 group."""
        for key, value in data_dict.items():
            if isinstance(value, dict):
                subgroup = group.create_group(key)
                self._write_dict_to_hdf5(subgroup, value)
            elif isinstance(value, (list, np.ndarray)):
                try:
                    group.create_dataset(key, data=value)
                except:
                    # If data can't be stored as dataset, store as string
                    group.attrs[key] = str(value)
            else:
                group.attrs[key] = value if value is not None else 'None'
    
    def _export_csv(self, filename: str) -> bool:
        """Export results as CSV."""
        if not HAS_PANDAS:
            logger.error("pandas not available for CSV export")
            return False
        
        # Flatten frame data for CSV export
        frame_data = []
        for frame in self.results.get('frames', []):
            flattened = self._flatten_dict(frame)
            frame_data.append(flattened)
        
        if frame_data:
            df = pd.DataFrame(frame_data)
            df.to_csv(filename, index=False)
            logger.info(f"Frame data exported to {filename} (CSV format)")
            return True
        else:
            logger.warning("No frame data available for CSV export")
            return False
    
    def _flatten_dict(self, d, parent_key='', sep='_'):
        """Flatten nested dictionary for CSV export."""
        items = []
        for k, v in d.items():
            new_key = f"{parent_key}{sep}{k}" if parent_key else k
            if isinstance(v, dict):
                items.extend(self._flatten_dict(v, new_key, sep=sep).items())
            elif isinstance(v, list) and len(v) > 0 and isinstance(v[0], (int, float)):
                # Store list statistics for CSV
                if len(v) > 1:
                    items.append((f"{new_key}_mean", np.mean(v)))
                    items.append((f"{new_key}_std", np.std(v)))
                    items.append((f"{new_key}_min", np.min(v)))
                    items.append((f"{new_key}_max", np.max(v)))
                else:
                    items.append((new_key, v[0]))
            elif isinstance(v, (int, float, str)):
                items.append((new_key, v))
        return dict(items)
    
    def get_results(self) -> Dict[str, Any]:
        """Get current analysis results."""
        return self.results.copy()
    
    def clear_results(self) -> None:
        """Clear stored analysis results."""
        self.results.clear()
        self.performance_monitor.clear_timings()
        self.real_time_processor.clear_buffers()
        logger.info("Analysis results cleared")
    
    def get_analyzer_list(self) -> List[str]:
        """Get list of configured analyzers."""
        return list(self.analyzers.keys())
    
    def get_performance_summary(self) -> Dict[str, Any]:
        """Get performance monitoring summary."""
        return self._get_performance_summary()


# Export main classes
__all__ = ['AnalysisWorkflow']