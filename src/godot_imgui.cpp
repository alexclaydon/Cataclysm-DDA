#if defined(CDDA_GODOT)

// ImGui screens drawn only when they change (P24; the user's report of
// sluggish menus on the phone, 2026-11-19). Hosted, SDL's renderer is the
// software one: an ImGui screen (crafting and the others) rebuilds its
// draw lists every input pass, ~60 a second, and each was rasterised on
// the CPU at the frame's size (~11 ms on the M4, 66% of the engine thread
// sitting in crafting) and presented, changed or not. Now the draw data is
// hashed first (vertices, indices, commands, the display's size, the
// renderer's resources): identical to the last one rasterised, and no
// curses window drawn under it since, the render and the present are
// skipped. The frame keeps what was drawn. A press then costs the one
// render that shows it.

#include <cstdint>
#include <cstring>

#include "godot_imgui.h"
#include "imgui/imgui.h"
#include "sdltiles.h"

namespace
{
uint64_t last_hash = 0;
bool have_last = false;
bool drawn_under = true;
bool last_skipped = false;

void mix( uint64_t &h, const void *data, size_t bytes )
{
    const unsigned char *p = static_cast<const unsigned char *>( data );
    for( size_t i = 0; i < bytes; ++i ) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
}

uint64_t hash_of( const ImDrawData *data )
{
    uint64_t h = 1469598103934665603ULL;
    mix( h, &data->DisplaySize, sizeof( data->DisplaySize ) );
    mix( h, &data->DisplayPos, sizeof( data->DisplayPos ) );
    const uint64_t generation = renderer_resource_generation();
    mix( h, &generation, sizeof( generation ) );
    for( int n = 0; n < data->CmdListsCount; ++n ) {
        const ImDrawList *list = data->CmdLists[n];
        mix( h, list->VtxBuffer.Data, static_cast<size_t>( list->VtxBuffer.Size ) * sizeof( ImDrawVert ) );
        mix( h, list->IdxBuffer.Data, static_cast<size_t>( list->IdxBuffer.Size ) * sizeof( ImDrawIdx ) );
        for( const ImDrawCmd &cmd : list->CmdBuffer ) {
            mix( h, &cmd.ClipRect, sizeof( cmd.ClipRect ) );
            // The reference as it stands, not GetTexID(): that asserts on a
            // texture the backend hasn't created yet, and this runs before
            // the render that creates it (a new font atlas texture, as the
            // atlas grows: the phone's crafting screen crashed on it,
            // 2026-11-20).
            const void *tex_data = cmd.TexRef._TexData;
            const ImTextureID tex_id = cmd.TexRef._TexID;
            mix( h, &tex_data, sizeof( tex_data ) );
            mix( h, &tex_id, sizeof( tex_id ) );
            mix( h, &cmd.VtxOffset, sizeof( cmd.VtxOffset ) );
            mix( h, &cmd.IdxOffset, sizeof( cmd.IdxOffset ) );
            mix( h, &cmd.ElemCount, sizeof( cmd.ElemCount ) );
            const bool callback = cmd.UserCallback != nullptr;
            mix( h, &callback, sizeof( callback ) );
        }
    }
    return h;
}
} // namespace

bool godot_imgui::unchanged( const ImDrawData *data )
{
    if( data == nullptr ) {
        last_skipped = false;
        return false;
    }
    // A texture waiting on the backend (a font atlas growing: ImGui 1.92
    // adds glyphs as they're first drawn) is always rendered: the render
    // is what uploads it.
    bool textures_settled = true;
    if( data->Textures != nullptr ) {
        for( const ImTextureData *tex : *data->Textures ) {
            if( tex->Status != ImTextureStatus_OK ) {
                textures_settled = false;
            }
        }
    }
    const uint64_t h = hash_of( data );
    const bool same = have_last && h == last_hash && !drawn_under && textures_settled;
    last_hash = h;
    have_last = true;
    drawn_under = false;
    last_skipped = same;
    return same;
}

void godot_imgui::note_drawn_under()
{
    drawn_under = true;
}

bool godot_imgui::skipped()
{
    return last_skipped;
}

#endif // CDDA_GODOT
