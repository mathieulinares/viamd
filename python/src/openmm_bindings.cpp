/**
 * @file openmm_bindings.cpp
 * @brief OpenMM integration bindings for seamless molecular dynamics simulation
 * 
 * This module provides Python bindings that enable seamless integration between
 * VIAMD's molecular data structures and OpenMM's simulation engine. It allows
 * for efficient coordinate transfer, simulation setup, and real-time dynamics
 * with coordinate feedback to VIAMD.
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <md_molecule.h>
#include <md_pdb.h>
#include <md_gro.h>
#include <md_util.h>

#include <vector>
#include <memory>
#include <stdexcept>
#include <fstream>
#include <iomanip>

namespace py = pybind11;

/**
 * @brief VIAMD-OpenMM interface class for seamless molecular dynamics integration
 * 
 * This class provides utilities for setting up OpenMM simulations using VIAMD
 * molecular data and maintaining bidirectional coordinate synchronization.
 */
class VIAMDOpenMMInterface {
private:
    md_molecule_t* molecule;
    bool owns_molecule;
    
public:
    VIAMDOpenMMInterface() : molecule(nullptr), owns_molecule(false) {}
    
    ~VIAMDOpenMMInterface() {
        if (owns_molecule && molecule) {
            md_molecule_free(molecule, md_get_heap_allocator());
        }
    }
    
    /**
     * @brief Set the VIAMD molecule for this interface
     * @param mol Reference to VIAMD molecule
     * @param take_ownership Whether to take ownership of the molecule
     */
    void set_molecule(md_molecule_t& mol, bool take_ownership = false) {
        if (owns_molecule && molecule) {
            md_molecule_free(molecule, md_get_heap_allocator());
        }
        molecule = &mol;
        owns_molecule = take_ownership;
    }
    
    /**
     * @brief Get current molecular coordinates as NumPy array
     * @return NumPy array of shape (n_atoms, 3) with coordinates in Angstroms
     */
    py::array_t<float> get_coordinates() const {
        if (!molecule || !molecule->atom.x || !molecule->atom.y || !molecule->atom.z) {
            throw std::runtime_error("No valid molecule or coordinates available");
        }
        
        std::vector<py::ssize_t> shape = {(py::ssize_t)molecule->atom.count, 3};
        auto coords = py::array_t<float>(shape);
        auto buf = coords.request();
        float* ptr = static_cast<float*>(buf.ptr);
        
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            ptr[i * 3 + 0] = molecule->atom.x[i];
            ptr[i * 3 + 1] = molecule->atom.y[i];
            ptr[i * 3 + 2] = molecule->atom.z[i];
        }
        
        return coords;
    }
    
    /**
     * @brief Set molecular coordinates from NumPy array
     * @param coords NumPy array of shape (n_atoms, 3) with coordinates in Angstroms
     */
    void set_coordinates(py::array_t<float> coords) {
        if (!molecule) {
            throw std::runtime_error("No molecule available");
        }
        
        auto buf = coords.request();
        if (buf.ndim != 2 || buf.shape[1] != 3) {
            throw std::runtime_error("Coordinates must be (N, 3) array");
        }
        if ((size_t)buf.shape[0] != molecule->atom.count) {
            throw std::runtime_error("Coordinate array size must match number of atoms");
        }
        
        float* ptr = static_cast<float*>(buf.ptr);
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            molecule->atom.x[i] = ptr[i * 3 + 0];
            molecule->atom.y[i] = ptr[i * 3 + 1];
            molecule->atom.z[i] = ptr[i * 3 + 2];
        }
    }
    
    /**
     * @brief Get molecular masses as NumPy array
     * @return NumPy array of atomic masses
     */
    py::array_t<float> get_masses() const {
        if (!molecule || !molecule->atom.mass) {
            throw std::runtime_error("No valid molecule or masses available");
        }
        
        return py::array_t<float>(molecule->atom.count, molecule->atom.mass);
    }
    
    /**
     * @brief Get atomic elements as list of strings
     * @return Vector of element symbols
     */
    std::vector<std::string> get_elements() const {
        if (!molecule || !molecule->atom.element) {
            throw std::runtime_error("No valid molecule or elements available");
        }
        
        std::vector<std::string> elements;
        elements.reserve(molecule->atom.count);
        
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            str_t element_str = md_util_element_symbol(molecule->atom.element[i]);
            elements.push_back(std::string(element_str.ptr, element_str.len));
        }
        
        return elements;
    }
    
    /**
     * @brief Get number of atoms in the molecule
     * @return Number of atoms
     */
    size_t get_num_atoms() const {
        return molecule ? molecule->atom.count : 0;
    }
    
    /**
     * @brief Get residue information for OpenMM topology setup
     * @return Vector of residue names
     */
    std::vector<std::string> get_residue_names() const {
        if (!molecule || !molecule->atom.resname) {
            throw std::runtime_error("No valid molecule or residue names available");
        }
        
        std::vector<std::string> resnames;
        resnames.reserve(molecule->atom.count);
        
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            resnames.emplace_back(molecule->atom.resname[i]);
        }
        
        return resnames;
    }
    
    /**
     * @brief Get atom types for OpenMM topology setup
     * @return Vector of atom type names
     */
    std::vector<std::string> get_atom_types() const {
        if (!molecule || !molecule->atom.type) {
            throw std::runtime_error("No valid molecule or atom types available");
        }
        
        std::vector<std::string> types;
        types.reserve(molecule->atom.count);
        
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            types.emplace_back(molecule->atom.type[i]);
        }
        
        return types;
    }
    
    /**
     * @brief Create OpenMM-compatible topology information
     * @return Dictionary with topology data for OpenMM
     */
    py::dict get_topology_data() const {
        if (!molecule) {
            throw std::runtime_error("No molecule available");
        }
        
        py::dict topology;
        
        // Basic molecular information
        topology["n_atoms"] = molecule->atom.count;
        topology["n_residues"] = molecule->residue.count;
        topology["n_chains"] = molecule->chain.count;
        
        // Atom-level data
        topology["coordinates"] = get_coordinates();
        topology["masses"] = get_masses();
        topology["elements"] = get_elements();
        topology["atom_types"] = get_atom_types();
        topology["residue_names"] = get_residue_names();
        
        return topology;
    }
    
    /**
     * @brief Export current coordinates in PDB format for OpenMM
     * @param filename Output PDB filename
     * @note This is a simplified implementation - full PDB export may require additional functionality
     */
    void export_pdb(const std::string& filename) const {
        if (!molecule) {
            throw std::runtime_error("No molecule available for export");
        }
        
        // Simple PDB-like format export (minimal implementation)
        // For production use, a full PDB writer would be needed
        std::ofstream file(filename);
        if (!file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + filename);
        }
        
        file << "HEADER    VIAMD-OpenMM exported structure\n";
        
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            // Safe element symbol extraction
            std::string element = "C";  // Default element
            if (molecule->atom.element && i < molecule->atom.count) {
                str_t element_str = md_util_element_symbol(molecule->atom.element[i]);
                if (element_str.ptr && element_str.len > 0) {
                    element = std::string(element_str.ptr, element_str.len);
                }
            }
            
            // Default values if fields are not available  
            std::string resname = "UNK";
            if (molecule->atom.resname && molecule->atom.resname[i]) {
                resname = molecule->atom.resname[i];
            }
            
            std::string chainid = "A";
            if (molecule->atom.chainid && molecule->atom.chainid[i]) {
                chainid = molecule->atom.chainid[i];
            }
            
            int resid = 1;
            if (molecule->atom.resid) {
                resid = molecule->atom.resid[i];
            }
            
            file << std::fixed << std::setprecision(3);
            file << "ATOM  " << std::setw(5) << (i + 1) 
                 << "  " << std::setw(4) << element
                 << " " << std::setw(3) << resname
                 << " " << std::setw(1) << chainid
                 << std::setw(4) << resid
                 << "    " << std::setw(8) << molecule->atom.x[i]
                 << std::setw(8) << molecule->atom.y[i]  
                 << std::setw(8) << molecule->atom.z[i]
                 << "  1.00 20.00          " << element << "\n";
        }
        
        file << "END\n";
        file.close();
    }
    
    /**
     * @brief Get coordinates in OpenMM format (nanometers)
     * @return Coordinates as NumPy array in nanometers
     */
    py::array_t<double> get_openmm_coordinates() const {
        if (!molecule || !molecule->atom.x || !molecule->atom.y || !molecule->atom.z) {
            throw std::runtime_error("No valid molecule or coordinates available");
        }
        
        std::vector<py::ssize_t> shape = {(py::ssize_t)molecule->atom.count, 3};
        auto coords = py::array_t<double>(shape);
        auto buf = coords.request();
        double* ptr = static_cast<double*>(buf.ptr);
        
        // Convert from Angstroms to nanometers
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            ptr[i * 3 + 0] = molecule->atom.x[i] * 0.1;
            ptr[i * 3 + 1] = molecule->atom.y[i] * 0.1;
            ptr[i * 3 + 2] = molecule->atom.z[i] * 0.1;
        }
        
        return coords;
    }
    
    /**
     * @brief Set coordinates from OpenMM format (nanometers)
     * @param coords Coordinates from OpenMM in nanometers
     */
    void set_openmm_coordinates(py::array_t<double> coords) {
        if (!molecule) {
            throw std::runtime_error("No molecule available");
        }
        
        auto buf = coords.request();
        if (buf.ndim != 2 || buf.shape[1] != 3) {
            throw std::runtime_error("OpenMM coordinates must be (N, 3) array");
        }
        if ((size_t)buf.shape[0] != molecule->atom.count) {
            throw std::runtime_error("Coordinate array size must match number of atoms");
        }
        
        double* ptr = static_cast<double*>(buf.ptr);
        // Convert from nanometers to Angstroms
        for (size_t i = 0; i < molecule->atom.count; ++i) {
            molecule->atom.x[i] = static_cast<float>(ptr[i * 3 + 0] * 10.0);
            molecule->atom.y[i] = static_cast<float>(ptr[i * 3 + 1] * 10.0);
            molecule->atom.z[i] = static_cast<float>(ptr[i * 3 + 2] * 10.0);
        }
    }
};

/**
 * @brief Utility class for OpenMM simulation management with VIAMD integration
 */
class OpenMMSimulationManager {
private:
    std::shared_ptr<VIAMDOpenMMInterface> viamd_interface;
    
public:
    OpenMMSimulationManager() = default;
    
    /**
     * @brief Set the VIAMD interface for this simulation manager
     * @param interface Shared pointer to VIAMD-OpenMM interface
     */
    void set_viamd_interface(std::shared_ptr<VIAMDOpenMMInterface> interface) {
        viamd_interface = interface;
    }
    
    /**
     * @brief Get the current VIAMD interface
     * @return Shared pointer to VIAMD-OpenMM interface
     */
    std::shared_ptr<VIAMDOpenMMInterface> get_viamd_interface() const {
        return viamd_interface;
    }
    
    /**
     * @brief Update VIAMD coordinates from OpenMM simulation
     * @param openmm_coords Coordinates from OpenMM in nanometers
     */
    void update_viamd_coordinates(py::array_t<double> openmm_coords) {
        if (!viamd_interface) {
            throw std::runtime_error("No VIAMD interface available");
        }
        
        auto buf = openmm_coords.request();
        if (buf.ndim != 2 || buf.shape[1] != 3) {
            throw std::runtime_error("OpenMM coordinates must be (N, 3) array");
        }
        
        // Convert from OpenMM units (nm) to VIAMD units (Angstroms)
        std::vector<py::ssize_t> shape = {buf.shape[0], 3};
        auto viamd_coords = py::array_t<float>(shape);
        auto viamd_buf = viamd_coords.request();
        
        double* openmm_ptr = static_cast<double*>(buf.ptr);
        float* viamd_ptr = static_cast<float*>(viamd_buf.ptr);
        
        for (py::ssize_t i = 0; i < buf.shape[0] * 3; ++i) {
            viamd_ptr[i] = static_cast<float>(openmm_ptr[i] * 10.0);  // nm to Angstroms
        }
        
        viamd_interface->set_coordinates(viamd_coords);
    }
    
    /**
     * @brief Get VIAMD coordinates for OpenMM simulation
     * @return Coordinates in OpenMM units (nanometers)
     */
    py::array_t<double> get_openmm_coordinates() const {
        if (!viamd_interface) {
            throw std::runtime_error("No VIAMD interface available");
        }
        
        auto viamd_coords = viamd_interface->get_coordinates();
        auto viamd_buf = viamd_coords.request();
        
        // Convert from VIAMD units (Angstroms) to OpenMM units (nm)
        std::vector<py::ssize_t> shape = {viamd_buf.shape[0], 3};
        auto openmm_coords = py::array_t<double>(shape);
        auto openmm_buf = openmm_coords.request();
        
        float* viamd_ptr = static_cast<float*>(viamd_buf.ptr);
        double* openmm_ptr = static_cast<double*>(openmm_buf.ptr);
        
        for (py::ssize_t i = 0; i < viamd_buf.shape[0] * 3; ++i) {
            openmm_ptr[i] = static_cast<double>(viamd_ptr[i] * 0.1);  // Angstroms to nm
        }
        
        return openmm_coords;
    }
};

void bind_openmm(py::module &m) {
    // Create a submodule for OpenMM integration
    auto openmm = m.def_submodule("openmm", "OpenMM integration for molecular dynamics simulations");
    
    // Bind the VIAMD-OpenMM interface class
    py::class_<VIAMDOpenMMInterface, std::shared_ptr<VIAMDOpenMMInterface>>(openmm, "VIAMDInterface")
        .def(py::init<>())
        .def("set_molecule", &VIAMDOpenMMInterface::set_molecule, 
             "Set the VIAMD molecule for this interface", 
             py::arg("molecule"), py::arg("take_ownership") = false)
        .def("get_coordinates", &VIAMDOpenMMInterface::get_coordinates,
             "Get current molecular coordinates as NumPy array (N, 3) in Angstroms")
        .def("set_coordinates", &VIAMDOpenMMInterface::set_coordinates,
             "Set molecular coordinates from NumPy array (N, 3) in Angstroms",
             py::arg("coordinates"))
        .def("get_masses", &VIAMDOpenMMInterface::get_masses,
             "Get atomic masses as NumPy array")
        .def("get_elements", &VIAMDOpenMMInterface::get_elements,
             "Get atomic elements as list of element symbols")
        .def("get_num_atoms", &VIAMDOpenMMInterface::get_num_atoms,
             "Get number of atoms in the molecule")
        .def("get_residue_names", &VIAMDOpenMMInterface::get_residue_names,
             "Get residue names for OpenMM topology setup")
        .def("get_atom_types", &VIAMDOpenMMInterface::get_atom_types,
             "Get atom types for OpenMM topology setup")
        .def("get_topology_data", &VIAMDOpenMMInterface::get_topology_data,
             "Create OpenMM-compatible topology information as dictionary")
        .def("get_openmm_coordinates", &VIAMDOpenMMInterface::get_openmm_coordinates,
             "Get coordinates in OpenMM format (nanometers)")
        .def("set_openmm_coordinates", &VIAMDOpenMMInterface::set_openmm_coordinates,
             "Set coordinates from OpenMM format (nanometers)", py::arg("coordinates"))
        .def("export_pdb", &VIAMDOpenMMInterface::export_pdb,
             "Export current coordinates in PDB format for OpenMM",
             py::arg("filename"));
    
    // Bind the OpenMM simulation manager class
    py::class_<OpenMMSimulationManager>(openmm, "SimulationManager")
        .def(py::init<>())
        .def("set_viamd_interface", &OpenMMSimulationManager::set_viamd_interface,
             "Set the VIAMD interface for this simulation manager",
             py::arg("interface"))
        .def("get_viamd_interface", &OpenMMSimulationManager::get_viamd_interface,
             "Get the current VIAMD interface")
        .def("update_viamd_coordinates", &OpenMMSimulationManager::update_viamd_coordinates,
             "Update VIAMD coordinates from OpenMM simulation (coordinates in nm)",
             py::arg("openmm_coordinates"))
        .def("get_openmm_coordinates", &OpenMMSimulationManager::get_openmm_coordinates,
             "Get VIAMD coordinates for OpenMM simulation (returns coordinates in nm)");
    
    // Utility functions for OpenMM integration
    openmm.def("create_interface_from_molecule", [](md_molecule_t& mol) -> std::shared_ptr<VIAMDOpenMMInterface> {
        auto interface = std::make_shared<VIAMDOpenMMInterface>();
        interface->set_molecule(mol, false);
        return interface;
    }, "Create VIAMD-OpenMM interface from existing molecule", py::arg("molecule"));
    
    openmm.def("create_interface_from_pdb", [](const std::string& filename) -> std::shared_ptr<VIAMDOpenMMInterface> {
        md_molecule_t* molecule = new md_molecule_t{};
        md_allocator_i* alloc = md_get_heap_allocator();
        
        // Parse PDB data
        md_pdb_data_t pdb_data = {};
        if (md_pdb_data_parse_file(&pdb_data, str_from_cstr(filename.c_str()), alloc)) {
            // Initialize molecule from parsed data
            md_pdb_options_t options = {}; // Use default options
            if (md_pdb_molecule_init(molecule, &pdb_data, options, alloc)) {
                md_pdb_data_free(&pdb_data, alloc);
                
                // Postprocess to generate missing data (elements, masses, etc.)
                uint32_t postprocess_flags = MD_UTIL_POSTPROCESS_ELEMENT_BIT | 
                                            MD_UTIL_POSTPROCESS_MASS_BIT | 
                                            MD_UTIL_POSTPROCESS_RADIUS_BIT;
                if (!md_util_molecule_postprocess(molecule, alloc, postprocess_flags)) {
                    // If postprocessing fails, continue anyway (some data might still be available)
                }
                
                auto interface = std::make_shared<VIAMDOpenMMInterface>();
                interface->set_molecule(*molecule, true);  // Take ownership
                return interface;
            }
            md_pdb_data_free(&pdb_data, alloc);
        }
        
        delete molecule;
        throw std::runtime_error("Failed to load PDB file: " + filename);
    }, "Create VIAMD-OpenMM interface from PDB file", py::arg("filename"));
}