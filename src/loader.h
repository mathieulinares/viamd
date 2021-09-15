#pragma once

#include <core/md_str.h>

struct md_allocator_i;
struct md_molecule_t;
struct md_trajectory_i;

namespace load {
namespace mol {
    bool is_extension_supported(str_t filename);

    bool load_string_pdb(md_molecule_t* mol, str_t string, md_allocator_i* alloc);
    bool load_string_gro(md_molecule_t* mol, str_t string, md_allocator_i* alloc);
    bool load_file(md_molecule_t* mol, str_t filename, md_allocator_i* alloc);
    bool free(md_molecule_t* mol);
}

namespace traj {
    bool is_extension_supported(str_t filename);

    bool open_file(md_trajectory_i* traj, str_t filename, const md_molecule_t* mol, md_allocator_i* alloc);
    bool close(md_trajectory_i* traj);
}

}  // namespace load