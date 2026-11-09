#pragma once
#ifndef CATA_SRC_GODOT_ACTIONS_H
#define CATA_SRC_GODOT_ACTIONS_H

#if defined(CDDA_GODOT)

// What the play loop (handle_action.cpp) does for Dark Forest, kept out of
// upstream's handle_action (plan 0022: upstream's code gets one-line hooks
// into our own files). A friend of game, for its route preview.

#include <optional>
#include <string>

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
};

#endif // CDDA_GODOT

#endif // CATA_SRC_GODOT_ACTIONS_H
