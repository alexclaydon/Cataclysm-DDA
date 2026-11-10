#pragma once
#ifndef CATA_SRC_GODOT_ACTIONS_H
#define CATA_SRC_GODOT_ACTIONS_H

#if defined(CDDA_GODOT)

// What the play loop (handle_action.cpp) does for Dark Forest, kept out of
// upstream's handle_action (plan 0022: upstream's code gets one-line hooks
// into our own files). A friend of game, for its route preview.

#include <optional>
#include <string>
#include <utility>

#include "coords_fwd.h"

class avatar;
class game;

struct godot_actions {
    // At the top of game::handle_action: Go here's held view, back once its
    // route is walked or dropped.
    static void settle_view( game &g, avatar &you );
    // Just before the play loop waits for input: the touch pad's context
    // row and whether a route preview is pending.
    static void before_input( game &g );
    // After input: the host's requests (the view radius, the catalogue, a
    // wheel's or the hotbar's action, a tile clicked, the test hooks for
    // time, goto and look). `action` may become the requested one, with
    // `wheel_aim` where the wheel opened, or SELECT with `host_click` its
    // tile. False when handle_action should return now (false).
    static bool take_requests( game &g, avatar &you, std::string &action,
                               std::optional<tripoint_rel_ms> &wheel_aim,
                               std::optional<tripoint_bub_ms> &host_click );
    // On the play loop's idle TIMEOUT: a snapshot owed (a held walk's
    // skipped one), the view turned 2D or 3D, or the native sidebar
    // switched: flush and invalidate the main UI. True if it did.
    static bool flush_owed( const std::string &action );
    // At the top of each look-around pass (game::look_around): the host's
    // touch drag (the cursor and the view centre to its tile), the touch
    // pad's radius slider, and its Go here and Recenter, which leave the
    // look (`action` QUIT). True when look-around should leave now.
    static bool look_pass( avatar &u, tripoint_bub_ms &lp, tripoint_bub_ms &center,
                           tripoint_rel_ms &prev_offset, bool select_zone, std::string &action );
    // Go here from look-around: the route to the cursor's tile (`lp`) as a
    // tapped one, the view held where it was looking until it is settled.
    static void look_go_here( avatar &u, const tripoint_bub_ms &lp, const tripoint_bub_ms &center,
                              tripoint_rel_ms &prev_offset );
    // The game's wait popup ("Wait till you wake up…", an activity's
    // progress) is up: on the UI stack, but taking no input, so not a menu
    // (gamepad::is_in_menu; the right stick stays the camera while sleeping
    // or waiting, the user's call 2026-11-10).
    static bool wait_popup_shown();
    // The route preview's length and end (sdltiles.cpp's unchanged-map
    // signature): {0, origin} with none.
    static std::pair<size_t, tripoint_bub_ms> route_preview();
    // The overmap's loop: a slide on the map the touch pad sent
    // (godot_host::request_map_drag) moves the cursor the other way, a
    // map cell for each cell's width the finger crossed, so the map
    // follows it; the part of a cell left over waits for the next.
    static void overmap_drag( tripoint_abs_omt &curs );
};

#endif // CDDA_GODOT

#endif // CATA_SRC_GODOT_ACTIONS_H
