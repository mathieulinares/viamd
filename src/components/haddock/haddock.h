#pragma once

#include <core/md_str.h>

struct ApplicationState;

// Loads a CNS distance restraint table (the "assign" lines of a HADDOCK .tbl file) for the current system
bool haddock_restraints_load(ApplicationState* state, md_str_t path);

// Names the models of the ensemble at path (one per frame, in order), as shown in the Map fit table
void haddock_ensemble_names_set(md_str_t path, const md_str_t* names, size_t count);
