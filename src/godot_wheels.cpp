#if defined(CDDA_GODOT)

// The catalogue the host's shoulder wheels offer (ADR 0009): the action
// menu's actions, with what the host needs to lay them out and grey them.
// Game code, kept apart from godot_host.cpp, which the engine's lower
// layers use.

#include <string>
#include <vector>

#include "action.h"
#include "avatar.h"
#include "coordinates.h"
#include "game.h"
#include "godot_host.h"
#include "input_context.h"
#include "map.h"
#include "point.h"

bool godot_host::wheel_action_at_tile( const std::string &id )
{
    switch( look_up_action( id ) ) {
        // The verbs game::handle_action aims with the left stick, and those
        // do_regular_action can also point at a tile.
        case ACTION_SMASH:
        case ACTION_GRAB:
        case ACTION_PEEK:
        case ACTION_UNLOAD_CONTAINER:
        case ACTION_DIR_DROP:
        case ACTION_EXAMINE:
        case ACTION_EXAMINE_AND_PICKUP:
        case ACTION_PICKUP:
        case ACTION_PICKUP_ALL:
        case ACTION_CONTROL_VEHICLE:
        case ACTION_OPEN:
        case ACTION_CLOSE:
        case ACTION_CHAT:
            return true;
        default:
            return false;
    }
}

// Whether `act` can be done now. Only the verbs can_interact_at knows are
// checked, at the aimed tile, or with no aim where the game would ask
// "which way?": any adjacent tile. The rest are always offered.
static bool wheel_action_applies( action_id act, map &here, const tripoint_bub_ms &pos,
                                  const tripoint_rel_ms &aim )
{
    switch( act ) {
        case ACTION_MOVE_UP:
        case ACTION_MOVE_DOWN:
        case ACTION_BUTCHER:
            return can_interact_at( act, here, pos );
        case ACTION_OPEN:
        case ACTION_CLOSE:
        case ACTION_EXAMINE:
        case ACTION_EXAMINE_AND_PICKUP:
        case ACTION_PICKUP:
        case ACTION_SMASH:
        case ACTION_CHAT:
            break;
        default:
            return true;
    }
    if( aim != tripoint_rel_ms::zero ) {
        return can_interact_at( act, here, pos + aim );
    }
    for( const tripoint_bub_ms &p : here.points_in_radius( pos, 1 ) ) {
        if( can_interact_at( act, here, p ) ) {
            return true;
        }
    }
    return false;
}

std::vector<godot_host::wheel_action> godot_host::wheel_catalogue( int aim_dx, int aim_dy )
{
    std::vector<wheel_action> out;
    if( g == nullptr ) {
        return out;
    }
    const input_context ctxt = get_default_mode_input_context();
    map &here = get_map();
    const tripoint_bub_ms pos = get_avatar().pos_bub();
    const tripoint_rel_ms aim( aim_dx, aim_dy, 0 );
    for( const action_menu_category &category : action_menu_categories() ) {
        for( const action_id act : category.actions ) {
            wheel_action entry;
            entry.id = action_ident( act );
            entry.name = ctxt.get_action_name( entry.id );
            entry.category = category.id;
            entry.category_name = category.name;
            entry.applies = wheel_action_applies( act, here, pos, aim );
            entry.at_tile = wheel_action_at_tile( entry.id );
            out.push_back( std::move( entry ) );
        }
    }
    return out;
}

#endif // CDDA_GODOT
