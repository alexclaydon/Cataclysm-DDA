#if defined(TILES)
#include "sdl_gamepad.h"

#include <array>
#include <cmath>
#include <map>
#include <ostream>

#include "debug.h"
#include "input.h"
#include "input_enums.h"
#include "point.h"
#include "ui_manager.h"
#include "game.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace gamepad
{

// SDL3 compat: normalized event and API constants
#if SDL_MAJOR_VERSION >= 3
static constexpr Uint32 CATA_CONTROLLERBUTTONDOWN    = SDL_EVENT_GAMEPAD_BUTTON_DOWN;
static constexpr Uint32 CATA_CONTROLLERBUTTONUP      = SDL_EVENT_GAMEPAD_BUTTON_UP;
static constexpr Uint32 CATA_CONTROLLERAXISMOTION    = SDL_EVENT_GAMEPAD_AXIS_MOTION;
static constexpr Uint32 CATA_CONTROLLERDEVICEADDED   = SDL_EVENT_GAMEPAD_ADDED;
static constexpr Uint32 CATA_CONTROLLERDEVICEREMOVED = SDL_EVENT_GAMEPAD_REMOVED;
static constexpr int CATA_AXIS_TRIGGERLEFT  = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
static constexpr int CATA_AXIS_TRIGGERRIGHT = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
static constexpr int CATA_AXIS_LEFTX       = SDL_GAMEPAD_AXIS_LEFTX;
static constexpr int CATA_AXIS_LEFTY       = SDL_GAMEPAD_AXIS_LEFTY;
static constexpr int CATA_AXIS_RIGHTX      = SDL_GAMEPAD_AXIS_RIGHTX;
static constexpr int CATA_AXIS_RIGHTY      = SDL_GAMEPAD_AXIS_RIGHTY;
static constexpr int CATA_BUTTON_A              = SDL_GAMEPAD_BUTTON_SOUTH;
static constexpr int CATA_BUTTON_B              = SDL_GAMEPAD_BUTTON_EAST;
static constexpr int CATA_BUTTON_X              = SDL_GAMEPAD_BUTTON_WEST;
static constexpr int CATA_BUTTON_Y              = SDL_GAMEPAD_BUTTON_NORTH;
static constexpr int CATA_BUTTON_LEFTSHOULDER   = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
static constexpr int CATA_BUTTON_RIGHTSHOULDER  = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
static constexpr int CATA_BUTTON_LEFTSTICK      = SDL_GAMEPAD_BUTTON_LEFT_STICK;
static constexpr int CATA_BUTTON_RIGHTSTICK     = SDL_GAMEPAD_BUTTON_RIGHT_STICK;
static constexpr int CATA_BUTTON_START          = SDL_GAMEPAD_BUTTON_START;
static constexpr int CATA_BUTTON_BACK           = SDL_GAMEPAD_BUTTON_BACK;
static constexpr int CATA_BUTTON_DPAD_UP        = SDL_GAMEPAD_BUTTON_DPAD_UP;
static constexpr int CATA_BUTTON_DPAD_DOWN      = SDL_GAMEPAD_BUTTON_DPAD_DOWN;
static constexpr int CATA_BUTTON_DPAD_LEFT      = SDL_GAMEPAD_BUTTON_DPAD_LEFT;
static constexpr int CATA_BUTTON_DPAD_RIGHT     = SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
#else
static constexpr Uint32 CATA_CONTROLLERBUTTONDOWN    = SDL_CONTROLLERBUTTONDOWN;
static constexpr Uint32 CATA_CONTROLLERBUTTONUP      = SDL_CONTROLLERBUTTONUP;
static constexpr Uint32 CATA_CONTROLLERAXISMOTION    = SDL_CONTROLLERAXISMOTION;
static constexpr Uint32 CATA_CONTROLLERDEVICEADDED   = SDL_CONTROLLERDEVICEADDED;
static constexpr Uint32 CATA_CONTROLLERDEVICEREMOVED = SDL_CONTROLLERDEVICEREMOVED;
static constexpr int CATA_AXIS_TRIGGERLEFT  = SDL_CONTROLLER_AXIS_TRIGGERLEFT;
static constexpr int CATA_AXIS_TRIGGERRIGHT = SDL_CONTROLLER_AXIS_TRIGGERRIGHT;
static constexpr int CATA_AXIS_LEFTX       = SDL_CONTROLLER_AXIS_LEFTX;
static constexpr int CATA_AXIS_LEFTY       = SDL_CONTROLLER_AXIS_LEFTY;
static constexpr int CATA_AXIS_RIGHTX      = SDL_CONTROLLER_AXIS_RIGHTX;
static constexpr int CATA_AXIS_RIGHTY      = SDL_CONTROLLER_AXIS_RIGHTY;
static constexpr int CATA_BUTTON_A              = SDL_CONTROLLER_BUTTON_A;
static constexpr int CATA_BUTTON_B              = SDL_CONTROLLER_BUTTON_B;
static constexpr int CATA_BUTTON_X              = SDL_CONTROLLER_BUTTON_X;
static constexpr int CATA_BUTTON_Y              = SDL_CONTROLLER_BUTTON_Y;
static constexpr int CATA_BUTTON_LEFTSHOULDER   = SDL_CONTROLLER_BUTTON_LEFTSHOULDER;
static constexpr int CATA_BUTTON_RIGHTSHOULDER  = SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
static constexpr int CATA_BUTTON_LEFTSTICK      = SDL_CONTROLLER_BUTTON_LEFTSTICK;
static constexpr int CATA_BUTTON_RIGHTSTICK     = SDL_CONTROLLER_BUTTON_RIGHTSTICK;
static constexpr int CATA_BUTTON_START          = SDL_CONTROLLER_BUTTON_START;
static constexpr int CATA_BUTTON_BACK           = SDL_CONTROLLER_BUTTON_BACK;
static constexpr int CATA_BUTTON_DPAD_UP        = SDL_CONTROLLER_BUTTON_DPAD_UP;
static constexpr int CATA_BUTTON_DPAD_DOWN      = SDL_CONTROLLER_BUTTON_DPAD_DOWN;
static constexpr int CATA_BUTTON_DPAD_LEFT      = SDL_CONTROLLER_BUTTON_DPAD_LEFT;
static constexpr int CATA_BUTTON_DPAD_RIGHT     = SDL_CONTROLLER_BUTTON_DPAD_RIGHT;
#endif

static constexpr int max_triggers = 2;
static constexpr int max_sticks = 2;
static constexpr int max_buttons = 30;

static std::array<int, max_triggers> triggers_axis = {
    CATA_AXIS_TRIGGERLEFT,
    CATA_AXIS_TRIGGERRIGHT
};
static std::array<std::array<int, 2>, max_sticks> sticks_axis = { {
        { {CATA_AXIS_LEFTX,  CATA_AXIS_LEFTY}  },
        { {CATA_AXIS_RIGHTX, CATA_AXIS_RIGHTY} }
    }
};

static std::array<int, max_triggers> triggers_state = {0, 0};

static int triggers_threshold = 16000;
static int sticks_threshold = 16000;
static int error_margin = 2000;

namespace
{
struct task_t {
    // Millisecond timestamp from GetTicks(), not raw event timestamps.
    // SDL3 event timestamps are nanoseconds (Uint64); using GetTicks() keeps
    // everything in a consistent millisecond timebase across both versions.
    uint32_t when;
    int button;
    int counter;
    int state;
    input_event_t type;
};
} // namespace

static constexpr int max_tasks           = max_buttons + max_sticks + max_triggers + 1;
static constexpr int sticks_task_index   = max_buttons;
static constexpr int triggers_task_index = max_buttons + max_sticks;

static std::array<task_t, max_tasks> all_tasks;

static int repeat_delay = 400;
static int repeat_interval = 50;
// RT-held continuous movement repeats faster than the generic repeat —
// walking cadence wants to be snappier than menu-scroll cadence.
static int movement_repeat_interval = 33;

// SDL related stuff
static SDL_TimerID timer_id;
#if SDL_MAJOR_VERSION >= 3
using cata_controller_t = SDL_Gamepad;
static cata_controller_t *controller = nullptr;
static SDL_JoystickID controller_id = 0; // SDL3 uses 0 as invalid
#else
using cata_controller_t = SDL_GameController;
static cata_controller_t *controller = nullptr;
static SDL_JoystickID controller_id = -1;
#endif
// Every connected controller stays open so all of them generate events
// (e.g. a physical pad alongside the iOS on-screen virtual pad).
// `controller` is only the POLLING target — see set_active_controller().
static std::map<SDL_JoystickID, cata_controller_t *> open_controllers;

// Polling (get_controller_axis) must read the device the player is
// actually using: whichever controller last sent SIGNIFICANT input is
// active. Significance matters because idle physical sticks stream tiny
// noise deltas; if noise could claim the active slot, a connected pad
// would clobber the on-screen virtual pad's stick state dozens of times
// a second.
static void set_active_controller( SDL_JoystickID which )
{
    if( which == controller_id ) {
        return;
    }
    auto it = open_controllers.find( which );
    if( it != open_controllers.end() ) {
        controller = it->second;
        controller_id = which;
    }
}

static direction left_stick_dir = direction::NONE;
static direction right_stick_dir = direction::NONE;
static direction radial_left_last_dir = direction::NONE;
static direction radial_right_last_dir = direction::NONE;
static bool radial_left_open = false;
static bool radial_right_open = false;
static bool alt_modifier_held = false;
// Layer 2: LT held, then RT pulled. RT can only mean this while LT is down —
// on its own it stays the movement commit, which is far too busy to modify.
static bool alt2_modifier_held = false;

// A radial is "open" once a stick has been deflected while LT is held. The
// highlighted slot then latches: it survives the stick returning to centre,
// so the right radial can be aimed with the right stick and confirmed with A
// (same thumb) without the selection evaporating on the way.
static bool any_radial_open()
{
    return radial_left_open || radial_right_open;
}

// A stick still deflected when a wheel closes has already been spent choosing
// the slot. Anything that reads it again inherits that direction by accident:
// a wheel slot holding a directional verb fires straight into it (smash on the
// E slot smashes east instead of asking), a jitter event reopens the wheel just
// dismissed, and sliding the stick afterwards walks you. So a spent stick
// simply reports as centred until it physically re-centres — one rule, and
// every consumer follows from it.
static std::array<bool, max_sticks> stick_consumed = {{false, false}};

static direction stick_dir( int idx )
{
    if( stick_consumed[idx] ) {
        return direction::NONE;
    }
    return idx == 0 ? left_stick_dir : right_stick_dir;
}

static void close_radials()
{
    radial_left_open = false;
    radial_right_open = false;
    radial_left_last_dir = direction::NONE;
    radial_right_last_dir = direction::NONE;
    // Only sticks actually deflected are marked, or one already at rest would
    // need a pointless waggle to wake up.
    stick_consumed[0] = left_stick_dir != direction::NONE;
    stick_consumed[1] = right_stick_dir != direction::NONE;
}

// SDL3: callback signature changes to (void *userdata, SDL_TimerID timerID, Uint32 interval)
#if SDL_MAJOR_VERSION >= 3
static Uint32 timer_func( void *, SDL_TimerID, Uint32 interval )
#else
static Uint32 timer_func( Uint32 interval, void * )
#endif
{
    SDL_Event event;
    SDL_UserEvent userevent;

    userevent.type = SDL_GAMEPAD_SCHEDULER;
    userevent.code = 0;
    userevent.data1 = nullptr;
    userevent.data2 = nullptr;

    event.type = SDL_GAMEPAD_SCHEDULER;
    event.user = userevent;

    SDL_PushEvent( &event );
    return interval;
}

void init()
{
    for( gamepad::task_t &task : all_tasks ) {
        task.counter = 0;
    }

#if SDL_MAJOR_VERSION >= 3
    // SDL3: SDL_INIT_GAMECONTROLLER removed; use SDL_INIT_GAMEPAD.
    // SDL3: SDL_INIT_TIMER removed (timers work without explicit init).
    int ret = SDL_Init( SDL_INIT_GAMEPAD );
    if( !ret ) {
        printErrorIf( true, "Init gamepad+timer failed" );
        return;
    }

    int num_joysticks = 0;
    SDL_JoystickID *joysticks = SDL_GetJoysticks( &num_joysticks );
    if( joysticks && num_joysticks > 0 ) {
        controller = SDL_OpenGamepad( joysticks[0] );
        if( controller ) {
            controller_id = SDL_GetGamepadID( controller );
        }
    }
    SDL_free( joysticks );
#else
    int ret = SDL_Init( SDL_INIT_TIMER | SDL_INIT_GAMECONTROLLER );
    if( ret < 0 ) {
        printErrorIf( ret != 0, "Init gamecontroller+timer failed" );
        return;
    }

    if( SDL_NumJoysticks() > 0 ) {
        controller = SDL_GameControllerOpen( 0 );
        if( controller ) {
            controller_id = SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( controller ) );
            SDL_GameControllerEventState( SDL_ENABLE );
        }
    }
#endif

    timer_id = SDL_AddTimer( 50, timer_func, nullptr );
    printErrorIf( timer_id == 0, "SDL_AddTimer failed" );
}

void quit()
{
    if( timer_id ) {
        SDL_RemoveTimer( timer_id );
        timer_id = 0;
    }
    if( controller ) {
#if SDL_MAJOR_VERSION >= 3
        SDL_CloseGamepad( controller );
#else
        SDL_GameControllerClose( controller );
#endif
        controller = nullptr;
    }
}

static Sint16 get_controller_axis( int axis )
{
#if SDL_MAJOR_VERSION >= 3
    return SDL_GetGamepadAxis( controller, static_cast<SDL_GamepadAxis>( axis ) );
#else
    return SDL_GameControllerGetAxis( controller, static_cast<SDL_GameControllerAxis>( axis ) );
#endif
}

static int one_of_two( const std::array<int, 2> &arr, int val )
{
    if( arr[0] == val ) {
        return 0;
    }
    if( arr[1] == val ) {
        return 1;
    }
    return -1;
}

static void schedule_task( task_t &task, uint32_t when, int button, int state,
                           input_event_t type = input_event_t::gamepad )
{
    task.when = when;
    task.button = button;
    task.counter = 1;
    task.state = state;
    task.type = type;
}

static void cancel_task( task_t &task )
{
    task.counter = 0;
}

static void send_input( int ibtn, input_event_t itype = input_event_t::gamepad )
{
    last_input = input_event( ibtn, itype );
}


// Helper to get ALT variant of a button code
// Second layer, entered by pulling RT while LT is already held. Buttons with
// no ALT2 twin fall through to their base code rather than to the ALT one:
// silently dropping to layer 1 would make a chord fire something unrelated.
static int get_alt2_button( int base_code )
{
    switch( base_code ) {
        case JOY_A:
            return JOY_ALT2_A;
        case JOY_B:
            return JOY_ALT2_B;
        case JOY_X:
            return JOY_ALT2_X;
        case JOY_Y:
            return JOY_ALT2_Y;
        case JOY_LB:
            return JOY_ALT2_LB;
        case JOY_RB:
            return JOY_ALT2_RB;
        case JOY_LS:
            return JOY_ALT2_LS;
        case JOY_RS:
            return JOY_ALT2_RS;
        case JOY_UP:
            return JOY_ALT2_UP;
        case JOY_DOWN:
            return JOY_ALT2_DOWN;
        case JOY_LEFT:
            return JOY_ALT2_LEFT;
        case JOY_RIGHT:
            return JOY_ALT2_RIGHT;
        case JOY_START:
            return JOY_ALT2_START;
        case JOY_BACK:
            return JOY_ALT2_BACK;
        default:
            return base_code;
    }
}

static int get_alt_button( int base_code )
{
    switch( base_code ) {
        case JOY_A:
            return JOY_ALT_A;
        case JOY_B:
            return JOY_ALT_B;
        case JOY_X:
            return JOY_ALT_X;
        case JOY_Y:
            return JOY_ALT_Y;
        case JOY_LB:
            return JOY_ALT_LB;
        case JOY_RB:
            return JOY_ALT_RB;
        case JOY_RT:
            return JOY_ALT_RT;
        case JOY_LS:
            return JOY_ALT_LS;
        case JOY_RS:
            return JOY_ALT_RS;
        case JOY_UP:
            return JOY_ALT_UP;
        case JOY_DOWN:
            return JOY_ALT_DOWN;
        case JOY_LEFT:
            return JOY_ALT_LEFT;
        case JOY_RIGHT:
            return JOY_ALT_RIGHT;
        case JOY_START:
            return JOY_ALT_START;
        case JOY_BACK:
            return JOY_ALT_BACK;
        default:
            return base_code;
    }
}

// Convert direction to numpad key ('1'-'9')
static int direction_to_num_key( direction dir )
{
    switch( dir ) {
        case direction::N:
            return '8';
        case direction::NE:
            return '9';
        case direction::E:
            return '6';
        case direction::SE:
            return '3';
        case direction::S:
            return '2';
        case direction::SW:
            return '1';
        case direction::W:
            return '4';
        case direction::NW:
            return '7';
        default:
            return -1;
    }
}

// Convert direction to radial joy event
int direction_to_radial_joy( direction dir, int stick_idx )
{
    if( dir == direction::NONE ) {
        return -1;
    }
    int base = ( stick_idx == 0 ) ? JOY_L_RADIAL_N : JOY_R_RADIAL_N;
    switch( dir ) {
        case direction::N:
            return base + 0;
        case direction::NE:
            return base + 1;
        case direction::E:
            return base + 2;
        case direction::SE:
            return base + 3;
        case direction::S:
            return base + 4;
        case direction::SW:
            return base + 5;
        case direction::W:
            return base + 6;
        case direction::NW:
            return base + 7;
        default:
            return -1;
    }
}

// The slot currently highlighted, or -1 if no wheel is up.
//
// Kept separate from the firing itself because the two commit paths want
// different cleanup. Confirming with A leaves LT held, so a stick still off
// centre has to be locked out or jitter reopens the wheel; releasing LT ends
// the gesture outright, and locking there would make a deliberate re-press
// feel dead.
static int highlighted_radial_slot()
{
    if( radial_left_open && radial_left_last_dir != direction::NONE ) {
        return direction_to_radial_joy( radial_left_last_dir, 0 );
    }
    if( radial_right_open && radial_right_last_dir != direction::NONE ) {
        return direction_to_radial_joy( radial_right_last_dir, 1 );
    }
    return -1;
}

// Calculate 8-way direction from raw axis values using angle
static direction angle_to_direction( const point &p )
{
    // atan2 returns angle in radians, with 0 pointing right (East)
    // We want 0 to be North, so we use atan2(x, -y)
    double angle = std::atan2( static_cast<double>( p.x ), static_cast<double>( -p.y ) );

    // Convert to degrees (0-360, with 0 = North, clockwise)
    double degrees = angle * 180.0 / M_PI;
    if( degrees < 0 ) {
        degrees += 360.0;
    }

    // Each direction covers 45 degrees, centered on its cardinal/diagonal
    // N: 337.5 - 22.5, NE: 22.5 - 67.5, E: 67.5 - 112.5, etc.
    if( degrees >= 337.5 || degrees < 22.5 ) {
        return direction::N;
    } else if( degrees >= 22.5 && degrees < 67.5 ) {
        return direction::NE;
    } else if( degrees >= 67.5 && degrees < 112.5 ) {
        return direction::E;
    } else if( degrees >= 112.5 && degrees < 157.5 ) {
        return direction::SE;
    } else if( degrees >= 157.5 && degrees < 202.5 ) {
        return direction::S;
    } else if( degrees >= 202.5 && degrees < 247.5 ) {
        return direction::SW;
    } else if( degrees >= 247.5 && degrees < 292.5 ) {
        return direction::W;
    } else {
        return direction::NW;
    }
}

// Helper to send directional movement input
static void send_direction_movement()
{
    const direction dir = stick_dir( 0 );
    if( dir == direction::NONE ) {
        return;
    }

    // Convert direction to old-style JOY movement for keybinding lookup
    switch( dir ) {
        case direction::N:
            send_input( JOY_LS_UP );
            break;
        case direction::NE:
            send_input( JOY_LS_UP_RIGHT );
            break;
        case direction::E:
            send_input( JOY_LS_RIGHT );
            break;
        case direction::SE:
            send_input( JOY_LS_DOWN_RIGHT );
            break;
        case direction::S:
            send_input( JOY_LS_DOWN );
            break;
        case direction::SW:
            send_input( JOY_LS_DOWN_LEFT );
            break;
        case direction::W:
            send_input( JOY_LS_LEFT );
            break;
        case direction::NW:
            send_input( JOY_LS_UP_LEFT );
            break;
        default:
            break;
    }
}

static bool handle_axis_event( SDL_Event &event )
{
    if( event.type != CATA_CONTROLLERAXISMOTION ) {
        return false;
    }

    // SDL3: event.caxis becomes event.gaxis
#if SDL_MAJOR_VERSION >= 3
    int axis = event.gaxis.axis;
    int value = event.gaxis.value;
    const SDL_JoystickID event_source = event.gaxis.which;
#else
    int axis = event.caxis.axis;
    int value = event.caxis.value;
    const SDL_JoystickID event_source = event.caxis.which;
#endif
    // Only deliberate input claims the active-device slot (see
    // set_active_controller): a stick past the movement threshold or a
    // trigger past its press threshold. Sub-threshold events still run
    // below, interpreted against the active device's polled state.
    const bool is_trigger_axis = one_of_two( triggers_axis, axis ) >= 0;
    if( ( is_trigger_axis && value > triggers_threshold ) ||
        ( !is_trigger_axis && std::abs( value ) > sticks_threshold ) ) {
        set_active_controller( event_source );
    }
    // Use GetTicks() instead of event timestamps for consistent millisecond
    // timebase. SDL3 event timestamps are nanoseconds, not milliseconds.
    uint32_t now = GetTicks();
    bool direction_changed = false;

    // check triggers
    int idx = one_of_two( triggers_axis, axis );
    if( idx >= 0 ) {
        int state = triggers_state[idx];
        task_t &task = all_tasks[triggers_task_index + idx];
        bool trigger_pressed = value > triggers_threshold + error_margin;
        bool trigger_released = value < triggers_threshold - error_margin;

        if( idx == 1 ) {
            // Right trigger (RT)
            if( !state && trigger_pressed ) {
                triggers_state[idx] = 1;
                if( alt_modifier_held ) {
                    // LT is already down, so RT is the layer-2 gate rather than
                    // the movement commit. Deliberately swallowed: walking with
                    // the alt layer open served no purpose, and this is what
                    // buys the second layer without spending a new button.
                    alt2_modifier_held = true;
                    // Drop any open wheel on the way in. A and B are intercepted
                    // for the radial before the layer transform runs, so leaving
                    // one up would make layer-2 chords fire wheel slots instead.
                    close_radials();
                } else if( stick_dir( 0 ) != direction::NONE ) {
                    // Move in selected direction
                    send_direction_movement();
                    // Schedule repeat for continuous movement
                    schedule_task( task, now + repeat_delay, -1, 1 );
                } else {
                    // No direction selected - send JOY_RT event and schedule repeat
                    send_input( JOY_RT );
                    schedule_task( task, now + repeat_delay, JOY_RT, 1 );
                }
            }
            if( state && trigger_released ) {
                triggers_state[idx] = 0;
                alt2_modifier_held = false;
                cancel_task( task );
            }
        } else {
            // Left trigger (LT)
            if( !state && trigger_pressed ) {
                alt_modifier_held = true;
                triggers_state[idx] = 1;
            }
            if( state && trigger_released ) {
                triggers_state[idx] = 0;
                alt_modifier_held = false;
                // Layer 2 is gated on LT as well, so it cannot outlive it —
                // otherwise letting go of LT first would strand the pad in a
                // layer with no visible way back out.
                alt2_modifier_held = false;

                // Releasing LT also fires the latched slot, keeping the original
                // one-motion flick-and-release as the fast path. A does the same
                // thing deliberately when you'd rather not commit on a release
                // that might jostle the stick; B is the way out of a wheel.
                const int joy_code = highlighted_radial_slot();
                close_radials();
                if( joy_code != -1 ) {
                    send_input( joy_code );
                }

                return true;
            }
        }
        return false;
    }

    // check sticks
    for( int i = 0; i < max_sticks; ++i ) {
        int axis_idx = one_of_two( sticks_axis[i], axis );
        if( axis_idx >= 0 ) {
            // Get current values for both axes of this stick
            const point val(
                get_controller_axis( sticks_axis[i][0] ),
                get_controller_axis( sticks_axis[i][1] ) );

            // Calculate magnitude (distance from center)
            double magnitude = std::sqrt( static_cast<double>( val.x ) * val.x +
                                          static_cast<double>( val.y ) * val.y );

            direction dir = ( magnitude > sticks_threshold ) ? angle_to_direction( val ) : direction::NONE;

            task_t &stick_task = all_tasks[sticks_task_index + i];
            direction old_dir = ( i == 0 ) ? left_stick_dir : right_stick_dir;

            if( dir != old_dir ) {
                direction_changed = true;
                if( i == 0 && g ) {
                    g->invalidate_main_ui_adaptor();
                }
            }

            if( i == 0 ) {
                left_stick_dir = dir;
            } else {
                right_stick_dir = dir;
            }

            // Physically back at centre, so the stick is live again.
            if( dir == direction::NONE ) {
                stick_consumed[i] = false;
            }

            if( alt_modifier_held && !alt2_modifier_held && !stick_consumed[i] ) {
                // When LT is held, sticks control radial menu state
                direction &radial_last = ( i == 0 ) ? radial_left_last_dir : radial_right_last_dir;
                bool &radial_open = ( i == 0 ) ? radial_left_open : radial_right_open;
                bool &radial_other = ( i == 0 ) ? radial_right_open : radial_left_open;

                // Only update direction when stick is deflected beyond threshold
                if( dir != direction::NONE ) {
                    if( !radial_open || dir != radial_last || radial_other ) {
                        direction_changed = true;
                    }
                    radial_open = true;
                    radial_other = false; // Mutually exclusive
                    if( dir != radial_last ) {
                        radial_last = dir;
                    }
                } else {
                    // When stick is released just update the UI
                    if( radial_open ) {
                        direction_changed = true;
                    }
                }

                // Cancel any existing repeat tasks when entering radial mode
                cancel_task( stick_task );
            } else if( stick_consumed[i] ) {
                // Spent on a wheel slot and not yet re-centred. Emitting octants
                // here would walk the player (or reopen look on the right stick)
                // just for sliding a thumb that never left the stick.
                cancel_task( stick_task );
            } else {
                int joy_code = -1;
                input_event_t itype = input_event_t::gamepad;

                if( i == 1 ) {
                    // Right stick (i=1): Map to num keys in menu if the radial menu is not open, or JOY_R_* in gameplay
                    if( is_in_menu() ) {
                        if( !radial_right_open ) {
                            joy_code = direction_to_num_key( dir );
                            itype = input_event_t::keyboard_char;
                        }
                    } else {
                        switch( dir ) {
                            case direction::N:
                                joy_code = JOY_RS_UP;
                                break;
                            case direction::NE:
                                joy_code = JOY_RS_UP_RIGHT;
                                break;
                            case direction::E:
                                joy_code = JOY_RS_RIGHT;
                                break;
                            case direction::SE:
                                joy_code = JOY_RS_DOWN_RIGHT;
                                break;
                            case direction::S:
                                joy_code = JOY_RS_DOWN;
                                break;
                            case direction::SW:
                                joy_code = JOY_RS_DOWN_LEFT;
                                break;
                            case direction::W:
                                joy_code = JOY_RS_LEFT;
                                break;
                            case direction::NW:
                                joy_code = JOY_RS_UP_LEFT;
                                break;
                            default:
                                break;
                        }
                    }
                }

                if( dir == direction::NONE || joy_code == -1 ) {
                    cancel_task( stick_task );
                } else if( dir != old_dir ) {
                    // Only send input and schedule repeat if direction changed.
                    send_input( joy_code, itype );
                    schedule_task( stick_task, now + repeat_delay, joy_code, 1, itype );
                }
            }
        }
    }
    return direction_changed;
}


// Returns true if the UI needs repainting (the radial overlay is drawn in
// the present path, so cancelling one has to force a frame or it lingers).
static bool handle_button_event( SDL_Event &event )
{
    // SDL3: event.cbutton becomes event.gbutton
#if SDL_MAJOR_VERSION >= 3
    int button = event.gbutton.button;
    set_active_controller( event.gbutton.which );
#else
    int button = event.cbutton.button;
    set_active_controller( event.cbutton.which );
#endif
    uint32_t now = GetTicks();

    if( event.type == CATA_CONTROLLERBUTTONUP ) {
        // Button released - only handle cleanup/task cancellation
        if( button < max_buttons ) {
            cancel_task( all_tasks[button] );
        }
        return false;
    }

    // While a radial is open, A confirms the highlighted slot and B backs out.
    // Both are swallowed so they don't also reach the screen underneath. This
    // is gated on a radial being open rather than on LT being held, because LT
    // gates the whole ALT layer — stairs, zoom, craft — and those chords must
    // keep working whenever no wheel is up.
    if( any_radial_open() ) {
        if( button == CATA_BUTTON_A ) {
            const int joy_code = highlighted_radial_slot();
            close_radials();
            if( joy_code != -1 ) {
                send_input( joy_code );
            }
            return true;
        }
        if( button == CATA_BUTTON_B ) {
            close_radials();
            return true;
        }
    }

    // Button pressed - determine which input to send
    int joy_code = -1;
    input_event_t input_type = input_event_t::gamepad;

    switch( button ) {
        case CATA_BUTTON_A:
            if( is_in_menu() ) {
                joy_code = '\n';
                input_type = input_event_t::keyboard_char;
            } else {
                joy_code = JOY_A;
            }
            break;
        case CATA_BUTTON_B:
            if( is_in_menu() ) {
                joy_code = KEY_ESCAPE;
                input_type = input_event_t::keyboard_code;
            } else {
                joy_code = JOY_B;
            }
            break;
        case CATA_BUTTON_X:
            joy_code = JOY_X;
            break;
        case CATA_BUTTON_Y:
            joy_code = JOY_Y;
            break;
        // Shoulders stay first-class gamepad events in menus, like X and Y, so
        // each screen can bind them itself. Tab cycling still works everywhere:
        // NEXT_TAB/PREV_TAB carry gamepad RB/LB in the default context.
        case CATA_BUTTON_RIGHTSHOULDER:
            joy_code = JOY_RB;
            break;
        case CATA_BUTTON_LEFTSHOULDER:
            joy_code = JOY_LB;
            break;
        case CATA_BUTTON_LEFTSTICK:
            joy_code = JOY_LS;
            break;
        case CATA_BUTTON_RIGHTSTICK:
            joy_code = JOY_RS;
            break;
        case CATA_BUTTON_START:
            if( is_in_menu() ) {
                joy_code = KEY_ESCAPE;
                input_type = input_event_t::keyboard_code;
            } else {
                joy_code = JOY_START;
            }
            break;
        case CATA_BUTTON_BACK:
            if( is_in_menu() ) {
                joy_code = KEY_ESCAPE;
                input_type = input_event_t::keyboard_code;
            } else {
                joy_code = JOY_BACK;
            }
            break;

        // D-pad handling
        // If in a menu, send keyboard arrow keys for navigation
        // If in gameplay, send JOY_* codes for mapped actions
        case CATA_BUTTON_DPAD_UP:
            if( is_in_menu() && !alt_modifier_held ) {
                joy_code = KEY_UP;
                input_type = input_event_t::keyboard_char;
            } else {
                joy_code = JOY_UP;
            }
            break;
        case CATA_BUTTON_DPAD_DOWN:
            if( is_in_menu() && !alt_modifier_held ) {
                joy_code = KEY_DOWN;
                input_type = input_event_t::keyboard_char;
            } else {
                joy_code = JOY_DOWN;
            }
            break;
        case CATA_BUTTON_DPAD_LEFT:
            if( is_in_menu() && !alt_modifier_held ) {
                joy_code = KEY_LEFT;
                input_type = input_event_t::keyboard_char;
            } else {
                joy_code = JOY_LEFT;
            }
            break;
        case CATA_BUTTON_DPAD_RIGHT:
            if( is_in_menu() && !alt_modifier_held ) {
                joy_code = KEY_RIGHT;
                input_type = input_event_t::keyboard_char;
            } else {
                joy_code = JOY_RIGHT;
            }
            break;

        default:
            break;
    }

    // Apply ALT modifier if held (only for non-keyboard inputs). Layer 2 wins
    // when both are down, since it can only be entered through layer 1.
    if( joy_code != -1 && input_type == input_event_t::gamepad ) {
        if( alt2_modifier_held ) {
            joy_code = get_alt2_button( joy_code );
        } else if( alt_modifier_held ) {
            joy_code = get_alt_button( joy_code );
        }
    }

    if( joy_code != -1 ) {
        send_input( joy_code, input_type );
        // Only D-pad buttons repeat
        if( button >= CATA_BUTTON_DPAD_UP && button <= CATA_BUTTON_DPAD_RIGHT ) {
            schedule_task( all_tasks[button], now + repeat_delay, joy_code, 1, input_type );
        }
    }
    return false;
}

static void handle_device_event( SDL_Event &event )
{
    if( event.type == CATA_CONTROLLERDEVICEADDED ) {
#if SDL_MAJOR_VERSION >= 3
        // SDL3: event provides instance ID directly; SDL_OpenGamepad takes it.
        cata_controller_t *opened = SDL_OpenGamepad( event.gdevice.which );
        if( opened ) {
            SDL_JoystickID id = SDL_GetGamepadID( opened );
#else
        cata_controller_t *opened = SDL_GameControllerOpen( event.cdevice.which );
        if( opened ) {
            SDL_JoystickID id = SDL_JoystickInstanceID( SDL_GameControllerGetJoystick( opened ) );
#endif
            open_controllers[id] = opened;
            if( controller == nullptr ) {
                controller = opened;
                controller_id = id;
            }
        }
    } else if( event.type == CATA_CONTROLLERDEVICEREMOVED ) {
#if SDL_MAJOR_VERSION >= 3
        const SDL_JoystickID gone = event.gdevice.which;
#else
        const SDL_JoystickID gone = event.cdevice.which;
#endif
        auto it = open_controllers.find( gone );
        if( it != open_controllers.end() ) {
#if SDL_MAJOR_VERSION >= 3
            SDL_CloseGamepad( it->second );
#else
            SDL_GameControllerClose( it->second );
#endif
            open_controllers.erase( it );
        }
        if( controller_id == gone ) {
            if( open_controllers.empty() ) {
                controller = nullptr;
#if SDL_MAJOR_VERSION >= 3
                controller_id = 0;
#else
                controller_id = -1;
#endif
            } else {
                controller = open_controllers.begin()->second;
                controller_id = open_controllers.begin()->first;
            }
        }
    }
}

static void handle_scheduler_event( SDL_Event &/*event*/ )
{
    uint32_t now = GetTicks();
    for( size_t i = 0; i < all_tasks.size(); ++i ) {
        gamepad::task_t &task = all_tasks[i];
        if( task.counter && task.when <= now ) {
            // Trigger repeats must check the trigger's CURRENT state, not the
            // state when the repeat was scheduled: long turns queue scheduler
            // events behind them, so a queued repeat can fire after the
            // player released the trigger but before the release event has
            // been processed — one tap then moves 2-3 times. Polling reads
            // the value as of the last joystick update, which is already
            // current when the stale scheduler event is handled.
            if( i == triggers_task_index || i == triggers_task_index + 1 ) {
                int trigger_idx = static_cast<int>( i - triggers_task_index );
                if( get_controller_axis( triggers_axis[trigger_idx] ) < triggers_threshold ) {
                    cancel_task( task );
                    continue;
                }
            }
            // Check if this is the RT repeat task
            int interval = repeat_interval;
            if( i == triggers_task_index + 1 && stick_dir( 0 ) != direction::NONE ) {
                // RT continuous movement - send direction
                send_direction_movement();
                interval = movement_repeat_interval;
            } else if( i >= sticks_task_index && i < triggers_task_index ) {
                // Stick repeat (sticks_task_index + i where i is stick index)
                if( task.button != -1 ) {
                    send_input( task.button, task.type );
                }
            } else if( task.button != -1 ) {
                // Regular button repeat (including RT when no direction is held)
                send_input( task.button, task.type );
            }
            task.counter += 1;
            task.when = now + interval;
        }
    }
}

direction get_left_stick_direction()
{
    return stick_dir( 0 );
}

direction get_right_stick_direction()
{
    return stick_dir( 1 );
}

direction get_radial_left_direction()
{
    return radial_left_last_dir;
}

direction get_radial_right_direction()
{
    return radial_right_last_dir;
}

bool is_radial_left_open()
{
    return radial_left_open;
}

bool is_radial_right_open()
{
    return radial_right_open;
}

bool is_alt_held()
{
    return alt_modifier_held;
}

bool is_in_menu()
{
    // If the UI stack has more than 1 element, it usually means a menu is open
    // on top of the main game UI.
    return ui_adaptor::ui_stack_size() > 1;
}

bool is_active()
{
    return controller != nullptr;
}

tripoint direction_to_offset( direction dir )
{
    switch( dir ) {
        case direction::N:
            return tripoint::north;
        case direction::NE:
            return tripoint::north_east;
        case direction::E:
            return tripoint::east;
        case direction::SE:
            return tripoint::south_east;
        case direction::S:
            return tripoint::south;
        case direction::SW:
            return tripoint::south_west;
        case direction::W:
            return tripoint::west;
        case direction::NW:
            return tripoint::north_west;
        case direction::NONE:
        default:
            return tripoint::zero;
    }
}

// SDL3: event constants and struct members are remapped via the CATA_CONTROLLER*
// constants and #if blocks defined at the top of this file.
// SDL3: event.caxis/cbutton/cdevice become event.gaxis/gbutton/gdevice.
bool is_gamepad_event( const SDL_Event &event )
{
    switch( event.type ) {
        case CATA_CONTROLLERBUTTONDOWN:
        case CATA_CONTROLLERBUTTONUP:
        case CATA_CONTROLLERAXISMOTION:
        case CATA_CONTROLLERDEVICEADDED:
        case CATA_CONTROLLERDEVICEREMOVED:
        case SDL_GAMEPAD_SCHEDULER:
            return true;
        default:
            return false;
    }
}

bool handle_event( SDL_Event &event )
{
    switch( event.type ) {
        case CATA_CONTROLLERBUTTONDOWN:
        case CATA_CONTROLLERBUTTONUP:
            return handle_button_event( event );
        case CATA_CONTROLLERAXISMOTION:
            return handle_axis_event( event );
        case CATA_CONTROLLERDEVICEADDED:
        case CATA_CONTROLLERDEVICEREMOVED:
            handle_device_event( event );
            return false;
        case SDL_GAMEPAD_SCHEDULER:
            handle_scheduler_event( event );
            return false;
        default:
            return false;
    }
}

} // namespace gamepad

#endif // TILES
