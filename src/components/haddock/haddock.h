#pragma once

#include <core/md_str.h>

struct ApplicationState;

// Loads a CNS distance restraint table (the "assign" lines of a HADDOCK .tbl file) for the current system
bool haddock_restraints_load(ApplicationState* state, md_str_t path);
