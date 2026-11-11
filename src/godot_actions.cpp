#if defined(CDDA_GODOT)

// The play loop's additions for Dark Forest (godot_actions.h). Moved here
// from handle_action.cpp unchanged (2026-11-09, plan 0022).

#include <algorithm>
#include <array>
#include <chrono>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "coordinates.h"
#include "game.h"
#include "input_context.h"
#include "map_scale_constants.h"
#include "godot_actions.h"
#include "godot_host.h"
#include "messages.h"
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

bool godot_actions::take_requests( game &g, avatar &you, std::string &action,
                                   std::optional<tripoint_rel_ms> &wheel_aim,
                                   std::optional<tripoint_bub_ms> &host_click )
{
    // The touch pad's view radius slider, as the zoom actions.
    if( const int steps = godot_host::take_zoom_view_request() ) {
        godot_host::zoom_view( steps );
        g.mark_main_ui_adaptor_resize();
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
    u.view_offset = center - u.pos_bub();
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
