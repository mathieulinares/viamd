#include "utest.h"

#include <loader.h>
#include <components/dataset/dataset_particle_type.h>

#include <core/md_str.h>
#include <core/md_arena_allocator.h>
#include <md_system.h>
#include <md_util.h>
#include <core/md_os.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#if !defined(_WIN32)
#include <unistd.h>
#endif

#if MD_AMBER_NETCDF
#include <netcdf.h>
#include <cstdio>

static bool write_amber_loader_fixture(const char* path) {
    int ncid, frame_dim, atom_dim, spatial_dim, coordinates_var, spatial_var, time_var;
    if (nc_create(path, NC_CLOBBER, &ncid) != NC_NOERR) {
        return false;
    }
    const bool status =
        nc_put_att_text(ncid, NC_GLOBAL, "Conventions", 5, "AMBER") == NC_NOERR &&
        nc_put_att_text(ncid, NC_GLOBAL, "ConventionVersion", 3, "1.0") == NC_NOERR &&
        nc_def_dim(ncid, "frame", 2, &frame_dim) == NC_NOERR &&
        nc_def_dim(ncid, "atom", 1, &atom_dim) == NC_NOERR &&
        nc_def_dim(ncid, "spatial", 3, &spatial_dim) == NC_NOERR &&
        nc_def_var(ncid, "spatial", NC_CHAR, 1, &spatial_dim, &spatial_var) == NC_NOERR;
    if (!status) {
        nc_close(ncid);
        return false;
    }
    const int coordinate_dims[3] = { frame_dim, atom_dim, spatial_dim };
    const int frame_dims[1] = { frame_dim };
    if (nc_def_var(ncid, "coordinates", NC_FLOAT, 3, coordinate_dims, &coordinates_var) != NC_NOERR ||
        nc_put_att_text(ncid, coordinates_var, "units", 8, "angstrom") != NC_NOERR ||
        nc_def_var(ncid, "time", NC_DOUBLE, 1, frame_dims, &time_var) != NC_NOERR ||
        nc_put_att_text(ncid, time_var, "units", 10, "picosecond") != NC_NOERR ||
        nc_enddef(ncid) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }
    const float coordinates[6] = {1, 2, 3, 4, 5, 6};
    const double times[2] = {0, 1};
    const bool ok =
        nc_put_var_text(ncid, spatial_var, "xyz") == NC_NOERR &&
        nc_put_var_float(ncid, coordinates_var, coordinates) == NC_NOERR &&
        nc_put_var_double(ncid, time_var, times) == NC_NOERR;
    return nc_close(ncid) == NC_NOERR && ok;
}

static bool write_amber_loader_restart(const char* path) {
    int ncid, atom_dim, spatial_dim, spatial_var, coordinates_var;
    if (nc_create(path, NC_CLOBBER, &ncid) != NC_NOERR) return false;
    if (nc_put_att_text(ncid, NC_GLOBAL, "Conventions", 12, "AMBERRESTART") != NC_NOERR ||
        nc_put_att_text(ncid, NC_GLOBAL, "ConventionVersion", 3, "1.0") != NC_NOERR ||
        nc_def_dim(ncid, "atom", 12, &atom_dim) != NC_NOERR ||
        nc_def_dim(ncid, "spatial", 3, &spatial_dim) != NC_NOERR ||
        nc_def_var(ncid, "spatial", NC_CHAR, 1, &spatial_dim, &spatial_var) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }
    const int coordinate_dims[2] = {atom_dim, spatial_dim};
    if (nc_def_var(ncid, "coordinates", NC_DOUBLE, 2, coordinate_dims, &coordinates_var) != NC_NOERR ||
        nc_put_att_text(ncid, coordinates_var, "units", 8, "angstrom") != NC_NOERR ||
        nc_enddef(ncid) != NC_NOERR) {
        nc_close(ncid);
        return false;
    }
    double coordinates[36];
    for (size_t i = 0; i < 36; ++i) coordinates[i] = (double)(i + 1);
    const bool ok = nc_put_var_text(ncid, spatial_var, "xyz") == NC_NOERR &&
                    nc_put_var_double(ncid, coordinates_var, coordinates) == NC_NOERR;
    return nc_close(ncid) == NC_NOERR && ok;
}
#endif

static bool write_periodic_amber_topology(const char* path, int first_molecule_atoms) {
    std::ifstream source(VIAMD_AMBER_DATA_DIR "/nma.prmtop", std::ios::binary);
    if (!source) return false;
    std::string text((std::istreambuf_iterator<char>(source)), std::istreambuf_iterator<char>());
    const size_t flag = text.find("%FLAG POINTERS");
    const size_t format = flag == std::string::npos ? flag : text.find("%FORMAT", flag);
    const size_t format_end = format == std::string::npos ? format : text.find('\n', format);
    const size_t first_end = format_end == std::string::npos ? format_end : text.find('\n', format_end + 1);
    const size_t second_end = first_end == std::string::npos ? first_end : text.find('\n', first_end + 1);
    if (second_end == std::string::npos) return false;
    const size_t ifbox = second_end + 1 + 7 * 8;
    if (ifbox + 8 > text.size() || text.compare(ifbox, 8, "       0") != 0) return false;
    text.replace(ifbox, 8, "       1");
    char molecule_counts[32];
    std::snprintf(molecule_counts, sizeof(molecule_counts), "%8d%8d\n",
                  first_molecule_atoms, 12 - first_molecule_atoms);
    text += "\n%FLAG SOLVENT_POINTERS\n%FORMAT(10I8)\n       1       2       2\n"
            "%FLAG ATOMS_PER_MOLECULE\n%FORMAT(10I8)\n";
    text += molecule_counts;
    std::FILE* output = std::fopen(path, "wb");
    if (!output) return false;
    const bool ok = std::fwrite(text.data(), 1, text.size(), output) == text.size();
    return std::fclose(output) == 0 && ok;
}

/* loader is the table that decides which reader gets a file. It is three parallel arrays indexed by
 * LoaderType plus a lookup, and the arrays are maintained by hand behind #if MD_HDF5 guards - so
 * the failure mode is not a crash but a silent shift, where adding a format one place and not
 * another makes every entry after it describe the wrong reader. Most of what follows checks
 * that the three arrays still agree with each other and with the enum. */

UTEST(viamd_loader, every_type_has_a_name_and_an_extension) {
    /* Undefined is the one entry allowed an empty extension - it is what a lookup returns when it
     * recognises nothing. */
    EXPECT_TRUE(str_empty(loader::type_ext(LoaderType_Undefined)));

    for (LoaderType t = LoaderType_Undefined + 1; t < LoaderType_COUNT; ++t) {
        EXPECT_FALSE(str_empty(loader::type_name(t)));
        EXPECT_FALSE(str_empty(loader::type_ext(t)));
        /* A type that can load nothing would never be reachable. */
        EXPECT_NE(0u, loader::type_flags(t) & (LoaderFlag_System | LoaderFlag_Trajectory | LoaderFlag_Supplemental));
    }
}

UTEST(viamd_loader, extensions_are_unique) {
    /* Two types claiming one extension makes type_from_ext return whichever comes first, and the
     * other format becomes unreachable without anyone noticing. */
    for (LoaderType a = LoaderType_Undefined + 1; a < LoaderType_COUNT; ++a) {
        for (LoaderType b = a + 1; b < LoaderType_COUNT; ++b) {
            EXPECT_FALSE(str_eq_ignore_case(loader::type_ext(a), loader::type_ext(b)));
        }
    }
}

UTEST(viamd_loader, an_extension_round_trips_to_its_own_type) {
    /* The property that keeps the tables aligned: whatever extension a type advertises must look
     * that same type back up. A shifted array fails this immediately. */
    for (LoaderType t = LoaderType_Undefined + 1; t < LoaderType_COUNT; ++t) {
        EXPECT_EQ(t, loader::type_from_ext(loader::type_ext(t)));
    }
}

UTEST(viamd_loader, extension_lookup_ignores_case_and_rejects_the_unknown) {
    EXPECT_EQ((LoaderType)LoaderType_PDB, loader::type_from_ext(STR_LIT("pdb")));
    EXPECT_EQ((LoaderType)LoaderType_PDB, loader::type_from_ext(STR_LIT("PDB")));
    EXPECT_EQ((LoaderType)LoaderType_PDB, loader::type_from_ext(STR_LIT("Pdb")));
    EXPECT_EQ((LoaderType)LoaderType_GRO, loader::type_from_ext(STR_LIT("GRO")));

    EXPECT_EQ((LoaderType)LoaderType_Undefined, loader::type_from_ext(STR_LIT("")));
    EXPECT_EQ((LoaderType)LoaderType_Undefined, loader::type_from_ext(STR_LIT("txt")));
    /* A prefix of a real extension is not a match. */
    EXPECT_EQ((LoaderType)LoaderType_Undefined, loader::type_from_ext(STR_LIT("pd")));
    EXPECT_EQ((LoaderType)LoaderType_Undefined, loader::type_from_ext(STR_LIT("pdbx")));
#if MD_AMBER_NETCDF
    EXPECT_EQ((LoaderType)LoaderType_AMBER_NETCDF, loader::type_from_ext(STR_LIT("NC")));
    EXPECT_EQ((LoaderType)LoaderType_AMBER_NETCDF, loader::type_from_ext(STR_LIT("netcdf")));
    loader::LoaderState amber = {};
    loader::init(&amber, STR_LIT(VIAMD_TEST_DATA_DIR "/not-an-amber-trajectory.nc"));
    EXPECT_EQ((LoaderType)LoaderType_Undefined, amber.type);
    EXPECT_NE(0u, amber.flags & LoaderFlag_RequiresDialogue);
#endif
}

UTEST(viamd_loader, amber_mdout_is_detected_and_supplements_run_energy) {
    const str_t path = STR_LIT(VIAMD_AMBER_ENERGY_TEST_DATA_DIR "/amber-mdout.out");
    loader::LoaderState reader = {};
    loader::init(&reader, path);
    ASSERT_EQ((LoaderType)LoaderType_AMBER_ENERGY, reader.type);
    EXPECT_NE(0u, reader.flags & LoaderFlag_Supplemental);
    EXPECT_NE(0u, reader.flags & LoaderFlag_Temporal);

    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    const double times[] = {0.0, 1.0, 2.0};
    const md_attribute_desc_t time_desc = {
        .path = STR_LIT("run/test/time"),
        .format = {.type = MD_ATTRIBUTE_TYPE_F64, .components = 1, .rank = 1, .shape = {3}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL,
        .unit = md_unit_picosecond(),
        .data = times,
        .byte_size = sizeof(times),
    };
    ASSERT_NE(MD_ATTRIBUTE_INVALID, md_attributes_create(&sys.attributes, &time_desc));
    ASSERT_TRUE(loader::load_supplemental(&sys, path, reader, STR_LIT("run/test")));
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("run/test/amber_energy/etot")) != nullptr);
    md_system_free(&sys);
}

UTEST(viamd_loader, amber_mden_is_detected_and_supplements_run_energy) {
    const str_t path = STR_LIT(VIAMD_AMBER_ENERGY_TEST_DATA_DIR "/amber-mden.mden");
    EXPECT_EQ((LoaderType)LoaderType_AMBER_ENERGY, loader::type_from_ext(STR_LIT("mden")));
    loader::LoaderState reader = {};
    loader::init(&reader, path);
    ASSERT_EQ((LoaderType)LoaderType_AMBER_ENERGY, reader.type);
    EXPECT_NE(0u, reader.flags & LoaderFlag_Temporal);
    EXPECT_EQ(0u, reader.flags & LoaderFlag_RequiresDialogue);

    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    const double times[] = {0.0, 1.0, 2.0};
    const md_attribute_desc_t time_desc = {
        .path = STR_LIT("run/test/time"),
        .format = {.type = MD_ATTRIBUTE_TYPE_F64, .components = 1, .rank = 1, .shape = {3}},
        .flags = MD_ATTRIBUTE_FLAG_TEMPORAL,
        .unit = md_unit_picosecond(),
        .data = times,
        .byte_size = sizeof(times),
    };
    ASSERT_NE(MD_ATTRIBUTE_INVALID, md_attributes_create(&sys.attributes, &time_desc));
    ASSERT_TRUE(loader::load_supplemental(&sys, path, reader, STR_LIT("run/test")));
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("run/test/amber_energy/etot")) != nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("run/test/amber_energy/eptot")) != nullptr);
    md_system_free(&sys);
}

UTEST(viamd_loader, amber_prmtop_loads_with_matching_ascii_restart) {
    const str_t topology = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.prmtop");
    const char* coordinates = VIAMD_AMBER_DATA_DIR "/nma.inpcrd";
    loader::LoaderState reader = {};
    loader::init(&reader, topology);
    ASSERT_EQ((LoaderType)LoaderType_AMBER_PRMTOP, reader.type);
    EXPECT_NE(0u, reader.flags & LoaderFlag_Topology);
    reader.coordinates = coordinates;

    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(loader::load(&sys, &state, topology, reader));
    EXPECT_EQ(12u, sys.atom.count);
    EXPECT_EQ(2u, sys.residue.count);
    EXPECT_EQ(11u, sys.bond.count);
    EXPECT_EQ(12u, state.num_atoms);
    EXPECT_NEAR(0.7520751, state.xyz[0].x, 1.0e-5);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("atom/charge")) != nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("atom/mass")) != nullptr);

    md_system_state_free(&state);
    md_system_free(&sys);
}

UTEST(viamd_loader, amber_prmtop_supplements_a_matching_pdb) {
    const str_t structure = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.pdb");
    const str_t topology = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.prmtop");
    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    md_system_state_t state = {.alloc = alloc};

    loader::LoaderState structure_reader = {};
    loader::init(&structure_reader, structure);
    ASSERT_EQ((LoaderType)LoaderType_PDB, structure_reader.type);
    ASSERT_TRUE(loader::load(&sys, &state, structure, structure_reader));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    const vec3_t initial_position = state.xyz[0];
    ASSERT_TRUE(sys.atom.hydrogen_count != nullptr);
    const uint8_t methyl_hydrogens = sys.atom.hydrogen_count[1];
    ASSERT_GT(methyl_hydrogens, 0u);
    sys.atom.hydrogen_count[1] = 0;
    const md_bond_idx_t user_bond = md_bond_find(&sys.bond, 4, 5);
    ASSERT_NE(-1, user_bond);
    sys.bond.pairs[user_bond] = {{5, 4}};
    sys.bond.flags[user_bond] = md_bond_flags_set_origin(sys.bond.flags[user_bond], MD_BOND_ORIGIN_USER);
    const md_particle_type_idx_t original_type = sys.atom.type_idx[0];
    sys.atom.type.radius[original_type] = 3.5f;
    sys.atom.type.color[original_type] = 0xff123456;

    loader::LoaderState topology_reader = {};
    loader::init(&topology_reader, topology, &sys);
    ASSERT_EQ((LoaderType)LoaderType_AMBER_PRMTOP, topology_reader.type);
    EXPECT_NE(0u, topology_reader.flags & LoaderFlag_Supplemental);
    EXPECT_EQ(0u, topology_reader.flags & LoaderFlag_RequiresDialogue);
    ASSERT_TRUE(loader::load_supplemental(&sys, topology, topology_reader, {}));

    EXPECT_EQ(12u, sys.atom.count);
    EXPECT_EQ(11u, sys.bond.count);
    const md_bond_idx_t preserved_bond = md_bond_find(&sys.bond, 4, 5);
    ASSERT_NE(-1, preserved_bond);
    EXPECT_EQ(MD_BOND_ORIGIN_USER, md_bond_origin(sys.bond.flags[preserved_bond]));
    EXPECT_NEAR(initial_position.x, state.xyz[0].x, 1.0e-6);
    EXPECT_NEAR(initial_position.y, state.xyz[0].y, 1.0e-6);
    EXPECT_NEAR(initial_position.z, state.xyz[0].z, 1.0e-6);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("atom/charge")) != nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("atom/mass")) != nullptr);
    EXPECT_FALSE(str_empty(md_particle_type_ff_type(&sys.atom.type, sys.atom.type_idx[0])));
    EXPECT_NEAR(3.5, sys.atom.type.radius[sys.atom.type_idx[0]], 1.0e-6);
    EXPECT_EQ(0xff123456u, sys.atom.type.color[sys.atom.type_idx[0]]);
    EXPECT_EQ(methyl_hydrogens, sys.atom.hydrogen_count[1]);

    md_system_state_free(&state);
    md_system_free(&sys);
}

UTEST(viamd_loader, amber_prmtop_rejects_mismatched_pdb_atom_names) {
    const str_t structure = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.pdb");
    const str_t topology = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.prmtop");
    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    md_system_state_t state = {.alloc = alloc};

    loader::LoaderState structure_reader = {};
    loader::init(&structure_reader, structure);
    ASSERT_TRUE(loader::load(&sys, &state, structure, structure_reader));
    const md_particle_type_idx_t atom_type = sys.atom.type_idx[0];
    sys.atom.type.name[atom_type] = make_label(STR_LIT("BAD"));
    const size_t bonds_before = sys.bond.count;

    loader::LoaderState topology_reader = {};
    loader::init(&topology_reader, topology, &sys);
    EXPECT_FALSE(loader::load_supplemental(&sys, topology, topology_reader, {}));
    EXPECT_EQ(bonds_before, sys.bond.count);

    md_system_state_free(&state);
    md_system_free(&sys);
}

UTEST(viamd_loader, amber_periodic_molecules_must_end_at_residue_boundaries) {
    const char* valid = "viamd_test_periodic_valid.prmtop";
    const char* invalid = "viamd_test_periodic_invalid.prmtop";
    ASSERT_TRUE(write_periodic_amber_topology(valid, 6));
    ASSERT_TRUE(write_periodic_amber_topology(invalid, 5));

    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    md_system_state_t state = {.alloc = alloc};
    loader::LoaderState reader = {};
    const str_t structure = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.pdb");
    loader::init(&reader, structure);
    ASSERT_TRUE(loader::load(&sys, &state, structure, reader));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    loader::init(&reader, str_from_cstr(valid), &sys);
    ASSERT_TRUE(loader::load_supplemental(&sys, str_from_cstr(valid), reader, {}));
    EXPECT_EQ(11u, sys.bond.count);
    loader::init(&reader, str_from_cstr(invalid), &sys);
    EXPECT_FALSE(loader::load_supplemental(&sys, str_from_cstr(invalid), reader, {}));
    EXPECT_EQ(11u, sys.bond.count);

    md_system_state_free(&state);
    md_system_free(&sys);
    std::remove(valid);
    std::remove(invalid);
}

UTEST(viamd_loader, amber_supplement_keeps_source_types_distinct_for_workspace_overrides) {
    const str_t structure = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.pdb");
    const str_t topology = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.prmtop");
    md_particle_type_idx_t original_first = 0, original_second = 0;

    for (int pass = 0; pass < 2; ++pass) {
        md_allocator_i* alloc = md_get_heap_allocator();
        md_system_t sys = {.alloc = alloc};
        sys.attributes.alloc = alloc;
        md_system_state_t state = {.alloc = alloc};
        loader::LoaderState structure_reader = {};
        loader::init(&structure_reader, structure);
        ASSERT_TRUE(loader::load(&sys, &state, structure, structure_reader));
        ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));

        const md_particle_type_idx_t source = sys.atom.type_idx[8];
        const md_atomic_number_t z = sys.atom.type.z[source];
        const float mass = sys.atom.type.mass[source];
        const float radius = sys.atom.type.radius[source];
        const uint32_t color = sys.atom.type.color[source];
        const md_particle_type_flags_t flags = sys.atom.type.flags[source];
        const md_particle_type_idx_t alternate = md_particle_type_add(
            &sys.atom.type, STR_LIT("ch3"), {}, z, mass, radius, color, flags, alloc);
        sys.atom.type_idx[8] = alternate;
        if (pass == 0) sys.atom.type.color[alternate] = 0xff123456;

        loader::LoaderState topology_reader = {};
        loader::init(&topology_reader, topology, &sys);
        ASSERT_TRUE(loader::load_supplemental(&sys, topology, topology_reader, {}));

        const md_particle_type_idx_t first = sys.atom.type_idx[1];
        const md_particle_type_idx_t second = sys.atom.type_idx[8];
        ASSERT_NE(first, second);
        const str_t name = md_particle_type_name(&sys.atom.type, first);
        const str_t ff_type = md_particle_type_ff_type(&sys.atom.type, first);
        EXPECT_TRUE(str_eq(name, md_particle_type_name(&sys.atom.type, second)));
        EXPECT_TRUE(str_eq(ff_type, md_particle_type_ff_type(&sys.atom.type, second)));
        EXPECT_EQ(sys.atom.type.z[first], sys.atom.type.z[second]);
        EXPECT_FALSE(dataset::matches_particle_type_override(sys, first, sys.atom.type.z[first], name, ff_type, z, 8));
        EXPECT_TRUE(dataset::matches_particle_type_override(sys, second, sys.atom.type.z[second], name, ff_type, z, 8));
        EXPECT_TRUE(dataset::matches_particle_type_override(sys, first, sys.atom.type.z[first], name, ff_type, z, -1));
        EXPECT_FALSE(dataset::matches_particle_type_override(sys, first, sys.atom.type.z[first], name, ff_type, z, 12));

        if (pass == 0) {
            EXPECT_EQ(0xff123456u, sys.atom.type.color[second]);
            original_first = first;
            original_second = second;
        } else {
            EXPECT_EQ(original_first, first);
            EXPECT_EQ(original_second, second);
            const size_t type_count = sys.atom.type.count;
            ASSERT_TRUE(loader::load_supplemental(&sys, topology, topology_reader, {}));
            EXPECT_EQ(type_count, sys.atom.type.count);
            EXPECT_EQ(first, sys.atom.type_idx[1]);
            EXPECT_EQ(second, sys.atom.type_idx[8]);
        }
        md_system_state_free(&state);
        md_system_free(&sys);
    }
}

#if MD_AMBER_NETCDF
UTEST(viamd_loader, amber_nma_dataset_imports_matching_trajectory_and_energy) {
    const str_t structure = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.pdb");
    const str_t topology = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.prmtop");
    const str_t trajectory = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.nc");
    const str_t energy_files[] = {
        STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.mdout"),
        STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.out"),
        STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.mden"),
    };
    const str_t run = STR_LIT("run/nma");
    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    md_system_state_t state = {.alloc = alloc};
    loader::LoaderState reader = {};

    loader::init(&reader, structure);
    ASSERT_TRUE(loader::load(&sys, &state, structure, reader));
    ASSERT_TRUE(md_util_system_infer(&sys, &state, MD_UTIL_INFER_ALL));
    loader::init(&reader, topology, &sys);
    ASSERT_TRUE(loader::load_supplemental(&sys, topology, reader, {}));
    ASSERT_TRUE(loader::publish_run(&sys, trajectory, run, 0));

    double reference[20] = {};
    for (size_t file = 0; file < ARRAY_SIZE(energy_files); ++file) {
        loader::init(&reader, energy_files[file], &sys);
        ASSERT_EQ((LoaderType)LoaderType_AMBER_ENERGY, reader.type);
        ASSERT_TRUE(loader::load_supplemental(&sys, energy_files[file], reader, run));
        const md_attribute_t* run_time = md_attributes_find(&sys.attributes, STR_LIT("run/nma/time"));
        const md_attribute_t* energy_time = md_attributes_find(&sys.attributes, STR_LIT("run/nma/amber_energy/time"));
        const md_attribute_t* etot = md_attributes_find(&sys.attributes, STR_LIT("run/nma/amber_energy/etot"));
        ASSERT_TRUE(run_time && energy_time && etot);
        ASSERT_EQ(20u, run_time->format.shape[0]);
        ASSERT_LE(etot->format.shape[0], 128u);
        double values[128];
        ASSERT_EQ(etot->format.shape[0],
                  md_attribute_extract_f64(values, ARRAY_SIZE(values), etot, md_attribute_slice_all(), etot->unit));
        for (size_t frame = 0; frame < ARRAY_SIZE(reference); ++frame) {
            size_t row = 0;
            ASSERT_TRUE(md_attribute_axis_map(&row, run_time, frame, energy_time));
            ASSERT_LT(row, etot->format.shape[0]);
            if (file == 0) reference[frame] = values[row];
            else EXPECT_NEAR(reference[frame], values[row], 1.0e-8);
        }
    }

    md_system_state_free(&state);
    md_system_free(&sys);
}

UTEST(viamd_loader, amber_prmtop_loads_with_classic_netcdf_restart) {
    const char* coordinates = "viamd_test_amber_restart.nc";
    std::remove(coordinates);
    ASSERT_TRUE(write_amber_loader_restart(coordinates));
    const str_t topology = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.prmtop");
    loader::LoaderState reader = {};
    loader::init(&reader, topology);
    reader.coordinates = coordinates;

    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(loader::load(&sys, &state, topology, reader));
    EXPECT_EQ(12u, sys.atom.count);
    EXPECT_EQ(12u, state.num_atoms);
    EXPECT_NEAR(1.0, state.xyz[0].x, 1.0e-6);
    EXPECT_NEAR(36.0, state.xyz[11].z, 1.0e-6);

    md_system_state_free(&state);
    md_system_free(&sys);
    std::remove(coordinates);
}

UTEST(viamd_loader, amber_prmtop_can_use_the_trajectory_first_frame_as_initial_state) {
    const str_t topology = STR_LIT(VIAMD_AMBER_DATA_DIR "/nma.prmtop");
    const char* coordinates = VIAMD_AMBER_DATA_DIR "/nma.nc";
    loader::LoaderState reader = {};
    loader::init(&reader, topology);
    ASSERT_EQ((LoaderType)LoaderType_AMBER_PRMTOP, reader.type);
    reader.coordinates = coordinates;

    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    sys.attributes.alloc = alloc;
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(loader::load(&sys, &state, topology, reader));
    EXPECT_EQ(12u, sys.atom.count);
    EXPECT_EQ(12u, state.num_atoms);
    EXPECT_NEAR(1.903, state.xyz[0].x, 1.0e-5);

    md_system_state_free(&state);
    md_system_free(&sys);
}
#endif

#if !defined(_WIN32)
UTEST(viamd_loader, extensionless_amber_energy_in_a_dotted_directory_is_detected) {
    // Amber's default output names are "mdout" and "mden", often under a directory with a '.' in it
    const str_t sources[] = {STR_LIT(VIAMD_AMBER_ENERGY_TEST_DATA_DIR "/amber-mden.mden"), STR_LIT(VIAMD_AMBER_ENERGY_TEST_DATA_DIR "/amber-mdout.out")};
    const char* names[] = {"mden", "mdout"};
    char dir[] = "/tmp/viamd.amber.XXXXXX";
    ASSERT_TRUE(mkdtemp(dir) != nullptr);
    for (int i = 0; i < 2; ++i) {
        char path[256];
        snprintf(path, sizeof(path), "%s/%s", dir, names[i]);
        md_file_t src = {}, dst = {};
        ASSERT_TRUE(md_file_open(&src, sources[i], MD_FILE_READ));
        ASSERT_TRUE(md_file_open(&dst, str_from_cstr(path), MD_FILE_WRITE | MD_FILE_CREATE | MD_FILE_TRUNCATE));
        char buf[8192];
        size_t n;
        while ((n = md_file_read(src, buf, sizeof(buf))) > 0) md_file_write(dst, buf, n);
        md_file_close(&src);
        md_file_close(&dst);

        loader::LoaderState reader = {};
        loader::init(&reader, str_from_cstr(path));
        EXPECT_EQ((LoaderType)LoaderType_AMBER_ENERGY, reader.type);
        EXPECT_EQ(0u, reader.flags & LoaderFlag_RequiresDialogue);
        remove(path);
    }
    rmdir(dir);
}
#endif

UTEST(viamd_loader, out_of_range_types_fall_back_rather_than_read_past_the_table) {
    /* type_name/type_ext/type_flags are handed values that came from a file or a saved workspace,
     * so a stale or corrupt one has to land on Undefined instead of indexing off the end. */
    const LoaderType bad = LoaderType_COUNT;
    EXPECT_TRUE(str_eq(loader::type_name(bad),  loader::type_name(LoaderType_Undefined)));
    EXPECT_TRUE(str_eq(loader::type_ext(bad),   loader::type_ext(LoaderType_Undefined)));
    EXPECT_EQ(loader::type_flags(bad),          loader::type_flags(LoaderType_Undefined));

    const LoaderType way_off = (LoaderType)0x7FFFFFFFu;
    EXPECT_TRUE(str_eq(loader::type_name(way_off), loader::type_name(LoaderType_Undefined)));
    EXPECT_EQ(loader::type_flags(way_off),         loader::type_flags(LoaderType_Undefined));
}

UTEST(viamd_loader, molecular_and_quantum_formats_are_labelled_as_such) {
    /* The MM/QM flags drive which parts of the UI offer a file, so a format landing in the wrong
     * camp is a visible bug with an invisible cause. */
    EXPECT_NE(0u, loader::type_flags(LoaderType_PDB)    & LoaderFlag_MM);
    EXPECT_EQ(0u, loader::type_flags(LoaderType_PDB)    & LoaderFlag_QM);
    EXPECT_NE(0u, loader::type_flags(LoaderType_MOLDEN) & LoaderFlag_QM);
    EXPECT_EQ(0u, loader::type_flags(LoaderType_MOLDEN) & LoaderFlag_MM);
#if MD_HDF5
    EXPECT_NE(0u, loader::type_flags(LoaderType_TREXIO) & LoaderFlag_QM);
    EXPECT_EQ(0u, loader::type_flags(LoaderType_TREXIO) & LoaderFlag_MM);
    /* VeloxChem h5 is the one format that is both: a system with coordinates and a trajectory, and
     * quantum data alongside. */
    EXPECT_NE(0u, loader::type_flags(LoaderType_VLX_H5) & LoaderFlag_QM);
    EXPECT_NE(0u, loader::type_flags(LoaderType_VLX_H5) & LoaderFlag_MM);
#endif
}

UTEST(viamd_loader, ase_trajectory_contains_a_system_and_a_run) {
    const str_t path = STR_LIT(VIAMD_ASE_TRAJ_TEST_DATA_DIR "/ase_fixed.traj");
    loader::LoaderState reader = {};
    loader::init(&reader, path);
    ASSERT_EQ((LoaderType)LoaderType_ASE_TRAJ, reader.type);
    EXPECT_NE(0u, reader.flags & LoaderFlag_System);
    EXPECT_NE(0u, reader.flags & LoaderFlag_Trajectory);

    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(loader::load(&sys, &state, path, reader));
    EXPECT_EQ(3u, sys.atom.count);
    ASSERT_TRUE(loader::publish_run(&sys, path, STR_LIT("run/ase"), 0));
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("run/ase/atom/position")) != nullptr);
    md_system_state_free(&state);
    md_system_free(&sys);
}

UTEST(viamd_loader, trajectory_only_formats_do_not_claim_to_be_systems) {
    /* A trajectory has no topology of its own, so offering one as a system would load atoms with no
     * elements or names. */
    const LoaderType traj_only[] = {
        LoaderType_XTC, LoaderType_TRR, LoaderType_DCD, LoaderType_LAMMPSTRJ
#if MD_AMBER_NETCDF
        , LoaderType_AMBER_NETCDF
#endif
    };
    for (size_t i = 0; i < ARRAY_SIZE(traj_only); ++i) {
        EXPECT_NE(0u, loader::type_flags(traj_only[i]) & LoaderFlag_Trajectory);
        EXPECT_EQ(0u, loader::type_flags(traj_only[i]) & LoaderFlag_System);
    }
}

#if MD_AMBER_NETCDF
UTEST(viamd_loader, amber_netcdf_uses_the_run_publisher) {
    const char* filename = "viamd_test_amber.nc";
    std::remove(filename);
    ASSERT_TRUE(write_amber_loader_fixture(filename));

    const str_t path = str_from_cstr(filename);
    loader::LoaderState reader = {};
    loader::init(&reader, path);
    ASSERT_EQ((LoaderType)LoaderType_AMBER_NETCDF, reader.type);
    EXPECT_NE(0u, reader.flags & LoaderFlag_Trajectory);
    EXPECT_NE(0u, reader.flags & LoaderFlag_MM);
    md_allocator_i* alloc = md_get_heap_allocator();
    md_system_t sys = {.alloc = alloc};
    ASSERT_TRUE(loader::publish_run(&sys, path, STR_LIT("run/amber_loader"), 0));
    const md_attribute_t* pos = md_attributes_find(&sys.attributes, STR_LIT("run/amber_loader/atom/position"));
    ASSERT_TRUE(pos != nullptr);
    float xyz[3];
    ASSERT_EQ(3u, md_attribute_extract_f32(xyz, ARRAY_SIZE(xyz), pos, md_attribute_slice_1(1), md_unit_angstrom()));
    EXPECT_NEAR(4.0, xyz[0], 1.0e-6);
    EXPECT_NEAR(6.0, xyz[2], 1.0e-6);

    const str_t paths[] = { STR_LIT("atom/position"), STR_LIT("unitcell") };
    md_system_extract_t* ex = md_system_extract_begin(&sys, STR_LIT("run/amber_loader"), paths, ARRAY_SIZE(paths), alloc);
    ASSERT_TRUE(ex != nullptr);
    md_system_state_t state = {.alloc = alloc};
    ASSERT_TRUE(md_system_state_init(&state, 1));
    ASSERT_TRUE(md_system_extract_frame(ex, 1, &state));
    EXPECT_NEAR(4.0, state.xyz[0].x, 1.0e-6);
    EXPECT_NEAR(6.0, state.xyz[0].z, 1.0e-6);
    EXPECT_FALSE(md_system_state_has_unitcell(&state));
    md_system_extract_end(ex);
    md_system_state_free(&state);

    md_system_free(&sys);
    std::remove(filename);
}
#endif

/* ---- init(), which is where the extension is only the first guess ---------------------------- */

UTEST(viamd_loader, init_picks_a_reader_from_the_path) {
    loader::LoaderState state = {};

    loader::init(&state, STR_LIT(VIAMD_TEST_DATA_DIR "/molden/h2o_ccpvdz.molden"));
    EXPECT_EQ((LoaderType)LoaderType_MOLDEN, state.type);
    EXPECT_NE(0u, state.flags & LoaderFlag_QM);

    loader::init(&state, STR_LIT(VIAMD_TEST_DATA_DIR "/1ALA-560ns.pdb"));
    EXPECT_EQ((LoaderType)LoaderType_PDB, state.type);

    /* An unrecognised extension on a file that is not a molden file leaves the type undefined and
     * asks for the dialogue rather than guessing a reader - init() is also called on whatever a
     * user drags in, and picking a reader by accident is worse than asking. */
    loader::init(&state, STR_LIT(VIAMD_TEST_DATA_DIR "/script.txt"));
    EXPECT_EQ((LoaderType)LoaderType_Undefined, state.type);
    EXPECT_NE(0u, state.flags & LoaderFlag_RequiresDialogue);

    /* A path with no extension at all, and one that does not exist, both have to be survivable and
     * reach the same answer rather than asserting somewhere inside a reader. */
    loader::init(&state, STR_LIT(VIAMD_TEST_DATA_DIR "/there_is_no_such_file"));
    EXPECT_EQ((LoaderType)LoaderType_Undefined, state.type);
    EXPECT_NE(0u, state.flags & LoaderFlag_RequiresDialogue);
    loader::init(&state, STR_LIT(""));
    EXPECT_EQ((LoaderType)LoaderType_Undefined, state.type);
}

#if MD_HDF5
UTEST(viamd_loader, trexio_is_recognised_by_content_not_only_by_extension) {
    /* .h5 is not one format: VeloxChem and TREXIO both use it, and they are told apart by looking
     * inside. Here the extension already says trexio, so this checks the other half - that the
     * content check agrees rather than overriding a correct guess. */
    loader::LoaderState state = {};
    loader::init(&state, STR_LIT(VIAMD_TEST_DATA_DIR "/trexio/h2o.trexio"));
    EXPECT_EQ((LoaderType)LoaderType_TREXIO, state.type);
    EXPECT_NE(0u, state.flags & LoaderFlag_QM);
}
#endif

#if MD_HDF5
UTEST(viamd_loader, h5md_is_a_system_and_its_own_trajectory) {
    /* One file, both halves: load() gives the system from it and publish_run() the positions, which
     * is the path a structure file with frames of its own takes (init_trajectory_data). */
    const str_t path = STR_LIT(VIAMD_TEST_DATA_DIR "/h5md/peptide_tip3p.h5md");
    loader::LoaderState state = {};
    loader::init(&state, path);
    EXPECT_EQ((LoaderType)LoaderType_H5MD, state.type);
    const LoaderFlags expected = LoaderFlag_System | LoaderFlag_Trajectory | LoaderFlag_MM | LoaderFlag_Topology;
    EXPECT_EQ(expected, state.flags & expected);

    md_allocator_i* arena = md_vm_arena_create(GIGABYTES(1));
    md_system_t sys = {};
    sys.alloc = arena;
    md_system_state_t sys_state = {};
    sys_state.alloc = arena;
    ASSERT_TRUE(loader::load(&sys, &sys_state, path, state));
    EXPECT_EQ(1577u, sys.atom.count);
    EXPECT_LT(0u, sys.bond.count);
    EXPECT_TRUE(loader::publish_run(&sys, path, STR_LIT("run/h5md"), 0));
    const md_attribute_t* time = md_attributes_find(&sys.attributes, STR_LIT("run/h5md/time"));
    ASSERT_TRUE(time != NULL);
    EXPECT_EQ(5u, time->format.shape[0]);
    md_vm_arena_destroy(arena);
}
#endif

#if MD_HDF5
UTEST(viamd_loader, a_veloxchem_h5_stays_veloxchem) {
    /* The mirror of the case above: a .h5 that is not TREXIO must not be rerouted by the content
     * check. Getting this backwards would send every VeloxChem file to the wrong reader. */
    loader::LoaderState state = {};
    loader::init(&state, STR_LIT(VIAMD_TEST_DATA_DIR "/vlx/h2o.h5"));
    EXPECT_EQ((LoaderType)LoaderType_VLX_H5, state.type);
    EXPECT_NE(0u, state.flags & LoaderFlag_QM);
}
#endif
