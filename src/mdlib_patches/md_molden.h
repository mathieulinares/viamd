#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#include <core/md_str.h>

#ifdef __cplusplus
extern "C" {
#endif

struct md_allocator_i;
struct md_molecule_t;
struct md_molecule_loader_i;

typedef struct md_molden_atom_t {
    char element_symbol[4];
    int atomic_number;
    int atom_number;  // Index in file
    float x;
    float y;
    float z;
} md_molden_atom_t;

typedef struct md_molden_data_t {
    size_t num_atoms;
    md_molden_atom_t* atoms;
} md_molden_data_t;

// RAW FUNCTIONS
// Parse a text-blob as Molden
bool md_molden_data_parse_str(md_molden_data_t* data, str_t str, struct md_allocator_i* alloc);
bool md_molden_data_parse_file(md_molden_data_t* data, str_t filename, struct md_allocator_i* alloc);
void md_molden_data_free(md_molden_data_t* data, struct md_allocator_i* alloc);

// MOLECULE
bool md_molden_molecule_init(struct md_molecule_t* mol, const md_molden_data_t* data, struct md_allocator_i* alloc);

struct md_molecule_loader_i* md_molden_molecule_api(void);

#ifdef __cplusplus
}
#endif
