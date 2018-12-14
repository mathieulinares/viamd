#include <core/gl.h>
#include <core/types.h>
#include <core/hash.h>
#include <core/log.h>
#include <core/math_utils.h>
#include <core/camera.h>
#include <core/camera_utils.h>
#include <core/string_utils.h>
#include <core/volume.h>

#include <mol/molecule_structure.h>
#include <mol/molecule_trajectory.h>
#include <mol/trajectory_utils.h>
#include <mol/molecule_utils.h>
#include <mol/hydrogen_bond.h>
#include <mol/filter.h>
#include <mol/pdb_utils.h>
#include <mol/gro_utils.h>
#include <mol/spatial_hash.h>

#include <glm/gtx/io.hpp>

#define CATCH_CONFIG_MAIN
#include "catch.hpp"

constexpr const char* CAFFINE_PDB = R"(
ATOM      1  N1  BENZ    1       5.040   1.944  -8.324                          
ATOM      2  C2  BENZ    1       6.469   2.092  -7.915                          
ATOM      3  C3  BENZ    1       7.431   0.865  -8.072                          
ATOM      4  C4  BENZ    1       6.916  -0.391  -8.544                          
ATOM      5  N5  BENZ    1       5.532  -0.541  -8.901                          
ATOM      6  C6  BENZ    1       4.590   0.523  -8.394                          
ATOM      7  C11 BENZ    1       4.045   3.041  -8.005                          
ATOM      8  H111BENZ    1       4.453   4.038  -8.264                          
ATOM      9  H112BENZ    1       3.101   2.907  -8.570                          
ATOM     10  H113BENZ    1       3.795   3.050  -6.926                          
ATOM     11  O21 BENZ    1       6.879   3.181  -7.503                          
ATOM     12  C51 BENZ    1       4.907  -1.659  -9.696                          
ATOM     13  H511BENZ    1       4.397  -1.273 -10.599                          
ATOM     14  H512BENZ    1       5.669  -2.391 -10.028                          
ATOM     15  H513BENZ    1       4.161  -2.209  -9.089                          
ATOM     16  O61 BENZ    1       3.470   0.208  -7.986                          
ATOM     17  N1  NSP3    1B      8.807   0.809  -7.799                          
ATOM     18  N1  NSP3    1C      7.982  -1.285  -8.604                          
ATOM     19  C1  CSP3    1D      9.015  -0.500  -8.152                          
ATOM     20  H1  CSP3    1D     10.007  -0.926  -8.079                          
ATOM     21  C1  CSP3    1E      9.756   1.835  -7.299                          
ATOM     22  H11 CSP3    1E     10.776   1.419  -7.199                          
ATOM     23  H12 CSP3    1E      9.437   2.207  -6.309                          
ATOM     24  H13 CSP3    1E      9.801   2.693  -7.994
)";

TEST_CASE("Testing pdb loader caffine", "[parse_pdb]") {
    MoleculeDynamic md;
    allocate_and_parse_pdb_from_string(&md, CAFFINE_PDB);
    defer { free_molecule_structure(&md.molecule); };

    REQUIRE(md.molecule.atom.count == 24);
}

TEST_CASE("Testing filter", "[filter]") {
    MoleculeDynamic md;
    allocate_and_parse_pdb_from_string(&md, CAFFINE_PDB);
    defer { free_molecule_structure(&md.molecule); };

    filter::initialize();
    DynamicArray<bool> mask(md.molecule.atom.count);

    SECTION("filter element N") {
        filter::compute_filter_mask(mask, md, "element N");
        for (int32 i = 0; i < mask.size(); i++) {
            if (i == 0 || i == 4 || i == 16 || i == 17) {
                REQUIRE(mask[i] == true);
            } else {
                REQUIRE(mask[i] == false);
            }
        }
    }

    SECTION("filter atom 1:10") {
        filter::compute_filter_mask(mask, md, "atom 1:10");
        for (int32 i = 0; i < mask.size(); i++) {
            if (i < 10) {
                REQUIRE(mask[i] == true);
            } else {
                REQUIRE(mask[i] == false);
            }
        }
    }

    SECTION("filter atom 10:*") {
        filter::compute_filter_mask(mask, md, "atom 10:*");
        for (int32 i = 0; i < mask.size(); i++) {
            if (i > 10) {
                REQUIRE(mask[i] == true);
            } else {
                REQUIRE(mask[i] == false);
            }
        }
    }

    SECTION("filter atom *:*") {
        filter::compute_filter_mask(mask, md, "atom *:*");
        for (int32 i = 0; i < mask.size(); i++) {
            REQUIRE(mask[i] == true);
        }
    }

    SECTION("filter all") {
        filter::compute_filter_mask(mask, md, "all");
        for (int32 i = 0; i < mask.size(); i++) {
            REQUIRE(mask[i] == true);
        }
    }

    SECTION("filter not all") {
        filter::compute_filter_mask(mask, md, "not all");
        for (int32 i = 0; i < mask.size(); i++) {
            REQUIRE(mask[i] == false);
        }
    }
}