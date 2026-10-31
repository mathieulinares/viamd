#pragma once

#include <md_system.h>

namespace dataset {

inline bool matches_particle_type_override(const md_system_t& sys, size_t type_idx,
                                           md_atomic_number_t loaded_element, str_t name,
                                           str_t ff_type, md_atomic_number_t expected_element,
                                           int first_atom) {
    return loaded_element == expected_element &&
           str_eq(md_particle_type_name(&sys.atom.type, type_idx), name) &&
           str_eq(md_particle_type_ff_type(&sys.atom.type, type_idx), ff_type) &&
           (first_atom < 0 ||
            ((size_t)first_atom < sys.atom.count && sys.atom.type_idx[first_atom] == type_idx));
}

}
