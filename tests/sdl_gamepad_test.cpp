#if defined(TILES)

#include <vector>

#include "cata_catch.h"
#include "input.h"
#include "input_context.h"
#include "input_enums.h"
#include "sdl_gamepad.h"
#include "ui_manager.h"

// Our fork's controller scheme (aec-dev), driven through the module's test
// seam with no device: the LT layer, the LT-then-RT second layer, the
// radial wheels (latching, A or LT release firing, B cancelling), a stick
// spent on a wheel reading centred, and the sticks' hysteresis. All in the
// gameplay plane (no menu open), but for A and B in a menu, which a
// yes/no prompt answers Yes and No (query_popup), and the d-pad spinning a
// number prompt (gamepad::number_step), and a which-way prompt taking map
// directions while a list keeps its arrows, and pad bindings named as
// buttons ("LT+X", "LT+LS SW"). A rebase that loses one of these shows up
// here before it reaches the Deck.

using gamepad::pad_axis;
using gamepad::pad_button;

namespace
{
constexpr int full = 30000;  // past the 16000 threshold, sticks and triggers

// Start from a known state, and leave it.
struct pad_session {
    pad_session() {
        gamepad::test_begin();
        last_input = input_event();
    }
    ~pad_session() {
        gamepad::test_end();
    }
};

void press( pad_button b )
{
    gamepad::test_button( b, true );
    gamepad::test_button( b, false );
}

int last_code()
{
    return last_input.sequence.empty() ? -1 : last_input.get_first_input();
}
} // namespace

TEST_CASE( "gamepad_lt_layer_sends_alt_twins", "[gamepad][fork]" )
{
    pad_session pad;
    press( pad_button::A );
    CHECK( last_code() == JOY_A );
    gamepad::test_axis( pad_axis::LT, full );
    REQUIRE( gamepad::is_alt_held() );
    press( pad_button::A );
    CHECK( last_code() == JOY_ALT_A );
    gamepad::test_axis( pad_axis::LT, 0 );
    CHECK_FALSE( gamepad::is_alt_held() );
    press( pad_button::A );
    CHECK( last_code() == JOY_A );
}

TEST_CASE( "gamepad_lt_then_rt_is_the_second_layer", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_axis( pad_axis::LT, full );
    last_input = input_event();
    // RT under LT is the layer's gate: it sends nothing itself.
    gamepad::test_axis( pad_axis::RT, full );
    CHECK( last_code() == -1 );
    press( pad_button::X );
    CHECK( last_code() == JOY_ALT2_X );
    // Letting RT go drops back to layer 1.
    gamepad::test_axis( pad_axis::RT, 0 );
    press( pad_button::X );
    CHECK( last_code() == JOY_ALT_X );
    // And letting LT go first leaves no layer stranded.
    gamepad::test_axis( pad_axis::RT, full );
    gamepad::test_axis( pad_axis::LT, 0 );
    gamepad::test_axis( pad_axis::RT, 0 );
    press( pad_button::X );
    CHECK( last_code() == JOY_X );
}

TEST_CASE( "gamepad_wheel_latches_and_fires_on_lt_release", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_axis( pad_axis::LT, full );
    gamepad::test_axis( pad_axis::RIGHT_Y, -full );  // up: north
    REQUIRE( gamepad::is_radial_right_open() );
    CHECK( gamepad::get_radial_right_direction() == gamepad::direction::N );
    // The slot latches: back at centre, it's still the one highlighted.
    gamepad::test_axis( pad_axis::RIGHT_Y, 0 );
    CHECK( gamepad::is_radial_right_open() );
    CHECK( gamepad::get_radial_right_direction() == gamepad::direction::N );
    last_input = input_event();
    gamepad::test_axis( pad_axis::LT, 0 );
    CHECK( last_code() == JOY_R_RADIAL_N );
    CHECK_FALSE( gamepad::is_radial_right_open() );
}

TEST_CASE( "gamepad_a_confirms_and_b_cancels_a_wheel", "[gamepad][fork]" )
{
    pad_session pad;
    SECTION( "A fires the highlighted slot" ) {
        gamepad::test_axis( pad_axis::LT, full );
        gamepad::test_axis( pad_axis::RIGHT_X, full );  // east
        REQUIRE( gamepad::is_radial_right_open() );
        last_input = input_event();
        press( pad_button::A );
        CHECK( last_code() == JOY_R_RADIAL_E );
        CHECK_FALSE( gamepad::is_radial_right_open() );
    }
    SECTION( "B closes it and nothing fires, then or on LT release" ) {
        gamepad::test_axis( pad_axis::LT, full );
        gamepad::test_axis( pad_axis::RIGHT_X, full );
        REQUIRE( gamepad::is_radial_right_open() );
        last_input = input_event();
        press( pad_button::B );
        CHECK_FALSE( gamepad::is_radial_right_open() );
        CHECK( last_code() == -1 );
        gamepad::test_axis( pad_axis::RIGHT_X, 0 );
        gamepad::test_axis( pad_axis::LT, 0 );
        CHECK( last_code() == -1 );
    }
}

TEST_CASE( "gamepad_stick_spent_on_a_wheel_reads_centred", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_axis( pad_axis::LT, full );
    gamepad::test_axis( pad_axis::LEFT_X, full );  // the left wheel, east
    REQUIRE( gamepad::is_radial_left_open() );
    gamepad::test_axis( pad_axis::LT, 0 );         // fires it; the stick is still east
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::NONE );
    gamepad::test_axis( pad_axis::LEFT_X, 0 );     // re-centred: live again
    gamepad::test_axis( pad_axis::LEFT_X, full );
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::E );
}

TEST_CASE( "gamepad_stick_has_hysteresis", "[gamepad][fork]" )
{
    pad_session pad;
    // Engages past the threshold (16000)...
    gamepad::test_axis( pad_axis::LEFT_Y, -15000 );
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::NONE );
    gamepad::test_axis( pad_axis::LEFT_Y, -17000 );
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::N );
    // ...holds until it falls well below it (4000 under)...
    gamepad::test_axis( pad_axis::LEFT_Y, -13000 );
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::N );
    gamepad::test_axis( pad_axis::LEFT_Y, -11000 );
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::NONE );
    // ...and needs the full threshold to engage again.
    gamepad::test_axis( pad_axis::LEFT_Y, -14000 );
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::NONE );
}

TEST_CASE( "gamepad_a_and_b_in_a_menu_are_known_as_the_pads", "[gamepad][fork]" )
{
    pad_session pad;
    // The game's own UI under a menu: two layers.
    ui_adaptor game_ui;
    ui_adaptor menu;
    REQUIRE( gamepad::is_in_menu() );
    press( pad_button::A );
    CHECK( last_code() == '\n' );
    CHECK( gamepad::last_menu_press() == gamepad::menu_press::a );
    // A key on the keyboard: its Enter is its own.
    gamepad::note_keyboard_input();
    CHECK( gamepad::last_menu_press() == gamepad::menu_press::none );
    press( pad_button::B );
    CHECK( last_code() == KEY_ESCAPE );
    CHECK( gamepad::last_menu_press() == gamepad::menu_press::b );
}

TEST_CASE( "gamepad_a_which_way_prompt_takes_map_directions", "[gamepad][fork]" )
{
    pad_session pad;
    ui_adaptor game_ui;
    ui_adaptor prompt;
    REQUIRE( gamepad::is_in_menu() );
    SECTION( "a prompt for a map direction (choose_direction's context)" ) {
        // Marked set_iso, as peek, close and the like ask: sticks and d-pad
        // as on the map, eight ways (and turned by the VR host's facing).
        input_context ctxt( "DEFAULTMODE" );
        ctxt.set_iso( true );
        gamepad::test_axis( pad_axis::LEFT_X, full );
        gamepad::test_axis( pad_axis::LEFT_Y, -full );  // up and right
        CHECK( last_code() == '9' );
        gamepad::test_axis( pad_axis::LEFT_X, 0 );
        gamepad::test_axis( pad_axis::LEFT_Y, 0 );
        press( pad_button::DPAD_UP );
        CHECK( last_code() == '8' );
        press( pad_button::DPAD_RIGHT );
        CHECK( last_code() == '6' );
    }
    SECTION( "a list keeps its arrows" ) {
        input_context ctxt( "INVENTORY" );
        gamepad::test_axis( pad_axis::LEFT_Y, -full );
        CHECK( last_code() == KEY_UP );
        gamepad::test_axis( pad_axis::LEFT_Y, 0 );
        press( pad_button::DPAD_UP );
        CHECK( last_code() == KEY_UP );
    }
}

TEST_CASE( "gamepad_bindings_read_as_buttons", "[gamepad][fork]" )
{
    // Hints and the keybindings screen name a pad binding as the player
    // reads it, not by its JSON name. No pad here, so the glyph style is
    // Xbox letters.
    const auto label = []( int code ) {
        return input_event( code, input_event_t::gamepad ).long_description();
    };
    CHECK( label( JOY_A ) == "A" );
    CHECK( label( JOY_LB ) == "LB" );
    CHECK( label( JOY_ALT_X ) == "LT+X" );
    CHECK( label( JOY_ALT2_B ) == "LT+RT+B" );
    CHECK( label( JOY_L_RADIAL_SW ) == "LT+LS SW" );
    CHECK( label( JOY_RS_UP_RIGHT ) == "RS NE" );
    CHECK( label( JOY_UP ) == "D\u2191" );
    CHECK( label( JOY_START ) == "Start" );
}

TEST_CASE( "gamepad_dpad_spins_a_number_prompt", "[gamepad][fork]" )
{
    pad_session pad;
    ui_adaptor game_ui;
    ui_adaptor menu;
    REQUIRE( gamepad::is_in_menu() );
    // In a menu the d-pad arrives as arrow keys; from the pad they step.
    press( pad_button::DPAD_UP );
    CHECK( last_code() == KEY_UP );
    CHECK( gamepad::last_input_was_pad() );
    CHECK( gamepad::number_step( last_input ) == 1 );
    press( pad_button::DPAD_DOWN );
    CHECK( gamepad::number_step( last_input ) == -1 );
    // With LT held, by ten.
    gamepad::test_axis( pad_axis::LT, full );
    press( pad_button::DPAD_UP );
    CHECK( last_code() == JOY_ALT_UP );
    CHECK( gamepad::number_step( last_input ) == 10 );
    press( pad_button::DPAD_DOWN );
    CHECK( gamepad::number_step( last_input ) == -10 );
    gamepad::test_axis( pad_axis::LT, 0 );
    // The keyboard's own arrows keep their meaning.
    gamepad::note_keyboard_input();
    CHECK_FALSE( gamepad::last_input_was_pad() );
    CHECK( gamepad::number_step( input_event( KEY_UP, input_event_t::keyboard_code ) ) == 0 );
    // Other pad input doesn't step.
    press( pad_button::A );
    CHECK( gamepad::number_step( last_input ) == 0 );
}

// The host's shoulder wheels (ADR 0009): kinds as godot_host::wheel_event.
namespace
{
constexpr int wheel_opened = 0;
constexpr int wheel_stick = 1;
constexpr int wheel_confirm = 2;
constexpr int wheel_released = 4;
} // namespace

TEST_CASE( "gamepad_shoulders_without_host_wheels_are_as_ever", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_button( pad_button::LB, true );
    CHECK( last_code() == JOY_LB );  // on press
    gamepad::test_button( pad_button::LB, false );
    gamepad::test_advance( 1000 );
    CHECK_FALSE( gamepad::wheel_is_open() );
}

TEST_CASE( "gamepad_shoulder_tap_sends_its_binding_on_release", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_enable_wheels();
    gamepad::test_button( pad_button::RB, true );
    CHECK( last_code() == -1 );
    gamepad::test_advance( 100 );
    gamepad::test_button( pad_button::RB, false );
    CHECK( last_code() == JOY_RB );
    CHECK_FALSE( gamepad::wheel_is_open() );
    CHECK( gamepad::test_take_wheel_events().empty() );
}

TEST_CASE( "gamepad_shoulder_held_opens_its_wheel_and_captures_the_pad", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_enable_wheels();
    gamepad::test_button( pad_button::LB, true );
    gamepad::test_advance( 300 );
    REQUIRE( gamepad::wheel_is_open() );
    gamepad::test_axis( pad_axis::RIGHT_X, full );
    press( pad_button::A );
    gamepad::test_axis( pad_axis::RT, full );  // swallowed: no walking, no pause
    press( pad_button::X );                    // swallowed
    CHECK( last_code() == -1 );
    gamepad::test_button( pad_button::LB, false );
    CHECK_FALSE( gamepad::wheel_is_open() );
    CHECK( last_code() == -1 );  // no tap after a wheel
    const std::vector<int> kinds = gamepad::test_take_wheel_events();
    REQUIRE( kinds.size() == 4 );
    CHECK( kinds[0] == wheel_opened );
    CHECK( kinds[1] == wheel_stick );
    CHECK( kinds[2] == wheel_confirm );
    CHECK( kinds[3] == wheel_released );
    // The stick that picked is spent: it doesn't aim until it re-centres.
    CHECK( gamepad::get_right_stick_direction() == gamepad::direction::NONE );
    gamepad::test_axis( pad_axis::RT, 0 );
}

TEST_CASE( "gamepad_a_stick_pushed_opens_a_wheel_but_not_the_aiming_one", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_enable_wheels();
    gamepad::test_axis( pad_axis::LEFT_X, full );  // aiming east
    gamepad::test_button( pad_button::LB, true );
    gamepad::test_axis( pad_axis::LEFT_Y, -full );  // still aiming: no wheel yet
    CHECK_FALSE( gamepad::wheel_is_open() );
    gamepad::test_axis( pad_axis::RIGHT_Y, -full );  // the other stick opens it
    REQUIRE( gamepad::wheel_is_open() );
    gamepad::test_button( pad_button::LB, false );
    gamepad::test_axis( pad_axis::RIGHT_Y, 0 );
    // The aiming stick was spent on the wheel: centred until it re-centres.
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::NONE );
    gamepad::test_axis( pad_axis::LEFT_X, 0 );
    gamepad::test_axis( pad_axis::LEFT_Y, 0 );
    gamepad::test_axis( pad_axis::LEFT_X, full );
    CHECK( gamepad::get_left_stick_direction() == gamepad::direction::E );
}

TEST_CASE( "gamepad_lt_with_a_shoulder_stays_its_alt_twin", "[gamepad][fork]" )
{
    pad_session pad;
    gamepad::test_enable_wheels();
    gamepad::test_axis( pad_axis::LT, full );
    gamepad::test_button( pad_button::LB, true );
    CHECK( last_code() == JOY_ALT_LB );  // on press, as ever
    gamepad::test_button( pad_button::LB, false );
    gamepad::test_advance( 1000 );
    CHECK_FALSE( gamepad::wheel_is_open() );
    gamepad::test_axis( pad_axis::LT, 0 );
}

#endif // TILES
