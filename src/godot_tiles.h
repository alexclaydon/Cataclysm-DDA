#pragma once
#ifndef CATA_SRC_GODOT_TILES_H
#define CATA_SRC_GODOT_TILES_H

#if defined(CDDA_GODOT)

// What cata_tiles::draw adds for Dark Forest's 3D and 2D views, kept out
// of cata_tiles.cpp (ADR 0018: upstream's busy files get one-line hooks
// into our own). A friend of cata_tiles, for its tileset and its layer
// functions.

#include <string>
#include <vector>

#include "coords_fwd.h"
#include "sdl_wrappers.h"
#include "weighted_list.h"

class cata_tiles;
class map;
class texture;
struct point;

struct godot_tiles {
    // Once per tileset and per world's data: the tileset's sheets behind
    // the snapshot's sprite indices, and every terrain, furniture and field
    // type's properties for the 3D view's modelling rules.
    static void publish_tileset_and_objects( cata_tiles &ct );
    // A map pass skipped as unchanged (godot_host::map_draw_skipped): the
    // one-shot overlays the draw before it asked for (the route preview's
    // line, the cursor, weather...) dropped, as a full pass drops them once
    // drawn. Kept, a route preview cancelled after a skipped pass came back
    // in the next one and stayed on the board (2026-11-11).
    static void drop_one_shots( cata_tiles &ct );
    // An effect animation is playing (a projectile, an explosion, a hit
    // flash, a monster's attack): each step's redraw must be recorded, not
    // skipped as an unchanged map (sdltiles.cpp map_unchanged), or the 3D
    // view never sees it while the game still waits on it (2026-11-11).
    static bool animating( const cata_tiles &ct );
    // While a snapshot is recorded: the weather, the sky, the creatures the
    // avatar sees, the light on its storey, then the other storeys (what
    // the avatar remembers below it, the terrain above), recorded through
    // cata_tiles' own layer functions without drawing.
    static void record_extras( cata_tiles &ct, map &here, const tripoint_bub_ms &center,
                               const tripoint_bub_ms &you_pos );
    // Each blit draw_sprite_at makes (its sprite, rotation, flip, offset,
    // size, scale and opaque bounds), and an animated tile's whole cycle,
    // recorded for the host's snapshot.
    // The 3D translation catalogue (ADR 0020; godot_host::request_tile_catalogue):
    // every id of the categories `spec` asks for, drawn by the tileset's
    // own lookup into a snapshot of its own, record-only, one tile each.
    // On the engine's thread, at the main menu or in play.
    static void record_catalogue( cata_tiles &ct, const std::string &spec );
    static void record_blit( const cata_tiles &ct, const texture &sprite_tex, int sprite_index,
                             double render_angle, CataFlipMode render_flip, const point &blit_at,
                             int width, int height, float pixelscale,
                             const weighted_int_list<std::vector<int>> &svlist, int rota,
                             unsigned int loc_rand );
};

#endif // CDDA_GODOT

#endif // CATA_SRC_GODOT_TILES_H
