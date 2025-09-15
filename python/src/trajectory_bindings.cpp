/**
 * @file trajectory_bindings.cpp
 * @brief Trajectory data bindings for time-dependent molecular analysis
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <md_trajectory.h>
#include <md_xtc.h>
#include <md_trr.h>

namespace py = pybind11;

void bind_trajectory(py::module &m) {
    // Create a submodule for trajectory functionality
    auto traj = m.def_submodule("trajectory", "Trajectory data access and manipulation");
    
    // Bind trajectory header
    py::class_<md_trajectory_header_t>(traj, "TrajectoryHeader")
        .def_readonly("num_frames", &md_trajectory_header_t::num_frames)
        .def_readonly("num_atoms", &md_trajectory_header_t::num_atoms)
        .def_readonly("max_frame_data_size", &md_trajectory_header_t::max_frame_data_size)
        .def_property_readonly("frame_times", [](const md_trajectory_header_t& header) -> py::array {
            if (!header.frame_times || header.num_frames == 0) {
                return py::array();
            }
            return py::array_t<double>(header.num_frames, header.frame_times);
        }, "Get frame timestamps as NumPy array");
    
    // Bind trajectory frame header
    py::class_<md_trajectory_frame_header_t>(traj, "FrameHeader")
        .def_readonly("num_atoms", &md_trajectory_frame_header_t::num_atoms)
        .def_readonly("index", &md_trajectory_frame_header_t::index)
        .def_readonly("timestamp", &md_trajectory_frame_header_t::timestamp);
    
    // Bind trajectory interface (simplified wrapper)
    py::class_<md_trajectory_i>(traj, "Trajectory")
        .def("get_header", [](md_trajectory_i* traj_ptr) -> py::object {
            if (!traj_ptr || !traj_ptr->get_header) {
                return py::none();
            }
            md_trajectory_header_t header;
            if (traj_ptr->get_header(traj_ptr->inst, &header)) {
                return py::cast(header);
            }
            return py::none();
        }, "Get trajectory header information")
        .def("load_frame", [](md_trajectory_i* traj_ptr, int64_t frame_idx, size_t num_atoms) -> py::dict {
            py::dict result;
            if (!traj_ptr || !traj_ptr->load_frame) {
                return result;
            }
            
            md_trajectory_frame_header_t frame_header;
            
            // Allocate coordinate arrays
            md_allocator_i* temp_alloc = md_get_heap_allocator();
            size_t coord_size = num_atoms * sizeof(float);
            float* x = (float*)md_alloc(temp_alloc, coord_size);
            float* y = (float*)md_alloc(temp_alloc, coord_size);
            float* z = (float*)md_alloc(temp_alloc, coord_size);
            
            if (!x || !y || !z) {
                if (x) md_free(temp_alloc, x, coord_size);
                if (y) md_free(temp_alloc, y, coord_size);
                if (z) md_free(temp_alloc, z, coord_size);
                return result;
            }
            
            bool success = traj_ptr->load_frame(traj_ptr->inst, frame_idx, &frame_header, x, y, z);
            if (success) {
                result["timestamp"] = frame_header.timestamp;
                result["frame_index"] = frame_header.index;
                
                // Create coordinate array (N, 3)
                std::vector<py::ssize_t> shape = {(py::ssize_t)num_atoms, 3};
                auto coord_array = py::array_t<float>(shape);
                auto buf = coord_array.request();
                float* ptr = static_cast<float*>(buf.ptr);
                
                for (size_t i = 0; i < num_atoms; ++i) {
                    ptr[i * 3 + 0] = x[i];
                    ptr[i * 3 + 1] = y[i];
                    ptr[i * 3 + 2] = z[i];
                }
                
                result["coordinates"] = coord_array;
            }
            
            md_free(temp_alloc, x, coord_size);
            md_free(temp_alloc, y, coord_size);
            md_free(temp_alloc, z, coord_size);
            
            return result;
        }, "Load frame data with coordinates", py::arg("frame_index"), py::arg("num_atoms"));
    
    // Bind trajectory loading functions
    traj.def("load_xtc", [](const std::string& filename) -> py::object {
        md_allocator_i* alloc = md_get_heap_allocator();
        
        md_trajectory_i* traj_ptr = md_xtc_trajectory_create(str_from_cstr(filename.c_str()), alloc, 0);
        if (traj_ptr) {
            return py::cast(traj_ptr, py::return_value_policy::take_ownership);
        }
        return py::none();
    }, "Load trajectory from XTC file", py::arg("filename"));
    
    traj.def("load_trr", [](const std::string& filename) -> py::object {
        md_allocator_i* alloc = md_get_heap_allocator();
        
        md_trajectory_i* traj_ptr = md_trr_trajectory_create(str_from_cstr(filename.c_str()), alloc, 0);
        if (traj_ptr) {
            return py::cast(traj_ptr, py::return_value_policy::take_ownership);
        }
        return py::none();
    }, "Load trajectory from TRR file", py::arg("filename"));
    
    // Utility functions for MDAnalysis integration
    traj.def("extract_frame_data_for_mdanalysis", 
        [](md_trajectory_i* traj_ptr, int64_t frame_idx, size_t num_atoms) -> py::dict {
            py::dict frame_data;
            
            if (!traj_ptr) {
                return frame_data;
            }
            
            // Get frame data using load_frame
            md_trajectory_frame_header_t frame_header;
            
            // Allocate coordinate arrays
            md_allocator_i* temp_alloc = md_get_heap_allocator();
            size_t coord_size = num_atoms * sizeof(float);
            float* x = (float*)md_alloc(temp_alloc, coord_size);
            float* y = (float*)md_alloc(temp_alloc, coord_size);
            float* z = (float*)md_alloc(temp_alloc, coord_size);
            
            if (x && y && z && traj_ptr->load_frame && 
                traj_ptr->load_frame(traj_ptr->inst, frame_idx, &frame_header, x, y, z)) {
                
                frame_data["timestamp"] = frame_header.timestamp;
                frame_data["frame_index"] = frame_header.index;
                
                std::vector<py::ssize_t> shape = {(py::ssize_t)num_atoms, 3};
                auto coord_array = py::array_t<float>(shape);
                auto buf = coord_array.request();
                float* ptr = static_cast<float*>(buf.ptr);
                
                for (size_t i = 0; i < num_atoms; ++i) {
                    ptr[i * 3 + 0] = x[i];
                    ptr[i * 3 + 1] = y[i];
                    ptr[i * 3 + 2] = z[i];
                }
                
                frame_data["coordinates"] = coord_array;
            }
            
            if (x) md_free(temp_alloc, x, coord_size);
            if (y) md_free(temp_alloc, y, coord_size);
            if (z) md_free(temp_alloc, z, coord_size);
            
            return frame_data;
        }, "Extract complete frame data suitable for MDAnalysis integration",
           py::arg("trajectory"), py::arg("frame_index"), py::arg("num_atoms"));
}