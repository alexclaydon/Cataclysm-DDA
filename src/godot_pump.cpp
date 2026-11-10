#if defined(CDDA_GODOT)

// Each event pump (sdltiles.cpp CheckMessages, engine thread): the host's
// mouse applied, and the engine state Dark Forest reads published. Moved
// here from CheckMessages unchanged (2026-11-09, ADR 0018), where upstream's
// renderer coordinator kept conflicting with it.

#include <memory>
#include <string>

#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "cata_tiles.h"
#include "coordinates.h"
#include "game.h"
#include "godot_host.h"
#include "input.h"
#include "input_context.h"
#include "point.h"
#include "sdl_gamepad.h"
#include "sdltiles.h"

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
    } else {
        godot_host::set_move_mode( std::string(), false );
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
}

#endif // CDDA_GODOT
