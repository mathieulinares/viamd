#pragma once

#include <vector>
#include <string>
#include <cmath>

namespace lightweight_mol {

// Basic atom data
struct Atom {
    int element;           // Atomic number
    float x, y, z;        // 3D coordinates (in Angstroms)
    int valence;          // Expected valence
    int bonds_count;      // Current number of bonds
    std::vector<int> neighbors; // Connected atom indices
    bool aromatic;        // Is in aromatic ring
};

// Basic bond data  
struct Bond {
    int atom1, atom2;     // Atom indices
    int order;            // Bond order (1=single, 2=double, 3=triple)
    bool aromatic;        // Is aromatic bond
};

// Simple molecule representation
struct Molecule {
    std::vector<Atom> atoms;
    std::vector<Bond> bonds;
    std::string formula;
    
    void clear() {
        atoms.clear();
        bonds.clear();
        formula.clear();
    }
};

// Lightweight SMILES parser and 3D coordinate generator
class MoleculeBuilder {
public:
    // Parse SMILES and generate 3D coordinates
    bool build_from_smiles(const std::string& smiles, Molecule& mol);
    
    // Get the last error message
    const std::string& get_error() const { return error_msg; }

private:
    std::string error_msg;
    
    // SMILES parsing
    bool parse_smiles(const std::string& smiles, Molecule& mol);
    bool parse_atom(const std::string& atom_str, Atom& atom);
    int get_atomic_number(const std::string& symbol);
    int get_default_valence(int atomic_num);
    
    // Hydrogen addition
    void add_hydrogens(Molecule& mol);
    
    // 3D coordinate generation
    void generate_3d_coordinates(Molecule& mol);
    void place_atom_3d(Molecule& mol, int atom_idx, int ref_atom1 = -1, int ref_atom2 = -1);
    
    // Utility functions
    void calculate_molecular_formula(Molecule& mol);
    std::string element_symbol(int atomic_num);
    float get_bond_length(int elem1, int elem2);
    float get_bond_angle(int center_elem, int bond_order = 1);
    
    // Geometric helpers
    void normalize_vector(float& x, float& y, float& z);
    void cross_product(float ax, float ay, float az, float bx, float by, float bz, 
                      float& cx, float& cy, float& cz);
};

} // namespace lightweight_mol