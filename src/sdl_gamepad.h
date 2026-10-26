#pragma once
#ifndef CATA_SRC_SDL_GAMEPAD_H
#define CATA_SRC_SDL_GAMEPAD_H
#if defined(TILES)
#include <vector>

#include "input_enums.h"
#include "sdl_wrappers.h"

#if SDL_MAJOR_VERSION >= 3
#define SDL_GAMEPAD_SCHEDULER (SDL_EVENT_USER+1)
#else
#define SDL_GAMEPAD_SCHEDULER (SDL_USEREVENT+1)
#endif

struct tripoint;

// IWYU pragma: no_forward_declare input_event  // Is valid, but looks silly
extern input_event last_input;

namespace gamepad
{

// Direction states for left stick
enum class direction : int {
    NONE = 0,
    N,    // North (up)
    NE,   // Northeast
    E,    // East (right)
    SE,   // Southeast
    S,    // South (down)
    SW,   // Southwest
    W,    // West (left)
    NW    // Northwest
};

void init();
void quit();
// Returns true if this event type belongs to the gamepad module.
bool is_gamepad_event( const SDL_Event &event );
// Dispatch any gamepad-related SDL event. Returns true if UI needs refresh.
// Handles button, axis, device hotplug, and scheduler events internally.
bool handle_event( SDL_Event &event );


direction get_left_stick_direction();
direction get_right_stick_direction();
direction get_radial_left_direction();
direction get_radial_right_direction();
bool is_radial_left_open();
bool is_radial_right_open();
bool is_alt_held();

// In a menu the pad's A arrives as Enter and B as Escape. Which pad button
// sent the latest of those, or none once a key on the keyboard is pressed
// (note_keyboard_input): so a yes/no prompt can answer A with Yes while the
// keyboard's Enter keeps the prompt's default (query_popup).
enum class menu_press { none, a, b };
menu_press last_menu_press();
void note_keyboard_input();

// Whether the latest input came from the pad: set by every event it sends
// (in a menu, its synthesized keys too), cleared by the keyboard and mouse
// clicks (note_keyboard_input).
bool last_input_was_pad();
// The step a number prompt takes for `ev` (the d-pad spinner): the pad's
// up or down +1 or -1 (in a menu they arrive as arrow keys, so only while
// last_input_was_pad), with LT held +10 or -10 (JOY_ALT_UP/DOWN); 0 for
// anything else, the keyboard's own arrows included.
int number_step( const input_event &ev );

// A seam for the unit tests (tests/sdl_gamepad_test.cpp): with no device,
// test_begin resets all state and makes axis reads come from test_axis,
// which also delivers the motion as an event; test_button delivers a press
// or release. last_input and the getters above show the result.
enum class pad_button : int {
    A, B, X, Y, LB, RB, LS, RS, START, BACK, DPAD_UP, DPAD_DOWN, DPAD_LEFT, DPAD_RIGHT
};
enum class pad_axis : int { LEFT_X, LEFT_Y, RIGHT_X, RIGHT_Y, LT, RT };
void test_begin();
void test_end();
void test_axis( pad_axis which, int value );
void test_button( pad_button which, bool down );
// The host's shoulder wheels (ADR 0009): test_enable_wheels turns them on
// as a host drawing wheels would; test_advance moves the clock on and runs
// the scheduler; test_take_wheel_events returns the kinds of the wheel
// events sent since (godot_host::wheel_event::kind_t).
void test_enable_wheels();
void test_advance( int ms );
std::vector<int> test_take_wheel_events();
bool wheel_is_open();
bool is_in_menu();
bool is_active();

// Button glyphs for hints (the fork's, from the BN backlog): the
// GAMEPAD_GLYPHS option, where auto follows the pad in use (PlayStation's
// shapes for a Sony pad, Xbox letters for the rest, the Deck's included).
enum class glyph_style { xbox, playstation, text };
glyph_style current_glyph_style();
// The width draw_glyph takes for it, in pixels.
float glyph_width( pad_button button );
// Draws a button's glyph at the ImGui cursor as one item, a framed widget's
// height (it lines up with a button beside it). The face buttons in the
// makers' colours; any other button as its short name.
void draw_glyph( pad_button button );

// Convert direction enum to movement offset
tripoint direction_to_offset( direction dir );

// Convert direction to radial joy event
int direction_to_radial_joy( direction dir, int stick_idx );

} // namespace gamepad

#endif // TILES
#endif // CATA_SRC_SDL_GAMEPAD_H

