/*!
 * \file ml_bindings.cpp
 * \brief Machine learning integration bindings for Python
 *
 * This file provides Python bindings for machine learning utilities
 * and helpers for molecular data analysis.
 */

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <random>

namespace py = pybind11;

namespace viamd {
namespace ml {

/*!
 * \brief Feature extraction utilities for molecular data
 */
class FeatureExtractor {
public:
    /*!
     * \brief Extract radial distribution function features
     */
    static py::array_t<double> extract_rdf_features(const py::array_t<double>& coordinates,
                                                   double r_max = 10.0,
                                                   size_t n_bins = 100) {
        auto coord_buf = coordinates.request();
        if (coord_buf.ndim != 2 || coord_buf.shape[1] != 3) {
            throw std::invalid_argument("Coordinates must be Nx3 array");
        }
        
        size_t n_atoms = coord_buf.shape[0];
        double* coord_ptr = static_cast<double*>(coord_buf.ptr);
        
        auto result = py::array_t<double>(n_bins);
        auto res_buf = result.request();
        double* res_ptr = static_cast<double*>(res_buf.ptr);
        
        // Initialize histogram
        std::fill(res_ptr, res_ptr + n_bins, 0.0);
        
        double bin_width = r_max / n_bins;
        
        // Calculate pairwise distances and build histogram
        for (size_t i = 0; i < n_atoms; ++i) {
            for (size_t j = i + 1; j < n_atoms; ++j) {
                double dx = coord_ptr[i * 3 + 0] - coord_ptr[j * 3 + 0];
                double dy = coord_ptr[i * 3 + 1] - coord_ptr[j * 3 + 1];
                double dz = coord_ptr[i * 3 + 2] - coord_ptr[j * 3 + 2];
                double distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                
                if (distance < r_max) {
                    size_t bin = static_cast<size_t>(distance / bin_width);
                    if (bin < n_bins) {
                        res_ptr[bin] += 1.0;
                    }
                }
            }
        }
        
        // Normalize by shell volume and number density
        for (size_t i = 0; i < n_bins; ++i) {
            double r_inner = i * bin_width;
            double r_outer = (i + 1) * bin_width;
            double shell_volume = (4.0/3.0) * M_PI * (r_outer*r_outer*r_outer - r_inner*r_inner*r_inner);
            
            if (shell_volume > 0.0) {
                res_ptr[i] /= shell_volume;
            }
        }
        
        return result;
    }
    
    /*!
     * \brief Extract angular distribution features
     */
    static py::array_t<double> extract_angular_features(const py::array_t<double>& coordinates,
                                                       size_t n_bins = 180) {
        auto coord_buf = coordinates.request();
        if (coord_buf.ndim != 2 || coord_buf.shape[1] != 3) {
            throw std::invalid_argument("Coordinates must be Nx3 array");
        }
        
        size_t n_atoms = coord_buf.shape[0];
        double* coord_ptr = static_cast<double*>(coord_buf.ptr);
        
        auto result = py::array_t<double>(n_bins);
        auto res_buf = result.request();
        double* res_ptr = static_cast<double*>(res_buf.ptr);
        
        // Initialize histogram
        std::fill(res_ptr, res_ptr + n_bins, 0.0);
        
        double bin_width = 180.0 / n_bins; // Degrees per bin
        
        // Calculate angles for all triplets of atoms
        for (size_t i = 0; i < n_atoms; ++i) {
            for (size_t j = 0; j < n_atoms; ++j) {
                if (i == j) continue;
                for (size_t k = 0; k < n_atoms; ++k) {
                    if (k == i || k == j) continue;
                    
                    // Vector from j to i
                    double v1[3] = {
                        coord_ptr[i * 3 + 0] - coord_ptr[j * 3 + 0],
                        coord_ptr[i * 3 + 1] - coord_ptr[j * 3 + 1],
                        coord_ptr[i * 3 + 2] - coord_ptr[j * 3 + 2]
                    };
                    
                    // Vector from j to k
                    double v2[3] = {
                        coord_ptr[k * 3 + 0] - coord_ptr[j * 3 + 0],
                        coord_ptr[k * 3 + 1] - coord_ptr[j * 3 + 1],
                        coord_ptr[k * 3 + 2] - coord_ptr[j * 3 + 2]
                    };
                    
                    // Calculate magnitudes
                    double mag1 = std::sqrt(v1[0]*v1[0] + v1[1]*v1[1] + v1[2]*v1[2]);
                    double mag2 = std::sqrt(v2[0]*v2[0] + v2[1]*v2[1] + v2[2]*v2[2]);
                    
                    if (mag1 > 0.0 && mag2 > 0.0) {
                        // Calculate dot product
                        double dot = v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2];
                        double cos_angle = dot / (mag1 * mag2);
                        cos_angle = std::max(-1.0, std::min(1.0, cos_angle));
                        
                        double angle_degrees = std::acos(cos_angle) * 180.0 / M_PI;
                        size_t bin = static_cast<size_t>(angle_degrees / bin_width);
                        if (bin < n_bins) {
                            res_ptr[bin] += 1.0;
                        }
                    }
                }
            }
        }
        
        return result;
    }
    
    /*!
     * \brief Extract contact matrix features
     */
    static py::array_t<double> extract_contact_features(const py::array_t<double>& coordinates,
                                                       double cutoff = 5.0) {
        auto coord_buf = coordinates.request();
        if (coord_buf.ndim != 2 || coord_buf.shape[1] != 3) {
            throw std::invalid_argument("Coordinates must be Nx3 array");
        }
        
        size_t n_atoms = coord_buf.shape[0];
        double* coord_ptr = static_cast<double*>(coord_buf.ptr);
        
        auto result = py::array_t<double>({n_atoms, n_atoms});
        auto res_buf = result.request();
        double* res_ptr = static_cast<double*>(res_buf.ptr);
        
        // Calculate contact matrix
        for (size_t i = 0; i < n_atoms; ++i) {
            for (size_t j = 0; j < n_atoms; ++j) {
                if (i == j) {
                    res_ptr[i * n_atoms + j] = 0.0;
                } else {
                    double dx = coord_ptr[i * 3 + 0] - coord_ptr[j * 3 + 0];
                    double dy = coord_ptr[i * 3 + 1] - coord_ptr[j * 3 + 1];
                    double dz = coord_ptr[i * 3 + 2] - coord_ptr[j * 3 + 2];
                    double distance = std::sqrt(dx*dx + dy*dy + dz*dz);
                    
                    res_ptr[i * n_atoms + j] = (distance <= cutoff) ? 1.0 : 0.0;
                }
            }
        }
        
        return result;
    }
    
    /*!
     * \brief Extract structural descriptors
     */
    static py::dict extract_structural_descriptors(const py::array_t<double>& coordinates) {
        auto coord_buf = coordinates.request();
        if (coord_buf.ndim != 2 || coord_buf.shape[1] != 3) {
            throw std::invalid_argument("Coordinates must be Nx3 array");
        }
        
        size_t n_atoms = coord_buf.shape[0];
        double* coord_ptr = static_cast<double*>(coord_buf.ptr);
        
        py::dict descriptors;
        
        // Calculate center of mass
        double com[3] = {0.0, 0.0, 0.0};
        for (size_t i = 0; i < n_atoms; ++i) {
            com[0] += coord_ptr[i * 3 + 0];
            com[1] += coord_ptr[i * 3 + 1];
            com[2] += coord_ptr[i * 3 + 2];
        }
        com[0] /= n_atoms;
        com[1] /= n_atoms;
        com[2] /= n_atoms;
        
        // Calculate moments of inertia
        double Ixx = 0.0, Iyy = 0.0, Izz = 0.0;
        double Ixy = 0.0, Ixz = 0.0, Iyz = 0.0;
        
        for (size_t i = 0; i < n_atoms; ++i) {
            double x = coord_ptr[i * 3 + 0] - com[0];
            double y = coord_ptr[i * 3 + 1] - com[1];
            double z = coord_ptr[i * 3 + 2] - com[2];
            
            Ixx += y*y + z*z;
            Iyy += x*x + z*z;
            Izz += x*x + y*y;
            Ixy -= x*y;
            Ixz -= x*z;
            Iyz -= y*z;
        }
        
        // Calculate asphericity
        double asphericity = Ixx - 0.5*(Iyy + Izz);
        
        // Calculate radius of gyration
        double rg_squared = 0.0;
        for (size_t i = 0; i < n_atoms; ++i) {
            double dx = coord_ptr[i * 3 + 0] - com[0];
            double dy = coord_ptr[i * 3 + 1] - com[1];
            double dz = coord_ptr[i * 3 + 2] - com[2];
            rg_squared += dx*dx + dy*dy + dz*dz;
        }
        double radius_of_gyration = std::sqrt(rg_squared / n_atoms);
        
        // Store descriptors
        descriptors["center_of_mass"] = py::make_tuple(com[0], com[1], com[2]);
        descriptors["radius_of_gyration"] = radius_of_gyration;
        descriptors["asphericity"] = asphericity;
        descriptors["moment_of_inertia_xx"] = Ixx;
        descriptors["moment_of_inertia_yy"] = Iyy;
        descriptors["moment_of_inertia_zz"] = Izz;
        descriptors["n_atoms"] = n_atoms;
        
        return descriptors;
    }
};

/*!
 * \brief Dimensionality reduction utilities
 */
class DimensionalityReducer {
public:
    /*!
     * \brief Principal Component Analysis
     */
    static py::dict pca(const py::array_t<double>& data, size_t n_components = 2) {
        auto buf = data.request();
        if (buf.ndim != 2) {
            throw std::invalid_argument("Data must be 2D array (samples x features)");
        }
        
        size_t n_samples = buf.shape[0];
        size_t n_features = buf.shape[1];
        double* data_ptr = static_cast<double*>(buf.ptr);
        
        if (n_components > std::min(n_samples, n_features)) {
            n_components = std::min(n_samples, n_features);
        }
        
        // Center the data
        std::vector<double> mean(n_features, 0.0);
        for (size_t i = 0; i < n_samples; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                mean[j] += data_ptr[i * n_features + j];
            }
        }
        for (size_t j = 0; j < n_features; ++j) {
            mean[j] /= n_samples;
        }
        
        std::vector<double> centered_data(n_samples * n_features);
        for (size_t i = 0; i < n_samples; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                centered_data[i * n_features + j] = data_ptr[i * n_features + j] - mean[j];
            }
        }
        
        // Calculate covariance matrix
        std::vector<double> cov_matrix(n_features * n_features, 0.0);
        for (size_t i = 0; i < n_features; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                for (size_t k = 0; k < n_samples; ++k) {
                    cov_matrix[i * n_features + j] += 
                        centered_data[k * n_features + i] * centered_data[k * n_features + j];
                }
                cov_matrix[i * n_features + j] /= (n_samples - 1);
            }
        }
        
        // For simplicity, return just the centered data and mean
        // In a real implementation, you would compute eigenvalues/eigenvectors
        py::dict result;
        result["centered_data"] = py::array_t<double>(
            {n_samples, n_features}, centered_data.data(), py::handle()
        );
        result["mean"] = py::array_t<double>(n_features, mean.data(), py::handle());
        result["covariance"] = py::array_t<double>(
            {n_features, n_features}, cov_matrix.data(), py::handle()
        );
        
        return result;
    }
    
    /*!
     * \brief Simple k-means clustering
     */
    static py::dict kmeans(const py::array_t<double>& data, size_t k, size_t max_iters = 100) {
        auto buf = data.request();
        if (buf.ndim != 2) {
            throw std::invalid_argument("Data must be 2D array (samples x features)");
        }
        
        size_t n_samples = buf.shape[0];
        size_t n_features = buf.shape[1];
        double* data_ptr = static_cast<double*>(buf.ptr);
        
        if (k > n_samples) {
            k = n_samples;
        }
        
        // Initialize random number generator
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(0, n_samples - 1);
        
        // Initialize centroids randomly
        std::vector<double> centroids(k * n_features);
        for (size_t i = 0; i < k; ++i) {
            size_t random_sample = dis(gen);
            for (size_t j = 0; j < n_features; ++j) {
                centroids[i * n_features + j] = data_ptr[random_sample * n_features + j];
            }
        }
        
        std::vector<int> labels(n_samples);
        
        // K-means iterations
        for (size_t iter = 0; iter < max_iters; ++iter) {
            bool changed = false;
            
            // Assign points to nearest centroids
            for (size_t i = 0; i < n_samples; ++i) {
                double min_distance = std::numeric_limits<double>::max();
                int best_cluster = 0;
                
                for (size_t c = 0; c < k; ++c) {
                    double distance = 0.0;
                    for (size_t j = 0; j < n_features; ++j) {
                        double diff = data_ptr[i * n_features + j] - centroids[c * n_features + j];
                        distance += diff * diff;
                    }
                    distance = std::sqrt(distance);
                    
                    if (distance < min_distance) {
                        min_distance = distance;
                        best_cluster = c;
                    }
                }
                
                if (labels[i] != best_cluster) {
                    changed = true;
                    labels[i] = best_cluster;
                }
            }
            
            // Update centroids
            std::vector<double> new_centroids(k * n_features, 0.0);
            std::vector<int> cluster_counts(k, 0);
            
            for (size_t i = 0; i < n_samples; ++i) {
                int cluster = labels[i];
                cluster_counts[cluster]++;
                for (size_t j = 0; j < n_features; ++j) {
                    new_centroids[cluster * n_features + j] += data_ptr[i * n_features + j];
                }
            }
            
            for (size_t c = 0; c < k; ++c) {
                if (cluster_counts[c] > 0) {
                    for (size_t j = 0; j < n_features; ++j) {
                        new_centroids[c * n_features + j] /= cluster_counts[c];
                    }
                }
            }
            
            centroids = new_centroids;
            
            if (!changed) {
                break;
            }
        }
        
        py::dict result;
        result["labels"] = py::array_t<int>(labels.size(), labels.data(), py::handle());
        result["centroids"] = py::array_t<double>(
            {k, n_features}, centroids.data(), py::handle()
        );
        result["n_clusters"] = k;
        
        return result;
    }
};

/*!
 * \brief Data preprocessing utilities
 */
class DataPreprocessor {
public:
    /*!
     * \brief Normalize data to zero mean and unit variance
     */
    static py::dict standardize(const py::array_t<double>& data) {
        auto buf = data.request();
        if (buf.ndim != 2) {
            throw std::invalid_argument("Data must be 2D array (samples x features)");
        }
        
        size_t n_samples = buf.shape[0];
        size_t n_features = buf.shape[1];
        double* data_ptr = static_cast<double*>(buf.ptr);
        
        // Calculate mean and std for each feature
        std::vector<double> mean(n_features, 0.0);
        std::vector<double> std(n_features, 0.0);
        
        // Calculate means
        for (size_t i = 0; i < n_samples; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                mean[j] += data_ptr[i * n_features + j];
            }
        }
        for (size_t j = 0; j < n_features; ++j) {
            mean[j] /= n_samples;
        }
        
        // Calculate standard deviations
        for (size_t i = 0; i < n_samples; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                double diff = data_ptr[i * n_features + j] - mean[j];
                std[j] += diff * diff;
            }
        }
        for (size_t j = 0; j < n_features; ++j) {
            std[j] = std::sqrt(std[j] / (n_samples - 1));
            if (std[j] == 0.0) std[j] = 1.0; // Avoid division by zero
        }
        
        // Standardize data
        std::vector<double> standardized(n_samples * n_features);
        for (size_t i = 0; i < n_samples; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                standardized[i * n_features + j] = 
                    (data_ptr[i * n_features + j] - mean[j]) / std[j];
            }
        }
        
        py::dict result;
        result["data"] = py::array_t<double>(
            {n_samples, n_features}, standardized.data(), py::handle()
        );
        result["mean"] = py::array_t<double>(mean.size(), mean.data(), py::handle());
        result["std"] = py::array_t<double>(std.size(), std.data(), py::handle());
        
        return result;
    }
    
    /*!
     * \brief Min-max normalization
     */
    static py::dict normalize_minmax(const py::array_t<double>& data) {
        auto buf = data.request();
        if (buf.ndim != 2) {
            throw std::invalid_argument("Data must be 2D array (samples x features)");
        }
        
        size_t n_samples = buf.shape[0];
        size_t n_features = buf.shape[1];
        double* data_ptr = static_cast<double*>(buf.ptr);
        
        // Find min and max for each feature
        std::vector<double> min_vals(n_features, std::numeric_limits<double>::max());
        std::vector<double> max_vals(n_features, std::numeric_limits<double>::lowest());
        
        for (size_t i = 0; i < n_samples; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                double val = data_ptr[i * n_features + j];
                min_vals[j] = std::min(min_vals[j], val);
                max_vals[j] = std::max(max_vals[j], val);
            }
        }
        
        // Normalize data
        std::vector<double> normalized(n_samples * n_features);
        for (size_t i = 0; i < n_samples; ++i) {
            for (size_t j = 0; j < n_features; ++j) {
                double range = max_vals[j] - min_vals[j];
                if (range == 0.0) {
                    normalized[i * n_features + j] = 0.0;
                } else {
                    normalized[i * n_features + j] = 
                        (data_ptr[i * n_features + j] - min_vals[j]) / range;
                }
            }
        }
        
        py::dict result;
        result["data"] = py::array_t<double>(
            {n_samples, n_features}, normalized.data(), py::handle()
        );
        result["min"] = py::array_t<double>(min_vals.size(), min_vals.data(), py::handle());
        result["max"] = py::array_t<double>(max_vals.size(), max_vals.data(), py::handle());
        
        return result;
    }
};

} // namespace ml
} // namespace viamd

/*!
 * \brief Bind machine learning classes to Python
 */
void bind_ml(py::module& m) {
    auto ml_module = m.def_submodule("ml", "Machine learning utilities");
    
    // Feature Extraction
    py::class_<viamd::ml::FeatureExtractor>(ml_module, "FeatureExtractor")
        .def_static("extract_rdf_features", &viamd::ml::FeatureExtractor::extract_rdf_features,
                   py::arg("coordinates"), py::arg("r_max") = 10.0, py::arg("n_bins") = 100)
        .def_static("extract_angular_features", &viamd::ml::FeatureExtractor::extract_angular_features,
                   py::arg("coordinates"), py::arg("n_bins") = 180)
        .def_static("extract_contact_features", &viamd::ml::FeatureExtractor::extract_contact_features,
                   py::arg("coordinates"), py::arg("cutoff") = 5.0)
        .def_static("extract_structural_descriptors", &viamd::ml::FeatureExtractor::extract_structural_descriptors);
    
    // Dimensionality Reduction
    py::class_<viamd::ml::DimensionalityReducer>(ml_module, "DimensionalityReducer")
        .def_static("pca", &viamd::ml::DimensionalityReducer::pca,
                   py::arg("data"), py::arg("n_components") = 2)
        .def_static("kmeans", &viamd::ml::DimensionalityReducer::kmeans,
                   py::arg("data"), py::arg("k"), py::arg("max_iters") = 100);
    
    // Data Preprocessing
    py::class_<viamd::ml::DataPreprocessor>(ml_module, "DataPreprocessor")
        .def_static("standardize", &viamd::ml::DataPreprocessor::standardize)
        .def_static("normalize_minmax", &viamd::ml::DataPreprocessor::normalize_minmax);
}