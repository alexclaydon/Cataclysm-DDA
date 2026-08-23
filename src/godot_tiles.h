#pragma once
#ifndef CATA_SRC_GODOT_TILES_H
#define CATA_SRC_GODOT_TILES_H

#if defined(CDDA_GODOT)

// What cata_tiles::draw adds for Dark Forest's 3D and 2D views, kept out
// of cata_tiles.cpp (ADR 0018: upstream's busy files get one-line hooks
// into our own). A friend of cata_tiles, for its tileset and its layer
// functions.

#include "coords_fwd.h"

class cata_tiles;
class map;

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
};

#endif // CDDA_GODOT

#endif // CATA_SRC_GODOT_TILES_H
