#if defined(CDDA_GODOT)

#include "godot_options.h"

#include <array>
#include <string>

#include "options.h"

void godot_options::hide_hosted( options_manager &om )
{
    // The display profile sets the first ones; the animations are drawn on
    // the engine's own map, which Dark Forest never shows, so they only cost
    // redraws (display/dark-forest-common.json sets them off, 2026-11-11);
    // the renderer's are read nowhere when hosted (it always renders in
    // software to a target the host reads back: sdltiles.cpp, CDDA_GODOT),
    // and the scaling mode scales only that unseen map.
    static const std::array<const char *, 22> hosted = {
        "TERMINAL_X", "TERMINAL_Y",
        "FONT_WIDTH", "FONT_HEIGHT", "FONT_SIZE",
        "MAP_FONT_WIDTH", "MAP_FONT_HEIGHT", "MAP_FONT_SIZE",
        "OVERMAP_FONT_WIDTH", "OVERMAP_FONT_HEIGHT", "OVERMAP_FONT_SIZE",
        "FULLSCREEN", "SCALING_FACTOR", "DISPLAY",
        "ANIMATION_RAIN", "ANIMATION_SCT",
        "RENDERER", "SOFTWARE_RENDERING", "FRAMEBUFFER_ACCEL", "USE_COLOR_MODULATED_TEXTURES",
        "SCALING_MODE",
        // Held at 0: Dark Forest plays its own music (music.gd).
        "MUSIC_VOLUME",
    };
    for( const char *name : hosted ) {
        const auto it = om.options.find( name );
        if( it != om.options.end() ) {
            it->second.hide = options_manager::COPT_ALWAYS_HIDE;
        }
    }
}

#endif // CDDA_GODOT
