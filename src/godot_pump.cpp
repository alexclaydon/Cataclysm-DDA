#if defined(CDDA_GODOT)

// Each event pump (sdltiles.cpp CheckMessages, engine thread): the host's
// mouse applied, and the engine state Dark Forest reads published. Moved
// here from CheckMessages unchanged (2026-11-09, ADR 0018), where upstream's
// renderer coordinator kept conflicting with it.

#include <memory>
#include <string>

#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "cata_tiles.h"
#include "coordinates.h"
#include "game.h"
#include "godot_actions.h"
#include "godot_host.h"
#include "godot_tiles.h"
#include "input.h"
#include "input_context.h"
#include "options.h"
#include "player_activity.h"
#include "point.h"
#include "sdl_gamepad.h"
#include "sdltiles.h"

// The watch (godot_host::set_watch), as display::time_string and
// date_string read the time and the date.
static void godot_watch_publish( const avatar &you )
{
    if( !you.has_watch() ) {
        godot_host::set_watch( -1, 0, 0, 0 );
        return;
    }
    const std::string fmt = get_option<std::string>( "24_HOUR" );
    const int format = fmt == "military" ? 2 : fmt == "24h" ? 1 : 0;
    const int minute = hour_of_day<int>( calendar::turn ) * 60 + minute_of_hour<int>( calendar::turn );
    int day = day_of_season<int>( calendar::turn ) + 1;
    if( calendar::year_length() == 364_days && get_option<bool>( "SHOW_MONTHS" ) ) {
        day = month_and_day( calendar::turn ).second;
    }
    godot_host::set_watch( minute, format, static_cast<int>( day_of_week( calendar::turn ) ), day );
}

void godot_host::pump_engine_state()
{
    // The VR host's mouse (pointing at a window, or at a tile on the
    // 3D view), queued as events below.
    godot_host::apply_mouse( []( int dx, int dy, int &x, int &y ) {
        if( !tilecontext ) {
            return false;
        }
        const point_bub_ms tile = get_avatar().pos_bub().xy() + point( dx, dy );
        const point px = tilecontext->player_to_screen( tile ) +
                         point( tilecontext->get_tile_width() / 2, tilecontext->get_tile_height() / 2 );
        x = px.x;
        y = px.y;
        return true;
    } );
    // The host shows its virtual keyboard while the game wants typed text,
    // judged as the Android soft keyboard is (is_string_input, plus a
    // focused ImGui text field).
    {
        input_context *top = input_context::input_context_stack.back();
        bool wanted = cataimgui::client::want_text_input();
        if( top != nullptr ) {
            const std::string &category = top->input_category();
            wanted = wanted || category == "STRING_INPUT" || category == "STRING_EDITOR";
        }
        godot_host::set_text_wanted( wanted );
    }
    godot_host::set_in_menu( gamepad::is_in_menu() );
    {
        // The context waiting for input (polling), not the stack's top: a
        // context made once and kept (the first inventory opened made one)
        // sat above the inventory's, and RADIUS didn't page the first time.
        input_context *waiting = input_context::polling ? input_context::polling :
                                 input_context::input_context_stack.back();
        godot_host::set_menu_pages( waiting != nullptr && waiting->is_registered_action( "PAGE_UP" ) );
        godot_host::set_waiting_category( waiting != nullptr ? waiting->input_category() : std::string() );
        const std::string *filter = waiting != nullptr ? godot_actions::filter_action( *waiting ) : nullptr;
        godot_host::set_filter_action( filter != nullptr ? *filter : std::string() );
    }
    {
        // 3: on, and it just refused to attack a neutral creature.
        int safe = g ? static_cast<int>( g->safe_mode ) : 0;
        if( safe == SAFE_MODE_ON && godot_host::safe_refusal_turn() ==
            to_turns<int>( calendar::turn - calendar::turn_zero ) ) {
            safe = 3;
        }
        godot_host::set_safe_mode( safe );
    }
    if( g && g->is_core_data_loaded() ) {
        const avatar &you = get_avatar();
        godot_host::set_move_mode( you.get_desired_move_mode().str(),
                                   you.get_desired_move_mode() != you.current_movement_mode() );
        // As handle_key_blocking_activity (do_turn.cpp) decides.
        const bool unfinished = you.activity && ( you.activity.id()->based_on() == based_on_type::NEITHER ||
                                you.activity.moves_left > 0 );
        godot_host::set_activity_stoppable( ( unfinished && you.activity.is_interruptible_with_kb() ) ||
                                            you.has_destination() );
        godot_watch_publish( you );
    } else {
        godot_host::set_watch( -1, 0, 0, 0 );
        godot_host::set_move_mode( std::string(), false );
        godot_host::set_activity_stoppable( false );
    }
    // Which LT wheels the waiting context fills, for the touch pad.
    {
        input_context *ctxt = input_context::polling ? input_context::polling :
                              input_context::input_context_stack.back();
        int sides = 0;
        if( ctxt != nullptr ) {
            static const gamepad::direction dirs[] = {
                gamepad::direction::N, gamepad::direction::NE, gamepad::direction::E,
                gamepad::direction::SE, gamepad::direction::S, gamepad::direction::SW,
                gamepad::direction::W, gamepad::direction::NW
            };
            for( int side = 0; side < 2; ++side ) {
                for( gamepad::direction dir : dirs ) {
                    const input_event event( gamepad::direction_to_radial_joy( dir, side ),
                                             input_event_t::gamepad );
                    if( ctxt->input_to_action( event ) != "ERROR" ) {
                        sides |= 1 << side;
                        break;
                    }
                }
            }
        }
        godot_host::set_radial_sides( sides );
    }
    // The 3D translation catalogue, asked for by the host (ADR 0020): drawn
    // here on the engine's thread, in play or waiting at the main menu (not
    // while data loads: its loading screen pumps too).
    std::string tile_catalogue_spec;
    const bool at_main_menu = input_context::polling != nullptr &&
                              input_context::polling->input_category() == "MAIN_MENU";
    if( tilecontext && g && ( g->is_core_data_loaded() || at_main_menu ) &&
        godot_host::take_tile_catalogue_request( tile_catalogue_spec ) ) {
        godot_tiles::record_catalogue( *tilecontext, tile_catalogue_spec );
    }
}

#endif // CDDA_GODOT
