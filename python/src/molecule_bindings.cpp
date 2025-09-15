/**
 * @file molecule_bindings.cpp
 * @brief Molecule structure and data bindings for MDAnalysis integration
 */

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>

#include <md_molecule.h>
#include <md_pdb.h>
#include <md_gro.h>
#include <md_xyz.h>

namespace py = pybind11;

void bind_molecule(py::module &m) {
    // Create a submodule for molecule functionality
    auto mol = m.def_submodule("molecule", "Molecular structure and data access");
    
    // Bind atom data structure - provides NumPy array access to coordinates
    py::class_<md_atom_data_t>(mol, "AtomData")
        .def_readonly("count", &md_atom_data_t::count)
        .def_property_readonly("coordinates", [](const md_atom_data_t& atoms) -> py::array {
            if (!atoms.x || !atoms.y || !atoms.z) {
                return py::array();
            }
            // Create a 2D NumPy array (N, 3) for coordinates
            std::vector<py::ssize_t> shape = {(py::ssize_t)atoms.count, 3};
            auto coords = py::array_t<float>(shape);
            auto buf = coords.request();
            float* ptr = static_cast<float*>(buf.ptr);
            
            for (size_t i = 0; i < atoms.count; ++i) {
                ptr[i * 3 + 0] = atoms.x[i];
                ptr[i * 3 + 1] = atoms.y[i];
                ptr[i * 3 + 2] = atoms.z[i];
            }
            return coords;
        }, "Get atomic coordinates as NumPy array (N, 3)")
        .def_property_readonly("masses", [](const md_atom_data_t& atoms) -> py::array {
            if (!atoms.mass) return py::array();
            return py::array_t<float>(atoms.count, atoms.mass);
        }, "Get atomic masses as NumPy array")
        .def_property_readonly("radii", [](const md_atom_data_t& atoms) -> py::array {
            if (!atoms.radius) return py::array();
            return py::array_t<float>(atoms.count, atoms.radius);
        }, "Get atomic radii as NumPy array")
        .def_property_readonly("elements", [](const md_atom_data_t& atoms) -> py::array {
            if (!atoms.element) return py::array();
            return py::array_t<md_element_t>(atoms.count, atoms.element);
        }, "Get atomic elements as NumPy array")
        .def_property_readonly("residue_names", [](const md_atom_data_t& atoms) -> std::vector<std::string> {
            std::vector<std::string> names;
            if (atoms.resname) {
                names.reserve(atoms.count);
                for (size_t i = 0; i < atoms.count; ++i) {
                    names.emplace_back(atoms.resname[i]);
                }
            }
            return names;
        }, "Get residue names for each atom")
        .def_property_readonly("chain_ids", [](const md_atom_data_t& atoms) -> std::vector<std::string> {
            std::vector<std::string> chains;
            if (atoms.chainid) {
                chains.reserve(atoms.count);
                for (size_t i = 0; i < atoms.count; ++i) {
                    chains.emplace_back(atoms.chainid[i]);
                }
            }
            return chains;
        }, "Get chain IDs for each atom");
    
    // Bind residue data structure
    py::class_<md_residue_data_t>(mol, "ResidueData")
        .def_readonly("count", &md_residue_data_t::count)
        .def_property_readonly("names", [](const md_residue_data_t& residues) -> std::vector<std::string> {
            std::vector<std::string> names;
            if (residues.name) {
                names.reserve(residues.count);
                for (size_t i = 0; i < residues.count; ++i) {
                    names.emplace_back(residues.name[i]);
                }
            }
            return names;
        }, "Get residue names")
        .def("get_atom_range", [](const md_residue_data_t& residues, size_t res_idx) -> py::tuple {
            md_range_t range = md_residue_atom_range(residues, res_idx);
            return py::make_tuple(range.beg, range.end);
        }, "Get atom index range for residue", py::arg("residue_index"));
    
    // Bind chain data structure
    py::class_<md_chain_data_t>(mol, "ChainData")
        .def_readonly("count", &md_chain_data_t::count)
        .def("get_residue_range", [](const md_chain_data_t& chains, size_t chain_idx) -> py::tuple {
            md_range_t range = md_chain_residue_range(chains, chain_idx);
            return py::make_tuple(range.beg, range.end);
        }, "Get residue index range for chain", py::arg("chain_index"))
        .def("get_atom_range", [](const md_chain_data_t& chains, size_t chain_idx) -> py::tuple {
            md_range_t range = md_chain_atom_range(chains, chain_idx);
            return py::make_tuple(range.beg, range.end);
        }, "Get atom index range for chain", py::arg("chain_index"));
    
    // Bind the main molecule structure
    py::class_<md_molecule_t>(mol, "Molecule")
        .def_readonly("atom", &md_molecule_t::atom)
        .def_readonly("residue", &md_molecule_t::residue) 
        .def_readonly("chain", &md_molecule_t::chain)
        .def_property_readonly("n_atoms", [](const md_molecule_t& mol) {
            return mol.atom.count;
        }, "Number of atoms")
        .def_property_readonly("n_residues", [](const md_molecule_t& mol) {
            return mol.residue.count;
        }, "Number of residues")
        .def_property_readonly("n_chains", [](const md_molecule_t& mol) {
            return mol.chain.count;
        }, "Number of chains")
        .def("set_coordinates", [](md_molecule_t& mol, py::array_t<float> coords) {
            auto buf = coords.request();
            if (buf.ndim != 2 || buf.shape[1] != 3) {
                throw std::runtime_error("Coordinates must be (N, 3) array");
            }
            if ((size_t)buf.shape[0] != mol.atom.count) {
                throw std::runtime_error("Coordinate array size must match number of atoms");
            }
            
            float* ptr = static_cast<float*>(buf.ptr);
            for (size_t i = 0; i < mol.atom.count; ++i) {
                mol.atom.x[i] = ptr[i * 3 + 0];
                mol.atom.y[i] = ptr[i * 3 + 1];
                mol.atom.z[i] = ptr[i * 3 + 2];
            }
        }, "Set atomic coordinates from NumPy array", py::arg("coordinates"));
    
    // Bind molecule I/O functions for integration with file formats
    mol.def("load_pdb", [](const std::string& filename) -> py::object {
        md_molecule_t molecule = {};
        md_allocator_i* alloc = md_get_heap_allocator();
        
        // First parse the PDB data
        md_pdb_data_t pdb_data = {};
        if (md_pdb_data_parse_file(&pdb_data, str_from_cstr(filename.c_str()), alloc)) {
            // Then initialize the molecule from the parsed data
            md_pdb_options_t options = {}; // Use default options
            if (md_pdb_molecule_init(&molecule, &pdb_data, options, alloc)) {
                md_pdb_data_free(&pdb_data, alloc);
                return py::cast(molecule);
            }
            md_pdb_data_free(&pdb_data, alloc);
        }
        return py::none();
    }, "Load molecule from PDB file", py::arg("filename"));
    
    mol.def("load_gro", [](const std::string& filename) -> py::object {
        md_molecule_t molecule = {};
        md_allocator_i* alloc = md_get_heap_allocator();
        
        // First parse the GRO data
        md_gro_data_t gro_data = {};
        if (md_gro_data_parse_file(&gro_data, str_from_cstr(filename.c_str()), alloc)) {
            // Then initialize the molecule from the parsed data
            if (md_gro_molecule_init(&molecule, &gro_data, alloc)) {
                md_gro_data_free(&gro_data, alloc);
                return py::cast(molecule);
            }
            md_gro_data_free(&gro_data, alloc);
        }
        return py::none();
    }, "Load molecule from GRO file", py::arg("filename"));
    
    mol.def("load_xyz", [](const std::string& filename) -> py::object {
        md_molecule_t molecule = {};
        md_allocator_i* alloc = md_get_heap_allocator();
        
        // First parse the XYZ data  
        md_xyz_data_t xyz_data = {};
        if (md_xyz_data_parse_file(&xyz_data, str_from_cstr(filename.c_str()), alloc)) {
            // Then initialize the molecule from the parsed data
            if (md_xyz_molecule_init(&molecule, &xyz_data, alloc)) {
                md_xyz_data_free(&xyz_data, alloc);
                return py::cast(molecule);
            }
            md_xyz_data_free(&xyz_data, alloc);
        }
        return py::none();
    }, "Load molecule from XYZ file", py::arg("filename"));
}