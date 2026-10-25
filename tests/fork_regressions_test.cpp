#include <string>
#include <vector>

#include "cata_catch.h"
#include "coordinates.h"
#include "debug.h"
#include "line.h"
#include "map.h"
#include "map_helpers.h"
#include "map_helpers_tests.h"
#include "map_scale_constants.h"
#include "monster.h"
#include "point.h"
#include "type_id.h"

// Regression tests for engine fixes made in our fork (aec-dev), each named
// after its commit, so a rebase onto a new upstream release that drops one
// fails here.

static const furn_str_id furn_f_dumpster( "f_dumpster" );

static const mtype_id mon_zombie( "mon_zombie" );

// 1f2290cf5f: a shot at a target two tiles away and one storey up, carried
// on to a rifle's range, used to land a dozen storeys above the map.
TEST_CASE( "fork_overshoot_line_stays_in_the_maps_z_range", "[line][fork]" )
{
    const std::vector<tripoint> up = line_to( tripoint( 60, 60, 0 ), tripoint( 62, 60, 1 ) );
    REQUIRE_FALSE( up.empty() );
    const tripoint high = move_along_line( up.back(), up, 30 );
    CHECK( high.z <= OVERMAP_HEIGHT );
    CHECK( high.z >= -OVERMAP_DEPTH );

    const std::vector<tripoint> down = line_to( tripoint( 60, 60, 0 ), tripoint( 62, 60, -1 ) );
    REQUIRE_FALSE( down.empty() );
    const tripoint low = move_along_line( down.back(), down, 30 );
    CHECK( low.z <= OVERMAP_HEIGHT );
    CHECK( low.z >= -OVERMAP_DEPTH );
}

// 72754773f6: a monster that can't climb may step off climbable furniture
// (no climbing needed) but not onto it; the checks were the wrong way round.
TEST_CASE( "fork_non_climber_steps_off_climbable_furniture", "[monster][fork]" )
{
    clear_map();
    map &here = get_map();
    const tripoint_bub_ms on( 60, 60, 0 );
    const tripoint_bub_ms off( 61, 60, 0 );
    here.furn_set( on, furn_f_dumpster );
    REQUIRE( here.furn( on ) == furn_f_dumpster );
    monster &zed = spawn_test_monster( mon_zombie.str(), off );
    REQUIRE_FALSE( zed.climbs() );

    int stepping_off = 0;
    const std::string off_msg = capture_debugmsg_during( [&]() {
        stepping_off = zed.calc_movecost( here, on, off );
    } );
    CHECK( off_msg.empty() );
    CHECK( stepping_off > 0 );

    int stepping_on = -1;
    const std::string on_msg = capture_debugmsg_during( [&]() {
        stepping_on = zed.calc_movecost( here, off, on );
    } );
    CHECK( stepping_on == 0 );
    CHECK_FALSE( on_msg.empty() );
    clear_map();
}
