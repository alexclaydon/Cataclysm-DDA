#if defined(CDDA_GODOT)

// The play loop's additions for Dark Forest (godot_actions.h). Moved here
// from handle_action.cpp unchanged (2026-11-09, plan 0022).

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "coordinates.h"
#include "creature_tracker.h"
#include "game.h"
#include "input.h"
#include "explosion.h"
#include "field_type.h"
#include "veh_type.h"
#include "vehicle.h"
#include "input_context.h"
#include "line.h"
#include "map.h"
#include "map_scale_constants.h"
#include "godot_actions.h"
#include "godot_host.h"
#include "godot_menus.h"
#include "messages.h"
#include "monster.h"
#include "mtype.h"
#include "rng.h"
#include "move_mode.h"
#include "omdata.h"
#include "overmapbuffer.h"
#include "point.h"
#include "sdl_gamepad.h"
#include "sdltiles.h"
#include "type_id.h"

void godot_actions::settle_view( game &g, avatar &you )
{
    // Go here's held view, back once its route is walked or dropped.
    {
        int dx = 0;
        int dy = 0;
        int dz = 0;
        if( godot_host::take_settled_view( !g.destination_preview.empty(), dx, dy, dz ) ) {
            you.view_offset = tripoint_rel_ms( dx, dy, dz );
        }
    }
}

void godot_actions::before_input( game &g )
{
    // The touch pad's context row: what applies here, before waiting.
    godot_host::note_turn( to_turns<int>( calendar::turn - calendar::turn_zero ) );
    std::vector<std::string> here = godot_host::context_actions_here();
    // Dark Forest's context cues: how each of them is bound.
    godot_host::publish_context_bindings( godot_host::context_bindings_here( here ) );
    godot_host::publish_context_actions( std::move( here ) );
    godot_host::set_route_preview( !g.destination_preview.empty() );
}

// The crowd test hook's keep-alive (godot_host::request_crowd).
static bool crowd_keep_alive = false;

bool godot_actions::take_requests( game &g, avatar &you, std::string &action,
                                   std::optional<tripoint_rel_ms> &wheel_aim,
                                   std::optional<tripoint_bub_ms> &host_click )
{
    if( crowd_keep_alive ) {
        you.set_all_parts_hp_to_max();
    }
    godot_host::set_bubble_creatures( static_cast<int>( g.num_creatures() ) );
    godot_host::note_turn_timers();
    // The touch pad's view radius slider, as the zoom actions.
    if( const int steps = godot_host::take_zoom_view_request() ) {
        godot_host::zoom_view( steps );
        g.mark_main_ui_adaptor_resize();
    }
    // The host's move wheel chose a mode: the desired one, which the next
    // step takes up, as upstream's menu sets it. No turn.
    {
        std::string mode;
        if( godot_host::take_move_mode_request( mode ) ) {
            const move_mode_id id( mode );
            if( id.is_valid() && you.can_switch_to( id ) ) {
                you.set_desired_movement_mode( id );
            }
            return false;
        }
    }
    // The host's hotbar asked for the action catalogue (ADR 0013).
    if( godot_host::take_catalogue_request() ) {
        godot_host::publish_catalogue( godot_host::wheel_catalogue( 0, 0 ) );
    }
    // The host's shoulder wheel chose an action (ADR 0009), or its hotbar
    // did (no aim): taken as if its key had been pressed, aimed where the
    // wheel opened.
    {
        std::string wheel_action;
        int aim_dx = 0;
        int aim_dy = 0;
        if( godot_host::take_action_request( wheel_action, aim_dx, aim_dy ) ) {
            // Recenter: the view back on the avatar (at driving's offset
            // in a moving vehicle, as upstream's centre action), no turn.
            if( wheel_action == godot_host::recenter_action_id ) {
                you.view_offset = tripoint_rel_ms( g.driving_view_offset.x(), g.driving_view_offset.y(), 0 );
                g.invalidate_main_ui_adaptor();
                return false;
            }
            // Move mode from the hotbar or the RB wheel: the host's move
            // wheel, as from a pad.
            if( wheel_action == "open_movement" && godot_host::move_wheel_enabled() ) {
                godot_host::request_move_wheel();
                return false;
            }
            if( wheel_action == godot_host::stairs_action_id ) {
                wheel_action = godot_host::stairs_action();
                if( wheel_action.empty() ) {
                    return false;
                }
            }
            action = wheel_action;
            if( ( aim_dx != 0 || aim_dy != 0 ) && godot_host::wheel_action_at_tile( wheel_action ) ) {
                wheel_aim = tripoint_rel_ms( aim_dx, aim_dy, 0 );
            }
        }
    }
    // The host clicked a tile on its board (godot_host::request_click).
    {
        int click_dx = 0;
        int click_dy = 0;
        if( godot_host::take_click_request( click_dx, click_dy ) ) {
            godot_host::arm_held_view();
            action = "SELECT";
            host_click = you.pos_bub() + tripoint_rel_ms( click_dx, click_dy, 0 );
        }
    }
    // VR: the player pointed at a tile and asked to look at it.
    {
        int dx = 0;
        int dy = 0;
        int hour = 0;
        if( godot_host::take_time_request( hour ) ) {
            // On to the next HH:00 (never back: the world has moved on).
            const time_point now = calendar::turn;
            time_point target = now - time_past_midnight( now ) + time_duration::from_hours( hour % 24 );
            if( target <= now ) {
                target += 1_days;
            }
            calendar::turn = target;
            add_msg( m_info, "VR time: %s.", to_string_time_of_day( target ) );
            return false;
        }
        std::string field;
        int field_radius = 0;
        int field_intensity = 1;
        if( godot_host::take_field_request( field, field_radius, field_intensity ) ) {
            const field_type_id type( field );
            if( !type.is_valid() ) {
                add_msg( m_info, "VR field: no field %s.", field );
                return false;
            }
            map &here = get_map();
            int placed = 0;
            for( const tripoint_bub_ms &p : here.points_in_radius( you.pos_bub(), field_radius ) ) {
                if( p != you.pos_bub() && here.add_field( p, type, field_intensity ) ) {
                    ++placed;
                }
            }
            add_msg( m_info, "VR field: %s on %d tiles.", field, placed );
            return false;
        }
        std::string proto;
        int facing = 0;
        if( godot_host::take_vehicle_request( proto, dx, dy, facing ) ) {
            if( proto == "clear" ) {
                // Every vehicle within 40 tiles gone (the vehicle gallery's
                // clean slate).
                map &here = get_map();
                std::vector<vehicle *> gone;
                for( const wrapped_vehicle &w : here.get_vehicles() ) {
                    if( rl_dist( w.pos, you.pos_bub() ) <= 40 ) {
                        gone.push_back( w.v );
                    }
                }
                for( vehicle *v : gone ) {
                    here.destroy_vehicle( v );
                }
                add_msg( m_info, "VR vehicle: %d cleared.", static_cast<int>( gone.size() ) );
                return false;
            }
            const vproto_id id( proto );
            vehicle *veh = id.is_valid() ? get_map().add_vehicle( id, you.pos_bub() + tripoint_rel_ms( dx, dy, 0 ),
                           units::from_degrees( facing ), -1, veh_spawn_status::PRISTINE, true, true ) : nullptr;
            add_msg( m_info, "VR vehicle: %s %s.", proto, veh != nullptr ? "placed" : "not placed" );
            return false;
        }
        std::string crowd;
        int crowd_count = 0;
        int crowd_near = 0;
        int crowd_far = 0;
        if( godot_host::take_crowd_request( crowd, crowd_count, crowd_near, crowd_far ) ) {
            map &here = get_map();
            if( crowd == "clear" ) {
                int gone = 0;
                for( monster &m : g.all_monsters() ) {
                    if( rl_dist( m.pos_bub(), you.pos_bub() ) <= 60 &&
                        m.attitude_to( you ) == Creature::Attitude::HOSTILE ) {
                        m.die( &here, nullptr );
                        ++gone;
                    }
                }
                g.cleanup_dead();
                crowd_keep_alive = false;
                add_msg( m_info, "VR crowd: %d cleared.", gone );
                return false;
            }
            const mtype_id type( crowd );
            if( !type.is_valid() ) {
                add_msg( m_info, "VR crowd: no monster %s.", crowd );
                return false;
            }
            std::vector<tripoint_bub_ms> spots;
            for( const tripoint_bub_ms &p : here.points_in_radius( you.pos_bub(), crowd_far ) ) {
                if( rl_dist( p, you.pos_bub() ) >= crowd_near && here.passable( p ) &&
                    get_creature_tracker().creature_at( p ) == nullptr ) {
                    spots.push_back( p );
                }
            }
            std::shuffle( spots.begin(), spots.end(), rng_get_engine() );
            int placed = 0;
            for( const tripoint_bub_ms &p : spots ) {
                if( placed >= crowd_count ) {
                    break;
                }
                if( g.place_critter_at( type, p ) != nullptr ) {
                    ++placed;
                }
            }
            crowd_keep_alive = true;
            add_msg( m_info, "VR crowd: %d %s, the avatar kept alive.", placed, crowd );
            return false;
        }
        int boom_power = 0;
        if( godot_host::take_boom_request( dx, dy, boom_power ) ) {
            explosion_handler::explosion( &you, you.pos_bub() + tripoint_rel_ms( dx, dy, 0 ),
                                          static_cast<float>( boom_power ) );
            add_msg( m_info, "VR boom: %d at %d, %d.", boom_power, dx, dy );
            return false;
        }
        if( godot_host::take_menus_check_request() ) {
            godot_menus_check::run();
            return false;
        }
        std::string goto_type;
        if( godot_host::take_goto_request( goto_type ) ) {
            const tripoint_abs_omt dest = overmap_buffer.find_closest( you.pos_abs_omt(),
                                          goto_type, 200, false, ot_match_type::prefix );
            if( dest.is_invalid() ) {
                add_msg( m_info, "VR goto: no %s within 200.", goto_type );
            } else {
                g.place_player_overmap( dest );
                add_msg( m_info, "VR goto: %s.", overmap_buffer.ter( dest ).id().str() );
            }
            return false;
        }
        if( godot_host::take_look_request( dx, dy ) ) {
            tripoint_bub_ms center = you.pos_bub() + you.view_offset;
            const tripoint_bub_ms start = you.pos_bub() + tripoint_rel_ms( dx, dy, 0 );
            // A keeps the pan, B puts the view back, as the game's own look.
            g.look_around( /*show_window=*/true, center, start, false, false, false, false,
                         tripoint_bub_ms::zero, true, true );
            return false;
        }
    }
    return true;
}

std::pair<size_t, tripoint_bub_ms> godot_actions::route_preview()
{
    if( g == nullptr || g->destination_preview.empty() ) {
        return { 0, tripoint_bub_ms() };
    }
    return { g->destination_preview.size(), g->destination_preview.back() };
}

bool godot_actions::wait_popup_shown()
{
    return g != nullptr && g->wait_popup != nullptr;
}

bool godot_actions::open_movement()
{
    if( !godot_host::move_wheel_enabled() || !( gamepad::last_input_was_pad() || godot_host::touch_pad() ) ) {
        return false;
    }
    godot_host::request_move_wheel();
    return true;
}

void godot_actions::publish_move_modes( const avatar &you )
{
    std::vector<godot_host::move_mode_entry> modes;
    const move_mode_id &current = you.current_movement_mode();
    const int speed = std::max( 1, you.get_speed() );
    for( const move_mode_id &m : move_modes_by_speed() ) {
        godot_host::move_mode_entry e;
        e.id = m.str();
        e.name = m->name();
        e.letter = std::string( 1, m->letter() );
        e.can = you.can_switch_to( m );
        e.seconds = static_cast<float>( you.move_mode_switch_cost( current, m ) ) / speed;
        modes.push_back( std::move( e ) );
    }
    godot_host::set_move_modes( std::move( modes ) );
}

bool godot_actions::flush_owed( const std::string &action )
{
    if( action == "TIMEOUT" && ( godot_host::snapshot_owed() || godot_host::take_flat_view_changed() ||
                                 godot_host::take_sidebar_redraw() ) ) {
        godot_host::request_snapshot_flush();
        g->invalidate_main_ui_adaptor();
        return true;
    }
    return false;
}

void godot_actions::look_confirmed( avatar &u, const tripoint_bub_ms &lp )
{
    const tripoint_bub_ms pos = u.pos_bub();
    u.view_offset.x() = lp.x() - pos.x();
    u.view_offset.y() = lp.y() - pos.y();
}

bool godot_actions::look_pass( avatar &u, tripoint_bub_ms &lp, tripoint_bub_ms &center,
                               tripoint_rel_ms &prev_offset, bool select_zone, std::string &action )
{
    // The host's touch drag: the cursor to its tile, the view centre with
    // it. Taken at the top of a pass, so a request made before the look
    // opened (a slide that opens it) applies without waiting for input.
    {
        int cursor_dx = 0;
        int cursor_dy = 0;
        if( godot_host::take_look_cursor_request( cursor_dx, cursor_dy ) ) {
            // Inside the map, as the cursor's own moves stay.
            lp.x() = std::clamp( u.pos_bub().x() + cursor_dx, 0, MAPSIZE_X - 1 );
            lp.y() = std::clamp( u.pos_bub().y() + cursor_dy, 0, MAPSIZE_Y - 1 );
            // The view's centre follows only in jumps (godot_host::
            // look_margin): past half the margin from it, it moves that far
            // ahead of the cursor, the way the cursor went, so the cursor
            // crosses a whole margin before the next. In between the map
            // isn't redrawn, and the host pans over the wider snapshot.
            godot_host::set_look_margin( true );
            // The whole map recorded (look_whole): the centre stays put.
            const int half = godot_host::look_margin() / 2;
            const int dx = lp.x() - center.x();
            const int dy = lp.y() - center.y();
            if( !godot_host::look_whole() && std::max( std::abs( dx ), std::abs( dy ) ) > half ) {
                center.x() = std::clamp( lp.x() + std::clamp( dx, -half, half ), 0, MAPSIZE_X - 1 );
                center.y() = std::clamp( lp.y() + std::clamp( dy, -half, half ), 0, MAPSIZE_Y - 1 );
            }
        }
        // The slide settled: the view's centre onto the cursor (not with
        // the whole map recorded: the host eases onto the cursor itself).
        if( godot_host::take_look_centre_request() && !godot_host::look_whole() ) {
            center.x() = lp.x();
            center.y() = lp.y();
        }
        // The touch pad's view radius slider, as the zoom actions here.
        if( const int steps = godot_host::take_zoom_view_request() ) {
            godot_host::zoom_view( steps );
            g->mark_main_ui_adaptor_resize();
        }
    }
    // The touch pad's Go here and Recenter: out of look-around, the
    // view as it was (Go here: the route to the cursor's tile then waits
    // for Confirm, as a tapped one does) or centred on the avatar.
    if( !select_zone ) {
        const godot_host::look_exit how = godot_host::take_look_exit_request();
        if( how == godot_host::look_exit::go_here ) {
            // The view stays where it was looking until the route is
            // settled; the one before look-around is held for then.
            look_go_here( u, lp, center, prev_offset );
            action = "QUIT";
            return true;
        }
        if( how == godot_host::look_exit::recenter ) {
            center = u.pos_bub();
            lp = u.pos_bub();
            prev_offset = tripoint_rel_ms::zero;
            action = "QUIT";
            return true;
        }
    }
    return false;
}

void godot_actions::look_go_here( avatar &u, const tripoint_bub_ms &lp,
                                  const tripoint_bub_ms &center, tripoint_rel_ms &prev_offset )
{
    godot_host::request_click( lp.x() - u.pos_bub().x(), lp.y() - u.pos_bub().y() );
    godot_host::hold_view( prev_offset.x(), prev_offset.y(), prev_offset.z() );
    // After a slide the view is on the cursor whatever the engine's centre
    // (godot_host::look_margin: it follows in jumps, or not at all).
    const tripoint_bub_ms at = godot_host::look_margin() > 0 ? tripoint_bub_ms( lp.x(), lp.y(), center.z() ) : center;
    u.view_offset = at - u.pos_bub();
    prev_offset = u.view_offset;
}


void godot_actions::overmap_drag( tripoint_abs_omt &curs )
{
    static float rest_x = 0.0f;
    static float rest_y = 0.0f;
    static std::chrono::steady_clock::time_point last;
    const std::pair<float, float> d = godot_host::take_map_drag();
    if( d.first == 0.0f && d.second == 0.0f ) {
        return;
    }
    // A new slide (none for a while) starts with nothing left over.
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    if( now - last > std::chrono::milliseconds( 300 ) ) {
        rest_x = 0.0f;
        rest_y = 0.0f;
    }
    last = now;
    const point cell = get_window_dimensions( g->w_overmap ).scaled_font_size;
    if( cell.x <= 0 || cell.y <= 0 ) {
        return;
    }
    rest_x += d.first;
    rest_y += d.second;
    const int cx = static_cast<int>( rest_x / cell.x );
    const int cy = static_cast<int>( rest_y / cell.y );
    rest_x -= static_cast<float>( cx * cell.x );
    rest_y -= static_cast<float>( cy * cell.y );
    curs += point( -cx, -cy );
}


std::string godot_actions::overmap_cancel( const std::string &action )
{
    if( action == "QUIT" && gamepad::last_input_was_pad() &&
        gamepad::last_menu_press() == gamepad::menu_press::b && !get_avatar().omt_path.empty() ) {
        get_avatar().omt_path.clear();
        return std::string();
    }
    return action;
}

void godot_actions::publish_overmap_state( const tripoint_abs_omt &curs )
{
    const std::vector<tripoint_abs_omt> &route = get_avatar().omt_path;
    godot_host::set_overmap_state( !route.empty() && route.front().xy() == curs.xy() ? 2 : 1 );
}

#endif // CDDA_GODOT

#if defined(CDDA_GODOT)
const std::string *godot_actions::filter_action( const input_context &ctxt )
{
    static const std::array<std::string, 4> ids = {
        "UILIST.FILTER", "INVENTORY_FILTER", "FILTER", "SEARCH"
    };
    for( const std::string &id : ids ) {
        // A list with filtering off registers it all the same.
        if( id == "UILIST.FILTER" && !godot_host::list_filters() ) {
            continue;
        }
        if( ctxt.is_registered_action( id ) ) {
            return &id;
        }
    }
    return nullptr;
}

const std::string *godot_actions::count_action( const input_context &ctxt, int dir )
{
    static const std::array<std::array<std::string, 2>, 2> pairs = {{
            {{ "DECREASE_COUNT", "INCREASE_COUNT" }},
            {{ "BATCH_SIZE_DOWN", "BATCH_SIZE_UP" }}
        }
    };
    for( const std::array<std::string, 2> &pair : pairs ) {
        const std::string &id = pair[dir > 0 ? 1 : 0];
        if( ctxt.is_registered_action( id ) ) {
            return &id;
        }
    }
    return nullptr;
}

bool godot_actions::scrolls_description( const input_context &ctxt )
{
    static const std::set<std::string> ups = {
        "SCROLL_INFOBOX_UP", "SCROLL_ITEM_INFO_UP", "SCROLL_TRAIT_INFO_UP",
        "SCROLL_UP_SPELL_MENU", "SCROLL_DESC_UP", "DESC_LIST_UP"
    };
    static const std::set<std::string> downs = {
        "SCROLL_INFOBOX_DOWN", "SCROLL_ITEM_INFO_DOWN", "SCROLL_TRAIT_INFO_DOWN",
        "SCROLL_DOWN_SPELL_MENU", "SCROLL_DESC_DOWN", "DESC_LIST_DOWN"
    };
    return ups.count( ctxt.input_to_action( input_event( JOY_RS_UP, input_event_t::gamepad ) ) ) &&
           downs.count( ctxt.input_to_action( input_event( JOY_RS_DOWN, input_event_t::gamepad ) ) );
}
#endif // CDDA_GODOT

#if defined(CDDA_GODOT)
const std::string *godot_actions::turned_move( const input_context &ctxt, const std::string &action )
{
    // Clockwise from up, as the pad's directions.
    static const std::array<std::string, 8> moves = {
        "UP", "RIGHTUP", "RIGHT", "RIGHTDOWN", "DOWN", "LEFTDOWN", "LEFT", "LEFTUP"
    };
    const int step = godot_host::move_step();
    if( step == 0 || !( ctxt.input_category() == "DEFAULTMODE" || ctxt.is_iso() ) ) {
        return nullptr;
    }
    for( size_t i = 0; i < moves.size(); ++i ) {
        if( moves[i] == action ) {
            return &moves[( i + step ) % moves.size()];
        }
    }
    return nullptr;
}
#endif // CDDA_GODOT
