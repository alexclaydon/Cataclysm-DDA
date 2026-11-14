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
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "damage.h"
#include "flag.h"
#include "game.h"
#include "godot_host.h"
#include "input.h"
#include "input_context.h"
#include "map.h"
#include "point.h"
#include "string_formatter.h"
#include "translations.h"
#include "uilist.h"
#include "veh_type.h"
#include "vpart_position.h"

std::vector<godot_host::wheel_action> godot_host::radial_catalogue()
{
    std::vector<wheel_action> out;
    input_context *ctxt = input_context::polling ? input_context::polling :
                          input_context::input_context_stack.back();
    for( int side = 0; side < 2; ++side ) {
        for( int index = 0; index < 8; ++index ) {
            wheel_action a;
            a.id = string_format( "radial:%c:%d", side == 0 ? 'L' : 'R', index );
            a.applies = true;
            a.at_tile = false;
            if( ctxt != nullptr ) {
                const input_event event( ( side == 0 ? JOY_L_RADIAL_N : JOY_R_RADIAL_N ) + index,
                                         input_event_t::gamepad );
                const std::string &action = ctxt->input_to_action( event );
                if( action != "ERROR" ) {
                    a.name = ctxt->get_action_name( action );
                    a.category = action;
                }
            }
            out.push_back( std::move( a ) );
        }
    }
    return out;
}

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
const char *const godot_host::recenter_action_id = "recenter";

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

static const damage_type_id damage_bash( "bash" );
static const flag_id flag_JAVELIN( "JAVELIN" );

namespace
{
// Made for throwing (the user's call, 2026-11-15: the game throws anything,
// but Throw stands for what is meant to be thrown): thrown damage of its
// own, unlike the default every item gets on loading (item_factory's
// finalize: three quarters of its melee bash), or a throwing flag (rocks,
// javelins), or armed (DANGEROUS: a lit grenade or Molotov).
bool made_for_throwing( const item &it )
{
    if( it.has_flag( flag_NPC_THROWN ) || it.has_flag( flag_JAVELIN ) || it.has_flag( flag_DANGEROUS ) ) {
        return true;
    }
    const itype &t = *it.type;
    const auto bash = t.melee.damage_map.find( damage_bash );
    const float fallback = bash == t.melee.damage_map.end() ? 0.0f : bash->second * 0.75f;
    const std::vector<damage_unit> &units = t.thrown_damage.damage_units;
    return !( units.size() == 1 && units[0].type == damage_bash && units[0].amount == fallback );
}

// The context row's combat actions (context_actions_here), in order:
// autoattack while a hostile is in the wielded weapon's reach (as
// avatar_action::autoattack finds them, hostiles only), fire while wielding
// a gun, reload_wielded while it can be reloaded, throw while wielding
// something made for throwing (made_for_throwing).
std::vector<const char *> combat_actions_here( const avatar &you )
{
    std::vector<const char *> out;
    const item_location weapon = you.get_wielded_item();
    const int reach = weapon ? weapon->reach_range( you ).first : 1;
    for( Creature *c : you.get_targetable_creatures( reach, true ) ) {
        if( ( reach == 1 && !you.is_adjacent( c, true ) ) || !you.can_reach_attack( *c ) ) {
            continue;
        }
        if( c->attitude_to( you ) == Creature::Attitude::HOSTILE ) {
            out.push_back( "autoattack" );
            break;
        }
    }
    if( !weapon ) {
        return out;
    }
    if( weapon->is_gun() ) {
        out.push_back( "fire" );
    }
    if( weapon->is_reloadable() && you.can_reload( *weapon ) ) {
        out.push_back( "reload_wielded" );
    }
    if( made_for_throwing( *weapon ) ) {
        out.push_back( "throw" );
    }
    return out;
}
} // namespace

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
    // First what the wielded item and the creatures in reach offer (the
    // user's call, 2026-11-15: the touch pad's LB and RB are its wheels
    // alone, these their taps; Dark Forest leaves them out of the desktop's
    // cues): Attack, Fire, Reload, Throw.
    for( const char *id : combat_actions_here( you ) ) {
        out.emplace_back( id );
    }
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
    // The view off its rest (look-around left it offset): Recenter.
    const tripoint_rel_ms rest( g->driving_view_offset.x(), g->driving_view_offset.y(), 0 );
    if( you.view_offset != rest ) {
        out.emplace_back( godot_host::recenter_action_id );
    }
    return out;
}

godot_host::bindings_map godot_host::context_bindings_here( const std::vector<std::string> &actions )
{
    bindings_map out;
    if( g == nullptr ) {
        return out;
    }
    // The play loop's context's category, read from the input manager
    // directly: that context's keys_bound_to drops pad events (it doesn't
    // enable them).
    const std::string category = get_default_mode_input_context().input_category();
    std::vector<std::string> ids = actions;
    for( const char *id : {
             "ignore_enemy", "safemode", "whitelist_enemy"
         } ) {
        ids.emplace_back( id );
    }
    for( const std::string &id : ids ) {
        // Ours, not a keybinding: none to look up (the lookup would add it).
        if( id == recenter_action_id ) {
            continue;
        }
        action_bindings &b = out[id];
        for( const input_event &e : inp_mngr.get_input_for_action( id, category ) ) {
            if( e.sequence.size() != 1 ) {
                continue;
            }
            if( e.type == input_event_t::gamepad ) {
                b.pad.push_back( inp_mngr.get_keyname( e.sequence.front(), e.type, true ) );
            } else if( e.type == input_event_t::keyboard_char || e.type == input_event_t::keyboard_code ) {
                b.keys.push_back( e.short_description() );
            }
        }
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
