#pragma once
#ifndef CATA_SRC_GODOT_HOST_H
#define CATA_SRC_GODOT_HOST_H

// Hosting the engine inside another program (CDDA_GODOT builds only): the
// Godot VR host in the cataclysm-dda-vr repo. The host calls run() on a
// thread it owns, in place of main(). The engine renders to SDL's offscreen
// video driver and publishes every presented frame for the host to show;
// the host feeds input by pushing ordinary SDL events at window_id(), so
// the engine's own input path is unchanged.
//
// Deliberately plain types only, so the host never includes engine headers
// or depends on the engine's build defines.

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;

namespace godot_host
{

// The map view as the engine last drew it (plan 0003, Phase 3): one entry
// per sprite cata_tiles looked up for game::w_terrain, in draw order, so
// terrain comes before furniture, items and creatures on the same tile.
struct map_sprite {
    std::string id;
    std::string variant;
    int category;   // cata_tiles' TILE_CATEGORY
    int light;      // lit_level (MEMORIZED for remembered tiles)
    int x, y, z;    // tripoint_bub_ms
    int subtile;
    int rota;
    int height_3d;  // the tile's stack height so far, as cata_tiles has it
};

struct map_snapshot {
    int center[3];  // the view centre (tripoint_bub_ms)
    int player[3];  // the avatar (tripoint_bub_ms)
    std::vector<map_sprite> sprites;
};

// Host side.

// The engine's main(): same arguments, same exit code. Blocks until the
// game quits. Note that several engine paths call exit() outright, which
// ends the host process too.
int run( int argc, const char *argv[] );

// If a frame newer than `sequence` has been published, copy it (RGBA8,
// tightly packed, top row first), update `sequence` and return true.
bool take_frame( uint64_t &sequence, std::vector<unsigned char> &rgba, int &width,
                 int &height );

// The SDL window ID to address pushed events to; 0 until the window exists.
uint32_t window_id();

// If the map has been drawn since `sequence`, move the newest snapshot into
// `out`, update `sequence` and return true.
bool take_map_snapshot( uint64_t &sequence, map_snapshot &out );

// Engine side.

void publish_frame( SDL_Renderer *renderer );
void set_window_id( uint32_t id );

// Around the map view's draw: record sprite lookups between begin and end,
// then publish them. A draw that recorded nothing (aborted, or nothing
// visible) publishes nothing.
void begin_map_snapshot( int center_x, int center_y, int center_z, int player_x, int player_y,
                         int player_z );
void end_map_snapshot();

// Held for the duration of each cata_tiles sprite lookup. Lookups recurse
// for fallback ids, so only the outermost one is recorded.
class sprite_lookup_scope
{
    public:
        sprite_lookup_scope();
        ~sprite_lookup_scope();
        sprite_lookup_scope( const sprite_lookup_scope & ) = delete;
        sprite_lookup_scope &operator=( const sprite_lookup_scope & ) = delete;
        // True for the outermost lookup during a map snapshot.
        bool recording() const;
};
void record_map_sprite( const std::string &id, const std::string &variant, int category,
                        int light, int x, int y, int z, int subtile, int rota, int height_3d );

} // namespace godot_host

#endif // CATA_SRC_GODOT_HOST_H
