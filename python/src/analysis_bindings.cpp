/*!
 * \file analysis_bindings.cpp
 * \brief Advanced molecular analysis bindings for Python integration
 *
 * This file provides Python bindings for VIAMD's advanced analysis capabilities,
 * including statistical analysis, data processing utilities, and real-time
 * molecular analysis tools.
 */

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>
#include <pybind11/chrono.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <functional>
#include <memory>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <cmath>

namespace py = pybind11;

namespace viamd {
namespace analysis {

/*!
 * \brief Advanced statistical analysis utilities
 */
class StatisticalAnalyzer {
public:
    /*!
     * \brief Calculate basic statistics for a data series
     */
    struct Statistics {
        double mean;
        double std_dev;
        double min_val;
        double max_val;
        double median;
        size_t count;
        
        Statistics() : mean(0), std_dev(0), min_val(0), max_val(0), median(0), count(0) {}
    };
    
    /*!
     * \brief Calculate statistics for numpy array
     */
    static Statistics calculate_statistics(py::array_t<double> data) {
        auto buf = data.request();
        double* ptr = static_cast<double*>(buf.ptr);
        size_t size = buf.size;
        
        Statistics stats;
        stats.count = size;
        
        if (size == 0) return stats;
        
        // Create vector for sorting (median calculation)
        std::vector<double> values(ptr, ptr + size);
        
        // Calculate mean
        stats.mean = std::accumulate(values.begin(), values.end(), 0.0) / size;
        
        // Calculate standard deviation
        double variance = 0.0;
        for (const auto& val : values) {
            variance += (val - stats.mean) * (val - stats.mean);
        }
        stats.std_dev = std::sqrt(variance / size);
        
        // Min/Max
        auto minmax = std::minmax_element(values.begin(), values.end());
        stats.min_val = *minmax.first;
        stats.max_val = *minmax.second;
        
        // Median
        std::sort(values.begin(), values.end());
        if (size % 2 == 0) {
            stats.median = (values[size/2 - 1] + values[size/2]) / 2.0;
        } else {
            stats.median = values[size/2];
        }
        
        return stats;
    }
    
    /*!
     * \brief Calculate rolling statistics over time series
     */
    static std::vector<Statistics> rolling_statistics(py::array_t<double> data, size_t window_size) {
        auto buf = data.request();
        double* ptr = static_cast<double*>(buf.ptr);
        size_t size = buf.size;
        
        std::vector<Statistics> results;
        
        for (size_t i = 0; i <= size - window_size; ++i) {
            py::array_t<double> window = py::array_t<double>(
                window_size, ptr + i, py::handle()
            );
            results.push_back(calculate_statistics(window));
        }
        
        return results;
    }
    
    /*!
     * \brief Calculate correlation coefficient between two data series
     */
    static double correlation(py::array_t<double> x, py::array_t<double> y) {
        auto x_buf = x.request();
        auto y_buf = y.request();
        
        if (x_buf.size != y_buf.size) {
            throw std::invalid_argument("Arrays must have the same size");
        }
        
        double* x_ptr = static_cast<double*>(x_buf.ptr);
        double* y_ptr = static_cast<double*>(y_buf.ptr);
        size_t size = x_buf.size;
        
        if (size < 2) return 0.0;
        
        // Calculate means
        double x_mean = 0.0, y_mean = 0.0;
        for (size_t i = 0; i < size; ++i) {
            x_mean += x_ptr[i];
            y_mean += y_ptr[i];
        }
        x_mean /= size;
        y_mean /= size;
        
        // Calculate correlation
        double numerator = 0.0, x_var = 0.0, y_var = 0.0;
        for (size_t i = 0; i < size; ++i) {
            double x_diff = x_ptr[i] - x_mean;
            double y_diff = y_ptr[i] - y_mean;
            numerator += x_diff * y_diff;
            x_var += x_diff * x_diff;
            y_var += y_diff * y_diff;
        }
        
        if (x_var == 0.0 || y_var == 0.0) return 0.0;
        
        return numerator / std::sqrt(x_var * y_var);
    }
    
    /*!
     * \brief Calculate autocorrelation function
     */
    static py::array_t<double> autocorrelation(py::array_t<double> data, size_t max_lag) {
        auto buf = data.request();
        double* ptr = static_cast<double*>(buf.ptr);
        size_t size = buf.size;
        
        if (max_lag >= size) max_lag = size - 1;
        
        auto result = py::array_t<double>(max_lag + 1);
        auto res_buf = result.request();
        double* res_ptr = static_cast<double*>(res_buf.ptr);
        
        // Calculate mean
        double mean = std::accumulate(ptr, ptr + size, 0.0) / size;
        
        // Calculate variance
        double variance = 0.0;
        for (size_t i = 0; i < size; ++i) {
            variance += (ptr[i] - mean) * (ptr[i] - mean);
        }
        variance /= size;
        
        // Calculate autocorrelation for each lag
        for (size_t lag = 0; lag <= max_lag; ++lag) {
            double covariance = 0.0;
            for (size_t i = 0; i < size - lag; ++i) {
                covariance += (ptr[i] - mean) * (ptr[i + lag] - mean);
            }
            covariance /= (size - lag);
            res_ptr[lag] = covariance / variance;
        }
        
        return result;
    }
};

/*!
 * \brief Molecular geometry analysis tools
 */
class GeometryAnalyzer {
public:
    /*!
     * \brief Calculate distance matrix for a set of coordinates
     */
    static py::array_t<double> distance_matrix(py::array_t<double> coordinates) {
        auto buf = coordinates.request();
        
        if (buf.ndim != 2 || buf.shape[1] != 3) {
            throw std::invalid_argument("Coordinates must be Nx3 array");
        }
        
        size_t n_atoms = buf.shape[0];
        double* coord_ptr = static_cast<double*>(buf.ptr);
        
        auto result = py::array_t<double>({n_atoms, n_atoms});
        auto res_buf = result.request();
        double* res_ptr = static_cast<double*>(res_buf.ptr);
        
        for (size_t i = 0; i < n_atoms; ++i) {
            for (size_t j = 0; j < n_atoms; ++j) {
                if (i == j) {
                    res_ptr[i * n_atoms + j] = 0.0;
                } else {
                    double dx = coord_ptr[i * 3 + 0] - coord_ptr[j * 3 + 0];
                    double dy = coord_ptr[i * 3 + 1] - coord_ptr[j * 3 + 1];
                    double dz = coord_ptr[i * 3 + 2] - coord_ptr[j * 3 + 2];
                    res_ptr[i * n_atoms + j] = std::sqrt(dx*dx + dy*dy + dz*dz);
                }
            }
        }
        
        return result;
    }
    
    /*!
     * \brief Calculate angle between three atoms
     */
    static double calculate_angle(py::array_t<double> atom1, py::array_t<double> atom2, py::array_t<double> atom3) {
        auto buf1 = atom1.request();
        auto buf2 = atom2.request();
        auto buf3 = atom3.request();
        
        if (buf1.size != 3 || buf2.size != 3 || buf3.size != 3) {
            throw std::invalid_argument("Each atom coordinate must be 3D");
        }
        
        double* p1 = static_cast<double*>(buf1.ptr);
        double* p2 = static_cast<double*>(buf2.ptr);
        double* p3 = static_cast<double*>(buf3.ptr);
        
        // Vectors from center atom to other atoms
        double v1[3] = {p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2]};
        double v2[3] = {p3[0] - p2[0], p3[1] - p2[1], p3[2] - p2[2]};
        
        // Calculate magnitudes
        double mag1 = std::sqrt(v1[0]*v1[0] + v1[1]*v1[1] + v1[2]*v1[2]);
        double mag2 = std::sqrt(v2[0]*v2[0] + v2[1]*v2[1] + v2[2]*v2[2]);
        
        if (mag1 == 0.0 || mag2 == 0.0) return 0.0;
        
        // Calculate dot product
        double dot = v1[0]*v2[0] + v1[1]*v2[1] + v1[2]*v2[2];
        
        // Calculate angle
        double cos_angle = dot / (mag1 * mag2);
        cos_angle = std::max(-1.0, std::min(1.0, cos_angle)); // Clamp to valid range
        
        return std::acos(cos_angle) * 180.0 / M_PI; // Return in degrees
    }
    
    /*!
     * \brief Calculate dihedral angle between four atoms
     */
    static double calculate_dihedral(py::array_t<double> atom1, py::array_t<double> atom2,
                                   py::array_t<double> atom3, py::array_t<double> atom4) {
        auto buf1 = atom1.request();
        auto buf2 = atom2.request();
        auto buf3 = atom3.request();
        auto buf4 = atom4.request();
        
        if (buf1.size != 3 || buf2.size != 3 || buf3.size != 3 || buf4.size != 3) {
            throw std::invalid_argument("Each atom coordinate must be 3D");
        }
        
        double* p1 = static_cast<double*>(buf1.ptr);
        double* p2 = static_cast<double*>(buf2.ptr);
        double* p3 = static_cast<double*>(buf3.ptr);
        double* p4 = static_cast<double*>(buf4.ptr);
        
        // Vectors
        double v1[3] = {p2[0] - p1[0], p2[1] - p1[1], p2[2] - p1[2]};
        double v2[3] = {p3[0] - p2[0], p3[1] - p2[1], p3[2] - p2[2]};
        double v3[3] = {p4[0] - p3[0], p4[1] - p3[1], p4[2] - p3[2]};
        
        // Cross products
        double n1[3] = {v1[1]*v2[2] - v1[2]*v2[1], v1[2]*v2[0] - v1[0]*v2[2], v1[0]*v2[1] - v1[1]*v2[0]};
        double n2[3] = {v2[1]*v3[2] - v2[2]*v3[1], v2[2]*v3[0] - v2[0]*v3[2], v2[0]*v3[1] - v2[1]*v3[0]};
        
        // Magnitudes
        double mag1 = std::sqrt(n1[0]*n1[0] + n1[1]*n1[1] + n1[2]*n1[2]);
        double mag2 = std::sqrt(n2[0]*n2[0] + n2[1]*n2[1] + n2[2]*n2[2]);
        
        if (mag1 == 0.0 || mag2 == 0.0) return 0.0;
        
        // Normalize
        for (int i = 0; i < 3; ++i) {
            n1[i] /= mag1;
            n2[i] /= mag2;
        }
        
        // Dot product
        double dot = n1[0]*n2[0] + n1[1]*n2[1] + n1[2]*n2[2];
        dot = std::max(-1.0, std::min(1.0, dot));
        
        // Cross product for sign
        double cross[3] = {n1[1]*n2[2] - n1[2]*n2[1], n1[2]*n2[0] - n1[0]*n2[2], n1[0]*n2[1] - n1[1]*n2[0]};
        double sign = (cross[0]*v2[0] + cross[1]*v2[1] + cross[2]*v2[2]) >= 0 ? 1.0 : -1.0;
        
        return sign * std::acos(dot) * 180.0 / M_PI; // Return in degrees
    }
    
    /*!
     * \brief Calculate radius of gyration for a molecule
     */
    static double radius_of_gyration(py::array_t<double> coordinates, py::array_t<double> masses) {
        auto coord_buf = coordinates.request();
        auto mass_buf = masses.request();
        
        if (coord_buf.ndim != 2 || coord_buf.shape[1] != 3) {
            throw std::invalid_argument("Coordinates must be Nx3 array");
        }
        
        size_t n_atoms = coord_buf.shape[0];
        if (mass_buf.size != n_atoms) {
            throw std::invalid_argument("Mass array size must match number of atoms");
        }
        
        double* coord_ptr = static_cast<double*>(coord_buf.ptr);
        double* mass_ptr = static_cast<double*>(mass_buf.ptr);
        
        // Calculate center of mass
        double com[3] = {0.0, 0.0, 0.0};
        double total_mass = 0.0;
        
        for (size_t i = 0; i < n_atoms; ++i) {
            double mass = mass_ptr[i];
            total_mass += mass;
            com[0] += mass * coord_ptr[i * 3 + 0];
            com[1] += mass * coord_ptr[i * 3 + 1];
            com[2] += mass * coord_ptr[i * 3 + 2];
        }
        
        com[0] /= total_mass;
        com[1] /= total_mass;
        com[2] /= total_mass;
        
        // Calculate radius of gyration
        double rg_squared = 0.0;
        for (size_t i = 0; i < n_atoms; ++i) {
            double dx = coord_ptr[i * 3 + 0] - com[0];
            double dy = coord_ptr[i * 3 + 1] - com[1];
            double dz = coord_ptr[i * 3 + 2] - com[2];
            rg_squared += mass_ptr[i] * (dx*dx + dy*dy + dz*dz);
        }
        
        return std::sqrt(rg_squared / total_mass);
    }
};

/*!
 * \brief Real-time molecular data processor
 */
class RealTimeProcessor {
private:
    std::vector<std::function<void(const py::dict&)>> callbacks_;
    std::unordered_map<std::string, py::object> data_buffers_;
    
public:
    /*!
     * \brief Add a processing callback
     */
    void add_callback(const std::string& name, std::function<void(const py::dict&)> callback) {
        callbacks_.push_back(callback);
    }
    
    /*!
     * \brief Process molecular data frame
     */
    void process_frame(const py::dict& frame_data) {
        // Store in buffers
        for (auto& item : frame_data) {
            std::string key = py::str(item.first);
            data_buffers_[key] = py::reinterpret_borrow<py::object>(item.second);
        }
        
        // Call all callbacks
        for (const auto& callback : callbacks_) {
            try {
                callback(frame_data);
            } catch (const std::exception& e) {
                // Log error but continue processing
                py::print("Error in real-time processing callback:", e.what());
            }
        }
    }
    
    /*!
     * \brief Get buffered data
     */
    py::dict get_buffered_data() const {
        py::dict result;
        for (const auto& item : data_buffers_) {
            result[item.first.c_str()] = item.second;
        }
        return result;
    }
    
    /*!
     * \brief Clear data buffers
     */
    void clear_buffers() {
        data_buffers_.clear();
    }
    
    /*!
     * \brief Get buffer keys
     */
    std::vector<std::string> get_buffer_keys() const {
        std::vector<std::string> keys;
        keys.reserve(data_buffers_.size());
        for (const auto& item : data_buffers_) {
            keys.push_back(item.first);
        }
        return keys;
    }
};

/*!
 * \brief Performance monitoring and profiling utilities
 */
class PerformanceMonitor {
private:
    std::unordered_map<std::string, std::chrono::high_resolution_clock::time_point> start_times_;
    std::unordered_map<std::string, std::vector<double>> timing_data_;
    
public:
    /*!
     * \brief Start timing a operation
     */
    void start_timer(const std::string& name) {
        start_times_[name] = std::chrono::high_resolution_clock::now();
    }
    
    /*!
     * \brief Stop timing and record duration
     */
    double stop_timer(const std::string& name) {
        auto end_time = std::chrono::high_resolution_clock::now();
        auto it = start_times_.find(name);
        
        if (it == start_times_.end()) {
            throw std::invalid_argument("Timer '" + name + "' not started");
        }
        
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
            end_time - it->second
        ).count() / 1000.0; // Convert to milliseconds
        
        timing_data_[name].push_back(duration);
        start_times_.erase(it);
        
        return duration;
    }
    
    /*!
     * \brief Get timing statistics for an operation
     */
    StatisticalAnalyzer::Statistics get_timing_stats(const std::string& name) const {
        auto it = timing_data_.find(name);
        if (it == timing_data_.end()) {
            return StatisticalAnalyzer::Statistics();
        }
        
        const auto& timings = it->second;
        auto data = py::array_t<double>(timings.size(), timings.data(), py::handle());
        return StatisticalAnalyzer::calculate_statistics(data);
    }
    
    /*!
     * \brief Get all timing data
     */
    py::dict get_all_timings() const {
        py::dict result;
        for (const auto& item : timing_data_) {
            py::list timing_list;
            for (double timing : item.second) {
                timing_list.append(timing);
            }
            result[item.first.c_str()] = timing_list;
        }
        return result;
    }
    
    /*!
     * \brief Clear timing data
     */
    void clear_timings() {
        timing_data_.clear();
        start_times_.clear();
    }
    
    /*!
     * \brief Get operation names
     */
    std::vector<std::string> get_operation_names() const {
        std::vector<std::string> names;
        names.reserve(timing_data_.size());
        for (const auto& item : timing_data_) {
            names.push_back(item.first);
        }
        return names;
    }
};

} // namespace analysis
} // namespace viamd

/*!
 * \brief Bind analysis classes to Python
 */
void bind_analysis(py::module& m) {
    auto analysis_module = m.def_submodule("analysis", "Advanced molecular analysis tools");
    
    // Statistical Analysis
    py::class_<viamd::analysis::StatisticalAnalyzer::Statistics>(analysis_module, "Statistics")
        .def(py::init<>())
        .def_readwrite("mean", &viamd::analysis::StatisticalAnalyzer::Statistics::mean)
        .def_readwrite("std_dev", &viamd::analysis::StatisticalAnalyzer::Statistics::std_dev)
        .def_readwrite("min_val", &viamd::analysis::StatisticalAnalyzer::Statistics::min_val)
        .def_readwrite("max_val", &viamd::analysis::StatisticalAnalyzer::Statistics::max_val)
        .def_readwrite("median", &viamd::analysis::StatisticalAnalyzer::Statistics::median)
        .def_readwrite("count", &viamd::analysis::StatisticalAnalyzer::Statistics::count)
        .def("__repr__", [](const viamd::analysis::StatisticalAnalyzer::Statistics& s) {
            return "Statistics(mean=" + std::to_string(s.mean) + 
                   ", std_dev=" + std::to_string(s.std_dev) + 
                   ", count=" + std::to_string(s.count) + ")";
        });
    
    py::class_<viamd::analysis::StatisticalAnalyzer>(analysis_module, "StatisticalAnalyzer")
        .def_static("calculate_statistics", &viamd::analysis::StatisticalAnalyzer::calculate_statistics)
        .def_static("rolling_statistics", &viamd::analysis::StatisticalAnalyzer::rolling_statistics)
        .def_static("correlation", &viamd::analysis::StatisticalAnalyzer::correlation)
        .def_static("autocorrelation", &viamd::analysis::StatisticalAnalyzer::autocorrelation);
    
    // Geometry Analysis
    py::class_<viamd::analysis::GeometryAnalyzer>(analysis_module, "GeometryAnalyzer")
        .def_static("distance_matrix", &viamd::analysis::GeometryAnalyzer::distance_matrix)
        .def_static("calculate_angle", &viamd::analysis::GeometryAnalyzer::calculate_angle)
        .def_static("calculate_dihedral", &viamd::analysis::GeometryAnalyzer::calculate_dihedral)
        .def_static("radius_of_gyration", &viamd::analysis::GeometryAnalyzer::radius_of_gyration);
    
    // Real-time Processing
    py::class_<viamd::analysis::RealTimeProcessor>(analysis_module, "RealTimeProcessor")
        .def(py::init<>())
        .def("add_callback", &viamd::analysis::RealTimeProcessor::add_callback)
        .def("process_frame", &viamd::analysis::RealTimeProcessor::process_frame)
        .def("get_buffered_data", &viamd::analysis::RealTimeProcessor::get_buffered_data)
        .def("clear_buffers", &viamd::analysis::RealTimeProcessor::clear_buffers)
        .def("get_buffer_keys", &viamd::analysis::RealTimeProcessor::get_buffer_keys);
    
    // Performance Monitoring
    py::class_<viamd::analysis::PerformanceMonitor>(analysis_module, "PerformanceMonitor")
        .def(py::init<>())
        .def("start_timer", &viamd::analysis::PerformanceMonitor::start_timer)
        .def("stop_timer", &viamd::analysis::PerformanceMonitor::stop_timer)
        .def("get_timing_stats", &viamd::analysis::PerformanceMonitor::get_timing_stats)
        .def("get_all_timings", &viamd::analysis::PerformanceMonitor::get_all_timings)
        .def("clear_timings", &viamd::analysis::PerformanceMonitor::clear_timings)
        .def("get_operation_names", &viamd::analysis::PerformanceMonitor::get_operation_names);
}