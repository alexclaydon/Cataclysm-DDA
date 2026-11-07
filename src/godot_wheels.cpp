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
#include "creature.h"
#include "creature_tracker.h"
#include "game.h"
#include "godot_host.h"
#include "input_context.h"
#include "map.h"
#include "point.h"
#include "translations.h"
#include "uilist.h"
#include "veh_type.h"
#include "vpart_position.h"

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

const char *const godot_host::stairs_action_id = "stairs";

std::string godot_host::stairs_action()
{
    const map &here = get_map();
    const tripoint_bub_ms pos = get_avatar().pos_bub();
    const bool up = can_move_vertical_at( here, pos, 1 );
    const bool down = can_move_vertical_at( here, pos, -1 );
    if( up != down ) {
        return up ? "LEVEL_UP" : "LEVEL_DOWN";
    }
    uilist menu;
    menu.text = up ? _( "Go up or down?" ) : _( "No way up or down here by the terrain.  Try anyway?" );
    menu.addentry( 0, true, '<', _( "Go up" ) );
    menu.addentry( 1, true, '>', _( "Go down" ) );
    menu.query();
    if( menu.ret == 0 ) {
        return "LEVEL_UP";
    }
    if( menu.ret == 1 ) {
        return "LEVEL_DOWN";
    }
    return "";
}

std::vector<std::string> godot_host::context_actions_here()
{
    std::vector<std::string> out;
    if( g == nullptr ) {
        return out;
    }
    map &here = get_map();
    const avatar &you = get_avatar();
    const tripoint_bub_ms pos = you.pos_bub();
    bool open = false;
    bool close = false;
    bool pickup = can_interact_at( ACTION_PICKUP, here, pos );
    bool talk = false;
    bool examine = false;
    for( const tripoint_bub_ms &p : here.points_in_radius( pos, 1 ) ) {
        if( p == pos ) {
            continue;
        }
        open = open || can_interact_at( ACTION_OPEN, here, p );
        close = close || can_interact_at( ACTION_CLOSE, here, p );
        pickup = pickup || can_interact_at( ACTION_PICKUP, here, p );
        examine = examine || can_interact_at( ACTION_EXAMINE, here, p );
        if( !talk ) {
            const Creature *c = get_creature_tracker().creature_at( p );
            talk = c != nullptr && c->is_npc() && you.sees( here, p );
        }
    }
    const optional_vpart_position vp = here.veh_at( pos );
    const bool drive = vp && vp.part_with_feature( VPFLAG_CONTROLS, true );
    // The fixed priority order: the same action keeps its place.
    if( can_move_vertical_at( here, pos, 1 ) ) {
        out.emplace_back( "LEVEL_UP" );
    }
    if( can_move_vertical_at( here, pos, -1 ) ) {
        out.emplace_back( "LEVEL_DOWN" );
    }
    if( open ) {
        out.emplace_back( "open" );
    }
    if( close ) {
        out.emplace_back( "close" );
    }
    if( pickup ) {
        out.emplace_back( "pickup_all" );
    }
    if( talk ) {
        out.emplace_back( "chat" );
    }
    if( drive ) {
        out.emplace_back( "control_vehicle" );
    }
    if( can_interact_at( ACTION_BUTCHER, here, pos ) ) {
        out.emplace_back( "butcher" );
    }
    if( examine ) {
        out.emplace_back( "examine" );
    }
    return out;
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
            if( act == ACTION_MOVE_UP ) {
                // Up or down, whichever applies (stairs_action).
                wheel_action stairs;
                stairs.id = stairs_action_id;
                stairs.name = _( "Go up or down" );
                stairs.category = category.id;
                stairs.category_name = category.name;
                stairs.applies = can_move_vertical_at( here, pos, 1 ) || can_move_vertical_at( here, pos, -1 );
                stairs.at_tile = false;
                out.push_back( std::move( stairs ) );
            }
        }
    }
    return out;
}

#endif // CDDA_GODOT
