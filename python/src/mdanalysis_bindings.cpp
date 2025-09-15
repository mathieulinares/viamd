/**
 * @file mdanalysis_bindings.cpp
 * @brief PyBind11 bindings for VIAMD-MDAnalysis integration
 * 
 * This module provides C++ level bindings for efficient data conversion
 * between VIAMD and MDAnalysis, enabling high-performance molecular analysis
 * workflows.
 */

#include <pybind11/pybind11.h>
#include <pybind11/numpy.h>
#include <pybind11/stl.h>
#include <memory>
#include <vector>
#include <string>
#include <unordered_map>
#include <set>

namespace py = pybind11;

/**
 * @brief Interface for converting VIAMD molecular data to MDAnalysis format
 * 
 * Provides efficient, zero-copy access to VIAMD molecular structures
 * with automatic conversion to MDAnalysis-compatible data structures.
 */
class VIAMDMDAnalysisInterface {
public:
    VIAMDMDAnalysisInterface() = default;
    ~VIAMDMDAnalysisInterface() = default;

    /**
     * @brief Create interface from molecular data
     * @param coordinates Atomic coordinates in Angstroms (N×3 array)
     * @param elements Element symbols for each atom
     * @param residue_names Residue names for each atom
     * @param residue_ids Residue IDs for each atom
     * @param chain_ids Chain identifiers for each atom
     * @param atom_names Atom names for each atom
     */
    void initialize_from_arrays(
        py::array_t<double> coordinates,
        const std::vector<std::string>& elements,
        const std::vector<std::string>& residue_names,
        const std::vector<int>& residue_ids,
        const std::vector<std::string>& chain_ids,
        const std::vector<std::string>& atom_names
    ) {
        auto coords_buf = coordinates.request();
        if (coords_buf.ndim != 2 || coords_buf.shape[1] != 3) {
            throw std::runtime_error("Coordinates must be N×3 array");
        }
        
        n_atoms_ = coords_buf.shape[0];
        
        // Store references to coordinate data (zero-copy)
        coordinates_ = coordinates;
        elements_ = elements;
        residue_names_ = residue_names;
        residue_ids_ = residue_ids;
        chain_ids_ = chain_ids;
        atom_names_ = atom_names;
        
        // Validate data consistency
        if (elements_.size() != n_atoms_ || 
            residue_names_.size() != n_atoms_ ||
            residue_ids_.size() != n_atoms_ ||
            chain_ids_.size() != n_atoms_ ||
            atom_names_.size() != n_atoms_) {
            throw std::runtime_error("All atom property arrays must have same length as coordinates");
        }
        
        // Build topology information
        build_topology_info();
    }

    /**
     * @brief Get coordinates as NumPy array (zero-copy access)
     * @return N×3 array of atomic coordinates in Angstroms
     */
    py::array_t<double> get_coordinates() const {
        return coordinates_;
    }

    /**
     * @brief Update coordinates from NumPy array
     * @param new_coordinates N×3 array of new coordinates
     */
    void set_coordinates(py::array_t<double> new_coordinates) {
        auto buf = new_coordinates.request();
        if (buf.ndim != 2 || buf.shape[1] != 3 || buf.shape[0] != n_atoms_) {
            throw std::runtime_error("New coordinates must be N×3 array with correct number of atoms");
        }
        coordinates_ = new_coordinates;
    }

    /**
     * @brief Get MDAnalysis-compatible topology information
     * @return Dictionary with topology data
     */
    py::dict get_topology_dict() const {
        py::dict topology;
        
        topology["n_atoms"] = n_atoms_;
        topology["n_residues"] = n_residues_;
        topology["n_chains"] = n_chains_;
        
        // Atom-level properties
        topology["names"] = atom_names_;
        topology["types"] = elements_;  // Use elements as atom types
        topology["elements"] = elements_;
        topology["masses"] = get_atomic_masses();
        
        // Residue-level properties
        topology["resnames"] = residue_names_;
        topology["resids"] = residue_ids_;
        topology["resnums"] = get_residue_numbers();
        
        // Chain-level properties
        topology["chainIDs"] = chain_ids_;
        topology["segids"] = get_segment_ids();
        
        // Connectivity (bonds) - placeholder for future implementation
        topology["bonds"] = py::list(); // Empty for now
        
        return topology;
    }

    /**
     * @brief Get atomic masses based on element symbols
     * @return Vector of atomic masses in atomic mass units
     */
    std::vector<double> get_atomic_masses() const {
        std::vector<double> masses;
        masses.reserve(n_atoms_);
        
        // Standard atomic masses (simplified mapping)
        static const std::unordered_map<std::string, double> atomic_masses = {
            {"H", 1.008}, {"C", 12.011}, {"N", 14.007}, {"O", 15.999},
            {"P", 30.974}, {"S", 32.065}, {"Ca", 40.078}, {"Mg", 24.305},
            {"Na", 22.990}, {"Cl", 35.453}, {"K", 39.098}, {"Fe", 55.845},
            {"Zn", 65.38}, {"Mn", 54.938}, {"Cu", 63.546}
        };
        
        for (const auto& element : elements_) {
            auto it = atomic_masses.find(element);
            if (it != atomic_masses.end()) {
                masses.push_back(it->second);
            } else {
                masses.push_back(12.011); // Default to carbon mass
            }
        }
        
        return masses;
    }

    /**
     * @brief Get residue numbers (1-indexed)
     * @return Vector of residue numbers
     */
    std::vector<int> get_residue_numbers() const {
        std::vector<int> resnums;
        resnums.reserve(n_atoms_);
        
        for (int resid : residue_ids_) {
            resnums.push_back(resid + 1); // Convert to 1-indexed
        }
        
        return resnums;
    }

    /**
     * @brief Get segment IDs (using chain IDs)
     * @return Vector of segment identifiers
     */
    std::vector<std::string> get_segment_ids() const {
        return chain_ids_; // Use chain IDs as segment IDs
    }

    /**
     * @brief Get unique residue information
     * @return Dictionary with residue summary
     */
    py::dict get_residue_info() const {
        py::dict residue_info;
        
        std::vector<std::string> unique_resnames;
        std::vector<int> unique_resids;
        std::vector<std::string> unique_chains;
        
        int current_resid = -1;
        std::string current_chain = "";
        
        for (size_t i = 0; i < n_atoms_; ++i) {
            if (residue_ids_[i] != current_resid || chain_ids_[i] != current_chain) {
                unique_resnames.push_back(residue_names_[i]);
                unique_resids.push_back(residue_ids_[i]);
                unique_chains.push_back(chain_ids_[i]);
                current_resid = residue_ids_[i];
                current_chain = chain_ids_[i];
            }
        }
        
        residue_info["resnames"] = unique_resnames;
        residue_info["resids"] = unique_resids;
        residue_info["chainIDs"] = unique_chains;
        residue_info["n_residues"] = unique_resnames.size();
        
        return residue_info;
    }

    /**
     * @brief Calculate center of mass
     * @return 3D coordinates of center of mass
     */
    py::array_t<double> calculate_center_of_mass() const {
        auto masses = get_atomic_masses();
        auto coords_buf = coordinates_.request();
        double* coords_ptr = static_cast<double*>(coords_buf.ptr);
        
        double total_mass = 0.0;
        std::vector<double> com(3, 0.0);
        
        for (size_t i = 0; i < n_atoms_; ++i) {
            double mass = masses[i];
            total_mass += mass;
            
            for (int j = 0; j < 3; ++j) {
                com[j] += mass * coords_ptr[i * 3 + j];
            }
        }
        
        for (int j = 0; j < 3; ++j) {
            com[j] /= total_mass;
        }
        
        return py::cast(com);
    }

    /**
     * @brief Calculate radius of gyration
     * @return Radius of gyration in Angstroms
     */
    double calculate_radius_of_gyration() const {
        auto com_array = calculate_center_of_mass();
        auto com_buf = com_array.request();
        double* com_ptr = static_cast<double*>(com_buf.ptr);
        
        auto masses = get_atomic_masses();
        auto coords_buf = coordinates_.request();
        double* coords_ptr = static_cast<double*>(coords_buf.ptr);
        
        double total_mass = 0.0;
        double rg_squared = 0.0;
        
        for (size_t i = 0; i < n_atoms_; ++i) {
            double mass = masses[i];
            total_mass += mass;
            
            double dist_squared = 0.0;
            for (int j = 0; j < 3; ++j) {
                double diff = coords_ptr[i * 3 + j] - com_ptr[j];
                dist_squared += diff * diff;
            }
            
            rg_squared += mass * dist_squared;
        }
        
        return std::sqrt(rg_squared / total_mass);
    }

    // Getters
    size_t get_n_atoms() const { return n_atoms_; }
    size_t get_n_residues() const { return n_residues_; }
    size_t get_n_chains() const { return n_chains_; }

private:
    void build_topology_info() {
        // Count unique residues and chains
        std::set<std::pair<int, std::string>> unique_residues;
        std::set<std::string> unique_chains;
        
        for (size_t i = 0; i < n_atoms_; ++i) {
            unique_residues.insert({residue_ids_[i], chain_ids_[i]});
            unique_chains.insert(chain_ids_[i]);
        }
        
        n_residues_ = unique_residues.size();
        n_chains_ = unique_chains.size();
    }

    size_t n_atoms_ = 0;
    size_t n_residues_ = 0;
    size_t n_chains_ = 0;
    
    py::array_t<double> coordinates_;
    std::vector<std::string> elements_;
    std::vector<std::string> residue_names_;
    std::vector<int> residue_ids_;
    std::vector<std::string> chain_ids_;
    std::vector<std::string> atom_names_;
};

/**
 * @brief Trajectory analysis helper for MDAnalysis integration
 */
class MDAnalysisTrajectoryInterface {
public:
    MDAnalysisTrajectoryInterface() = default;
    ~MDAnalysisTrajectoryInterface() = default;

    /**
     * @brief Initialize with trajectory metadata
     */
    void initialize(int n_frames, int n_atoms, double timestep = 1.0) {
        n_frames_ = n_frames;
        n_atoms_ = n_atoms;
        timestep_ = timestep;
        current_frame_ = 0;
        
        // Allocate coordinate storage
        coordinates_buffer_.resize(n_frames * n_atoms * 3);
    }

    /**
     * @brief Add frame coordinates
     */
    void add_frame(int frame_idx, py::array_t<double> coordinates) {
        if (frame_idx >= n_frames_) {
            throw std::runtime_error("Frame index out of bounds");
        }
        
        auto buf = coordinates.request();
        if (buf.ndim != 2 || buf.shape[0] != n_atoms_ || buf.shape[1] != 3) {
            throw std::runtime_error("Coordinates must be N×3 array");
        }
        
        double* coords_ptr = static_cast<double*>(buf.ptr);
        size_t offset = frame_idx * n_atoms_ * 3;
        
        std::copy(coords_ptr, coords_ptr + n_atoms_ * 3, 
                  coordinates_buffer_.begin() + offset);
    }

    /**
     * @brief Get frame coordinates
     */
    py::array_t<double> get_frame(int frame_idx) const {
        if (frame_idx >= n_frames_) {
            throw std::runtime_error("Frame index out of bounds");
        }
        
        size_t offset = frame_idx * n_atoms_ * 3;
        
        // Create NumPy array with copy of data
        auto result = py::array_t<double>(
            {n_atoms_, 3},
            {sizeof(double) * 3, sizeof(double)},
            coordinates_buffer_.data() + offset,
            py::cast(*this)  // Keep this object alive
        );
        
        return result;
    }

    /**
     * @brief Get trajectory metadata
     */
    py::dict get_metadata() const {
        py::dict metadata;
        metadata["n_frames"] = n_frames_;
        metadata["n_atoms"] = n_atoms_;
        metadata["timestep"] = timestep_;
        metadata["total_time"] = n_frames_ * timestep_;
        return metadata;
    }

    // Getters
    int get_n_frames() const { return n_frames_; }
    int get_n_atoms() const { return n_atoms_; }
    double get_timestep() const { return timestep_; }

private:
    int n_frames_ = 0;
    int n_atoms_ = 0;
    double timestep_ = 1.0;
    int current_frame_ = 0;
    std::vector<double> coordinates_buffer_;
};

void init_mdanalysis_bindings(py::module& m) {
    py::module mdanalysis = m.def_submodule("mdanalysis", "MDAnalysis integration bindings");
    
    py::class_<VIAMDMDAnalysisInterface>(mdanalysis, "VIAMDMDAnalysisInterface")
        .def(py::init<>())
        .def("initialize_from_arrays", &VIAMDMDAnalysisInterface::initialize_from_arrays,
             "Initialize interface from molecular data arrays",
             py::arg("coordinates"), py::arg("elements"), py::arg("residue_names"),
             py::arg("residue_ids"), py::arg("chain_ids"), py::arg("atom_names"))
        .def("get_coordinates", &VIAMDMDAnalysisInterface::get_coordinates,
             "Get coordinates as NumPy array (zero-copy access)")
        .def("set_coordinates", &VIAMDMDAnalysisInterface::set_coordinates,
             "Update coordinates from NumPy array")
        .def("get_topology_dict", &VIAMDMDAnalysisInterface::get_topology_dict,
             "Get MDAnalysis-compatible topology information")
        .def("get_atomic_masses", &VIAMDMDAnalysisInterface::get_atomic_masses,
             "Get atomic masses based on element symbols")
        .def("get_residue_info", &VIAMDMDAnalysisInterface::get_residue_info,
             "Get unique residue information")
        .def("calculate_center_of_mass", &VIAMDMDAnalysisInterface::calculate_center_of_mass,
             "Calculate center of mass")
        .def("calculate_radius_of_gyration", &VIAMDMDAnalysisInterface::calculate_radius_of_gyration,
             "Calculate radius of gyration")
        .def_property_readonly("n_atoms", &VIAMDMDAnalysisInterface::get_n_atoms)
        .def_property_readonly("n_residues", &VIAMDMDAnalysisInterface::get_n_residues)
        .def_property_readonly("n_chains", &VIAMDMDAnalysisInterface::get_n_chains);
    
    py::class_<MDAnalysisTrajectoryInterface>(mdanalysis, "MDAnalysisTrajectoryInterface")
        .def(py::init<>())
        .def("initialize", &MDAnalysisTrajectoryInterface::initialize,
             "Initialize with trajectory metadata",
             py::arg("n_frames"), py::arg("n_atoms"), py::arg("timestep") = 1.0)
        .def("add_frame", &MDAnalysisTrajectoryInterface::add_frame,
             "Add frame coordinates")
        .def("get_frame", &MDAnalysisTrajectoryInterface::get_frame,
             "Get frame coordinates")
        .def("get_metadata", &MDAnalysisTrajectoryInterface::get_metadata,
             "Get trajectory metadata")
        .def_property_readonly("n_frames", &MDAnalysisTrajectoryInterface::get_n_frames)
        .def_property_readonly("n_atoms", &MDAnalysisTrajectoryInterface::get_n_atoms)
        .def_property_readonly("timestep", &MDAnalysisTrajectoryInterface::get_timestep);
    
    // Utility functions
    mdanalysis.def("create_mdanalysis_topology_dict", 
        [](py::array_t<double> coordinates,
           const std::vector<std::string>& elements,
           const std::vector<std::string>& residue_names,
           const std::vector<int>& residue_ids,
           const std::vector<std::string>& chain_ids,
           const std::vector<std::string>& atom_names) -> py::dict {
            
            VIAMDMDAnalysisInterface interface;
            interface.initialize_from_arrays(coordinates, elements, residue_names,
                                           residue_ids, chain_ids, atom_names);
            return interface.get_topology_dict();
        },
        "Create MDAnalysis-compatible topology dictionary from VIAMD data",
        py::arg("coordinates"), py::arg("elements"), py::arg("residue_names"),
        py::arg("residue_ids"), py::arg("chain_ids"), py::arg("atom_names"));
}