#!/usr/bin/env python3
"""
Machine Learning Integration Example for VIAMD Phase 5

This example demonstrates the comprehensive machine learning capabilities
provided by VIAMD's Phase 5 implementation, including feature extraction,
model training, and molecular data analysis workflows.

Features demonstrated:
- Molecular feature extraction (RDF, geometry, contacts)
- Data preprocessing and normalization
- Machine learning model training and evaluation
- Clustering analysis for molecular conformations
- Performance evaluation and model comparison
- Feature importance analysis

Usage:
    python ml_integration_example.py
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
    from pyviamd.integrations import ml_integration
    print("✓ VIAMD Python bindings loaded successfully")
except ImportError as e:
    print(f"✗ Failed to import VIAMD Python bindings: {e}")
    print("Please build VIAMD with VIAMD_ENABLE_PYTHON=ON")
    sys.exit(1)

# Configure logging
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)


def generate_test_conformations(n_conformations=200, n_atoms=25):
    """Generate test molecular conformations for ML analysis."""
    print(f"🧬 Generating {n_conformations} molecular conformations with {n_atoms} atoms each")
    
    conformations = []
    labels = []
    
    for i in range(n_conformations):
        # Create different conformation types
        conformation_type = i % 3  # 3 different types
        
        if conformation_type == 0:  # Extended conformation
            coords = []
            for j in range(n_atoms):
                x = j * 1.5 + np.random.normal(0, 0.2)
                y = np.random.normal(0, 0.5)
                z = np.random.normal(0, 0.5)
                coords.append([x, y, z])
            labels.append(0)  # Extended
            
        elif conformation_type == 1:  # Helical conformation
            coords = []
            for j in range(n_atoms):
                angle = j * 100 * np.pi / 180  # 100 degrees per residue
                radius = 3.0 + np.random.normal(0, 0.3)
                x = radius * np.cos(angle) + np.random.normal(0, 0.2)
                y = radius * np.sin(angle) + np.random.normal(0, 0.2)
                z = j * 1.5 + np.random.normal(0, 0.2)
                coords.append([x, y, z])
            labels.append(1)  # Helical
            
        else:  # Compact conformation
            coords = []
            center = np.random.normal(0, 2, 3)
            for j in range(n_atoms):
                # Random positions around center
                offset = np.random.normal(0, 3, 3)
                coords.append(center + offset)
            labels.append(2)  # Compact
        
        conformations.append(np.array(coords))
    
    print(f"   Generated conformations:")
    print(f"     Extended: {labels.count(0)} conformations")
    print(f"     Helical: {labels.count(1)} conformations")
    print(f"     Compact: {labels.count(2)} conformations")
    
    return conformations, np.array(labels)


def demonstrate_feature_extraction():
    """Demonstrate molecular feature extraction capabilities."""
    print("\n" + "="*60)
    print("FEATURE EXTRACTION DEMONSTRATION")
    print("="*60)
    
    # Generate test molecular structure
    n_atoms = 30
    np.random.seed(42)
    coordinates = np.random.uniform(-5, 5, (n_atoms, 3))
    
    # Create feature extractor
    feature_extractor = pyviamd.ml.FeatureExtractor()
    
    # Extract RDF features
    print("📊 Extracting Radial Distribution Function (RDF) features...")
    rdf_features = feature_extractor.extract_rdf_features(coordinates, r_max=10.0, n_bins=50)
    print(f"   RDF features shape: {rdf_features.shape}")
    print(f"   RDF feature range: {rdf_features.min():.4f} to {rdf_features.max():.4f}")
    print(f"   Non-zero bins: {np.count_nonzero(rdf_features)}")
    
    # Extract angular features
    print(f"\n📐 Extracting angular distribution features...")
    angular_features = feature_extractor.extract_angular_features(coordinates, n_bins=90)
    print(f"   Angular features shape: {angular_features.shape}")
    print(f"   Angular feature range: {angular_features.min():.4f} to {angular_features.max():.4f}")
    print(f"   Most populated angle bin: {np.argmax(angular_features)} (index)")
    
    # Extract contact features
    print(f"\n🤝 Extracting contact matrix features...")
    contact_features = feature_extractor.extract_contact_features(coordinates, cutoff=4.0)
    print(f"   Contact matrix shape: {contact_features.shape}")
    print(f"   Contact density: {np.mean(contact_features):.3f}")
    print(f"   Number of contacts: {np.sum(contact_features)}")
    
    # Extract structural descriptors
    print(f"\n🏗️  Extracting structural descriptors...")
    structural_desc = feature_extractor.extract_structural_descriptors(coordinates)
    print(f"   Available descriptors: {list(structural_desc.keys())}")
    
    for key, value in structural_desc.items():
        if key == 'center_of_mass':
            print(f"   {key}: ({value[0]:.2f}, {value[1]:.2f}, {value[2]:.2f})")
        elif isinstance(value, (int, float)):
            print(f"   {key}: {value:.4f}")
        else:
            print(f"   {key}: {value}")


def demonstrate_data_preprocessing():
    """Demonstrate data preprocessing capabilities."""
    print("\n" + "="*60)
    print("DATA PREPROCESSING DEMONSTRATION")
    print("="*60)
    
    # Generate test data with different scales
    np.random.seed(42)
    n_samples, n_features = 100, 10
    
    # Create data with different scales and distributions
    data = np.column_stack([
        np.random.normal(100, 20, n_samples),      # Large scale
        np.random.normal(0.01, 0.005, n_samples), # Small scale
        np.random.exponential(2, n_samples),       # Skewed distribution
        np.random.uniform(-1, 1, n_samples),       # Uniform distribution
        np.random.normal(50, 10, n_samples),       # Medium scale
        np.random.gamma(2, 2, n_samples),          # Gamma distribution
        np.random.normal(-10, 5, n_samples),       # Negative mean
        np.random.normal(1000, 100, n_samples),    # Very large scale
        np.random.normal(0, 1, n_samples),         # Standard normal
        np.random.beta(2, 5, n_samples)            # Beta distribution
    ])
    
    print(f"📊 Original data statistics:")
    print(f"   Shape: {data.shape}")
    print(f"   Means: {np.mean(data, axis=0)}")
    print(f"   Std devs: {np.std(data, axis=0)}")
    print(f"   Min values: {np.min(data, axis=0)}")
    print(f"   Max values: {np.max(data, axis=0)}")
    
    # Create data preprocessor
    preprocessor = pyviamd.ml.DataPreprocessor()
    
    # Standardization (z-score normalization)
    print(f"\n🔄 Applying standardization...")
    standardized_result = preprocessor.standardize(data)
    standardized_data = standardized_result['data']
    
    print(f"   Standardized data shape: {standardized_data.shape}")
    print(f"   Standardized means: {np.mean(standardized_data, axis=0)}")
    print(f"   Standardized std devs: {np.std(standardized_data, axis=0)}")
    print(f"   Original means stored: {standardized_result['mean'].shape}")
    print(f"   Original std devs stored: {standardized_result['std'].shape}")
    
    # Min-max normalization
    print(f"\n📏 Applying min-max normalization...")
    normalized_result = preprocessor.normalize_minmax(data)
    normalized_data = normalized_result['data']
    
    print(f"   Normalized data shape: {normalized_data.shape}")
    print(f"   Normalized mins: {np.min(normalized_data, axis=0)}")
    print(f"   Normalized maxs: {np.max(normalized_data, axis=0)}")
    print(f"   Original mins stored: {normalized_result['min'].shape}")
    print(f"   Original maxs stored: {normalized_result['max'].shape}")


def demonstrate_dimensionality_reduction():
    """Demonstrate dimensionality reduction capabilities."""
    print("\n" + "="*60)
    print("DIMENSIONALITY REDUCTION DEMONSTRATION")
    print("="*60)
    
    # Generate high-dimensional test data
    np.random.seed(42)
    n_samples, n_features = 150, 20
    
    # Create correlated data
    base_data = np.random.normal(0, 1, (n_samples, 5))
    
    # Add correlated and noisy features
    data = np.column_stack([
        base_data,
        base_data[:, 0:3] + np.random.normal(0, 0.1, (n_samples, 3)),  # Correlated
        base_data[:, 1:4] * 2 + np.random.normal(0, 0.2, (n_samples, 3)),  # Scaled and correlated
        np.random.normal(0, 0.1, (n_samples, 9))  # Noise features
    ])
    
    print(f"📊 High-dimensional data:")
    print(f"   Shape: {data.shape}")
    print(f"   Mean: {np.mean(data):.4f}")
    print(f"   Std: {np.std(data):.4f}")
    
    # Create dimensionality reducer
    dim_reducer = pyviamd.ml.DimensionalityReducer()
    
    # Principal Component Analysis
    print(f"\n🎯 Applying Principal Component Analysis (PCA)...")
    n_components = 5
    pca_result = dim_reducer.pca(data, n_components)
    
    print(f"   PCA result keys: {list(pca_result.keys())}")
    print(f"   Centered data shape: {pca_result['centered_data'].shape}")
    print(f"   Mean shape: {pca_result['mean'].shape}")
    print(f"   Covariance matrix shape: {pca_result['covariance'].shape}")
    
    # Show some covariance matrix statistics
    cov_matrix = pca_result['covariance']
    print(f"   Covariance matrix diagonal (variances): {np.diag(cov_matrix)[:5]}")
    
    # K-means clustering
    print(f"\n🎯 Applying K-means clustering...")
    n_clusters = 3
    kmeans_result = dim_reducer.kmeans(data, n_clusters, max_iters=50)
    
    print(f"   K-means result keys: {list(kmeans_result.keys())}")
    print(f"   Labels shape: {kmeans_result['labels'].shape}")
    print(f"   Centroids shape: {kmeans_result['centroids'].shape}")
    print(f"   Number of clusters: {kmeans_result['n_clusters']}")
    
    # Analyze cluster assignments
    labels = kmeans_result['labels']
    unique_labels, counts = np.unique(labels, return_counts=True)
    print(f"   Cluster distribution:")
    for label, count in zip(unique_labels, counts):
        percentage = count / len(labels) * 100
        print(f"     Cluster {label}: {count} samples ({percentage:.1f}%)")


def demonstrate_ml_pipeline():
    """Demonstrate the complete ML pipeline."""
    print("\n" + "="*60)
    print("MACHINE LEARNING PIPELINE DEMONSTRATION")
    print("="*60)
    
    # Check if scikit-learn is available
    try:
        import sklearn
        has_sklearn = True
        print("✓ Scikit-learn available for ML workflows")
    except ImportError:
        has_sklearn = False
        print("⚠️  Scikit-learn not available - ML training will be skipped")
    
    # Generate test molecular conformations
    conformations, labels = generate_test_conformations(100, 20)
    
    # Create ML pipeline
    pipeline = ml_integration.MLPipeline()
    
    # Add feature extractors
    print(f"\n🔧 Configuring ML pipeline...")
    pipeline.add_feature_extractor("rdf", "rdf", r_max=8.0, n_bins=40)
    pipeline.add_feature_extractor("geometry", "structural")
    pipeline.add_feature_extractor("contacts", "contact", cutoff=4.0)
    
    # Add preprocessors
    pipeline.add_preprocessor("standardize", "standardize")
    
    print(f"   Feature extractors: {pipeline.get_pipeline_summary()['feature_extractors']}")
    print(f"   Preprocessors: {pipeline.get_pipeline_summary()['preprocessors']}")
    
    # Extract features
    print(f"\n🔍 Extracting features from molecular conformations...")
    features = pipeline.extract_features(conformations)
    
    print(f"   Extracted feature types: {list(features.keys())}")
    for feature_name, feature_data in features.items():
        print(f"     {feature_name}: shape {feature_data.shape}")
    
    # Preprocess features
    print(f"\n🔄 Preprocessing extracted features...")
    processed_features = pipeline.preprocess_features(features)
    
    print(f"   Processed feature types: {list(processed_features.keys())}")
    for feature_name, feature_data in processed_features.items():
        print(f"     {feature_name}: shape {feature_data.shape}")
    
    # Combine features
    print(f"\n🔗 Combining features into single matrix...")
    combined_features = pipeline.combine_features(processed_features)
    print(f"   Combined features shape: {combined_features.shape}")
    print(f"   Feature range: {combined_features.min():.4f} to {combined_features.max():.4f}")
    
    # Machine learning with scikit-learn
    if has_sklearn:
        print(f"\n🤖 Training machine learning models...")
        
        # Classification
        print(f"   Training classification models...")
        
        # Random Forest
        rf_model = pipeline.train_model(
            combined_features, labels, 
            'random_forest_classifier',
            'rf_classifier',
            n_estimators=50,
            random_state=42
        )
        
        print(f"     Random Forest - Accuracy: {rf_model['test_score']:.3f}")
        print(f"     Training time: {rf_model['training_time']:.2f} seconds")
        print(f"     Cross-validation: {rf_model['cv_mean']:.3f} ± {rf_model['cv_std']:.3f}")
        
        # SVM
        svm_model = pipeline.train_model(
            combined_features, labels,
            'svm_classifier',
            'svm_classifier',
            kernel='rbf',
            C=1.0,
            random_state=42
        )
        
        print(f"     SVM - Accuracy: {svm_model['test_score']:.3f}")
        print(f"     Training time: {svm_model['training_time']:.2f} seconds")
        print(f"     Cross-validation: {svm_model['cv_mean']:.3f} ± {svm_model['cv_std']:.3f}")
        
        # Make predictions
        print(f"\n🔮 Making predictions...")
        
        # Use first 10 samples for prediction
        test_features = combined_features[:10]
        rf_predictions = pipeline.predict('rf_classifier', test_features)
        svm_predictions = pipeline.predict('svm_classifier', test_features)
        
        print(f"   Sample predictions (first 10):")
        print(f"     True labels:   {labels[:10]}")
        print(f"     RF predictions: {rf_predictions}")
        print(f"     SVM predictions: {svm_predictions}")
        
        # Feature importance analysis
        print(f"\n🎯 Analyzing feature importance...")
        
        rf_importance = pipeline.get_feature_importance('rf_classifier')
        if rf_importance is not None:
            top_features = np.argsort(rf_importance)[-5:]  # Top 5 features
            print(f"   Top 5 most important features (Random Forest):")
            for i, feature_idx in enumerate(reversed(top_features)):
                importance = rf_importance[feature_idx]
                print(f"     Feature {feature_idx}: {importance:.4f}")
        
        # Model performance summary
        performance = pipeline.get_model_performance()
        print(f"\n📊 Model performance summary:")
        for model_name, metrics in performance.items():
            print(f"   {model_name}:")
            for metric, value in metrics.items():
                print(f"     {metric}: {value:.3f}")
    
    # Clustering analysis
    print(f"\n🎯 Performing clustering analysis...")
    clustering_result = pipeline.cluster_data(combined_features, n_clusters=3)
    
    print(f"   Clustering results:")
    print(f"     Total samples: {clustering_result['total_samples']}")
    print(f"     Number of clusters: {clustering_result['n_clusters']}")
    
    cluster_stats = clustering_result['cluster_statistics']
    for cluster_id, stats in cluster_stats.items():
        print(f"     Cluster {cluster_id}: {stats['size']} samples ({stats['percentage']:.1f}%)")
    
    # Compare clustering with true labels
    cluster_labels = clustering_result['labels']
    print(f"\n🔍 Comparing clustering with true conformation types:")
    
    for true_label in [0, 1, 2]:
        true_mask = labels == true_label
        if np.any(true_mask):
            cluster_assignments = cluster_labels[true_mask]
            unique_clusters, counts = np.unique(cluster_assignments, return_counts=True)
            print(f"   Conformation type {true_label}:")
            for cluster, count in zip(unique_clusters, counts):
                percentage = count / len(cluster_assignments) * 100
                print(f"     Assigned to cluster {cluster}: {count} ({percentage:.1f}%)")


def main():
    """Main demonstration function."""
    print("VIAMD Phase 5: Machine Learning Integration Demo")
    print("=" * 60)
    print("This example demonstrates the machine learning capabilities")
    print("introduced in VIAMD Phase 5 implementation.")
    print()
    
    try:
        # Test basic C++ bindings availability
        print("🧪 Testing VIAMD ML bindings...")
        
        # Test feature extractor
        extractor = pyviamd.ml.FeatureExtractor()
        test_coords = np.array([[0, 0, 0], [1, 0, 0], [0, 1, 0]])
        rdf = extractor.extract_rdf_features(test_coords, 5.0, 10)
        print(f"   ✓ Feature extractor working (RDF shape: {rdf.shape})")
        
        # Test dimensionality reducer
        reducer = pyviamd.ml.DimensionalityReducer()
        test_data = np.random.rand(10, 5)
        pca_result = reducer.pca(test_data, 2)
        print(f"   ✓ Dimensionality reducer working")
        
        # Test data preprocessor
        preprocessor = pyviamd.ml.DataPreprocessor()
        norm_result = preprocessor.standardize(test_data)
        print(f"   ✓ Data preprocessor working")
        
        print("\n🎉 All Phase 5 ML bindings loaded successfully!")
        
        # Run demonstrations
        demonstrate_feature_extraction()
        demonstrate_data_preprocessing()
        demonstrate_dimensionality_reduction()
        demonstrate_ml_pipeline()
        
        print("\n" + "="*60)
        print("✅ PHASE 5 MACHINE LEARNING DEMONSTRATION COMPLETED SUCCESSFULLY")
        print("="*60)
        print()
        print("Phase 5 provides comprehensive machine learning capabilities:")
        print("• Molecular feature extraction (RDF, geometry, contacts)")
        print("• Advanced data preprocessing and normalization")
        print("• Dimensionality reduction with PCA and clustering")
        print("• Complete ML pipelines with model training and evaluation")
        print("• Feature importance analysis and model comparison")
        print("• Integration with scikit-learn for advanced algorithms")
        print()
        print("These tools enable sophisticated molecular ML workflows")
        print("for conformation analysis, property prediction, and pattern discovery.")
        
    except Exception as e:
        print(f"\n❌ Error during demonstration: {e}")
        import traceback
        traceback.print_exc()
        return 1
    
    return 0


if __name__ == "__main__":
    sys.exit(main())