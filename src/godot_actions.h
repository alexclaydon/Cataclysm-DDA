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
class input_context;

struct godot_actions {
    // The text filter the screen waiting for input offers, the first of
    // UILIST.FILTER, INVENTORY_FILTER, FILTER and the overmap's SEARCH it
    // registers; nullptr for none (the touch pad's Filter, 2026-11-12). A
    // pointer to a static string, as handle_input returns.
    static const std::string *filter_action( const input_context &ctxt );
    // The touch pad's - and +: the count action the screen offers, one
    // down (dir -1) or up (1): an item screen's DECREASE/INCREASE_COUNT,
    // crafting's BATCH_SIZE_DOWN/UP; nullptr for none.
    static const std::string *count_action( const input_context &ctxt, int dir );
    // Whether the right stick's up and down scroll a description on the
    // screen (the character creator's, item info, traits, spells...): the
    // touch pad's down-left and down-right send them there (menu_scroll).
    // By the actions they resolve to (the overlay binds them; LT + d-pad
    // until 2026-11-23, which pages now).
    static bool scrolls_description( const input_context &ctxt );
    // A keyboard move on the map (play, or a map cursor: look-around,
    // aiming) turned with the view as the pad's are (godot_host::move_step;
    // the user's call, 2026-11-12): the turned action, or nullptr when it
    // stays (not a move, not on the map, or no turn).
    static const std::string *turned_move( const input_context &ctxt, const std::string &action );
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
    // ACTION_OPEN_MOVEMENT (handle_action.cpp): from a pad (or the touch
    // pad), the host's move wheel instead of upstream's menu. True when the
    // host took it; the keyboard keeps the menu.
    static bool open_movement();
    // Each event pump (godot_pump.cpp): the movement modes for the wheel.
    static void publish_move_modes( const avatar &you );
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
    // Free look left with Confirm, the view kept (game::look_around's
    // persist_offset): the offset to the cursor's tile, not to the view's
    // centre, which follows the host's drag only in jumps or, with the
    // whole map recorded, not at all (2026-11-16: since 2026-11-15 Confirm
    // kept the centre, so the view went back to the avatar).
    static void look_confirmed( avatar &u, const tripoint_bub_ms &lp );
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
    // The overmap's input: a close (QUIT) from a pad's B (the touch pad's
    // Cancel too) with a route planned drops the route instead, returning
    // "" (the map stays); a second B closes it. Back (the Map button),
    // Start and the keyboard close it as ever (2026-11-11).
    static std::string overmap_cancel( const std::string &action );
    // The overmap's state for the touch pad (godot_host::set_overmap_state),
    // after each pass of its loop.
    static void publish_overmap_state( const tripoint_abs_omt &curs );
};

#endif // CDDA_GODOT

#endif // CATA_SRC_GODOT_ACTIONS_H
