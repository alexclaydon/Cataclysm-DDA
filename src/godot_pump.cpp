#if defined(CDDA_GODOT)

// Each event pump (sdltiles.cpp CheckMessages, engine thread): the host's
// mouse applied, and the engine state Dark Forest reads published. Moved
// here from CheckMessages unchanged (2026-11-09, ADR 0018), where upstream's
// renderer coordinator kept conflicting with it.

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

#include "activity_type.h"
#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "cata_tiles.h"
#include "catacharset.h"
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
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

// The text field the game types into (godot_host::set_text_field), for the
// host's on-screen keyboard to hold its text and cursor: an ImGui field's
// read from ImGui (every ImGui field: the number and text popups, the
// filter boxes, last words, the debug console), a curses prompt's as it
// published it (string_input_popup, string_editor_window). Published when
// the field changes, not as it's typed in: the keyboard's own field follows
// the typing from there.
static void godot_text_field_publish( bool wanted )
{
    // The field in use: ImGui's active id (tagged odd), or the prompt's input
    // context (a pointer, even); 0 for none.
    static std::uintptr_t field_was = 0;
    std::uintptr_t field = 0;
    std::string text;
    int select_from = 0;
    int cursor = 0;
    if( wanted && cataimgui::client::want_text_input() ) {
        const ImGuiID id = ImGui::GetActiveID();
        if( ImGuiInputTextState *state = ImGui::GetInputTextState( id ) ) {
            field = ( static_cast<std::uintptr_t>( id ) << 1 ) | 1;
            text.assign( state->TextA.Data != nullptr ? state->TextA.Data : "",
                         static_cast<size_t>( std::max( 0, state->TextLen ) ) );
            // ImGui's are byte offsets; the keyboard counts code points.
            const auto cpts = [&text]( int bytes ) {
                bytes = std::clamp( bytes, 0, static_cast<int>( text.size() ) );
                return static_cast<int>( utf8_wrapper( text.substr( 0, bytes ) ).length() );
            };
            cursor = cpts( state->GetCursorPos() );
            select_from = state->HasSelection() ? cpts( state->GetSelectionStart() ) : cursor;
        }
    }
    if( field == 0 && wanted ) {
        for( const input_context *ctxt : {
                 static_cast<const input_context *>( input_context::polling ),
                 static_cast<const input_context *>( input_context::input_context_stack.back() )
             } ) {
            if( ctxt != nullptr && godot_host::prompt_text( ctxt, text, cursor ) ) {
                field = reinterpret_cast<std::uintptr_t>( ctxt );
                select_from = cursor;
                break;
            }
        }
    }
    if( field != field_was ) {
        field_was = field;
        godot_host::set_text_field( text, select_from, cursor );
    }
}

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
        godot_text_field_publish( wanted );
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
        godot_host::set_counts( waiting != nullptr && godot_actions::count_action( *waiting, 1 ) != nullptr );
        godot_host::set_menu_sides( waiting != nullptr && !waiting->is_iso() &&
                                    waiting->input_category() != "DEFAULTMODE" &&
                                    waiting->input_to_action( input_event( JOY_LB, input_event_t::gamepad ) ) != "ERROR" );
        godot_host::set_menu_scroll( waiting != nullptr && !waiting->is_iso() &&
                                     godot_actions::scrolls_description( *waiting ) );
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
        godot_actions::publish_move_modes( you );
        // As handle_key_blocking_activity (do_turn.cpp) decides.
        const bool unfinished = you.activity && ( you.activity.id()->based_on() == based_on_type::NEITHER ||
                                you.activity.moves_left > 0 );
        godot_host::set_activity_stoppable( ( unfinished && you.activity.is_interruptible_with_kb() ) ||
                                            you.has_destination() );
        godot_watch_publish( you );
    } else {
        godot_host::set_watch( -1, 0, 0, 0 );
        godot_host::set_move_mode( std::string(), false );
        godot_host::set_move_modes( {} );
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
