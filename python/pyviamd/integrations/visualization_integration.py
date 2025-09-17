"""
Advanced Visualization Integration for VIAMD

This module provides high-level Python APIs for advanced molecular visualization
and real-time plotting capabilities, building upon VIAMD's efficient rendering core.

Classes:
    VisualizationManager: Advanced visualization coordination and management
    RealTimePlotter: Real-time molecular data plotting and monitoring
    ColorSchemeManager: Molecular color scheme and mapping utilities
    ExportManager: Data export utilities for external visualization tools
    PlotGenerator: Automated plot generation and customization

Example Usage:
    >>> import pyviamd
    >>> from pyviamd.integrations import visualization_integration
    >>> 
    >>> # Create visualization manager
    >>> viz_mgr = visualization_integration.VisualizationManager()
    >>> viz_mgr.setup_real_time_plotting(['energy', 'temperature', 'rdf'])
    >>> 
    >>> # Process and visualize molecular data
    >>> viz_mgr.update_plots(frame_data)
    >>> viz_mgr.export_visualization("output.png")
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
    from matplotlib.colors import ListedColormap
    HAS_MATPLOTLIB = True
except ImportError:
    logger.warning("Matplotlib not available. Install with: pip install matplotlib")
    HAS_MATPLOTLIB = False

try:
    import plotly.graph_objects as go
    import plotly.express as px
    from plotly.subplots import make_subplots
    HAS_PLOTLY = True
except ImportError:
    logger.warning("Plotly not available. Install with: pip install plotly")
    HAS_PLOTLY = False

try:
    import seaborn as sns
    HAS_SEABORN = True
except ImportError:
    logger.warning("Seaborn not available. Install with: pip install seaborn")
    HAS_SEABORN = False


class VisualizationManager:
    """
    Advanced visualization coordination and management.
    
    This class provides a comprehensive interface for managing molecular
    visualizations, real-time plotting, and data export for external
    visualization tools.
    """
    
    def __init__(self):
        if not HAS_PYVIAMD:
            raise ImportError("pyviamd module not available")
        
        self.real_time_plotter = pyviamd.visualization.RealTimePlotter()
        self.color_mapper = pyviamd.visualization.ColorMapper()
        self.data_exporter = pyviamd.visualization.DataExporter()
        self.mesh_generator = pyviamd.visualization.MeshGenerator()
        
        self.plot_configs = {}
        self.active_plots = {}
        self.export_settings = {
            'dpi': 300,
            'format': 'png',
            'transparent': False
        }
        
        # Color schemes
        self.color_schemes = {
            'cpk': 'element_based',
            'viridis': 'viridis',
            'plasma': 'plasma',
            'hot': 'hot',
            'cool': 'cool'
        }
        
        # Plot styles
        if HAS_SEABORN:
            sns.set_style("whitegrid")
    
    def setup_real_time_plotting(self, plot_names: List[str], **kwargs) -> None:
        """
        Setup real-time plotting for specified data series.
        
        Args:
            plot_names: List of plot series names to monitor
            **kwargs: Plot configuration options
        """
        if not HAS_MATPLOTLIB:
            logger.warning("Matplotlib not available for real-time plotting")
            return
        
        self.real_time_plotter.set_max_points(kwargs.get('max_points', 1000))
        
        for plot_name in plot_names:
            self.plot_configs[plot_name] = {
                'title': kwargs.get('title', plot_name.replace('_', ' ').title()),
                'xlabel': kwargs.get('xlabel', 'Time'),
                'ylabel': kwargs.get('ylabel', 'Value'),
                'color': kwargs.get('color', 'blue'),
                'style': kwargs.get('style', '-'),
                'update_interval': kwargs.get('update_interval', 100)  # ms
            }
            
            # Register plot callback
            self.real_time_plotter.register_plot_callback(
                plot_name, self._create_plot_callback(plot_name)
            )
        
        logger.info(f"Setup real-time plotting for: {plot_names}")
    
    def _create_plot_callback(self, plot_name: str) -> Callable:
        """Create a plot update callback for a specific series."""
        def update_plot(data):
            if not HAS_MATPLOTLIB:
                return
            
            config = self.plot_configs.get(plot_name, {})
            
            # Create or update plot
            if plot_name not in self.active_plots:
                fig, ax = plt.subplots(figsize=(10, 6))
                line, = ax.plot([], [], 
                              color=config.get('color', 'blue'),
                              linestyle=config.get('style', '-'),
                              linewidth=2)
                ax.set_title(config.get('title', plot_name))
                ax.set_xlabel(config.get('xlabel', 'Time'))
                ax.set_ylabel(config.get('ylabel', 'Value'))
                ax.grid(True, alpha=0.3)
                
                self.active_plots[plot_name] = {
                    'fig': fig,
                    'ax': ax,
                    'line': line
                }
            
            # Update plot data
            plot_info = self.active_plots[plot_name]
            x_data = list(range(len(data)))
            y_data = list(data)
            
            plot_info['line'].set_data(x_data, y_data)
            
            # Auto-scale axes
            if x_data and y_data:
                plot_info['ax'].set_xlim(0, max(x_data))
                plot_info['ax'].set_ylim(min(y_data) * 0.95, max(y_data) * 1.05)
            
            plot_info['fig'].canvas.draw()
            plot_info['fig'].canvas.flush_events()
        
        return update_plot
    
    def update_plots(self, data: Dict[str, float]) -> None:
        """
        Update real-time plots with new data.
        
        Args:
            data: Dictionary mapping plot names to values
        """
        for plot_name, value in data.items():
            if isinstance(value, (int, float)):
                self.real_time_plotter.add_data_point(plot_name, value)
    
    def create_molecular_visualization(self, coordinates: np.ndarray,
                                     atom_names: List[str],
                                     bonds: Optional[List[Tuple[int, int]]] = None,
                                     **kwargs) -> Optional[Dict[str, Any]]:
        """
        Create 3D molecular visualization.
        
        Args:
            coordinates: Atomic coordinates (Nx3)
            atom_names: List of atom element symbols
            bonds: Optional list of bond pairs
            **kwargs: Visualization options
            
        Returns:
            Visualization data dictionary or None if plotting unavailable
        """
        if not HAS_PLOTLY:
            logger.warning("Plotly not available for 3D molecular visualization")
            return None
        
        # Get atom colors
        atom_colors = self.color_mapper.element_colors(atom_names)
        
        # Create 3D scatter plot for atoms
        fig = go.Figure()
        
        # Add atoms
        fig.add_trace(go.Scatter3d(
            x=coordinates[:, 0],
            y=coordinates[:, 1],
            z=coordinates[:, 2],
            mode='markers',
            marker=dict(
                size=kwargs.get('atom_size', 8),
                color=[f'rgb({r*255},{g*255},{b*255})' for r, g, b in atom_colors],
                opacity=kwargs.get('opacity', 0.8)
            ),
            text=atom_names,
            name='Atoms'
        ))
        
        # Add bonds if provided
        if bonds:
            for bond in bonds:
                i, j = bond
                if i < len(coordinates) and j < len(coordinates):
                    fig.add_trace(go.Scatter3d(
                        x=[coordinates[i, 0], coordinates[j, 0]],
                        y=[coordinates[i, 1], coordinates[j, 1]],
                        z=[coordinates[i, 2], coordinates[j, 2]],
                        mode='lines',
                        line=dict(
                            color='gray',
                            width=kwargs.get('bond_width', 4)
                        ),
                        showlegend=False
                    ))
        
        # Update layout
        fig.update_layout(
            title=kwargs.get('title', 'Molecular Structure'),
            scene=dict(
                xaxis_title='X (Å)',
                yaxis_title='Y (Å)',
                zaxis_title='Z (Å)',
                aspectmode='cube'
            ),
            width=kwargs.get('width', 800),
            height=kwargs.get('height', 600)
        )
        
        visualization_data = {
            'figure': fig,
            'type': '3d_molecular',
            'n_atoms': len(coordinates),
            'n_bonds': len(bonds) if bonds else 0
        }
        
        return visualization_data
    
    def create_property_plot(self, data: Dict[str, np.ndarray],
                           plot_type: str = 'line',
                           **kwargs) -> Optional[Dict[str, Any]]:
        """
        Create property plots (RDF, energy, etc.).
        
        Args:
            data: Dictionary mapping series names to data arrays
            plot_type: Type of plot ('line', 'scatter', 'histogram', 'heatmap')
            **kwargs: Plot configuration options
            
        Returns:
            Plot data dictionary or None if plotting unavailable
        """
        if not HAS_MATPLOTLIB:
            logger.warning("Matplotlib not available for property plotting")
            return None
        
        if plot_type == 'line':
            return self._create_line_plot(data, **kwargs)
        elif plot_type == 'scatter':
            return self._create_scatter_plot(data, **kwargs)
        elif plot_type == 'histogram':
            return self._create_histogram(data, **kwargs)
        elif plot_type == 'heatmap':
            return self._create_heatmap(data, **kwargs)
        else:
            logger.error(f"Unknown plot type: {plot_type}")
            return None
    
    def _create_line_plot(self, data: Dict[str, np.ndarray], **kwargs) -> Dict[str, Any]:
        """Create line plot."""
        fig, ax = plt.subplots(figsize=kwargs.get('figsize', (10, 6)))
        
        colors = plt.cm.tab10(np.linspace(0, 1, len(data)))
        
        for i, (name, values) in enumerate(data.items()):
            x = kwargs.get('x_data', np.arange(len(values)))
            ax.plot(x, values, 
                   label=name, 
                   color=colors[i],
                   linewidth=kwargs.get('linewidth', 2))
        
        ax.set_title(kwargs.get('title', 'Property Plot'))
        ax.set_xlabel(kwargs.get('xlabel', 'X'))
        ax.set_ylabel(kwargs.get('ylabel', 'Y'))
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        return {
            'figure': fig,
            'type': 'line_plot',
            'series_count': len(data)
        }
    
    def _create_scatter_plot(self, data: Dict[str, np.ndarray], **kwargs) -> Dict[str, Any]:
        """Create scatter plot."""
        if len(data) < 2:
            logger.error("Scatter plot requires at least 2 data series")
            return {}
        
        fig, ax = plt.subplots(figsize=kwargs.get('figsize', (8, 8)))
        
        series_names = list(data.keys())
        x_data = data[series_names[0]]
        y_data = data[series_names[1]]
        
        ax.scatter(x_data, y_data, 
                  alpha=kwargs.get('alpha', 0.6),
                  s=kwargs.get('markersize', 50),
                  c=kwargs.get('color', 'blue'))
        
        ax.set_title(kwargs.get('title', f'{series_names[1]} vs {series_names[0]}'))
        ax.set_xlabel(kwargs.get('xlabel', series_names[0]))
        ax.set_ylabel(kwargs.get('ylabel', series_names[1]))
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        return {
            'figure': fig,
            'type': 'scatter_plot',
            'x_series': series_names[0],
            'y_series': series_names[1]
        }
    
    def _create_histogram(self, data: Dict[str, np.ndarray], **kwargs) -> Dict[str, Any]:
        """Create histogram plot."""
        fig, ax = plt.subplots(figsize=kwargs.get('figsize', (10, 6)))
        
        for name, values in data.items():
            ax.hist(values, 
                   bins=kwargs.get('bins', 50),
                   alpha=kwargs.get('alpha', 0.7),
                   label=name,
                   density=kwargs.get('density', False))
        
        ax.set_title(kwargs.get('title', 'Histogram'))
        ax.set_xlabel(kwargs.get('xlabel', 'Value'))
        ax.set_ylabel(kwargs.get('ylabel', 'Frequency'))
        ax.legend()
        ax.grid(True, alpha=0.3)
        
        plt.tight_layout()
        
        return {
            'figure': fig,
            'type': 'histogram',
            'series_count': len(data)
        }
    
    def _create_heatmap(self, data: Dict[str, np.ndarray], **kwargs) -> Dict[str, Any]:
        """Create heatmap."""
        if not HAS_SEABORN:
            logger.warning("Seaborn not available for heatmap")
            return {}
        
        # Assume first data series is a 2D array
        matrix_data = next(iter(data.values()))
        if matrix_data.ndim != 2:
            logger.error("Heatmap requires 2D data")
            return {}
        
        fig, ax = plt.subplots(figsize=kwargs.get('figsize', (10, 8)))
        
        sns.heatmap(matrix_data, 
                   ax=ax,
                   cmap=kwargs.get('colormap', 'viridis'),
                   annot=kwargs.get('annotate', False),
                   fmt=kwargs.get('format', '.2f'))
        
        ax.set_title(kwargs.get('title', 'Heatmap'))
        plt.tight_layout()
        
        return {
            'figure': fig,
            'type': 'heatmap',
            'shape': matrix_data.shape
        }
    
    def create_animation(self, trajectory_data: List[np.ndarray],
                        atom_names: List[str],
                        **kwargs) -> Optional[Dict[str, Any]]:
        """
        Create molecular trajectory animation.
        
        Args:
            trajectory_data: List of coordinate arrays for each frame
            atom_names: List of atom element symbols
            **kwargs: Animation options
            
        Returns:
            Animation data dictionary or None if unavailable
        """
        if not HAS_MATPLOTLIB:
            logger.warning("Matplotlib not available for animation")
            return None
        
        if not trajectory_data:
            logger.error("No trajectory data provided")
            return None
        
        # Get atom colors
        atom_colors = self.color_mapper.element_colors(atom_names)
        colors = [f'#{int(r*255):02x}{int(g*255):02x}{int(b*255):02x}' 
                 for r, g, b in atom_colors]
        
        # Setup figure
        fig = plt.figure(figsize=kwargs.get('figsize', (10, 8)))
        ax = fig.add_subplot(111, projection='3d')
        
        # Initialize scatter plot
        coordinates = trajectory_data[0]
        scat = ax.scatter(coordinates[:, 0], coordinates[:, 1], coordinates[:, 2],
                         c=colors, s=kwargs.get('atom_size', 100))
        
        ax.set_xlabel('X (Å)')
        ax.set_ylabel('Y (Å)')
        ax.set_zlabel('Z (Å)')
        ax.set_title(kwargs.get('title', 'Molecular Dynamics Trajectory'))
        
        # Set consistent axis limits
        all_coords = np.vstack(trajectory_data)
        margin = kwargs.get('margin', 2.0)
        ax.set_xlim(all_coords[:, 0].min() - margin, all_coords[:, 0].max() + margin)
        ax.set_ylim(all_coords[:, 1].min() - margin, all_coords[:, 1].max() + margin)
        ax.set_zlim(all_coords[:, 2].min() - margin, all_coords[:, 2].max() + margin)
        
        def update(frame):
            coordinates = trajectory_data[frame]
            scat._offsets3d = (coordinates[:, 0], coordinates[:, 1], coordinates[:, 2])
            ax.set_title(f'{kwargs.get("title", "Molecular Dynamics Trajectory")} - Frame {frame}')
            return scat,
        
        ani = animation.FuncAnimation(fig, update, frames=len(trajectory_data),
                                    interval=kwargs.get('interval', 100),
                                    blit=False, repeat=kwargs.get('repeat', True))
        
        return {
            'animation': ani,
            'figure': fig,
            'type': 'trajectory_animation',
            'n_frames': len(trajectory_data),
            'n_atoms': len(atom_names)
        }
    
    def export_visualization(self, viz_data: Dict[str, Any], filename: str, **kwargs) -> bool:
        """
        Export visualization to file.
        
        Args:
            viz_data: Visualization data from create_* methods
            filename: Output filename
            **kwargs: Export options
            
        Returns:
            True if export successful
        """
        if 'figure' not in viz_data:
            logger.error("No figure found in visualization data")
            return False
        
        try:
            figure = viz_data['figure']
            
            # Handle different figure types
            if hasattr(figure, 'savefig'):  # Matplotlib figure
                figure.savefig(filename, 
                              dpi=kwargs.get('dpi', self.export_settings['dpi']),
                              transparent=kwargs.get('transparent', self.export_settings['transparent']),
                              bbox_inches='tight')
            elif hasattr(figure, 'write_image'):  # Plotly figure
                figure.write_image(filename)
            else:
                logger.error("Unknown figure type for export")
                return False
            
            logger.info(f"Visualization exported to {filename}")
            return True
            
        except Exception as e:
            logger.error(f"Export failed: {e}")
            return False
    
    def export_animation(self, animation_data: Dict[str, Any], filename: str, **kwargs) -> bool:
        """
        Export animation to file.
        
        Args:
            animation_data: Animation data from create_animation
            filename: Output filename (.gif or .mp4)
            **kwargs: Export options
            
        Returns:
            True if export successful
        """
        if 'animation' not in animation_data:
            logger.error("No animation found in animation data")
            return False
        
        try:
            ani = animation_data['animation']
            
            if filename.endswith('.gif'):
                ani.save(filename, writer='pillow', 
                        fps=kwargs.get('fps', 10),
                        dpi=kwargs.get('dpi', 100))
            elif filename.endswith('.mp4'):
                ani.save(filename, writer='ffmpeg',
                        fps=kwargs.get('fps', 10),
                        bitrate=kwargs.get('bitrate', 1800))
            else:
                logger.error("Unsupported animation format. Use .gif or .mp4")
                return False
            
            logger.info(f"Animation exported to {filename}")
            return True
            
        except Exception as e:
            logger.error(f"Animation export failed: {e}")
            return False
    
    def export_data(self, data: Dict[str, Any], filename: str, format: str = 'auto') -> bool:
        """
        Export molecular data for external visualization tools.
        
        Args:
            data: Data dictionary containing coordinates, atom names, etc.
            filename: Output filename
            format: Export format ('xyz', 'pdb', 'json', 'csv', 'auto')
            
        Returns:
            True if export successful
        """
        if 'coordinates' not in data or 'atom_names' not in data:
            logger.error("Data must contain 'coordinates' and 'atom_names'")
            return False
        
        coordinates = data['coordinates']
        atom_names = data['atom_names']
        
        # Auto-detect format
        if format == 'auto':
            if filename.endswith('.xyz'):
                format = 'xyz'
            elif filename.endswith('.pdb'):
                format = 'pdb'
            elif filename.endswith('.json'):
                format = 'json'
            elif filename.endswith('.csv'):
                format = 'csv'
            else:
                format = 'xyz'  # Default
        
        try:
            if format in ['xyz', 'pdb']:
                return self.data_exporter.export_coordinates(
                    coordinates, atom_names, filename, format
                )
            elif format == 'json':
                if 'trajectory' in data:
                    return self.data_exporter.export_trajectory_json(
                        data['trajectory'], atom_names, filename
                    )
                else:
                    # Export single frame as trajectory
                    return self.data_exporter.export_trajectory_json(
                        [coordinates], atom_names, filename
                    )
            elif format == 'csv':
                csv_data = self.data_exporter.prepare_matplotlib_data(data)
                return self.data_exporter.export_analysis_csv(csv_data, filename)
            else:
                logger.error(f"Unsupported format: {format}")
                return False
                
        except Exception as e:
            logger.error(f"Data export failed: {e}")
            return False
    
    def clear_plots(self) -> None:
        """Clear all active plots and reset plotter."""
        self.real_time_plotter.clear_all()
        
        # Close matplotlib figures
        for plot_name, plot_info in self.active_plots.items():
            if 'fig' in plot_info:
                plt.close(plot_info['fig'])
        
        self.active_plots.clear()
        logger.info("All plots cleared")
    
    def get_plot_data(self) -> Dict[str, Any]:
        """Get current real-time plot data."""
        return self.real_time_plotter.get_all_data()
    
    def set_export_settings(self, **kwargs) -> None:
        """Update export settings."""
        self.export_settings.update(kwargs)
        logger.info(f"Export settings updated: {kwargs}")


class RealTimePlotter:
    """
    Specialized real-time molecular data plotter.
    
    This class provides focused real-time plotting capabilities
    for molecular dynamics simulations with optimized performance.
    """
    
    def __init__(self, max_points: int = 1000):
        if not HAS_PYVIAMD:
            raise ImportError("pyviamd module not available")
        
        self.plotter = pyviamd.visualization.RealTimePlotter(max_points)
        self.plot_windows = {}
        self.update_callbacks = {}
        
    def create_plot_window(self, name: str, title: str = None, **kwargs) -> bool:
        """Create a new plot window."""
        if not HAS_MATPLOTLIB:
            return False
        
        fig, ax = plt.subplots(figsize=kwargs.get('figsize', (8, 6)))
        
        plot_config = {
            'figure': fig,
            'axis': ax,
            'line': None,
            'title': title or name,
            'xlabel': kwargs.get('xlabel', 'Time'),
            'ylabel': kwargs.get('ylabel', 'Value'),
            'color': kwargs.get('color', 'blue')
        }
        
        ax.set_title(plot_config['title'])
        ax.set_xlabel(plot_config['xlabel'])
        ax.set_ylabel(plot_config['ylabel'])
        ax.grid(True, alpha=0.3)
        
        self.plot_windows[name] = plot_config
        
        # Register callback with C++ plotter
        def update_callback(data):
            self._update_plot_window(name, data)
        
        self.plotter.register_plot_callback(name, update_callback)
        self.update_callbacks[name] = update_callback
        
        return True
    
    def _update_plot_window(self, name: str, data: List[float]) -> None:
        """Update a specific plot window."""
        if name not in self.plot_windows:
            return
        
        plot_config = self.plot_windows[name]
        ax = plot_config['axis']
        
        x_data = list(range(len(data)))
        y_data = list(data)
        
        # Create or update line
        if plot_config['line'] is None:
            plot_config['line'], = ax.plot(x_data, y_data, 
                                          color=plot_config['color'],
                                          linewidth=2)
        else:
            plot_config['line'].set_data(x_data, y_data)
        
        # Auto-scale
        if x_data and y_data:
            ax.set_xlim(0, max(x_data))
            y_min, y_max = min(y_data), max(y_data)
            y_range = y_max - y_min
            if y_range > 0:
                margin = y_range * 0.1
                ax.set_ylim(y_min - margin, y_max + margin)
        
        plot_config['figure'].canvas.draw()
        plot_config['figure'].canvas.flush_events()
    
    def add_data(self, name: str, value: float) -> None:
        """Add data point to a plot series."""
        self.plotter.add_data_point(name, value)
    
    def get_data(self, name: str) -> List[float]:
        """Get current data for a plot series."""
        return self.plotter.get_series_data(name)
    
    def clear_series(self, name: str) -> None:
        """Clear data for a specific series."""
        self.plotter.clear_series(name)
        
        if name in self.plot_windows:
            # Clear the plot
            plot_config = self.plot_windows[name]
            if plot_config['line']:
                plot_config['line'].set_data([], [])
                plot_config['figure'].canvas.draw()
    
    def close_plot_window(self, name: str) -> None:
        """Close a plot window."""
        if name in self.plot_windows:
            plt.close(self.plot_windows[name]['figure'])
            del self.plot_windows[name]
        
        if name in self.update_callbacks:
            del self.update_callbacks[name]
        
        self.plotter.clear_series(name)


# Export main classes
__all__ = ['VisualizationManager', 'RealTimePlotter']