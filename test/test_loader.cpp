#include "utest.h"

#include <loader.h>

#include <core/md_str.h>
#include <core/md_arena_allocator.h>
#include <md_system.h>
#include <md_util.h>
#include <core/md_os.h>

#include <cstdio>
#include <cstdlib>
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
#endif

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

    loader::LoaderState topology_reader = {};
    loader::init(&topology_reader, topology, &sys);
    ASSERT_EQ((LoaderType)LoaderType_AMBER_PRMTOP, topology_reader.type);
    EXPECT_NE(0u, topology_reader.flags & LoaderFlag_Supplemental);
    EXPECT_EQ(0u, topology_reader.flags & LoaderFlag_RequiresDialogue);
    ASSERT_TRUE(loader::load_supplemental(&sys, topology, topology_reader, (str_t){0}));

    EXPECT_EQ(12u, sys.atom.count);
    EXPECT_EQ(11u, sys.bond.count);
    EXPECT_NEAR(initial_position.x, state.xyz[0].x, 1.0e-6);
    EXPECT_NEAR(initial_position.y, state.xyz[0].y, 1.0e-6);
    EXPECT_NEAR(initial_position.z, state.xyz[0].z, 1.0e-6);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("atom/charge")) != nullptr);
    EXPECT_TRUE(md_attributes_find(&sys.attributes, STR_LIT("atom/mass")) != nullptr);
    EXPECT_FALSE(str_empty(md_particle_type_ff_type(&sys.atom.type, sys.atom.type_idx[0])));

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
    EXPECT_FALSE(loader::load_supplemental(&sys, topology, topology_reader, (str_t){0}));
    EXPECT_EQ(bonds_before, sys.bond.count);

    md_system_state_free(&state);
    md_system_free(&sys);
}

#if MD_AMBER_NETCDF
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
