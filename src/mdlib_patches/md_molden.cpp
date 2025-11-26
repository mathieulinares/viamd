#include "md_molden.h"

#include <md_molecule.h>
#include <md_util.h>

#include <core/md_common.h>
#include <core/md_str.h>
#include <core/md_allocator.h>
#include <core/md_log.h>
#include <core/md_os.h>
#include <core/md_array.h>
#include <core/md_parse.h>

#include <string.h>

#define MD_MOLDEN_MOL_MAGIC  0xbabc3bf677bfaf67

typedef struct molden_molecule {
    uint64_t magic;
    struct md_allocator_i* allocator;
} molden_molecule_t;

static bool md_molden_data_parse(md_molden_data_t* data, md_buffered_reader_t* reader, struct md_allocator_i* alloc) {
    ASSERT(data);
    ASSERT(reader);
    ASSERT(alloc);
    
    str_t line;
    str_t tokens[16];
    bool in_atoms_section = false;
    bool found_molden_format = false;
    
    // Look for [Molden Format] header
    while (md_buffered_reader_extract_line(&line, reader)) {
        line = str_trim(line);
        if (str_empty(line)) continue;
        
        if (str_eq_ignore_case(line, STR_LIT("[Molden Format]"))) {
            found_molden_format = true;
            break;
        }
    }
    
    if (!found_molden_format) {
        MD_LOG_DEBUG("Molden format header not found, attempting to parse anyway");
        md_buffered_reader_reset(reader);
    }
    
    // Look for [Atoms] section
    while (md_buffered_reader_extract_line(&line, reader)) {
        line = str_trim(line);
        if (str_empty(line)) continue;
        
        // Check for [Atoms] section with optional units specification
        if (str_eq_ignore_case(str_substr(line, 0, 7), STR_LIT("[Atoms]"))) {
            in_atoms_section = true;
            break;
        }
    }
    
    if (!in_atoms_section) {
        MD_LOG_ERROR("Failed to find [Atoms] section in Molden file");
        return false;
    }
    
    // Read atoms
    // Format: Element_Symbol  Atom_Number  Atomic_Number  X  Y  Z
    // Units are typically Angstrom by default or specified in [Atoms] header
    md_array(md_molden_atom_t) atoms = 0;
    
    while (md_buffered_reader_extract_line(&line, reader)) {
        line = str_trim(line);
        if (str_empty(line)) continue;
        
        // Check for next section (starts with '[')
        if (line.ptr[0] == '[') break;
        
        size_t num_tokens = extract_tokens(tokens, ARRAY_SIZE(tokens), &line);
        if (num_tokens < 6) {
            MD_LOG_DEBUG("Skipping line with insufficient tokens: %.*s", (int)line.len, line.ptr);
            continue;
        }
        
        md_molden_atom_t atom = {0};
        
        // Parse element symbol (first token)
        str_t elem_str = str_trim(tokens[0]);
        str_copy_to_char_buf(atom.element_symbol, sizeof(atom.element_symbol), elem_str);
        
        // Validate and parse atom number (second token)
        str_t tok1 = str_trim(tokens[1]);
        str_t tok2 = str_trim(tokens[2]);
        str_t tok3 = str_trim(tokens[3]);
        str_t tok4 = str_trim(tokens[4]);
        str_t tok5 = str_trim(tokens[5]);
        
        if (!is_int(tok1) || !is_int(tok2)) {
            MD_LOG_DEBUG("Skipping line with invalid integer tokens: %.*s", (int)line.len, line.ptr);
            continue;
        }
        
        atom.atom_number = (int)parse_int(tok1);
        atom.atomic_number = (int)parse_int(tok2);
        
        // Parse coordinates (tokens 3, 4, 5)
        atom.x = (float)parse_float(tok3);
        atom.y = (float)parse_float(tok4);
        atom.z = (float)parse_float(tok5);
        
        md_array_push(atoms, atom, alloc);
    }
    
    if (md_array_size(atoms) == 0) {
        MD_LOG_ERROR("No atoms parsed from Molden file");
        return false;
    }
    
    // Copy to output
    data->num_atoms = md_array_size(atoms);
    data->atoms = (md_molden_atom_t*)md_alloc(alloc, sizeof(md_molden_atom_t) * data->num_atoms);
    MEMCPY(data->atoms, atoms, sizeof(md_molden_atom_t) * data->num_atoms);
    
    md_array_free(atoms, alloc);
    return true;
}

bool md_molden_data_parse_str(md_molden_data_t* data, str_t str, struct md_allocator_i* alloc) {
    ASSERT(data);
    ASSERT(alloc);
    
    md_buffered_reader_t reader = md_buffered_reader_from_str(str);
    return md_molden_data_parse(data, &reader, alloc);
}

bool md_molden_data_parse_file(md_molden_data_t* data, str_t filename, struct md_allocator_i* alloc) {
    bool result = false;
    md_file_o* file = md_file_open(filename, MD_FILE_READ | MD_FILE_BINARY);
    if (file) {
        const int64_t cap = MEGABYTES(16);  // Molden files can be large
        char* buf = (char*)md_alloc(md_get_heap_allocator(), cap);
        
        md_buffered_reader_t reader = md_buffered_reader_from_file(buf, cap, file);
        result = md_molden_data_parse(data, &reader, alloc);
        
        md_free(md_get_heap_allocator(), buf, cap);
        md_file_close(file);
    } else {
        MD_LOG_ERROR("Could not open file '%.*s'", (int)filename.len, filename.ptr);
    }
    return result;
}

void md_molden_data_free(md_molden_data_t* data, struct md_allocator_i* alloc) {
    ASSERT(data);
    if (data->atoms) {
        md_free(alloc, data->atoms, sizeof(md_molden_atom_t) * data->num_atoms);
    }
    MEMSET(data, 0, sizeof(md_molden_data_t));
}

bool md_molden_molecule_init(struct md_molecule_t* mol, const md_molden_data_t* data, struct md_allocator_i* alloc) {
    ASSERT(mol);
    ASSERT(data);
    ASSERT(alloc);
    
    MEMSET(mol, 0, sizeof(md_molecule_t));
    
    const size_t capacity = ROUND_UP(data->num_atoms, 16);
    
    mol->atom.x       = md_array_create(float, capacity, alloc);
    mol->atom.y       = md_array_create(float, capacity, alloc);
    mol->atom.z       = md_array_create(float, capacity, alloc);
    mol->atom.element = md_array_create(uint8_t, capacity, alloc);
    mol->atom.type    = md_array_create(md_label_t, capacity, alloc);
    mol->atom.flags   = md_array_create(md_flags_t, capacity, alloc);
    
    for (size_t i = 0; i < data->num_atoms; ++i) {
        const md_molden_atom_t* atom = &data->atoms[i];
        
        mol->atom.count += 1;
        mol->atom.x[i] = atom->x;
        mol->atom.y[i] = atom->y;
        mol->atom.z[i] = atom->z;
        
        // Create element symbol string once and reuse
        str_t elem_str = {atom->element_symbol, strnlen(atom->element_symbol, sizeof(atom->element_symbol))};
        
        // Set element from atomic number
        if (atom->atomic_number > 0 && atom->atomic_number <= 118) {
            mol->atom.element[i] = (uint8_t)atom->atomic_number;
        } else {
            // Try to get from symbol
            mol->atom.element[i] = md_util_element_lookup(elem_str);
        }
        
        // Set atom type label from element symbol
        mol->atom.type[i] = make_label(elem_str);
        mol->atom.flags[i] = 0;
    }
    
    return true;
}

static bool molden_init_from_str(md_molecule_t* mol, str_t str, const void* arg, md_allocator_i* alloc) {
    (void)arg;
    md_molden_data_t data = {0};
    bool success = false;
    if (md_molden_data_parse_str(&data, str, md_get_heap_allocator())) {
        success = md_molden_molecule_init(mol, &data, alloc);
    }
    md_molden_data_free(&data, md_get_heap_allocator());
    
    return success;
}

static bool molden_init_from_file(md_molecule_t* mol, str_t filename, const void* arg, md_allocator_i* alloc) {
    (void)arg;
    md_molden_data_t data = {0};
    bool success = false;
    if (md_molden_data_parse_file(&data, filename, md_get_heap_allocator())) {
        success = md_molden_molecule_init(mol, &data, alloc);
    }
    md_molden_data_free(&data, md_get_heap_allocator());
    
    return success;
}

static md_molecule_loader_i molden_api = {
    molden_init_from_str,
    molden_init_from_file,
};

md_molecule_loader_i* md_molden_molecule_api(void) {
    return &molden_api;
}
