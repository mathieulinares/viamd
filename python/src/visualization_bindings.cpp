/*!
 * \file visualization_bindings.cpp
 * \brief Visualization and rendering bindings for Python integration
 *
 * This file provides Python bindings for VIAMD's visualization capabilities,
 * including rendering system integration, data export for visualization tools,
 * and plotting utilities.
 */

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <pybind11/functional.h>

#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <fstream>
#include <sstream>
#include <algorithm>

#include "trajectory.h"
#include "molecule.h"

namespace py = pybind11;

namespace viamd {
namespace visualization {

/*!
 * \brief Data export utilities for external visualization tools
 */
class DataExporter {
public:
    /*!
     * \brief Export molecular coordinates to various formats
     */
    static bool export_coordinates(const py::array_t<double>& coordinates,
                                 const std::vector<std::string>& atom_names,
                                 const std::string& filename,
                                 const std::string& format = "xyz") {
        auto coord_buf = coordinates.request();
        if (coord_buf.ndim != 2 || coord_buf.shape[1] != 3) {
            throw std::invalid_argument("Coordinates must be Nx3 array");
        }
        
        size_t n_atoms = coord_buf.shape[0];
        if (atom_names.size() != n_atoms) {
            throw std::invalid_argument("Number of atom names must match coordinates");
        }
        
        double* coord_ptr = static_cast<double*>(coord_buf.ptr);
        
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        if (format == "xyz") {
            file << n_atoms << "\n";
            file << "Exported from VIAMD\n";
            
            for (size_t i = 0; i < n_atoms; ++i) {
                file << atom_names[i] << " "
                     << coord_ptr[i * 3 + 0] << " "
                     << coord_ptr[i * 3 + 1] << " "
                     << coord_ptr[i * 3 + 2] << "\n";
            }
        } else if (format == "pdb") {
            for (size_t i = 0; i < n_atoms; ++i) {
                file << "ATOM  ";
                file << std::setw(5) << (i + 1);
                file << "  " << std::setw(4) << std::left << atom_names[i];
                file << " MOL A   1    ";
                file << std::setw(8) << std::fixed << std::setprecision(3) << coord_ptr[i * 3 + 0];
                file << std::setw(8) << std::fixed << std::setprecision(3) << coord_ptr[i * 3 + 1];
                file << std::setw(8) << std::fixed << std::setprecision(3) << coord_ptr[i * 3 + 2];
                file << "  1.00 20.00           " << atom_names[i][0] << "\n";
            }
            file << "END\n";
        } else {
            file.close();
            return false;
        }
        
        file.close();
        return true;
    }
    
    /*!
     * \brief Export trajectory data to JSON format
     */
    static bool export_trajectory_json(const std::vector<py::array_t<double>>& trajectory,
                                     const std::vector<std::string>& atom_names,
                                     const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        file << "{\n";
        file << "  \"trajectory\": {\n";
        file << "    \"n_atoms\": " << atom_names.size() << ",\n";
        file << "    \"n_frames\": " << trajectory.size() << ",\n";
        file << "    \"atom_names\": [";
        
        for (size_t i = 0; i < atom_names.size(); ++i) {
            file << "\"" << atom_names[i] << "\"";
            if (i < atom_names.size() - 1) file << ", ";
        }
        file << "],\n";
        
        file << "    \"frames\": [\n";
        for (size_t frame = 0; frame < trajectory.size(); ++frame) {
            auto coord_buf = trajectory[frame].request();
            double* coord_ptr = static_cast<double*>(coord_buf.ptr);
            size_t n_atoms = coord_buf.shape[0];
            
            file << "      {\n";
            file << "        \"frame_id\": " << frame << ",\n";
            file << "        \"coordinates\": [\n";
            
            for (size_t i = 0; i < n_atoms; ++i) {
                file << "          [" 
                     << coord_ptr[i * 3 + 0] << ", "
                     << coord_ptr[i * 3 + 1] << ", "
                     << coord_ptr[i * 3 + 2] << "]";
                if (i < n_atoms - 1) file << ",";
                file << "\n";
            }
            
            file << "        ]\n";
            file << "      }";
            if (frame < trajectory.size() - 1) file << ",";
            file << "\n";
        }
        file << "    ]\n";
        file << "  }\n";
        file << "}\n";
        
        file.close();
        return true;
    }
    
    /*!
     * \brief Export analysis data to CSV format
     */
    static bool export_analysis_csv(const py::dict& data,
                                  const std::string& filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            return false;
        }
        
        // Extract column names and data
        std::vector<std::string> columns;
        std::vector<std::vector<double>> column_data;
        
        for (auto& item : data) {
            std::string key = py::str(item.first);
            columns.push_back(key);
            
            py::array_t<double> array = item.second.cast<py::array_t<double>>();
            auto buf = array.request();
            double* ptr = static_cast<double*>(buf.ptr);
            
            std::vector<double> values(ptr, ptr + buf.size);
            column_data.push_back(values);
        }
        
        if (columns.empty()) {
            file.close();
            return false;
        }
        
        // Write header
        for (size_t i = 0; i < columns.size(); ++i) {
            file << columns[i];
            if (i < columns.size() - 1) file << ",";
        }
        file << "\n";
        
        // Write data
        size_t n_rows = column_data[0].size();
        for (size_t row = 0; row < n_rows; ++row) {
            for (size_t col = 0; col < column_data.size(); ++col) {
                if (row < column_data[col].size()) {
                    file << column_data[col][row];
                }
                if (col < column_data.size() - 1) file << ",";
            }
            file << "\n";
        }
        
        file.close();
        return true;
    }
    
    /*!
     * \brief Export data for Matplotlib visualization
     */
    static py::dict prepare_matplotlib_data(const py::dict& analysis_data) {
        py::dict result;
        
        for (auto& item : analysis_data) {
            std::string key = py::str(item.first);
            
            try {
                py::array_t<double> array = item.second.cast<py::array_t<double>>();
                auto buf = array.request();
                
                // Convert to Python list for matplotlib compatibility
                py::list values;
                double* ptr = static_cast<double*>(buf.ptr);
                for (size_t i = 0; i < buf.size; ++i) {
                    values.append(ptr[i]);
                }
                
                result[key.c_str()] = values;
            } catch (const py::cast_error&) {
                // Keep non-array data as is
                result[key.c_str()] = item.second;
            }
        }
        
        return result;
    }
};

/*!
 * \brief Real-time plotting utilities
 */
class RealTimePlotter {
private:
    std::unordered_map<std::string, std::vector<double>> plot_data_;
    std::unordered_map<std::string, py::object> plot_callbacks_;
    size_t max_points_;
    
public:
    RealTimePlotter(size_t max_points = 1000) : max_points_(max_points) {}
    
    /*!
     * \brief Add data point to a plot series
     */
    void add_data_point(const std::string& series_name, double value) {
        auto& data = plot_data_[series_name];
        data.push_back(value);
        
        // Keep only recent points
        if (data.size() > max_points_) {
            data.erase(data.begin(), data.begin() + (data.size() - max_points_));
        }
        
        // Call plot callback if registered
        auto it = plot_callbacks_.find(series_name);
        if (it != plot_callbacks_.end()) {
            try {
                py::list py_data;
                for (double val : data) {
                    py_data.append(val);
                }
                it->second(py_data);
            } catch (const std::exception& e) {
                py::print("Error in plot callback:", e.what());
            }
        }
    }
    
    /*!
     * \brief Register a plot update callback
     */
    void register_plot_callback(const std::string& series_name, py::object callback) {
        plot_callbacks_[series_name] = callback;
    }
    
    /*!
     * \brief Get current data for a series
     */
    py::list get_series_data(const std::string& series_name) const {
        py::list result;
        auto it = plot_data_.find(series_name);
        if (it != plot_data_.end()) {
            for (double val : it->second) {
                result.append(val);
            }
        }
        return result;
    }
    
    /*!
     * \brief Get all series names
     */
    std::vector<std::string> get_series_names() const {
        std::vector<std::string> names;
        names.reserve(plot_data_.size());
        for (const auto& item : plot_data_) {
            names.push_back(item.first);
        }
        return names;
    }
    
    /*!
     * \brief Clear data for a series
     */
    void clear_series(const std::string& series_name) {
        plot_data_.erase(series_name);
        plot_callbacks_.erase(series_name);
    }
    
    /*!
     * \brief Clear all data
     */
    void clear_all() {
        plot_data_.clear();
        plot_callbacks_.clear();
    }
    
    /*!
     * \brief Set maximum number of points to keep
     */
    void set_max_points(size_t max_points) {
        max_points_ = max_points;
        
        // Trim existing data
        for (auto& item : plot_data_) {
            auto& data = item.second;
            if (data.size() > max_points_) {
                data.erase(data.begin(), data.begin() + (data.size() - max_points_));
            }
        }
    }
    
    /*!
     * \brief Get all plotting data
     */
    py::dict get_all_data() const {
        py::dict result;
        for (const auto& item : plot_data_) {
            py::list data;
            for (double val : item.second) {
                data.append(val);
            }
            result[item.first.c_str()] = data;
        }
        return result;
    }
};

/*!
 * \brief Color mapping utilities for molecular visualization
 */
class ColorMapper {
public:
    /*!
     * \brief Map scalar values to colors using various colormaps
     */
    static py::array_t<double> scalar_to_rgb(const py::array_t<double>& values,
                                            const std::string& colormap = "viridis",
                                            double vmin = 0.0, double vmax = 1.0) {
        auto buf = values.request();
        size_t n_values = buf.size;
        double* val_ptr = static_cast<double*>(buf.ptr);
        
        auto result = py::array_t<double>({n_values, 3});
        auto res_buf = result.request();
        double* res_ptr = static_cast<double*>(res_buf.ptr);
        
        for (size_t i = 0; i < n_values; ++i) {
            double normalized = (val_ptr[i] - vmin) / (vmax - vmin);
            normalized = std::max(0.0, std::min(1.0, normalized)); // Clamp to [0,1]
            
            double r, g, b;
            if (colormap == "viridis") {
                viridis_colormap(normalized, r, g, b);
            } else if (colormap == "plasma") {
                plasma_colormap(normalized, r, g, b);
            } else if (colormap == "hot") {
                hot_colormap(normalized, r, g, b);
            } else if (colormap == "cool") {
                cool_colormap(normalized, r, g, b);
            } else {
                // Default to grayscale
                r = g = b = normalized;
            }
            
            res_ptr[i * 3 + 0] = r;
            res_ptr[i * 3 + 1] = g;
            res_ptr[i * 3 + 2] = b;
        }
        
        return result;
    }
    
    /*!
     * \brief Get element colors based on CPK convention
     */
    static py::array_t<double> element_colors(const std::vector<std::string>& element_symbols) {
        static const std::unordered_map<std::string, std::vector<double>> cpk_colors = {
            {"H", {1.0, 1.0, 1.0}},    // White
            {"C", {0.2, 0.2, 0.2}},    // Black
            {"N", {0.2, 0.2, 1.0}},    // Blue
            {"O", {1.0, 0.2, 0.2}},    // Red
            {"S", {1.0, 1.0, 0.2}},    // Yellow
            {"P", {1.0, 0.5, 0.0}},    // Orange
            {"F", {0.2, 1.0, 0.2}},    // Green
            {"Cl", {0.2, 1.0, 0.2}},   // Green
            {"Br", {0.6, 0.2, 0.2}},   // Dark red
            {"I", {0.4, 0.0, 0.4}},    // Purple
            {"Na", {0.0, 0.0, 1.0}},   // Blue
            {"Mg", {0.0, 0.5, 0.0}},   // Dark green
            {"Al", {0.7, 0.7, 0.7}},   // Gray
            {"Si", {0.9, 0.8, 0.6}},   // Tan
            {"K", {0.5, 0.0, 0.5}},    // Purple
            {"Ca", {0.0, 0.8, 0.0}},   // Green
            {"Fe", {1.0, 0.5, 0.0}},   // Orange
            {"Cu", {1.0, 0.5, 0.0}},   // Orange
            {"Zn", {0.5, 0.5, 0.5}}    // Gray
        };
        
        auto result = py::array_t<double>({element_symbols.size(), 3});
        auto buf = result.request();
        double* ptr = static_cast<double*>(buf.ptr);
        
        for (size_t i = 0; i < element_symbols.size(); ++i) {
            std::string element = element_symbols[i];
            
            auto it = cpk_colors.find(element);
            if (it != cpk_colors.end()) {
                const auto& color = it->second;
                ptr[i * 3 + 0] = color[0];
                ptr[i * 3 + 1] = color[1];
                ptr[i * 3 + 2] = color[2];
            } else {
                // Default to magenta for unknown elements
                ptr[i * 3 + 0] = 1.0;
                ptr[i * 3 + 1] = 0.0;
                ptr[i * 3 + 2] = 1.0;
            }
        }
        
        return result;
    }

private:
    /*!
     * \brief Viridis colormap implementation
     */
    static void viridis_colormap(double t, double& r, double& g, double& b) {
        // Simplified viridis colormap approximation
        r = 0.267004 + t * (0.127568 + t * (-0.024876 + t * 0.579829));
        g = 0.004874 + t * (0.464034 + t * (0.513925 + t * 0.015556));
        b = 0.329415 + t * (0.751663 + t * (-0.486213 + t * 0.215770));
        
        // Clamp values
        r = std::max(0.0, std::min(1.0, r));
        g = std::max(0.0, std::min(1.0, g));
        b = std::max(0.0, std::min(1.0, b));
    }
    
    /*!
     * \brief Plasma colormap implementation
     */
    static void plasma_colormap(double t, double& r, double& g, double& b) {
        // Simplified plasma colormap approximation
        r = 0.050383 + t * (0.508010 + t * (0.434154 + t * 0.025556));
        g = 0.029873 + t * (0.000396 + t * (1.336863 + t * -0.328552));
        b = 0.527975 + t * (0.612328 + t * (-1.088060 + t * 0.043675));
        
        // Clamp values
        r = std::max(0.0, std::min(1.0, r));
        g = std::max(0.0, std::min(1.0, g));
        b = std::max(0.0, std::min(1.0, b));
    }
    
    /*!
     * \brief Hot colormap implementation
     */
    static void hot_colormap(double t, double& r, double& g, double& b) {
        if (t < 1.0/3.0) {
            r = 3.0 * t;
            g = 0.0;
            b = 0.0;
        } else if (t < 2.0/3.0) {
            r = 1.0;
            g = 3.0 * (t - 1.0/3.0);
            b = 0.0;
        } else {
            r = 1.0;
            g = 1.0;
            b = 3.0 * (t - 2.0/3.0);
        }
    }
    
    /*!
     * \brief Cool colormap implementation
     */
    static void cool_colormap(double t, double& r, double& g, double& b) {
        r = t;
        g = 1.0 - t;
        b = 1.0;
    }
};

/*!
 * \brief Mesh generation utilities for molecular surfaces
 */
class MeshGenerator {
public:
    /*!
     * \brief Generate a simple sphere mesh for atoms
     */
    static py::dict generate_sphere_mesh(double radius = 1.0, int resolution = 20) {
        std::vector<double> vertices;
        std::vector<int> faces;
        
        // Generate vertices
        for (int i = 0; i <= resolution; ++i) {
            double phi = M_PI * i / resolution;
            for (int j = 0; j <= resolution; ++j) {
                double theta = 2.0 * M_PI * j / resolution;
                
                double x = radius * std::sin(phi) * std::cos(theta);
                double y = radius * std::sin(phi) * std::sin(theta);
                double z = radius * std::cos(phi);
                
                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
            }
        }
        
        // Generate faces
        for (int i = 0; i < resolution; ++i) {
            for (int j = 0; j < resolution; ++j) {
                int v1 = i * (resolution + 1) + j;
                int v2 = v1 + 1;
                int v3 = (i + 1) * (resolution + 1) + j;
                int v4 = v3 + 1;
                
                // First triangle
                faces.push_back(v1);
                faces.push_back(v2);
                faces.push_back(v3);
                
                // Second triangle
                faces.push_back(v2);
                faces.push_back(v4);
                faces.push_back(v3);
            }
        }
        
        py::dict result;
        result["vertices"] = py::array_t<double>(vertices.size(), vertices.data(), py::handle());
        result["faces"] = py::array_t<int>(faces.size(), faces.data(), py::handle());
        result["n_vertices"] = vertices.size() / 3;
        result["n_faces"] = faces.size() / 3;
        
        return result;
    }
    
    /*!
     * \brief Generate cylindrical mesh for bonds
     */
    static py::dict generate_cylinder_mesh(const py::array_t<double>& start_pos,
                                         const py::array_t<double>& end_pos,
                                         double radius = 0.1, int resolution = 12) {
        auto start_buf = start_pos.request();
        auto end_buf = end_pos.request();
        
        if (start_buf.size != 3 || end_buf.size != 3) {
            throw std::invalid_argument("Start and end positions must be 3D");
        }
        
        double* start_ptr = static_cast<double*>(start_buf.ptr);
        double* end_ptr = static_cast<double*>(end_buf.ptr);
        
        // Calculate cylinder direction and length
        double dx = end_ptr[0] - start_ptr[0];
        double dy = end_ptr[1] - start_ptr[1];
        double dz = end_ptr[2] - start_ptr[2];
        double length = std::sqrt(dx*dx + dy*dy + dz*dz);
        
        if (length == 0.0) {
            throw std::invalid_argument("Start and end positions cannot be the same");
        }
        
        // Normalize direction
        dx /= length;
        dy /= length;
        dz /= length;
        
        // Find perpendicular vectors
        double px, py, pz; // First perpendicular
        if (std::abs(dx) < 0.9) {
            px = 0; py = dz; pz = -dy;
        } else {
            px = -dz; py = 0; pz = dx;
        }
        
        // Normalize
        double p_len = std::sqrt(px*px + py*py + pz*pz);
        px /= p_len; py /= p_len; pz /= p_len;
        
        // Second perpendicular (cross product)
        double qx = dy*pz - dz*py;
        double qy = dz*px - dx*pz;
        double qz = dx*py - dy*px;
        
        std::vector<double> vertices;
        std::vector<int> faces;
        
        // Generate vertices for both ends
        for (int end = 0; end < 2; ++end) {
            double cx = start_ptr[0] + end * dx * length;
            double cy = start_ptr[1] + end * dy * length;
            double cz = start_ptr[2] + end * dz * length;
            
            for (int i = 0; i < resolution; ++i) {
                double angle = 2.0 * M_PI * i / resolution;
                double cos_a = std::cos(angle);
                double sin_a = std::sin(angle);
                
                double x = cx + radius * (cos_a * px + sin_a * qx);
                double y = cy + radius * (cos_a * py + sin_a * qy);
                double z = cz + radius * (cos_a * pz + sin_a * qz);
                
                vertices.push_back(x);
                vertices.push_back(y);
                vertices.push_back(z);
            }
        }
        
        // Generate side faces
        for (int i = 0; i < resolution; ++i) {
            int next_i = (i + 1) % resolution;
            
            int v1 = i;
            int v2 = next_i;
            int v3 = i + resolution;
            int v4 = next_i + resolution;
            
            // First triangle
            faces.push_back(v1);
            faces.push_back(v2);
            faces.push_back(v3);
            
            // Second triangle
            faces.push_back(v2);
            faces.push_back(v4);
            faces.push_back(v3);
        }
        
        py::dict result;
        result["vertices"] = py::array_t<double>(vertices.size(), vertices.data(), py::handle());
        result["faces"] = py::array_t<int>(faces.size(), faces.data(), py::handle());
        result["n_vertices"] = vertices.size() / 3;
        result["n_faces"] = faces.size() / 3;
        
        return result;
    }
};

} // namespace visualization
} // namespace viamd

/*!
 * \brief Bind visualization classes to Python
 */
void bind_visualization(py::module& m) {
    auto viz_module = m.def_submodule("visualization", "Visualization and rendering tools");
    
    // Data Export
    py::class_<viamd::visualization::DataExporter>(viz_module, "DataExporter")
        .def_static("export_coordinates", &viamd::visualization::DataExporter::export_coordinates,
                   py::arg("coordinates"), py::arg("atom_names"), py::arg("filename"), py::arg("format") = "xyz")
        .def_static("export_trajectory_json", &viamd::visualization::DataExporter::export_trajectory_json)
        .def_static("export_analysis_csv", &viamd::visualization::DataExporter::export_analysis_csv)
        .def_static("prepare_matplotlib_data", &viamd::visualization::DataExporter::prepare_matplotlib_data);
    
    // Real-time Plotting
    py::class_<viamd::visualization::RealTimePlotter>(viz_module, "RealTimePlotter")
        .def(py::init<>())
        .def(py::init<size_t>())
        .def("add_data_point", &viamd::visualization::RealTimePlotter::add_data_point)
        .def("register_plot_callback", &viamd::visualization::RealTimePlotter::register_plot_callback)
        .def("get_series_data", &viamd::visualization::RealTimePlotter::get_series_data)
        .def("get_series_names", &viamd::visualization::RealTimePlotter::get_series_names)
        .def("clear_series", &viamd::visualization::RealTimePlotter::clear_series)
        .def("clear_all", &viamd::visualization::RealTimePlotter::clear_all)
        .def("set_max_points", &viamd::visualization::RealTimePlotter::set_max_points)
        .def("get_all_data", &viamd::visualization::RealTimePlotter::get_all_data);
    
    // Color Mapping
    py::class_<viamd::visualization::ColorMapper>(viz_module, "ColorMapper")
        .def_static("scalar_to_rgb", &viamd::visualization::ColorMapper::scalar_to_rgb,
                   py::arg("values"), py::arg("colormap") = "viridis", py::arg("vmin") = 0.0, py::arg("vmax") = 1.0)
        .def_static("element_colors", &viamd::visualization::ColorMapper::element_colors);
    
    // Mesh Generation
    py::class_<viamd::visualization::MeshGenerator>(viz_module, "MeshGenerator")
        .def_static("generate_sphere_mesh", &viamd::visualization::MeshGenerator::generate_sphere_mesh,
                   py::arg("radius") = 1.0, py::arg("resolution") = 20)
        .def_static("generate_cylinder_mesh", &viamd::visualization::MeshGenerator::generate_cylinder_mesh,
                   py::arg("start_pos"), py::arg("end_pos"), py::arg("radius") = 0.1, py::arg("resolution") = 12);
}