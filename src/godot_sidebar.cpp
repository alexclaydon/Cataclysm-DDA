#if defined(CDDA_GODOT)

// The sidebar for Dark Forest (plan 0020): the portrait band's layout, and
// a panel's window captured as BBCode rows for the host to draw. Moved here
// from game.cpp and sdltiles.cpp unchanged (2026-11-09, plan 0022); the
// palette stays behind sdltiles.cpp's godot_host::palette_rgb.

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "cursesdef.h"
#include "cursesport.h"
#include "game.h"
#include "godot_host.h"
#include "options.h"
#include "output.h"
#include "panels.h"
#include "point.h"
#include "string_formatter.h"
#include "widget.h"

// The sidebar as a band across the top (godot_host::sidebar_band, Dark
// Forest in portrait): the narrow layout's panels flowed top to bottom
// into columns side by side, the message log taking what its column has
// left. Panels that don't fit across are left out.
void game::draw_panels_band( bool draw_this_turn )
{
    panel_manager &mgr = panel_manager::get_manager();
    panel_layout *narrow = mgr.find_layout( "legacy_labels_narrow_sidebar" );
    panel_layout &layout = narrow != nullptr ? *narrow : mgr.get_current_layout();
    if( narrow != nullptr ) {
        static bool finalized = false;
        if( !finalized ) {
            widget *sidebar = mgr.get_sidebar( narrow->name() );
            if( sidebar != nullptr ) {
                widget::finalize_inherited_fields_recursive( sidebar->getId(), sidebar->_separator,
                        sidebar->_padding );
            }
            finalized = true;
        }
    }
    const int band = std::min( godot_host::sidebar_band(), TERMY );
    const int spacer = get_option<bool>( "SIDEBAR_SPACERS" ) ? 1 : 0;
    // The whole band first, so the gaps between columns are blank, not
    // whatever the map window under it last drew.
    catacurses::window blank = catacurses::newwin( band, TERMX, point::zero );
    werase( blank );
    wnoutrefresh( blank );
    int x = 0;
    int y = 0;
    for( const window_panel &panel : layout.panels() ) {
        if( !panel.toggle || !panel.render() ) {
            continue;
        }
        const int width = panel.get_width();
        int h = panel.get_height();
        // The message log (-2) fills the rest of a column, in a fresh one
        // when too little is left.
        if( h == -2 ) {
            if( band - y < 3 && y > 0 ) {
                x += width + 1;
                y = 0;
            }
            h = band - y;
        }
        if( h <= 0 ) {
            continue;
        }
        h += spacer;
        if( y + h > band && y > 0 ) {
            x += width + 1;
            y = 0;
        }
        if( x + width > TERMX ) {
            break;
        }
        h = std::min( h, band - y );
        if( panel.always_draw || draw_this_turn ) {
            // The host's log box shows more than the band's column holds:
            // the log drawn the terminal's height (plan 0020).
            // (Narrowed by the host's log zoom, its font that much larger.)
            const bool tall_log = godot_host::native_sidebar() && panel.get_id() == "Log";
            const int lw = tall_log ? std::max( 10, width * 100 / godot_host::log_zoom() ) : width;
            catacurses::window w = tall_log ? catacurses::newwin( TERMY, lw, point( x, 0 ) ) :
                                   catacurses::newwin( h, width, point( x, y ) );
            const int tmp_h = panel.draw( { u, w, panel.get_widget() } );
            // Lines skipped for rendering: reclaim the space.
            h += tmp_h;
            if( godot_host::native_sidebar() ) {
                // The host draws it (plan 0020): captured, then blanked.
                godot_host::sidebar_place( panel.get_id(), panel.get_name(), x, y, lw, h );
                godot_host::sidebar_capture( panel.get_id(), w );
            }
        } else if( godot_host::native_sidebar() ) {
            godot_host::sidebar_place( panel.get_id(), panel.get_name(), x, y,
                                       panel.get_id() == "Log" ? std::max( 10, width * 100 / godot_host::log_zoom() ) : width, h );
        }
        y += h;
    }
}

// A sidebar panel's window as BBCode rows, its cells' text in runs of one
// colour (the palette's), then the window blanked so the frame shows
// nothing there (plan 0020). Trailing blanks dropped; '[' escaped.
void godot_host::sidebar_capture( const std::string &id, const catacurses::window &w )
{
    cata_cursesport::WINDOW *const win = w.get<cata_cursesport::WINDOW>();
    if( win == nullptr ) {
        return;
    }
    std::vector<std::string> rows;
    for( const cata_cursesport::curseline &line : win->line ) {
        std::string row;
        int fg = -1;
        int bg = -1;
        size_t kept = 0;  // row's length up to its last non-blank cell
        int kept_bg = 0;  // that cell's run's background
        bool open = false;
        for( const cata_cursesport::cursecell &cell : line.chars ) {
            if( cell.ch.empty() ) {
                continue;  // a wide character's second half
            }
            const int cf = static_cast<int>( cell.FG );
            const int cb = static_cast<int>( cell.BG );
            if( cf != fg || cb != bg ) {
                if( open ) {
                    row += bg > 0 ? "[/bgcolor][/color]" : "[/color]";
                }
                const std::array<uint8_t, 3> c = godot_host::palette_rgb( cf );
                row += string_format( "[color=#%02x%02x%02x]", c[0], c[1], c[2] );
                if( cb > 0 ) {
                    const std::array<uint8_t, 3> b = godot_host::palette_rgb( cb );
                    row += string_format( "[bgcolor=#%02x%02x%02x]", b[0], b[1], b[2] );
                }
                fg = cf;
                bg = cb;
                open = true;
            }
            row += cell.ch == "[" ? "[lb]" : cell.ch;
            if( cell.ch != " " || cb > 0 ) {
                kept = row.size();
                kept_bg = cb;
            }
        }
        row.resize( kept );
        if( open && kept > 0 ) {
            row += kept_bg > 0 ? "[/bgcolor][/color]" : "[/color]";
        }
        rows.push_back( std::move( row ) );
    }
    while( !rows.empty() && rows.back().empty() ) {
        rows.pop_back();
    }
    godot_host::sidebar_rows( id, std::move( rows ) );
    werase( w );
    wnoutrefresh( w );
}

#endif // CDDA_GODOT
