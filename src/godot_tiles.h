#pragma once
#ifndef CATA_SRC_GODOT_TILES_H
#define CATA_SRC_GODOT_TILES_H

#if defined(CDDA_GODOT)

// What cata_tiles::draw adds for Dark Forest's 3D and 2D views, kept out
// of cata_tiles.cpp (ADR 0018: upstream's busy files get one-line hooks
// into our own). A friend of cata_tiles, for its tileset and its layer
// functions.

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
    // While a snapshot is recorded: the weather, the sky, the creatures the
    // avatar sees, the light on its storey, then the other storeys (what
    // the avatar remembers below it, the terrain above), recorded through
    // cata_tiles' own layer functions without drawing.
    static void record_extras( cata_tiles &ct, map &here, const tripoint_bub_ms &center,
                               const tripoint_bub_ms &you_pos );
    // Each blit draw_sprite_at makes (its sprite, rotation, flip, offset,
    // size, scale and opaque bounds), and an animated tile's whole cycle,
    // recorded for the host's snapshot.
    static void record_blit( const cata_tiles &ct, const texture &sprite_tex, int sprite_index,
                             double render_angle, CataFlipMode render_flip, const point &blit_at,
                             int width, int height, float pixelscale,
                             const weighted_int_list<std::vector<int>> &svlist, int rota,
                             unsigned int loc_rand );
};

#endif // CDDA_GODOT

#endif // CATA_SRC_GODOT_TILES_H
