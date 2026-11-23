#if defined(TILES)

// The pad on query_popup's prompts (the fork's controller scheme): which
// option each face button answers, and a press turned into that answer.
// Member functions of query_popup, defined here rather than in upstream's
// popup.cpp (plan 0022); query_once calls them in two short hooks.
//
// The rules (the cdda-gamepad-controls skill, references/layouts.md): A
// answers Yes (or the first option, where the buttons answer them all),
// whatever is highlighted; B the option Escape takes, else No, else a
// Cancel Escape doesn't take; X and Y the third and fourth
// (gamepad::answer_buttons); A never answers B's option. The keyboard's
// Enter and Escape keep the prompt's own default.

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

#include "input.h"
#include "input_context.h"
#include "popup.h"
#include "sdl_gamepad.h"

void query_popup::find_pad_answers( input_context &ctxt )
{
    // What the pad's B answers: the option taking Escape, else No where
    // nothing can cancel. Its glyph goes on that button.
    pad_b_option = -1;
    const std::string &escape_action = ctxt.input_to_action( input_event( KEY_ESCAPE,
                                       input_event_t::keyboard_code ) );
    for( size_t i = 0; i < options.size(); ++i ) {
        if( options[i].action == escape_action ) {
            pad_b_option = int( i );
        }
    }
    for( size_t i = 0; i < options.size() && pad_b_option < 0 && !cancel; ++i ) {
        if( options[i].action == "NO" ) {
            pad_b_option = int( i );
        }
    }
    // Else a Cancel that Escape doesn't take (a character template's).
    for( size_t i = 0; i < options.size() && pad_b_option < 0 && !cancel; ++i ) {
        if( options[i].action == "CANCEL" ) {
            pad_b_option = int( i );
        }
    }
    std::vector<std::string> actions;
    actions.reserve( options.size() );
    for( const query_option &o : options ) {
        actions.push_back( o.action );
    }
    const gamepad::prompt_answers pad = gamepad::answer_buttons( actions, pad_b_option );
    pad_a_option = pad.a;
    pad_x_option = pad.x;
    pad_y_option = pad.y;
    pad_all = pad.all;
    // Their presses come back to the prompt (pad_answered), not to the
    // action list.
    if( pad_x_option >= 0 || pad_y_option >= 0 ) {
        ctxt.take_pad_faces();
    }
}

bool query_popup::pad_answered( result &res, bool by_mouse ) const
{
    const auto has_option = [&]( const std::string & action ) {
        return std::any_of( options.begin(), options.end(), [&]( const query_option & o ) {
            return o.action == action;
        } );
    };
    const gamepad::menu_press pad = by_mouse ? gamepad::menu_press::none : gamepad::last_menu_press();
    int pad_answer = -1;
    if( pad == gamepad::menu_press::a && res.action == "CONFIRM" && pad_a_option >= 0 &&
        ( pad_all || options[pad_a_option].action == "YES" ) ) {
        pad_answer = pad_a_option;
    } else if( pad == gamepad::menu_press::b && !cancel && res.evt.type == input_event_t::keyboard_code &&
               res.evt.get_first_input() == KEY_ESCAPE && !has_option( res.action ) ) {
        pad_answer = pad_b_option;
    } else if( !by_mouse && res.evt.type == input_event_t::gamepad ) {
        const int button = res.evt.get_first_input();
        pad_answer = button == JOY_X ? pad_x_option : button == JOY_Y ? pad_y_option : -1;
    }
    if( pad_answer >= 0 && pad_answer < static_cast<int>( options.size() ) ) {
        res.action = options[pad_answer].action;
        res.wait_input = false;
        return true;
    }
    // A never answers B's option, highlighted or not (the user's call,
    // 2026-11-19): a lone Close or Cancel said B and closed on A too, and
    // an A pressed in a fight dismissed what it hadn't read.
    if( pad == gamepad::menu_press::a && res.action == "CONFIRM" && !anykey && pad_b_option >= 0 &&
        static_cast<int>( cur ) == pad_b_option ) {
        res.wait_input = true;
        return true;
    }
    return false;
}

#endif // TILES
