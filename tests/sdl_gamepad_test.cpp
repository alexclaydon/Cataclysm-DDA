#if defined(TILES)

#include "cata_catch.h"
#include "input.h"
#include "input_enums.h"
#include "sdl_gamepad.h"

// Our fork's controller scheme (aec-dev), driven through the module's test
// seam with no device: the LT layer, the LT-then-RT second layer, the
// radial wheels (latching, A or LT release firing, B cancelling), a stick
// spent on a wheel reading centred, and the sticks' hysteresis. All in the
// gameplay plane (no menu open). A rebase that loses one of these shows up
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

#endif // TILES
