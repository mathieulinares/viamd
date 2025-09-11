#include "lightweight_mol_builder.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <map>
#include <stack>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace lightweight_mol {

// Element data
static const std::map<std::string, int> element_map = {
    {"H", 1}, {"C", 6}, {"N", 7}, {"O", 8}, {"F", 9}, 
    {"P", 15}, {"S", 16}, {"Cl", 17}, {"Br", 35}, {"I", 53}
};

static const std::map<int, std::string> element_symbols = {
    {1, "H"}, {6, "C"}, {7, "N"}, {8, "O"}, {9, "F"},
    {15, "P"}, {16, "S"}, {17, "Cl"}, {35, "Br"}, {53, "I"}
};

// Default valences for elements
static const std::map<int, int> default_valences = {
    {1, 1},   // H
    {6, 4},   // C  
    {7, 3},   // N
    {8, 2},   // O
    {9, 1},   // F
    {15, 3},  // P
    {16, 2},  // S
    {17, 1},  // Cl
    {35, 1},  // Br
    {53, 1}   // I
};

// Bond lengths in Angstrom
static const std::map<std::pair<int,int>, float> bond_lengths = {
    {{1, 1}, 0.74f},   // H-H
    {{1, 6}, 1.09f},   // H-C
    {{1, 7}, 1.01f},   // H-N
    {{1, 8}, 0.96f},   // H-O
    {{6, 6}, 1.54f},   // C-C
    {{6, 7}, 1.47f},   // C-N
    {{6, 8}, 1.43f},   // C-O
    {{7, 7}, 1.45f},   // N-N
    {{7, 8}, 1.40f},   // N-O
    {{8, 8}, 1.48f},   // O-O
};

bool MoleculeBuilder::build_from_smiles(const std::string& smiles, Molecule& mol) {
    error_msg.clear();
    mol.clear();
    
    if (smiles.empty()) {
        error_msg = "Empty SMILES string";
        return false;
    }
    
    // Parse SMILES to build molecular graph
    if (!parse_smiles(smiles, mol)) {
        return false;
    }
    
    // Add hydrogen atoms based on valence
    add_hydrogens(mol);
    
    // Generate 3D coordinates
    generate_3d_coordinates(mol);
    
    // Calculate molecular formula
    calculate_molecular_formula(mol);
    
    return true;
}

bool MoleculeBuilder::parse_smiles(const std::string& smiles, Molecule& mol) {
    std::stack<int> branch_stack;
    std::vector<std::pair<int, int>> ring_bonds; // For handling rings
    std::map<int, int> ring_closures; // ring number -> atom index
    
    int current_atom = -1;
    bool in_brackets = false;
    std::string current_token;
    
    for (size_t i = 0; i < smiles.length(); ++i) {
        char c = smiles[i];
        
        if (c == '[') {
            in_brackets = true;
            current_token.clear();
            continue;
        } else if (c == ']') {
            in_brackets = false;
            if (!current_token.empty()) {
                Atom atom;
                if (!parse_atom(current_token, atom)) {
                    error_msg = "Invalid atom specification: " + current_token;
                    return false;
                }
                mol.atoms.push_back(atom);
                int new_atom_idx = mol.atoms.size() - 1;
                
                if (current_atom >= 0) {
                    Bond bond;
                    bond.atom1 = current_atom;
                    bond.atom2 = new_atom_idx;
                    bond.order = 1;
                    bond.aromatic = false;
                    mol.bonds.push_back(bond);
                    
                    mol.atoms[current_atom].neighbors.push_back(new_atom_idx);
                    mol.atoms[new_atom_idx].neighbors.push_back(current_atom);
                }
                current_atom = new_atom_idx;
                current_token.clear();
            }
            continue;
        }
        
        if (in_brackets) {
            current_token += c;
            continue;
        }
        
        // Handle simple cases outside brackets
        if (std::isalpha(c)) {
            // Element symbol
            std::string element;
            element += c;
            
            // Check for two-letter element (like Cl, Br)
            if (i + 1 < smiles.length() && std::islower(smiles[i + 1])) {
                element += smiles[i + 1];
                i++;
            }
            
            Atom atom;
            atom.element = get_atomic_number(element);
            if (atom.element == 0) {
                error_msg = "Unknown element: " + element;
                return false;
            }
            atom.valence = get_default_valence(atom.element);
            atom.bonds_count = 0;
            atom.aromatic = (c >= 'a' && c <= 'z'); // lowercase = aromatic
            atom.x = atom.y = atom.z = 0.0f;
            
            mol.atoms.push_back(atom);
            int new_atom_idx = mol.atoms.size() - 1;
            
            if (current_atom >= 0) {
                Bond bond;
                bond.atom1 = current_atom;
                bond.atom2 = new_atom_idx;
                bond.order = 1;
                bond.aromatic = atom.aromatic;
                mol.bonds.push_back(bond);
                
                mol.atoms[current_atom].neighbors.push_back(new_atom_idx);
                mol.atoms[new_atom_idx].neighbors.push_back(current_atom);
                mol.atoms[current_atom].bonds_count++;
                mol.atoms[new_atom_idx].bonds_count++;
            }
            current_atom = new_atom_idx;
            
        } else if (c == '(') {
            // Start branch
            branch_stack.push(current_atom);
            
        } else if (c == ')') {
            // End branch
            if (branch_stack.empty()) {
                error_msg = "Unmatched closing parenthesis";
                return false;
            }
            current_atom = branch_stack.top();
            branch_stack.pop();
            
        } else if (c == '=') {
            // Double bond - modify last bond
            if (!mol.bonds.empty()) {
                mol.bonds.back().order = 2;
            }
            
        } else if (c == '#') {
            // Triple bond - modify last bond
            if (!mol.bonds.empty()) {
                mol.bonds.back().order = 3;
            }
            
        } else if (std::isdigit(c)) {
            // Ring closure
            int ring_num = c - '0';
            auto it = ring_closures.find(ring_num);
            if (it == ring_closures.end()) {
                // First occurrence - store atom
                ring_closures[ring_num] = current_atom;
            } else {
                // Second occurrence - create bond
                Bond bond;
                bond.atom1 = it->second;
                bond.atom2 = current_atom;
                bond.order = 1;
                bond.aromatic = false;
                mol.bonds.push_back(bond);
                
                mol.atoms[it->second].neighbors.push_back(current_atom);
                mol.atoms[current_atom].neighbors.push_back(it->second);
                mol.atoms[it->second].bonds_count++;
                mol.atoms[current_atom].bonds_count++;
                
                ring_closures.erase(it);
            }
        }
    }
    
    if (!branch_stack.empty()) {
        error_msg = "Unmatched opening parenthesis";
        return false;
    }
    
    if (!ring_closures.empty()) {
        error_msg = "Unclosed ring";
        return false;
    }
    
    return true;
}

bool MoleculeBuilder::parse_atom(const std::string& atom_str, Atom& atom) {
    // Simple parsing for bracketed atoms like [CH4], [NH3], etc.
    // For now, just extract the element symbol
    std::string element;
    for (char c : atom_str) {
        if (std::isalpha(c)) {
            element += c;
        }
    }
    
    atom.element = get_atomic_number(element);
    if (atom.element == 0) {
        return false;
    }
    
    atom.valence = get_default_valence(atom.element);
    atom.bonds_count = 0;
    atom.aromatic = false;
    atom.x = atom.y = atom.z = 0.0f;
    
    return true;
}

int MoleculeBuilder::get_atomic_number(const std::string& symbol) {
    auto it = element_map.find(symbol);
    int result = (it != element_map.end()) ? it->second : 0;
    // Debug output
    // std::cout << "get_atomic_number: " << symbol << " -> " << result << std::endl;
    return result;
}

int MoleculeBuilder::get_default_valence(int atomic_num) {
    auto it = default_valences.find(atomic_num);
    return (it != default_valences.end()) ? it->second : 4;
}

void MoleculeBuilder::add_hydrogens(Molecule& mol) {
    int initial_atom_count = mol.atoms.size();
    
    for (int i = 0; i < initial_atom_count; ++i) {
        Atom& atom = mol.atoms[i];
        int missing_bonds = atom.valence - atom.bonds_count;
        
        // Add hydrogens for missing bonds
        for (int h = 0; h < missing_bonds; ++h) {
            Atom hydrogen;
            hydrogen.element = 1; // Hydrogen
            hydrogen.valence = 1;
            hydrogen.bonds_count = 1;
            hydrogen.aromatic = false;
            hydrogen.x = hydrogen.y = hydrogen.z = 0.0f;
            
            mol.atoms.push_back(hydrogen);
            int h_idx = mol.atoms.size() - 1;
            
            // Create bond
            Bond bond;
            bond.atom1 = i;
            bond.atom2 = h_idx;
            bond.order = 1;
            bond.aromatic = false;
            mol.bonds.push_back(bond);
            
            // Update connectivity
            atom.neighbors.push_back(h_idx);
            mol.atoms[h_idx].neighbors.push_back(i);
            atom.bonds_count++;
        }
    }
}

void MoleculeBuilder::generate_3d_coordinates(Molecule& mol) {
    if (mol.atoms.empty()) return;
    
    // Place first atom at origin
    mol.atoms[0].x = 0.0f;
    mol.atoms[0].y = 0.0f;
    mol.atoms[0].z = 0.0f;
    
    if (mol.atoms.size() == 1) return;
    
    // Place second atom along x-axis
    if (mol.atoms.size() > 1) {
        float bond_len = get_bond_length(mol.atoms[0].element, mol.atoms[1].element);
        mol.atoms[1].x = bond_len;
        mol.atoms[1].y = 0.0f;
        mol.atoms[1].z = 0.0f;
    }
    
    // Place remaining atoms using geometric constraints
    for (size_t i = 2; i < mol.atoms.size(); ++i) {
        place_atom_3d(mol, i);
    }
}

void MoleculeBuilder::place_atom_3d(Molecule& mol, int atom_idx, int ref_atom1, int ref_atom2) {
    Atom& atom = mol.atoms[atom_idx];
    
    // Find a bonded atom as reference
    int bonded_atom = -1;
    for (int neighbor : atom.neighbors) {
        if (neighbor < atom_idx) { // Only consider already placed atoms
            bonded_atom = neighbor;
            break;
        }
    }
    
    if (bonded_atom == -1) {
        // No bonded atom found, place randomly
        atom.x = static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
        atom.y = static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
        atom.z = static_cast<float>(rand()) / RAND_MAX * 2.0f - 1.0f;
        return;
    }
    
    Atom& ref_atom = mol.atoms[bonded_atom];
    float bond_len = get_bond_length(atom.element, ref_atom.element);
    
    // Find second reference atom
    int second_ref = -1;
    for (int neighbor : ref_atom.neighbors) {
        if (neighbor < atom_idx && neighbor != bonded_atom) {
            second_ref = neighbor;
            break;
        }
    }
    
    if (second_ref == -1) {
        // Only one reference - place along a reasonable direction
        float angle = static_cast<float>(atom_idx) * 0.5f; // Vary angle for different atoms
        atom.x = ref_atom.x + bond_len * cos(angle);
        atom.y = ref_atom.y + bond_len * sin(angle);
        atom.z = ref_atom.z;
    } else {
        // Two references - place with proper angle
        Atom& second_ref_atom = mol.atoms[second_ref];
        
        // Vector from second_ref to ref_atom
        float ref_vec_x = ref_atom.x - second_ref_atom.x;
        float ref_vec_y = ref_atom.y - second_ref_atom.y;
        float ref_vec_z = ref_atom.z - second_ref_atom.z;
        normalize_vector(ref_vec_x, ref_vec_y, ref_vec_z);
        
        // Create a perpendicular vector for the new bond
        float bond_angle = get_bond_angle(ref_atom.element);
        float cos_angle = cos(bond_angle);
        float sin_angle = sin(bond_angle);
        
        // Place atom with tetrahedral geometry
        atom.x = ref_atom.x + bond_len * (cos_angle * ref_vec_x + sin_angle * ref_vec_y);
        atom.y = ref_atom.y + bond_len * (cos_angle * ref_vec_y - sin_angle * ref_vec_x);
        atom.z = ref_atom.z + bond_len * sin_angle * 0.5f; // Add some Z component
    }
}

float MoleculeBuilder::get_bond_length(int elem1, int elem2) {
    std::pair<int, int> key = {std::min(elem1, elem2), std::max(elem1, elem2)};
    auto it = bond_lengths.find(key);
    return (it != bond_lengths.end()) ? it->second : 1.5f; // Default bond length
}

float MoleculeBuilder::get_bond_angle(int center_elem, int bond_order) {
    // Return bond angles in radians
    switch (center_elem) {
        case 6: // Carbon
            return (109.5f * M_PI / 180.0f); // Tetrahedral
        case 7: // Nitrogen  
            return (107.0f * M_PI / 180.0f); // Slightly compressed tetrahedral
        case 8: // Oxygen
            return (104.5f * M_PI / 180.0f); // Water-like angle
        default:
            return (109.5f * M_PI / 180.0f); // Default tetrahedral
    }
}

void MoleculeBuilder::normalize_vector(float& x, float& y, float& z) {
    float len = sqrt(x*x + y*y + z*z);
    if (len > 0.0f) {
        x /= len;
        y /= len;
        z /= len;
    }
}

void MoleculeBuilder::cross_product(float ax, float ay, float az, 
                                   float bx, float by, float bz,
                                   float& cx, float& cy, float& cz) {
    cx = ay * bz - az * by;
    cy = az * bx - ax * bz;
    cz = ax * by - ay * bx;
}

void MoleculeBuilder::calculate_molecular_formula(Molecule& mol) {
    std::map<int, int> element_count;
    
    // Count atoms by element
    for (const Atom& atom : mol.atoms) {
        element_count[atom.element]++;
    }
    
    // Build formula string (C, H, then others alphabetically)
    std::ostringstream formula;
    
    // Carbon first
    auto carbon_it = element_count.find(6);
    if (carbon_it != element_count.end() && carbon_it->second > 0) {
        formula << "C";
        if (carbon_it->second > 1) {
            formula << carbon_it->second;
        }
        element_count.erase(6);
    }
    
    // Hydrogen second
    auto hydrogen_it = element_count.find(1);
    if (hydrogen_it != element_count.end() && hydrogen_it->second > 0) {
        formula << "H";
        if (hydrogen_it->second > 1) {
            formula << hydrogen_it->second;
        }
        element_count.erase(1);
    }
    
    // Other elements alphabetically
    for (const auto& pair : element_count) {
        if (pair.second > 0) {  // Only process elements with count > 0
            std::string symbol = element_symbol(pair.first);
            formula << symbol;
            if (pair.second > 1) {
                formula << pair.second;
            }
        }
    }
    
    mol.formula = formula.str();
}

std::string MoleculeBuilder::element_symbol(int atomic_num) {
    auto it = element_symbols.find(atomic_num);
    return (it != element_symbols.end()) ? it->second : "X";
}

} // namespace lightweight_mol