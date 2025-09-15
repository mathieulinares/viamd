"""
Machine Learning Integration for VIAMD

This module provides high-level Python APIs for machine learning workflows
with molecular dynamics data, building upon VIAMD's efficient feature extraction.

Classes:
    MLPipeline: Complete machine learning workflow management
    FeatureEngineering: Advanced molecular feature extraction and engineering
    ModelManager: Machine learning model training and evaluation
    DataPipeline: Data preprocessing and transformation pipeline

Example Usage:
    >>> import pyviamd
    >>> from pyviamd.integrations import ml_integration
    >>> 
    >>> # Create ML pipeline
    >>> pipeline = ml_integration.MLPipeline()
    >>> pipeline.add_feature_extractor('rdf', r_max=12.0)
    >>> pipeline.add_feature_extractor('geometry', include_angles=True)
    >>> 
    >>> # Process molecular data and train model
    >>> features = pipeline.extract_features(trajectory_data)
    >>> model = pipeline.train_model(features, labels, algorithm='random_forest')
"""

import numpy as np
import tempfile
import os
from typing import Optional, Dict, List, Tuple, Any, Union, Callable
import logging
import time
import json
import pickle

# Set up logging
logger = logging.getLogger(__name__)

try:
    import pyviamd
    HAS_PYVIAMD = True
except ImportError:
    logger.warning("pyviamd not available. Build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    HAS_PYVIAMD = False

try:
    import pandas as pd
    HAS_PANDAS = True
except ImportError:
    logger.warning("Pandas not available. Install with: pip install pandas")
    HAS_PANDAS = False

try:
    from sklearn.ensemble import RandomForestClassifier, RandomForestRegressor
    from sklearn.svm import SVC, SVR
    from sklearn.linear_model import LogisticRegression, LinearRegression
    from sklearn.model_selection import train_test_split, cross_val_score
    from sklearn.metrics import accuracy_score, mean_squared_error, r2_score
    from sklearn.preprocessing import StandardScaler, MinMaxScaler
    HAS_SKLEARN = True
except ImportError:
    logger.warning("Scikit-learn not available. Install with: pip install scikit-learn")
    HAS_SKLEARN = False

try:
    import joblib
    HAS_JOBLIB = True
except ImportError:
    logger.warning("Joblib not available. Install with: pip install joblib")
    HAS_JOBLIB = False


class MLPipeline:
    """
    Complete machine learning workflow management for molecular data.
    
    This class provides a comprehensive interface for molecular machine learning
    workflows, including feature extraction, model training, and evaluation.
    """
    
    def __init__(self):
        if not HAS_PYVIAMD:
            raise ImportError("pyviamd module not available")
        
        self.feature_extractor = pyviamd.ml.FeatureExtractor()
        self.dimensionality_reducer = pyviamd.ml.DimensionalityReducer()
        self.data_preprocessor = pyviamd.ml.DataPreprocessor()
        
        self.feature_extractors = {}
        self.preprocessors = {}
        self.models = {}
        self.feature_data = {}
        self.model_performance = {}
        
        # Default ML algorithms
        if HAS_SKLEARN:
            self.available_algorithms = {
                'random_forest_classifier': RandomForestClassifier,
                'random_forest_regressor': RandomForestRegressor,
                'svm_classifier': SVC,
                'svm_regressor': SVR,
                'logistic_regression': LogisticRegression,
                'linear_regression': LinearRegression
            }
        else:
            self.available_algorithms = {}
    
    def add_feature_extractor(self, name: str, extractor_type: str, **kwargs) -> None:
        """
        Add a feature extractor to the pipeline.
        
        Args:
            name: Unique name for the feature extractor
            extractor_type: Type of extractor ('rdf', 'angular', 'contact', 'structural', 'custom')
            **kwargs: Configuration parameters for the extractor
        """
        if name in self.feature_extractors:
            raise ValueError(f"Feature extractor '{name}' already exists")
        
        extractor_config = {
            'type': extractor_type,
            'config': kwargs,
            'enabled': True
        }
        
        self.feature_extractors[name] = extractor_config
        logger.info(f"Added {extractor_type} feature extractor '{name}' with config: {kwargs}")
    
    def add_preprocessor(self, name: str, preprocessor_type: str, **kwargs) -> None:
        """
        Add a data preprocessor to the pipeline.
        
        Args:
            name: Unique name for the preprocessor
            preprocessor_type: Type of preprocessor ('standardize', 'normalize', 'pca', 'custom')
            **kwargs: Configuration parameters for the preprocessor
        """
        if name in self.preprocessors:
            raise ValueError(f"Preprocessor '{name}' already exists")
        
        preprocessor_config = {
            'type': preprocessor_type,
            'config': kwargs,
            'enabled': True
        }
        
        self.preprocessors[name] = preprocessor_config
        logger.info(f"Added {preprocessor_type} preprocessor '{name}' with config: {kwargs}")
    
    def extract_features(self, trajectory_data: List[np.ndarray], **kwargs) -> Dict[str, np.ndarray]:
        """
        Extract features from molecular trajectory data.
        
        Args:
            trajectory_data: List of coordinate arrays for each frame
            **kwargs: Additional extraction options
            
        Returns:
            Dictionary mapping feature names to feature arrays
        """
        logger.info(f"Extracting features from trajectory with {len(trajectory_data)} frames")
        
        all_features = {}
        
        for frame_idx, coordinates in enumerate(trajectory_data):
            frame_features = {}
            
            # Apply each feature extractor
            for extractor_name, extractor_config in self.feature_extractors.items():
                if not extractor_config['enabled']:
                    continue
                
                try:
                    features = self._extract_frame_features(
                        coordinates, extractor_config, frame_idx
                    )
                    frame_features[extractor_name] = features
                except Exception as e:
                    logger.error(f"Error extracting {extractor_name} features for frame {frame_idx}: {e}")
                    continue
            
            # Store features for this frame
            for extractor_name, features in frame_features.items():
                if extractor_name not in all_features:
                    all_features[extractor_name] = []
                all_features[extractor_name].append(features)
        
        # Convert lists to arrays
        for extractor_name in all_features:
            try:
                all_features[extractor_name] = np.array(all_features[extractor_name])
            except:
                # Handle variable-length features
                logger.warning(f"Variable-length features for {extractor_name}, keeping as list")
        
        self.feature_data = all_features
        logger.info(f"Feature extraction completed. Extracted {len(all_features)} feature types")
        
        return all_features
    
    def _extract_frame_features(self, coordinates: np.ndarray, 
                              extractor_config: Dict, frame_idx: int) -> np.ndarray:
        """Extract features from a single frame."""
        extractor_type = extractor_config['type']
        config = extractor_config['config']
        
        if extractor_type == 'rdf':
            return self.feature_extractor.extract_rdf_features(
                coordinates, 
                config.get('r_max', 10.0),
                config.get('n_bins', 100)
            )
        elif extractor_type == 'angular':
            return self.feature_extractor.extract_angular_features(
                coordinates,
                config.get('n_bins', 180)
            )
        elif extractor_type == 'contact':
            contact_matrix = self.feature_extractor.extract_contact_features(
                coordinates,
                config.get('cutoff', 5.0)
            )
            # Flatten matrix to 1D feature vector
            return contact_matrix.flatten()
        elif extractor_type == 'structural':
            descriptors = self.feature_extractor.extract_structural_descriptors(coordinates)
            # Convert dict to array
            feature_values = []
            for key in ['radius_of_gyration', 'asphericity', 'moment_of_inertia_xx', 
                       'moment_of_inertia_yy', 'moment_of_inertia_zz', 'n_atoms']:
                if key in descriptors:
                    if key == 'center_of_mass':
                        feature_values.extend(descriptors[key])
                    else:
                        feature_values.append(descriptors[key])
            return np.array(feature_values)
        elif extractor_type == 'custom':
            callback = config.get('callback')
            if callback and callable(callback):
                return callback(coordinates, frame_idx, config)
            else:
                raise ValueError("Custom extractor requires valid callback function")
        else:
            raise ValueError(f"Unknown feature extractor type: {extractor_type}")
    
    def preprocess_features(self, features: Dict[str, np.ndarray]) -> Dict[str, np.ndarray]:
        """
        Apply preprocessing to extracted features.
        
        Args:
            features: Dictionary of feature arrays
            
        Returns:
            Dictionary of preprocessed feature arrays
        """
        if not self.preprocessors:
            logger.info("No preprocessors configured, returning original features")
            return features
        
        preprocessed_features = features.copy()
        
        for preprocessor_name, preprocessor_config in self.preprocessors.items():
            if not preprocessor_config['enabled']:
                continue
            
            logger.info(f"Applying preprocessor: {preprocessor_name}")
            
            try:
                preprocessed_features = self._apply_preprocessor(
                    preprocessed_features, preprocessor_config
                )
            except Exception as e:
                logger.error(f"Error applying preprocessor {preprocessor_name}: {e}")
                continue
        
        return preprocessed_features
    
    def _apply_preprocessor(self, features: Dict[str, np.ndarray], 
                          preprocessor_config: Dict) -> Dict[str, np.ndarray]:
        """Apply a specific preprocessor to features."""
        preprocessor_type = preprocessor_config['type']
        config = preprocessor_config['config']
        
        processed_features = {}
        
        for feature_name, feature_data in features.items():
            if feature_data.ndim == 1:
                # Reshape 1D to 2D for sklearn compatibility
                feature_data = feature_data.reshape(-1, 1)
            elif feature_data.ndim > 2:
                # Flatten higher dimensional features
                original_shape = feature_data.shape
                feature_data = feature_data.reshape(original_shape[0], -1)
            
            if preprocessor_type == 'standardize':
                result = self.data_preprocessor.standardize(feature_data)
                processed_features[feature_name] = result['data']
            elif preprocessor_type == 'normalize':
                result = self.data_preprocessor.normalize_minmax(feature_data)
                processed_features[feature_name] = result['data']
            elif preprocessor_type == 'pca':
                n_components = config.get('n_components', 2)
                result = self.dimensionality_reducer.pca(feature_data, n_components)
                processed_features[feature_name] = result['centered_data']
            elif preprocessor_type == 'custom':
                callback = config.get('callback')
                if callback and callable(callback):
                    processed_features[feature_name] = callback(feature_data, config)
                else:
                    processed_features[feature_name] = feature_data
            else:
                logger.warning(f"Unknown preprocessor type: {preprocessor_type}")
                processed_features[feature_name] = feature_data
        
        return processed_features
    
    def combine_features(self, features: Dict[str, np.ndarray]) -> np.ndarray:
        """
        Combine multiple feature types into a single feature matrix.
        
        Args:
            features: Dictionary of feature arrays
            
        Returns:
            Combined feature matrix (n_samples, n_features)
        """
        if not features:
            raise ValueError("No features provided")
        
        # Get number of samples from first feature
        n_samples = None
        feature_arrays = []
        
        for feature_name, feature_data in features.items():
            if n_samples is None:
                n_samples = feature_data.shape[0]
            elif feature_data.shape[0] != n_samples:
                logger.warning(f"Feature {feature_name} has different number of samples: "
                             f"{feature_data.shape[0]} vs {n_samples}")
                continue
            
            # Flatten if necessary
            if feature_data.ndim > 2:
                feature_data = feature_data.reshape(n_samples, -1)
            elif feature_data.ndim == 1:
                feature_data = feature_data.reshape(-1, 1)
            
            feature_arrays.append(feature_data)
        
        # Concatenate all features
        if feature_arrays:
            combined = np.hstack(feature_arrays)
            logger.info(f"Combined features shape: {combined.shape}")
            return combined
        else:
            raise ValueError("No valid features to combine")
    
    def train_model(self, features: np.ndarray, labels: np.ndarray, 
                   algorithm: str, model_name: str = None, **kwargs) -> Dict[str, Any]:
        """
        Train a machine learning model.
        
        Args:
            features: Feature matrix (n_samples, n_features)
            labels: Target labels (n_samples,)
            algorithm: Algorithm name (e.g., 'random_forest_classifier')
            model_name: Name for the trained model
            **kwargs: Model parameters
            
        Returns:
            Training results dictionary
        """
        if not HAS_SKLEARN:
            raise ImportError("Scikit-learn not available for model training")
        
        if algorithm not in self.available_algorithms:
            raise ValueError(f"Unknown algorithm: {algorithm}. Available: {list(self.available_algorithms.keys())}")
        
        model_name = model_name or f"{algorithm}_{int(time.time())}"
        
        logger.info(f"Training {algorithm} model with features shape: {features.shape}")
        
        # Split data
        test_size = kwargs.pop('test_size', 0.2)
        random_state = kwargs.pop('random_state', 42)
        
        X_train, X_test, y_train, y_test = train_test_split(
            features, labels, test_size=test_size, random_state=random_state
        )
        
        # Create and train model
        model_class = self.available_algorithms[algorithm]
        model = model_class(**kwargs)
        
        start_time = time.time()
        model.fit(X_train, y_train)
        training_time = time.time() - start_time
        
        # Evaluate model
        train_predictions = model.predict(X_train)
        test_predictions = model.predict(X_test)
        
        # Calculate metrics
        if 'classifier' in algorithm:
            train_score = accuracy_score(y_train, train_predictions)
            test_score = accuracy_score(y_test, test_predictions)
            metric_name = 'accuracy'
        else:
            train_score = r2_score(y_train, train_predictions)
            test_score = r2_score(y_test, test_predictions)
            metric_name = 'r2_score'
        
        # Cross-validation
        cv_scores = cross_val_score(model, features, labels, cv=5)
        
        # Store model and results
        model_info = {
            'model': model,
            'algorithm': algorithm,
            'features_shape': features.shape,
            'training_time': training_time,
            'train_score': train_score,
            'test_score': test_score,
            'cv_mean': np.mean(cv_scores),
            'cv_std': np.std(cv_scores),
            'metric_name': metric_name,
            'parameters': kwargs
        }
        
        self.models[model_name] = model_info
        self.model_performance[model_name] = {
            'train_score': train_score,
            'test_score': test_score,
            'cv_mean': np.mean(cv_scores),
            'cv_std': np.std(cv_scores)
        }
        
        logger.info(f"Model training completed. {metric_name}: "
                   f"train={train_score:.3f}, test={test_score:.3f}, "
                   f"cv={np.mean(cv_scores):.3f}±{np.std(cv_scores):.3f}")
        
        return model_info
    
    def predict(self, model_name: str, features: np.ndarray) -> np.ndarray:
        """
        Make predictions with a trained model.
        
        Args:
            model_name: Name of the trained model
            features: Feature matrix for prediction
            
        Returns:
            Predictions array
        """
        if model_name not in self.models:
            raise ValueError(f"Model '{model_name}' not found")
        
        model = self.models[model_name]['model']
        return model.predict(features)
    
    def cluster_data(self, features: np.ndarray, n_clusters: int, **kwargs) -> Dict[str, Any]:
        """
        Perform clustering on molecular features.
        
        Args:
            features: Feature matrix
            n_clusters: Number of clusters
            **kwargs: Clustering parameters
            
        Returns:
            Clustering results dictionary
        """
        logger.info(f"Performing k-means clustering with {n_clusters} clusters")
        
        result = self.dimensionality_reducer.kmeans(features, n_clusters, **kwargs)
        
        # Calculate cluster statistics
        labels = result['labels']
        centroids = result['centroids']
        
        cluster_stats = {}
        for cluster_id in range(n_clusters):
            cluster_mask = labels == cluster_id
            cluster_size = np.sum(cluster_mask)
            
            if cluster_size > 0:
                cluster_features = features[cluster_mask]
                cluster_stats[cluster_id] = {
                    'size': int(cluster_size),
                    'percentage': float(cluster_size / len(labels) * 100),
                    'mean_features': np.mean(cluster_features, axis=0).tolist(),
                    'std_features': np.std(cluster_features, axis=0).tolist()
                }
        
        result['cluster_statistics'] = cluster_stats
        result['total_samples'] = len(labels)
        
        logger.info(f"Clustering completed. Cluster sizes: {[stats['size'] for stats in cluster_stats.values()]}")
        
        return result
    
    def save_model(self, model_name: str, filename: str) -> bool:
        """
        Save a trained model to file.
        
        Args:
            model_name: Name of the model to save
            filename: Output filename
            
        Returns:
            True if save successful
        """
        if model_name not in self.models:
            raise ValueError(f"Model '{model_name}' not found")
        
        try:
            if HAS_JOBLIB:
                joblib.dump(self.models[model_name], filename)
            else:
                with open(filename, 'wb') as f:
                    pickle.dump(self.models[model_name], f)
            
            logger.info(f"Model '{model_name}' saved to {filename}")
            return True
            
        except Exception as e:
            logger.error(f"Failed to save model: {e}")
            return False
    
    def load_model(self, filename: str, model_name: str = None) -> str:
        """
        Load a trained model from file.
        
        Args:
            filename: Input filename
            model_name: Name for the loaded model (auto-generated if None)
            
        Returns:
            Name of the loaded model
        """
        try:
            if HAS_JOBLIB:
                model_info = joblib.load(filename)
            else:
                with open(filename, 'rb') as f:
                    model_info = pickle.load(f)
            
            model_name = model_name or f"loaded_model_{int(time.time())}"
            self.models[model_name] = model_info
            
            logger.info(f"Model loaded from {filename} as '{model_name}'")
            return model_name
            
        except Exception as e:
            logger.error(f"Failed to load model: {e}")
            raise
    
    def get_model_list(self) -> List[str]:
        """Get list of trained models."""
        return list(self.models.keys())
    
    def get_model_performance(self) -> Dict[str, Dict[str, float]]:
        """Get performance metrics for all models."""
        return self.model_performance.copy()
    
    def get_feature_importance(self, model_name: str) -> Optional[np.ndarray]:
        """
        Get feature importance from a trained model.
        
        Args:
            model_name: Name of the trained model
            
        Returns:
            Feature importance array or None if not available
        """
        if model_name not in self.models:
            raise ValueError(f"Model '{model_name}' not found")
        
        model = self.models[model_name]['model']
        
        if hasattr(model, 'feature_importances_'):
            return model.feature_importances_
        elif hasattr(model, 'coef_'):
            return np.abs(model.coef_).flatten()
        else:
            logger.warning(f"Feature importance not available for model type: {type(model)}")
            return None
    
    def clear_models(self) -> None:
        """Clear all trained models."""
        self.models.clear()
        self.model_performance.clear()
        logger.info("All models cleared")
    
    def get_pipeline_summary(self) -> Dict[str, Any]:
        """Get summary of the ML pipeline configuration."""
        return {
            'feature_extractors': {name: config['type'] for name, config in self.feature_extractors.items()},
            'preprocessors': {name: config['type'] for name, config in self.preprocessors.items()},
            'trained_models': list(self.models.keys()),
            'available_algorithms': list(self.available_algorithms.keys()) if HAS_SKLEARN else [],
            'feature_data_available': bool(self.feature_data)
        }


# Export main classes
__all__ = ['MLPipeline']