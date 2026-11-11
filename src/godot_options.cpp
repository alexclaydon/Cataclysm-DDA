#if defined(CDDA_GODOT)

#include "godot_options.h"

#include <array>
#include <string>

#include "options.h"

void godot_options::hide_hosted( options_manager &om )
{
    static const std::array<const char *, 14> hosted = {
        "TERMINAL_X", "TERMINAL_Y",
        "FONT_WIDTH", "FONT_HEIGHT", "FONT_SIZE",
        "MAP_FONT_WIDTH", "MAP_FONT_HEIGHT", "MAP_FONT_SIZE",
        "OVERMAP_FONT_WIDTH", "OVERMAP_FONT_HEIGHT", "OVERMAP_FONT_SIZE",
        "FULLSCREEN", "SCALING_FACTOR", "DISPLAY",
    };
    for( const char *name : hosted ) {
        const auto it = om.options.find( name );
        if( it != om.options.end() ) {
            it->second.hide = COPT_ALWAYS_HIDE;
        }
    }
}

#endif // CDDA_GODOT
