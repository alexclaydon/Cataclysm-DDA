#if defined(CDDA_GODOT)

#if !defined(TILES)
#error "CDDA_GODOT needs the tiles build (SDL3, the only SDL upstream builds)"
#endif

#include "godot_host.h"

#include <string_view>

#include "calendar.h"
#include "color.h"
#include "output.h"
#include "sdl_utils.h"
#include "string_formatter.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

#include <SDL3/SDL.h>

#include "ui_manager.h"
#include "uistate.h"

namespace
{

std::mutex frame_mutex;
std::vector<unsigned char> frame_rgba;
int frame_width = 0;
int frame_height = 0;
uint64_t frame_sequence = 0;
// The last frame published, engine thread only: the engine redraws several
// times a second while idle (animations, the sidebar) and mostly presents
// identical pixels, which are not worth the host's upload.
std::vector<unsigned char> last_rgba;
int last_width = 0;
int last_height = 0;
// The last read-back as read (the renderer's format): compared first, so an
// unchanged frame skips the conversion (2026-11-10: the read-back on every
// present was a steady few percent of a core on the Deck with nothing
// happening).
std::vector<unsigned char> last_raw;
SDL_PixelFormat last_raw_format = SDL_PIXELFORMAT_UNKNOWN;
godot_host::frame_sink sink = nullptr;
void *sink_context = nullptr;

// The virtual pad: the host writes, SDL's Update callback reads on the
// engine thread.
std::atomic<bool> virtual_pad_wanted{ false };
SDL_JoystickID virtual_pad_id = 0;
std::atomic<uint32_t> virtual_buttons{ 0 };
std::array<std::atomic<int16_t>, godot_host::virtual_pad_axes> virtual_axes{};
std::atomic<uint32_t> virtual_rumble{ 0 };  // low << 16 | high
std::atomic<bool> virtual_rumble_new{ false };
std::atomic<bool> text_is_wanted{ false };
std::atomic<bool> menu_is_open{ false };
std::atomic<bool> touch_pad_on{ false };
std::atomic<int> safe_mode_now{ 0 };
std::atomic<bool> activity_stoppable_now{ false };
std::atomic<bool> filter_requested{ false };
std::atomic<bool> list_filters_now{ true };
std::mutex text_entry_mutex;
std::string text_entry_now;
std::mutex filter_mutex;
std::string filter_action_now;
// minute << 16 | format << 12 | weekday << 8 | day; -1 with no watch.
std::atomic<int> watch_now{ -1 };
std::atomic<int> radial_sides_now{ 3 };
std::atomic<bool> route_preview_now{ false };
std::atomic<int> look_exit_requested{ 0 };  // request_look_exit's, 0 for none
std::atomic<bool> game_is_on{ false };
// The snapshot radius levels the zoom steps through, and the current one.
constexpr std::array<int, 5> radius_levels = { 15, 20, 30, 45, 60 };
std::atomic<int> radius_level{ 2 };
std::atomic<int> zoom_requested{ 0 };  // request_zoom_view's steps, not yet taken
std::atomic<float> view_yaw_degrees{ 0.0f };

// The open UIs as of the latest frame (publish_frame), and the host's mouse.
std::mutex ui_windows_mutex;
std::vector<godot_host::ui_window> ui_windows;
uint64_t ui_windows_sequence = 0;
SDL_Rect radial_window{ 0, 0, 0, 0 };  // engine thread only
// The host's mouse commands, in order, applied one per CheckMessages: the
// game keeps only the last input event of each poll, and a press and its
// release must each have their own (a held state lost quick clicks while
// the engine was busy).
struct mouse_command {
    enum kind_t { pixels, tile, away, nudge } kind;
    int x;
    int y;
    uint32_t buttons;
    int wheel;
};
std::atomic<uint64_t> sidebar_cells{ 0 };  // set_sidebar, packed in 16 bits each
std::mutex look_mutex;
bool look_requested = false;  // request_look's, with its tile
std::string goto_type;  // request_goto's, empty for none (under look_mutex)
int time_hour = -1;     // request_time's, -1 for none (under look_mutex)
int look_dx = 0;
int look_dy = 0;
bool click_requested = false;  // request_click's, with its tile
int click_dx = 0;
int click_dy = 0;
// The wheels (ADR 0009): the events for the host, and its answers.
std::atomic<bool> wheels_wanted{ false };
std::mutex wheel_mutex;
std::deque<godot_host::wheel_event> wheel_events;
std::atomic<int> wheel_done_serial{ 0 };
// Under look_mutex: the action the host chose, from which wheel, and the
// aim each wheel opened with.
std::string action_requested;
int action_serial = 0;
int aim_serial = 0;
int aim_x = 0;
int aim_y = 0;
// The hotbar's catalogue (ADR 0013): asked for, and the play loop's answer.
bool catalogue_requested = false;  // under look_mutex
std::mutex catalogue_mutex;
std::vector<godot_host::wheel_action> catalogue;
bool catalogue_new = false;  // under catalogue_mutex
// The touch pad's context row: the actions that apply here, and whether
// they changed since the host last took them.
std::mutex context_mutex;
std::vector<std::string> context_actions;
bool context_new = false;  // under context_mutex
godot_host::bindings_map context_bindings;  // under context_mutex
bool bindings_new = false;  // under context_mutex
std::mutex mouse_mutex;
std::deque<mouse_command> mouse_queue;
uint32_t mouse_buttons_applied = 0;  // engine thread only
bool mouse_is_away = false;  // engine thread only
// A button change waits for a frame before the next command (or
// BUTTON_FRAME_WAIT ms, if nothing redraws): ImGui reads buttons once per
// frame, so a press and a release applied between two frames never
// clicked its buttons (a Touch trigger's quick click on a yes/no prompt).
// Engine thread only.
constexpr uint64_t BUTTON_FRAME_WAIT = 150;
uint64_t frames_published = 0;
uint64_t button_changed_frame = 0;
uint64_t button_changed_at = 0;
bool button_changed = false;

void push_mouse( const mouse_command &command )
{
    std::lock_guard<std::mutex> lock( mouse_mutex );
    if( !mouse_queue.empty() ) {
        // A move after a move with the same buttons replaces it.
        mouse_command &last = mouse_queue.back();
        if( last.kind == command.kind && command.kind != mouse_command::away &&
            last.buttons == command.buttons ) {
            last.x = command.x;
            last.y = command.y;
            last.wheel += command.wheel;
            return;
        }
    }
    if( mouse_queue.size() >= 64 ) {
        mouse_queue.pop_front();
    }
    mouse_queue.push_back( command );
}

void SDLCALL update_virtual_pad( void * )
{
    SDL_Joystick *joystick = SDL_GetJoystickFromID( virtual_pad_id );
    if( joystick == nullptr ) {
        return;
    }
    const uint32_t buttons = virtual_buttons.load();
    for( int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; ++b ) {
        SDL_SetJoystickVirtualButton( joystick, b, ( buttons >> b ) & 1 );
    }
    for( int a = 0; a < godot_host::virtual_pad_axes; ++a ) {
        int value = virtual_axes[a].load();
        if( a == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || a == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER ) {
            // A virtual gamepad's triggers span the joystick's whole range,
            // released at the bottom: 0 would read as half pulled, and once
            // pulled the trigger would never count as released again.
            value = value * 2 - 32768;
        }
        SDL_SetJoystickVirtualAxis( joystick, a, static_cast<Sint16>( value ) );
    }
    // A held stick turns with the view: SDL only reports a stick when it
    // moves, so when the view yaw changes while one is pushed, report it
    // again and the gamepad layer re-reads it with the new turn.
    static float reported_yaw = 0.0f;
    const float yaw = view_yaw_degrees.load();
    if( std::abs( std::remainder( yaw - reported_yaw, 360.0f ) ) < 1.0f ) {
        return;
    }
    reported_yaw = yaw;
    constexpr int pushed = 8000;
    for( const int a : { SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_RIGHTX } ) {
        const int x = virtual_axes[a].load();
        const int y = virtual_axes[a + 1].load();
        if( std::abs( x ) < pushed && std::abs( y ) < pushed ) {
            continue;
        }
        SDL_Event event{};
        event.type = SDL_EVENT_GAMEPAD_AXIS_MOTION;
        event.gaxis.which = virtual_pad_id;
        event.gaxis.axis = static_cast<Uint8>( a );
        event.gaxis.value = static_cast<Sint16>( x );
        SDL_PushEvent( &event );
    }
}

bool SDLCALL rumble_virtual_pad( void *, Uint16 low, Uint16 high )
{
    virtual_rumble.store( static_cast<uint32_t>( low ) << 16 | high );
    virtual_rumble_new.store( true );
    return true;
}

std::atomic<uint32_t> engine_window_id{ 0 };

// Recording happens on the engine thread only; the mutex guards the
// published snapshot the host takes.
bool map_recording = false;
bool recording_without_drawing = false;
int lookup_depth = 0;
godot_host::map_snapshot map_building;

std::mutex map_mutex;
godot_host::map_snapshot map_published;
uint64_t map_sequence = 0;

const void *tileset_key = nullptr;
uint64_t map_objects_key = 0;
std::mutex map_objects_mutex;
godot_host::map_objects_info map_objects_published;
std::mutex tileset_mutex;
godot_host::tileset_info tileset_published;

} // namespace

void godot_host::publish_frame( SDL_Renderer *renderer )
{
    const timed_scope frame_timed( timer::frame );
    ++frames_published;
    {
        // The open UIs, for the host's floating windows: kept with the
        // frame they belong to.
        std::vector<ui_window> windows;
        for( const ui_adaptor::ui_rect &r : ui_adaptor::ui_rects() ) {
            windows.push_back( { r.dimensions.p_min.x, r.dimensions.p_min.y,
                                 r.dimensions.p_max.x - r.dimensions.p_min.x,
                                 r.dimensions.p_max.y - r.dimensions.p_min.y,
                                 r.is_shim, r.is_imgui, r.disabling_uis_below, r.visible
                               } );
        }
        if( radial_window.w > 0 ) {
            windows.push_back( { radial_window.x, radial_window.y, radial_window.w, radial_window.h,
                                 false, false, false, true
                               } );
        }
        std::lock_guard<std::mutex> lock( ui_windows_mutex );
        const auto same = []( const ui_window & a, const ui_window & b ) {
            return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height &&
                   a.shim == b.shim && a.imgui == b.imgui && a.hides_below == b.hides_below &&
                   a.visible == b.visible;
        };
        if( !std::equal( windows.begin(), windows.end(), ui_windows.begin(), ui_windows.end(), same ) ) {
            ui_windows = std::move( windows );
            ++ui_windows_sequence;
        }
    }
    // Read back the composed window target just before it is presented.
    // Presents are event-driven, so this runs a handful of times a second
    // at most outside animations.
    SDL_Surface *shot = SDL_RenderReadPixels( renderer, nullptr );
    if( shot == nullptr ) {
        return;
    }
    {
        const size_t raw_row = static_cast<size_t>( shot->w ) * SDL_BYTESPERPIXEL( shot->format );
        const unsigned char *raw = static_cast<const unsigned char *>( shot->pixels );
        bool unchanged = shot->format == last_raw_format && shot->w == last_width &&
                         shot->h == last_height && last_raw.size() == raw_row * shot->h;
        for( int y = 0; y < shot->h && unchanged; ++y ) {
            unchanged = std::memcmp( last_raw.data() + raw_row * y,
                                     raw + static_cast<size_t>( shot->pitch ) * y, raw_row ) == 0;
        }
        if( unchanged ) {
            SDL_DestroySurface( shot );
            return;
        }
        last_raw.resize( raw_row * shot->h );
        for( int y = 0; y < shot->h; ++y ) {
            std::memcpy( last_raw.data() + raw_row * y, raw + static_cast<size_t>( shot->pitch ) * y,
                         raw_row );
        }
        last_raw_format = shot->format;
    }
    SDL_Surface *rgba = SDL_ConvertSurface( shot, SDL_PIXELFORMAT_RGBA32 );
    SDL_DestroySurface( shot );
    if( rgba == nullptr ) {
        return;
    }
    const size_t row = static_cast<size_t>( rgba->w ) * 4;
    const unsigned char *src = static_cast<const unsigned char *>( rgba->pixels );
    bool same = rgba->w == last_width && rgba->h == last_height;
    if( same ) {
        for( int y = 0; y < rgba->h && same; ++y ) {
            same = std::memcmp( last_rgba.data() + row * y, src + static_cast<size_t>( rgba->pitch ) * y,
                                row ) == 0;
        }
    }
    if( !same ) {
        last_rgba.resize( row * rgba->h );
        for( int y = 0; y < rgba->h; ++y ) {
            std::memcpy( last_rgba.data() + row * y, src + static_cast<size_t>( rgba->pitch ) * y, row );
        }
        last_width = rgba->w;
        last_height = rgba->h;
        if( sink != nullptr ) {
            sink( sink_context, last_rgba.data(), last_width, last_height );
            SDL_DestroySurface( rgba );
            return;
        }
        std::lock_guard<std::mutex> lock( frame_mutex );
        frame_rgba = last_rgba;
        frame_width = rgba->w;
        frame_height = rgba->h;
        ++frame_sequence;
    }
    SDL_DestroySurface( rgba );
}

bool godot_host::take_ui_windows( uint64_t &sequence, std::vector<ui_window> &out )
{
    std::lock_guard<std::mutex> lock( ui_windows_mutex );
    if( sequence == ui_windows_sequence ) {
        return false;
    }
    out = ui_windows;
    sequence = ui_windows_sequence;
    return true;
}

void godot_host::set_radial_window( int x, int y, int width, int height )
{
    radial_window = { x, y, width, height };
}

void godot_host::set_sidebar( int left, int right, int columns, int rows )
{
    sidebar_cells.store( ( static_cast<uint64_t>( left & 0xffff ) << 48 ) |
                         ( static_cast<uint64_t>( right & 0xffff ) << 32 ) |
                         ( static_cast<uint64_t>( columns & 0xffff ) << 16 ) |
                         static_cast<uint64_t>( rows & 0xffff ) );
}

void godot_host::get_sidebar( int &left, int &right, int &columns, int &rows )
{
    const uint64_t v = sidebar_cells.load();
    left = static_cast<int>( ( v >> 48 ) & 0xffff );
    right = static_cast<int>( ( v >> 32 ) & 0xffff );
    columns = static_cast<int>( ( v >> 16 ) & 0xffff );
    rows = static_cast<int>( v & 0xffff );
}

void godot_host::set_mouse( int x, int y, uint32_t buttons, int wheel )
{
    push_mouse( { mouse_command::pixels, x, y, buttons, wheel } );
}

void godot_host::set_mouse_tile( int dx, int dy, uint32_t buttons )
{
    push_mouse( { mouse_command::tile, dx, dy, buttons, 0 } );
}

void godot_host::set_mouse_away()
{
    push_mouse( { mouse_command::away, 0, 0, 0, 0 } );
}

namespace
{
std::atomic<uint32_t> terminal_request{ 0 };  // columns << 16 | rows, 0 for none
} // namespace

void godot_host::request_terminal( int columns, int rows )
{
    terminal_request.store( static_cast<uint32_t>( columns ) << 16 | static_cast<uint32_t>( rows & 0xffff ) );
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

namespace
{
std::atomic<int> sidebar_band_rows{ 0 };
} // namespace

void godot_host::set_sidebar_band( int rows )
{
    sidebar_band_rows.store( rows );
}

int godot_host::sidebar_band()
{
    return sidebar_band_rows.load();
}

void godot_host::wake()
{
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::take_terminal_request( int &columns, int &rows )
{
    const uint32_t r = terminal_request.exchange( 0 );
    if( r == 0 ) {
        return false;
    }
    columns = static_cast<int>( r >> 16 );
    rows = static_cast<int>( r & 0xffff );
    return true;
}

void godot_host::request_look( int dx, int dy )
{
    {
        std::lock_guard<std::mutex> lock( look_mutex );
        look_requested = true;
        look_dx = dx;
        look_dy = dy;
    }
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::look_pending()
{
    std::lock_guard<std::mutex> lock( look_mutex );
    return look_requested || click_requested || !goto_type.empty() || time_hour >= 0
           || !action_requested.empty() || catalogue_requested || zoom_requested.load() != 0;
}

void godot_host::enable_wheels()
{
    wheels_wanted = true;
}

bool godot_host::wheels_enabled()
{
    return wheels_wanted;
}

void godot_host::push_wheel_event( wheel_event event )
{
    std::lock_guard<std::mutex> lock( wheel_mutex );
    if( event.kind == wheel_event::stick && !wheel_events.empty() &&
        wheel_events.back().kind == wheel_event::stick ) {
        // Only where the stick is now matters.
        wheel_events.back() = std::move( event );
        return;
    }
    if( wheel_events.size() >= 256 ) {
        wheel_events.pop_front();
    }
    wheel_events.push_back( std::move( event ) );
}

void godot_host::take_wheel_events( std::vector<wheel_event> &out )
{
    out.clear();
    std::lock_guard<std::mutex> lock( wheel_mutex );
    out.assign( std::make_move_iterator( wheel_events.begin() ),
                std::make_move_iterator( wheel_events.end() ) );
    wheel_events.clear();
}

namespace
{
std::atomic<int> wheel_radial{ -1 };  // side * 8 + index, -1 none
} // namespace

bool godot_host::take_wheel_radial( int &side, int &index )
{
    const int v = wheel_radial.exchange( -1 );
    if( v < 0 ) {
        return false;
    }
    side = v / 8;
    index = v % 8;
    return true;
}

void godot_host::wheel_done( int serial, const std::string &action )
{
    // The game's wheel answers with a radial slot: the pad layer fires its
    // key (ADR 0017), in play or in a screen.
    if( action.rfind( "radial:", 0 ) == 0 && action.size() == 10 ) {
        const int side = action[7] == 'R' ? 1 : 0;
        const int index = action[9] - '0';
        if( index >= 0 && index < 8 ) {
            wheel_radial = side * 8 + index;
        }
        wheel_done_serial = serial;
        return;
    }
    // The gamepad layer closes the wheel on its next event (its scheduler
    // ticks every 50 ms, so no wake is needed for that).
    wheel_done_serial = serial;
    if( action.empty() ) {
        // No nudge: in play a mouse move with nothing waiting for it opens
        // the mouse view, which counts as a menu, so the next held shoulder
        // wouldn't open a wheel.
        return;
    }
    {
        std::lock_guard<std::mutex> lock( look_mutex );
        action_requested = action;
        action_serial = serial;
    }
    // Wake the play loop to take the action (look_pending keeps the mouse
    // view from taking the nudge).
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::take_wheel_done( int serial )
{
    int expected = serial;
    return wheel_done_serial.compare_exchange_strong( expected, 0 );
}

void godot_host::set_wheel_aim( int serial, int aim_dx, int aim_dy )
{
    std::lock_guard<std::mutex> lock( look_mutex );
    aim_serial = serial;
    aim_x = aim_dx;
    aim_y = aim_dy;
}

bool godot_host::take_action_request( std::string &action, int &aim_dx, int &aim_dy )
{
    std::lock_guard<std::mutex> lock( look_mutex );
    if( action_requested.empty() ) {
        return false;
    }
    action = action_requested;
    action_requested.clear();
    const bool aimed = action_serial == aim_serial;
    aim_dx = aimed ? aim_x : 0;
    aim_dy = aimed ? aim_y : 0;
    return true;
}

void godot_host::request_action( const std::string &id )
{
    if( id.empty() ) {
        return;
    }
    {
        std::lock_guard<std::mutex> lock( look_mutex );
        action_requested = id;
        // No wheel's: never the aim's serial, so no aim.
        action_serial = -1;
    }
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

void godot_host::request_catalogue()
{
    {
        std::lock_guard<std::mutex> lock( look_mutex );
        catalogue_requested = true;
    }
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::take_catalogue_request()
{
    std::lock_guard<std::mutex> lock( look_mutex );
    const bool asked = catalogue_requested;
    catalogue_requested = false;
    return asked;
}

void godot_host::publish_catalogue( std::vector<wheel_action> actions )
{
    std::lock_guard<std::mutex> lock( catalogue_mutex );
    catalogue = std::move( actions );
    catalogue_new = true;
}

bool godot_host::take_catalogue( std::vector<wheel_action> &out )
{
    std::lock_guard<std::mutex> lock( catalogue_mutex );
    if( !catalogue_new ) {
        return false;
    }
    out = std::move( catalogue );
    catalogue.clear();
    catalogue_new = false;
    return true;
}

void godot_host::publish_context_actions( std::vector<std::string> actions )
{
    std::lock_guard<std::mutex> lock( context_mutex );
    if( actions != context_actions ) {
        context_actions = std::move( actions );
        context_new = true;
    }
}

bool godot_host::take_context_actions( std::vector<std::string> &out )
{
    std::lock_guard<std::mutex> lock( context_mutex );
    if( !context_new ) {
        return false;
    }
    out = context_actions;
    context_new = false;
    return true;
}

void godot_host::publish_context_bindings( bindings_map bindings )
{
    std::lock_guard<std::mutex> lock( context_mutex );
    if( bindings != context_bindings ) {
        context_bindings = std::move( bindings );
        bindings_new = true;
    }
}

bool godot_host::take_context_bindings( bindings_map &out )
{
    std::lock_guard<std::mutex> lock( context_mutex );
    if( !bindings_new ) {
        return false;
    }
    out = context_bindings;
    bindings_new = false;
    return true;
}

void godot_host::request_goto( const std::string &type )
{
    {
        std::lock_guard<std::mutex> lock( look_mutex );
        goto_type = type;
    }
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

void godot_host::request_time( int hour )
{
    {
        std::lock_guard<std::mutex> lock( look_mutex );
        time_hour = hour;
    }
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::take_time_request( int &hour )
{
    std::lock_guard<std::mutex> lock( look_mutex );
    if( time_hour < 0 ) {
        return false;
    }
    hour = time_hour;
    time_hour = -1;
    return true;
}

bool godot_host::take_goto_request( std::string &type )
{
    std::lock_guard<std::mutex> lock( look_mutex );
    if( goto_type.empty() ) {
        return false;
    }
    type = goto_type;
    goto_type.clear();
    return true;
}

namespace
{
// Packed as looking << 32 | dx << 16 | dy, 16 bits each (two's complement).
std::atomic<uint64_t> look_cursor_state{ 0 };
std::atomic<uint64_t> look_cursor_request{ 0 };  // 1 << 32 | dx << 16 | dy, 0 for none
// Aiming's, packed the same way.
std::atomic<uint64_t> aim_cursor_state{ 0 };
std::atomic<uint64_t> aim_cursor_request{ 0 };

uint64_t pack_cursor( int dx, int dy )
{
    return uint64_t( 1 ) << 32 | uint64_t( static_cast<uint16_t>( dx ) ) << 16
           | uint64_t( static_cast<uint16_t>( dy ) );
}

void unpack_cursor( uint64_t v, int &dx, int &dy )
{
    dx = static_cast<int16_t>( static_cast<uint16_t>( v >> 16 ) );
    dy = static_cast<int16_t>( static_cast<uint16_t>( v ) );
}
} // namespace

void godot_host::set_look_cursor( bool looking, int dx, int dy )
{
    look_cursor_state.store( looking ? pack_cursor( dx, dy ) : 0 );
}

bool godot_host::look_cursor( int &dx, int &dy )
{
    const uint64_t v = look_cursor_state.load();
    if( v == 0 ) {
        return false;
    }
    unpack_cursor( v, dx, dy );
    return true;
}

void godot_host::request_look_cursor( int dx, int dy )
{
    look_cursor_request.store( pack_cursor( dx, dy ) );
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::take_look_cursor_request( int &dx, int &dy )
{
    const uint64_t v = look_cursor_request.exchange( 0 );
    if( v == 0 ) {
        return false;
    }
    unpack_cursor( v, dx, dy );
    return true;
}

void godot_host::request_click( int dx, int dy )
{
    {
        std::lock_guard<std::mutex> lock( look_mutex );
        click_requested = true;
        click_dx = dx;
        click_dy = dy;
    }
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::take_click_request( int &dx, int &dy )
{
    std::lock_guard<std::mutex> lock( look_mutex );
    if( !click_requested ) {
        return false;
    }
    click_requested = false;
    dx = click_dx;
    dy = click_dy;
    return true;
}

bool godot_host::take_look_request( int &dx, int &dy )
{
    std::lock_guard<std::mutex> lock( look_mutex );
    if( !look_requested ) {
        return false;
    }
    look_requested = false;
    dx = look_dx;
    dy = look_dy;
    return true;
}

bool godot_host::mouse_away()
{
    return mouse_is_away;
}

void godot_host::set_text_wanted( bool wanted )
{
    text_is_wanted = wanted;
}

bool godot_host::text_wanted()
{
    return text_is_wanted;
}

void godot_host::set_in_menu( bool menu )
{
    menu_is_open = menu;
}

bool godot_host::in_menu()
{
    return menu_is_open;
}

namespace
{
std::atomic<bool> menu_pages_now{ false };
std::mutex waiting_category_mutex;
std::string waiting_category_now;
std::atomic<int> talk_depth{ 0 };
} // namespace

void godot_host::set_waiting_category( const std::string &category )
{
    std::lock_guard<std::mutex> lock( waiting_category_mutex );
    waiting_category_now = category;
}

std::string godot_host::waiting_category()
{
    std::lock_guard<std::mutex> lock( waiting_category_mutex );
    return waiting_category_now;
}

// fetch_add, not ++: enum_traits' operators take ++ on an atomic.
godot_host::talk_scope::talk_scope()
{
    talk_depth.fetch_add( 1 );
}

godot_host::talk_scope::~talk_scope()
{
    talk_depth.fetch_sub( 1 );
}

bool godot_host::talking()
{
    return talk_depth > 0;
}

void godot_host::set_menu_pages( bool pages )
{
    menu_pages_now = pages;
}

bool godot_host::menu_pages()
{
    return menu_pages_now;
}

namespace
{
std::mutex move_mode_mutex;
std::string move_mode_now;
std::atomic<bool> move_mode_pending_now{ false };
} // namespace

void godot_host::set_move_mode( const std::string &id, bool pending )
{
    move_mode_pending_now = pending;
    std::lock_guard<std::mutex> lock( move_mode_mutex );
    if( move_mode_now != id ) {
        move_mode_now = id;
    }
}

bool godot_host::move_mode_pending()
{
    return move_mode_pending_now;
}

std::string godot_host::move_mode()
{
    std::lock_guard<std::mutex> lock( move_mode_mutex );
    return move_mode_now;
}

namespace
{
std::atomic<bool> native_sidebar_on{ false };
std::mutex sidebar_mutex;
std::vector<godot_host::sidebar_panel> sidebar_building;  // this pass's places
std::vector<godot_host::sidebar_panel> sidebar_shown;  // the last published
int sidebar_columns = 0;
int sidebar_rows_n = 0;
int sidebar_serial = 0;
} // namespace

std::atomic<bool> sidebar_switched{ false };
std::atomic<bool> sidebar_redraw{ false };

void godot_host::set_native_sidebar( bool on )
{
    if( native_sidebar_on.exchange( on ) != on ) {
        sidebar_switched = true;
        sidebar_redraw = true;
        godot_host::wake();
    }
}

bool godot_host::take_sidebar_redraw()
{
    return sidebar_redraw.exchange( false );
}

std::atomic<int> log_zoom_now{ 100 };

void godot_host::set_log_zoom( int percent )
{
    percent = std::max( 50, std::min( percent, 300 ) );
    if( log_zoom_now.exchange( percent ) != percent ) {
        sidebar_switched = true;
        sidebar_redraw = true;
        godot_host::wake();
    }
}

int godot_host::log_zoom()
{
    return log_zoom_now;
}

bool godot_host::take_sidebar_switched()
{
    return sidebar_switched.exchange( false );
}

namespace
{
std::atomic<bool> minimap_hidden{ false };
} // namespace

void godot_host::set_minimap_unseen( bool unseen )
{
    minimap_hidden.store( unseen );
}

bool godot_host::minimap_unseen()
{
    return minimap_hidden.load();
}

bool godot_host::native_sidebar()
{
    return native_sidebar_on;
}

void godot_host::sidebar_begin()
{
    sidebar_building.clear();
}

void godot_host::sidebar_place( const std::string &id, const std::string &name, int x, int y,
                                int width, int height )
{
    sidebar_panel p;
    p.id = id;
    p.name = name;
    p.x = x;
    p.y = y;
    p.width = width;
    p.height = height;
    // Its last rows until it is drawn again.
    std::lock_guard<std::mutex> lock( sidebar_mutex );
    for( const sidebar_panel &old : sidebar_shown ) {
        if( old.id == id ) {
            p.rows = old.rows;
            break;
        }
    }
    sidebar_building.push_back( std::move( p ) );
}

void godot_host::sidebar_rows( const std::string &id, std::vector<std::string> rows )
{
    for( sidebar_panel &p : sidebar_building ) {
        if( p.id == id ) {
            p.rows = std::move( rows );
            return;
        }
    }
}

void godot_host::sidebar_end( int columns, int rows )
{
    std::lock_guard<std::mutex> lock( sidebar_mutex );
    bool same = columns == sidebar_columns && rows == sidebar_rows_n &&
                sidebar_building.size() == sidebar_shown.size();
    for( size_t i = 0; same && i < sidebar_building.size(); i++ ) {
        const sidebar_panel &a = sidebar_building[i];
        const sidebar_panel &b = sidebar_shown[i];
        same = a.id == b.id && a.x == b.x && a.y == b.y && a.width == b.width &&
               a.height == b.height && a.rows == b.rows;
    }
    if( same ) {
        return;
    }
    sidebar_shown = sidebar_building;
    sidebar_columns = columns;
    sidebar_rows_n = rows;
    sidebar_serial++;
}

bool godot_host::get_sidebar_panels( std::vector<sidebar_panel> &out, int &columns, int &rows,
                                     int &serial )
{
    std::lock_guard<std::mutex> lock( sidebar_mutex );
    if( serial == sidebar_serial ) {
        return false;
    }
    serial = sidebar_serial;
    out = sidebar_shown;
    columns = sidebar_columns;
    rows = sidebar_rows_n;
    return true;
}

namespace
{
std::atomic<int> safe_refused_turn{ -1 };
} // namespace

void godot_host::note_safe_refusal()
{
    safe_refused_turn = to_turns<int>( calendar::turn - calendar::turn_zero );
}

int godot_host::safe_refusal_turn()
{
    return safe_refused_turn;
}

void godot_host::set_safe_mode( int mode )
{
    safe_mode_now = mode;
}

int godot_host::safe_mode()
{
    return safe_mode_now;
}

void godot_host::set_activity_stoppable( bool stoppable )
{
    activity_stoppable_now = stoppable;
}

bool godot_host::activity_stoppable()
{
    return activity_stoppable_now;
}

void godot_host::set_filter_action( const std::string &id )
{
    std::lock_guard<std::mutex> lock( filter_mutex );
    filter_action_now = id;
}

std::string godot_host::filter_action()
{
    std::lock_guard<std::mutex> lock( filter_mutex );
    return filter_action_now;
}

void godot_host::request_filter()
{
    filter_requested = true;
}

bool godot_host::take_filter_request()
{
    return filter_requested.exchange( false );
}

void godot_host::set_text_entry( const std::string &text )
{
    std::lock_guard<std::mutex> lock( text_entry_mutex );
    text_entry_now = text;
}

std::string godot_host::text_entry()
{
    std::lock_guard<std::mutex> lock( text_entry_mutex );
    return text_entry_now;
}

void godot_host::set_list_filters( bool filters )
{
    list_filters_now = filters;
}

bool godot_host::list_filters()
{
    return list_filters_now;
}

void godot_host::set_watch( int minute, int format, int weekday, int day )
{
    watch_now = minute < 0 ? -1 : ( minute << 16 ) | ( ( format & 0xf ) << 12 ) |
                ( ( weekday & 0xf ) << 8 ) | ( day & 0xff );
}

std::array<int, 4> godot_host::watch()
{
    const int w = watch_now;
    if( w < 0 ) {
        return { -1, 0, 0, 0 };
    }
    return { w >> 16, ( w >> 12 ) & 0xf, ( w >> 8 ) & 0xf, w & 0xff };
}

namespace
{
std::atomic<bool> pad_captured_now{ false };
std::atomic<bool> df_panel_requested{ false };
} // namespace

namespace
{
std::atomic<int> overmap_state_now{ 0 };
std::mutex screen_action_mutex;
std::string screen_action_requested;
} // namespace

namespace
{
std::atomic<bool> stick_camera_on{ false };
} // namespace

void godot_host::set_stick_camera( bool on )
{
    stick_camera_on = on;
}

bool godot_host::stick_camera()
{
    return stick_camera_on;
}

void godot_host::set_overmap_state( int state )
{
    overmap_state_now = state;
}

int godot_host::overmap_state()
{
    return overmap_state_now;
}

void godot_host::request_screen_action( const std::string &id )
{
    std::lock_guard<std::mutex> lock( screen_action_mutex );
    screen_action_requested = id;
}

std::string godot_host::take_screen_action()
{
    std::lock_guard<std::mutex> lock( screen_action_mutex );
    std::string id;
    id.swap( screen_action_requested );
    return id;
}

namespace
{
std::mutex map_drag_mutex;
float map_drag_x = 0.0f;
float map_drag_y = 0.0f;
} // namespace

void godot_host::request_map_drag( float dx, float dy )
{
    std::lock_guard<std::mutex> lock( map_drag_mutex );
    map_drag_x += dx;
    map_drag_y += dy;
}

std::pair<float, float> godot_host::take_map_drag()
{
    std::lock_guard<std::mutex> lock( map_drag_mutex );
    const std::pair<float, float> d( map_drag_x, map_drag_y );
    map_drag_x = 0.0f;
    map_drag_y = 0.0f;
    return d;
}

int godot_host::overmap_zoom()
{
    return uistate.overmap_tileset_zoom;
}

void godot_host::set_pad_captured( bool on )
{
    pad_captured_now = on;
}

bool godot_host::pad_captured()
{
    return pad_captured_now;
}

void godot_host::request_df_panel()
{
    df_panel_requested = true;
}

bool godot_host::take_df_panel_request()
{
    return df_panel_requested.exchange( false );
}

void godot_host::request_look_exit( look_exit how )
{
    look_exit_requested = static_cast<int>( how );
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

godot_host::look_exit godot_host::take_look_exit_request()
{
    return static_cast<look_exit>( look_exit_requested.exchange( 0 ) );
}

namespace
{
bool view_held = false;
bool view_armed = false;
int held_view[3] = { 0, 0, 0 };
} // namespace

void godot_host::hold_view( int dx, int dy, int dz )
{
    view_held = true;
    view_armed = false;
    held_view[0] = dx;
    held_view[1] = dy;
    held_view[2] = dz;
}

void godot_host::arm_held_view()
{
    view_armed = view_held;
}

bool godot_host::take_settled_view( bool preview_pending, int &dx, int &dy, int &dz )
{
    if( !view_held || !view_armed || preview_pending ) {
        return false;
    }
    view_held = false;
    view_armed = false;
    dx = held_view[0];
    dy = held_view[1];
    dz = held_view[2];
    return true;
}

void godot_host::set_route_preview( bool pending )
{
    route_preview_now = pending;
}

bool godot_host::route_preview()
{
    return route_preview_now;
}

void godot_host::set_radial_sides( int sides )
{
    radial_sides_now = sides;
}

int godot_host::radial_sides()
{
    return radial_sides_now;
}

void godot_host::set_in_game( bool playing )
{
    game_is_on = playing;
}

bool godot_host::in_game()
{
    return game_is_on;
}

int godot_host::snapshot_radius()
{
    return radius_levels[radius_level.load()];
}

void godot_host::zoom_view( int steps )
{
    radius_level = std::clamp( radius_level.load() - steps, 0, static_cast<int>( radius_levels.size() ) - 1 );
}

int godot_host::snapshot_radius_level()
{
    return radius_level.load();
}

int godot_host::snapshot_radius_level_count()
{
    return static_cast<int>( radius_levels.size() );
}

namespace
{
std::atomic<bool> native_menus_on{ false };
std::mutex menu_mutex;
const void *menu_owner = nullptr;
godot_host::menu_state menu_now;
bool menu_up = false;
uint32_t menu_serial = 0;
std::deque<godot_host::menu_request> menu_requests;
std::mutex dialog_mutex;
const void *dialog_owner = nullptr;
godot_host::dialog_state dialog_now;
bool dialog_up = false;
uint32_t dialog_serial = 0;
std::deque<godot_host::dialog_request> dialog_requests;
bool snapshot_owed_now = false;
bool snapshot_flush = false;
std::atomic<bool> flat_view_now{ false };
std::atomic<bool> aiming_now{ false };
std::atomic<int> glyph_style_now{ 3 };
std::atomic<bool> flat_view_changed{ false };
} // namespace

void godot_host::set_native_menus( bool on )
{
    native_menus_on = on;
}

bool godot_host::native_menus()
{
    return native_menus_on.load();
}

bool godot_host::get_menu( menu_state &out )
{
    std::lock_guard<std::mutex> lock( menu_mutex );
    if( !menu_up ) {
        return false;
    }
    if( out.serial != menu_now.serial ) {
        out = menu_now;
    }
    return true;
}

void godot_host::request_menu( const menu_request &r )
{
    {
        std::lock_guard<std::mutex> lock( menu_mutex );
        menu_requests.push_back( r );
    }
    wake();
}

void godot_host::publish_menu( const void *owner, menu_state state )
{
    std::lock_guard<std::mutex> lock( menu_mutex );
    state.serial = menu_now.serial;
    const bool same = menu_up && owner == menu_owner && state.title == menu_now.title &&
                      state.header == menu_now.header && state.footer == menu_now.footer &&
                      state.selected == menu_now.selected && state.tab == menu_now.tab &&
                      state.tabs == menu_now.tabs && state.desc == menu_now.desc &&
                      state.cancel == menu_now.cancel && state.filter == menu_now.filter &&
                      state.filter_text == menu_now.filter_text &&
                      state.entries.size() == menu_now.entries.size() &&
    std::equal( state.entries.begin(), state.entries.end(), menu_now.entries.begin(),
    []( const menu_entry & a, const menu_entry & b ) {
        return a.text == b.text && a.column == b.column && a.desc == b.desc &&
               a.hotkey == b.hotkey && a.enabled == b.enabled;
    } );
    if( same ) {
        return;
    }
    if( owner != menu_owner ) {
        // A new list: the last one's requests were for it.
        menu_requests.clear();
    }
    state.serial = ++menu_serial;
    menu_now = std::move( state );
    menu_owner = owner;
    menu_up = true;
}

void godot_host::clear_menu( const void *owner )
{
    std::lock_guard<std::mutex> lock( menu_mutex );
    if( owner == menu_owner ) {
        menu_up = false;
        menu_owner = nullptr;
        menu_requests.clear();
    }
}

bool godot_host::take_menu_request( menu_request &out )
{
    std::lock_guard<std::mutex> lock( menu_mutex );
    if( menu_requests.empty() ) {
        return false;
    }
    out = menu_requests.front();
    menu_requests.pop_front();
    return true;
}

// The engine's colour tags as BBCode, for the host ([color=#rrggbb]),
// brackets escaped.
std::string godot_host::bbcode( const std::string &tagged )
{
    std::string out;
    int open = 0;
    const auto text = []( std::string & o, std::string_view t ) {
        for( const char c : t ) {
            if( c == '[' ) {
                o += "[lb]";
            } else {
                o += c;
            }
        }
    };
    for( const std::string &seg : split_by_color( replace_colors( tagged ) ) ) {
        if( seg.empty() ) {
            continue;
        }
        if( seg[0] == '<' ) {
            const size_t end = seg.find( '>' );
            if( end != std::string::npos ) {
                const color_tag_parse_result tag = get_color_from_tag( seg, report_color_error::no );
                if( tag.type == color_tag_parse_result::open_color_tag ) {
                    const SDL_Color c = curses_color_to_SDL( tag.color );
                    out += string_format( "[color=#%02x%02x%02x]", c.r, c.g, c.b );
                    ++open;
                    text( out, std::string_view( seg ).substr( end + 1 ) );
                    continue;
                }
                if( tag.type == color_tag_parse_result::close_color_tag ) {
                    if( open > 0 ) {
                        out += "[/color]";
                        --open;
                    }
                    text( out, std::string_view( seg ).substr( end + 1 ) );
                    continue;
                }
            }
        }
        text( out, seg );
    }
    for( ; open > 0; --open ) {
        out += "[/color]";
    }
    return out;
}

bool godot_host::get_dialog( dialog_state &out )
{
    std::lock_guard<std::mutex> lock( dialog_mutex );
    if( !dialog_up ) {
        return false;
    }
    if( out.serial != dialog_now.serial ) {
        out = dialog_now;
    }
    return true;
}

void godot_host::request_dialog( const dialog_request &r )
{
    {
        std::lock_guard<std::mutex> lock( dialog_mutex );
        dialog_requests.push_back( r );
    }
    wake();
}

void godot_host::publish_dialog( const void *owner, dialog_state state )
{
    std::lock_guard<std::mutex> lock( dialog_mutex );
    const bool same = dialog_up && owner == dialog_owner && state.kind == dialog_now.kind &&
                      state.title == dialog_now.title && state.message == dialog_now.message &&
                      state.label == dialog_now.label && state.value == dialog_now.value &&
                      state.options == dialog_now.options && state.option_glyphs == dialog_now.option_glyphs &&
                      state.cancel == dialog_now.cancel &&
                      state.anykey == dialog_now.anykey && state.max_length == dialog_now.max_length;
    if( same ) {
        return;
    }
    if( owner != dialog_owner ) {
        dialog_requests.clear();
    }
    state.serial = ++dialog_serial;
    dialog_now = std::move( state );
    dialog_owner = owner;
    dialog_up = true;
}

void godot_host::clear_dialog( const void *owner )
{
    std::lock_guard<std::mutex> lock( dialog_mutex );
    if( owner == dialog_owner ) {
        dialog_up = false;
        dialog_owner = nullptr;
        dialog_requests.clear();
    }
}

bool godot_host::take_dialog_request( dialog_request &out )
{
    std::lock_guard<std::mutex> lock( dialog_mutex );
    if( dialog_requests.empty() ) {
        return false;
    }
    out = dialog_requests.front();
    dialog_requests.pop_front();
    return true;
}

void godot_host::set_glyph_style( int style )
{
    glyph_style_now = style;
}

int godot_host::glyph_style()
{
    return glyph_style_now.load();
}

void godot_host::set_aiming( bool aiming )
{
    aiming_now = aiming;
    if( !aiming ) {
        aim_cursor_state.store( 0 );
        aim_cursor_request.store( 0 );
    }
}

void godot_host::set_aim_cursor( int dx, int dy )
{
    aim_cursor_state.store( pack_cursor( dx, dy ) );
}

bool godot_host::aim_cursor( int &dx, int &dy )
{
    const uint64_t v = aim_cursor_state.load();
    if( v == 0 ) {
        return false;
    }
    unpack_cursor( v, dx, dy );
    return true;
}

void godot_host::request_aim_cursor( int dx, int dy )
{
    aim_cursor_request.store( pack_cursor( dx, dy ) );
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

bool godot_host::take_aim_cursor_request( int &dx, int &dy )
{
    const uint64_t v = aim_cursor_request.exchange( 0 );
    if( v == 0 ) {
        return false;
    }
    unpack_cursor( v, dx, dy );
    return true;
}

bool godot_host::aiming()
{
    return aiming_now.load();
}

void godot_host::set_flat_view( bool flat )
{
    if( flat_view_now.exchange( flat ) != flat ) {
        flat_view_changed = true;
    }
}

bool godot_host::flat_view()
{
    return flat_view_now.load();
}

bool godot_host::take_flat_view_changed()
{
    return flat_view_changed.exchange( false );
}

void godot_host::set_snapshot_owed( bool owed )
{
    snapshot_owed_now = owed;
}

bool godot_host::snapshot_owed()
{
    return snapshot_owed_now;
}

void godot_host::request_snapshot_flush()
{
    snapshot_flush = true;
}

bool godot_host::take_snapshot_flush()
{
    const bool was = snapshot_flush;
    snapshot_flush = false;
    return was;
}

void godot_host::request_zoom_view( int steps )
{
    if( steps == 0 ) {
        return;
    }
    zoom_requested += steps;
    push_mouse( { mouse_command::nudge, 0, 0, 0, 0 } );
}

int godot_host::take_zoom_view_request()
{
    return zoom_requested.exchange( 0 );
}

void godot_host::apply_mouse( bool ( *tile_to_pixels )( int dx, int dy, int &x, int &y ) )
{
    if( button_changed ) {
        if( frames_published == button_changed_frame &&
            SDL_GetTicks() - button_changed_at < BUTTON_FRAME_WAIT ) {
            return;
        }
        button_changed = false;
    }
    mouse_command command;
    {
        std::lock_guard<std::mutex> lock( mouse_mutex );
        if( mouse_queue.empty() ) {
            return;
        }
        command = mouse_queue.front();
        mouse_queue.pop_front();
    }
    SDL_Window *window = SDL_GetWindowFromID( window_id() );
    if( window == nullptr ) {
        return;
    }
    if( command.kind == mouse_command::away || command.kind == mouse_command::nudge ) {
        // A move where the mouse already is, for handle_mouseview to see
        // (away), or to wake the play loop for a look request (nudge).
        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState( &x, &y );
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window_id();
        event.motion.x = x;
        event.motion.y = y;
        SDL_PushEvent( &event );
        if( command.kind == mouse_command::away ) {
            mouse_is_away = true;
        }
        return;
    }
    if( command.kind == mouse_command::tile &&
        !tile_to_pixels( command.x, command.y, command.x, command.y ) ) {
        return;
    }
    // Pixels are a window's (a ray on a floating menu): in play the flat
    // map is never on show, so they don't point at a tile either.
    mouse_is_away = command.kind != mouse_command::tile;
    const float x = static_cast<float>( command.x );
    const float y = static_cast<float>( command.y );
    SDL_WarpMouseInWindow( window, x, y );
    for( const Uint8 b : { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT } ) {
        const uint32_t mask = SDL_BUTTON_MASK( b );
        if( ( command.buttons & mask ) == ( mouse_buttons_applied & mask ) ) {
            continue;
        }
        SDL_Event event{};
        event.type = ( command.buttons & mask ) ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.windowID = window_id();
        event.button.button = b;
        event.button.down = ( command.buttons & mask ) != 0;
        event.button.clicks = 1;
        event.button.x = x;
        event.button.y = y;
        SDL_PushEvent( &event );
    }
    if( command.buttons != mouse_buttons_applied ) {
        button_changed = true;
        button_changed_frame = frames_published;
        button_changed_at = SDL_GetTicks();
    }
    mouse_buttons_applied = command.buttons;
    if( command.wheel != 0 ) {
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_WHEEL;
        event.wheel.windowID = window_id();
        event.wheel.y = static_cast<float>( command.wheel );
        event.wheel.integer_y = command.wheel;
        event.wheel.mouse_x = x;
        event.wheel.mouse_y = y;
        SDL_PushEvent( &event );
    }
}

static std::atomic<int> move_step_now{ 0 };
static constexpr float MOVE_TURN_HYSTERESIS = 6.0f;

void godot_host::set_view_yaw( float degrees )
{
    view_yaw_degrees.store( degrees );
    const int step = move_step_now.load();
    const float off = std::remainder( degrees - step * 45.0f, 360.0f );
    if( std::abs( off ) > 22.5f + MOVE_TURN_HYSTERESIS ) {
        const int nearest = static_cast<int>( std::lround( degrees / 45.0f ) );
        move_step_now.store( ( ( nearest % 8 ) + 8 ) % 8 );
    }
}

int godot_host::move_step()
{
    return move_step_now.load();
}

float godot_host::view_yaw()
{
    return view_yaw_degrees.load();
}

void godot_host::enable_virtual_pad()
{
    virtual_pad_wanted.store( true );
}

void godot_host::set_virtual_pad( uint32_t buttons, const int16_t axes[virtual_pad_axes] )
{
    virtual_buttons.store( buttons );
    for( int a = 0; a < virtual_pad_axes; ++a ) {
        virtual_axes[a].store( axes[a] );
    }
}

bool godot_host::take_virtual_rumble( uint16_t &low, uint16_t &high )
{
    if( !virtual_rumble_new.exchange( false ) ) {
        return false;
    }
    const uint32_t r = virtual_rumble.load();
    low = static_cast<uint16_t>( r >> 16 );
    high = static_cast<uint16_t>( r & 0xffff );
    return true;
}

void godot_host::set_touch_pad( bool on )
{
    touch_pad_on = on;
}

bool godot_host::touch_pad()
{
    return touch_pad_on;
}

bool godot_host::is_virtual_pad( uint32_t id )
{
    return virtual_pad_id != 0 && id == virtual_pad_id;
}

void godot_host::attach_virtual_pad()
{
    if( !virtual_pad_wanted.load() || virtual_pad_id != 0 ) {
        return;
    }
    // A standard gamepad: SDL maps a virtual joystick of this type by
    // button and axis number, in SDL_GamepadButton and SDL_GamepadAxis order.
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE( &desc );
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = virtual_pad_axes;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_DPAD_RIGHT + 1;
    desc.button_mask = ( 1u << desc.nbuttons ) - 1;
    desc.axis_mask = ( 1u << virtual_pad_axes ) - 1;
    desc.name = "Cataclysm VR Touch";
    desc.Update = update_virtual_pad;
    desc.Rumble = rumble_virtual_pad;
    virtual_pad_id = SDL_AttachVirtualJoystick( &desc );
    if( virtual_pad_id == 0 ) {
        std::fprintf( stderr, "godot_host: virtual pad not attached: %s\n", SDL_GetError() );
    } else {
        std::fprintf( stderr, "godot_host: virtual pad attached (joystick %u, gamepad %d)\n",
                      static_cast<unsigned>( virtual_pad_id ), SDL_IsGamepad( virtual_pad_id ) ? 1 : 0 );
    }
}

void godot_host::set_frame_sink( frame_sink new_sink, void *context )
{
    sink = new_sink;
    sink_context = context;
}

bool godot_host::take_frame( uint64_t &sequence, std::vector<unsigned char> &rgba, int &width,
                             int &height )
{
    std::lock_guard<std::mutex> lock( frame_mutex );
    if( frame_sequence == sequence ) {
        return false;
    }
    // Swap rather than copy: the engine replaces the whole buffer on its
    // next publish anyway.
    rgba.swap( frame_rgba );
    width = frame_width;
    height = frame_height;
    sequence = frame_sequence;
    return true;
}

uint32_t godot_host::window_id()
{
    return engine_window_id.load();
}

void godot_host::set_window_id( uint32_t id )
{
    engine_window_id.store( id );
}

void godot_host::begin_map_snapshot( int center_x, int center_y, int center_z, int player_x,
                                     int player_y, int player_z, int player_abs_x, int player_abs_y )
{
    map_building.center[0] = center_x;
    map_building.center[1] = center_y;
    map_building.center[2] = center_z;
    map_building.player[0] = player_x;
    map_building.player[1] = player_y;
    map_building.player[2] = player_z;
    map_building.player_abs[0] = player_abs_x;
    map_building.player_abs[1] = player_abs_y;
    map_building.player_abs[2] = player_z;
    map_building.radius = snapshot_radius();
    map_building.sprites.clear();
    map_building.blits.clear();
    map_building.anim_frames.clear();
    map_building.storeys_usec = 0;
    map_building.natural_light = 0.0f;
    map_building.lights_x = 0;
    map_building.lights_y = 0;
    map_building.lights_size = 0;
    map_building.lights.clear();
    map_building.creatures.clear();
    map_building.wind_from = 0;
    map_building.wind_mph = 0;
    map_building.precip = 0;
    map_building.rains = false;
    map_building.sun_from = 0.0f;
    map_building.sun_altitude = -90.0f;
    map_building.moon_from = 0.0f;
    map_building.moon_altitude = -90.0f;
    map_building.moon_phase = 0;
    map_building.light_modifier = 0;
    map_recording = true;
}

void godot_host::end_map_snapshot()
{
    map_recording = false;
    if( map_building.sprites.empty() ) {
        return;
    }
    {
        // The draw publishes a changed tileset before its first sprite.
        std::lock_guard<std::mutex> lock( tileset_mutex );
        map_building.tileset_generation = tileset_published.generation;
    }
    std::lock_guard<std::mutex> lock( map_mutex );
    // The previous snapshot's storage comes back for the next draw.
    std::swap( map_published, map_building );
    ++map_sequence;
}

bool godot_host::recording_map()
{
    return map_recording;
}

godot_host::record_only_scope::record_only_scope( bool on ) : was( recording_without_drawing )
{
    recording_without_drawing = was || on;
}

godot_host::record_only_scope::~record_only_scope()
{
    recording_without_drawing = was;
}

namespace
{
std::atomic<bool> board_is_shown{ false };
bool map_draw_skip = false;  // engine thread
} // namespace

void godot_host::set_board_shown( bool shown )
{
    board_is_shown.store( shown );
}

bool godot_host::board_shown()
{
    return board_is_shown.load();
}

void godot_host::set_map_draw_skipped( bool skipped )
{
    map_draw_skip = skipped;
}

bool godot_host::map_draw_skipped()
{
    return map_draw_skip;
}

bool godot_host::map_unseen()
{
    return recording_map() || board_shown();
}

bool godot_host::record_only()
{
    return recording_without_drawing;
}

void godot_host::note_storeys_usec( int usec )
{
    map_building.storeys_usec = usec;
}

namespace
{
std::array<std::atomic<int64_t>, static_cast<int>( godot_host::timer::count )> timer_usec{};
std::atomic<int> timed_draws{ 0 };
std::atomic<int> timed_draw_max{ 0 };
std::atomic<int> turn_now{ -1 };
int turn_taken = -1;  // host side
} // namespace

godot_host::timed_scope::timed_scope( timer which ) : which( which ),
    start( std::chrono::steady_clock::now() )
{
}

godot_host::timed_scope::~timed_scope()
{
    const int usec = static_cast<int>( std::chrono::duration_cast<std::chrono::microseconds>
                                       ( std::chrono::steady_clock::now() - start ).count() );
    // (fetch_add: enum_traits.h's operator templates take ++ on an atomic.)
    timer_usec[static_cast<int>( which )].fetch_add( usec );
    if( which == timer::draw ) {
        timed_draws.fetch_add( 1 );
        int seen = timed_draw_max.load();
        while( usec > seen && !timed_draw_max.compare_exchange_weak( seen, usec ) ) {
        }
    }
}

void godot_host::note_turn( int turn )
{
    turn_now.store( turn );
}

godot_host::redraw_stats godot_host::take_redraw_stats()
{
    redraw_stats s;
    s.draws = timed_draws.exchange( 0 );
    s.draw_max_usec = timed_draw_max.exchange( 0 );
    for( size_t i = 0; i < s.usec.size(); ++i ) {
        s.usec[i] = timer_usec[i].exchange( 0 );
    }
    const int now = turn_now;
    s.turns = turn_taken >= 0 && now >= turn_taken ? now - turn_taken : 0;
    turn_taken = now;
    return s;
}

std::vector<godot_host::map_light> &godot_host::record_map_lights( float natural_light, int x,
        int y, int size )
{
    map_building.natural_light = natural_light;
    map_building.lights_x = x;
    map_building.lights_y = y;
    map_building.lights_size = size;
    map_building.lights.assign( static_cast<size_t>( size ) * size, map_light{} );
    return map_building.lights;
}

void godot_host::record_map_weather( int wind_from, int wind_mph, int precip, bool rains )
{
    map_building.wind_from = wind_from;
    map_building.wind_mph = wind_mph;
    map_building.precip = precip;
    map_building.rains = rains;
}

void godot_host::record_map_sky( float sun_from, float sun_altitude, float moon_from,
                                 float moon_altitude, int moon_phase, int light_modifier )
{
    map_building.sun_from = sun_from;
    map_building.sun_altitude = sun_altitude;
    map_building.moon_from = moon_from;
    map_building.moon_altitude = moon_altitude;
    map_building.moon_phase = moon_phase;
    map_building.light_modifier = light_modifier;
}

std::vector<godot_host::map_creature> &godot_host::record_map_creatures()
{
    map_building.creatures.clear();
    return map_building.creatures;
}

godot_host::sprite_lookup_scope::sprite_lookup_scope()
{
    ++lookup_depth;
}

godot_host::sprite_lookup_scope::~sprite_lookup_scope()
{
    --lookup_depth;
}

bool godot_host::sprite_lookup_scope::recording() const
{
    return map_recording && lookup_depth == 1;
}

void godot_host::record_map_sprite( const std::string &id, const std::string &variant,
                                    int category, int light, int x, int y, int z, int subtile, int rota, int height_3d )
{
    map_building.sprites.push_back( { id, variant, category, light, x, y, z, subtile, rota,
                                      height_3d } );
}

void godot_host::record_map_blit( int index, int angle, bool flip_h, bool flip_v, int offset_x,
                                  int offset_y, int width, int height, float pixelscale, int opaque_x, int opaque_y,
                                  int opaque_w, int opaque_h )
{
    if( !map_recording || lookup_depth == 0 || map_building.sprites.empty() ) {
        return;
    }
    map_building.blits.push_back( { static_cast<int>( map_building.sprites.size() ) - 1, index,
                                    angle, flip_h, flip_v, offset_x, offset_y, width, height, pixelscale,
                                    opaque_x, opaque_y, opaque_w, opaque_h } );
}

void godot_host::record_map_blit_animation( const std::vector<map_anim_frame> &frames, int base,
        int total )
{
    if( !map_recording || lookup_depth == 0 || map_building.blits.empty() || frames.empty() ) {
        return;
    }
    map_blit &b = map_building.blits.back();
    b.anim_first = static_cast<int>( map_building.anim_frames.size() );
    b.anim_count = static_cast<int>( frames.size() );
    b.anim_base = base;
    b.anim_total = total;
    map_building.anim_frames.insert( map_building.anim_frames.end(), frames.begin(), frames.end() );
}

bool godot_host::tileset_changed( const void *key )
{
    if( key == tileset_key ) {
        return false;
    }
    tileset_key = key;
    return true;
}

void godot_host::publish_tileset( tileset_info info )
{
    std::lock_guard<std::mutex> lock( tileset_mutex );
    info.generation = tileset_published.generation + 1;
    tileset_published = std::move( info );
}

bool godot_host::take_tileset( uint64_t &generation, tileset_info &out )
{
    std::lock_guard<std::mutex> lock( tileset_mutex );
    if( tileset_published.generation == generation ) {
        return false;
    }
    out = tileset_published;
    generation = tileset_published.generation;
    return true;
}

bool godot_host::map_objects_changed( uint64_t key )
{
    if( key == map_objects_key ) {
        return false;
    }
    map_objects_key = key;
    return true;
}

void godot_host::publish_map_objects( map_objects_info info )
{
    std::lock_guard<std::mutex> lock( map_objects_mutex );
    info.generation = map_objects_published.generation + 1;
    map_objects_published = std::move( info );
}

bool godot_host::take_map_objects( uint64_t &generation, map_objects_info &out )
{
    std::lock_guard<std::mutex> lock( map_objects_mutex );
    if( map_objects_published.generation == generation ) {
        return false;
    }
    out = map_objects_published;
    generation = map_objects_published.generation;
    return true;
}

bool godot_host::take_map_snapshot( uint64_t &sequence, map_snapshot &out )
{
    std::lock_guard<std::mutex> lock( map_mutex );
    if( map_sequence == sequence ) {
        return false;
    }
    out = std::move( map_published );
    map_published = map_snapshot();
    sequence = map_sequence;
    return true;
}

namespace
{
std::mutex tile_catalogue_mutex;
std::string tile_catalogue_spec;
bool tile_catalogue_requested = false;
godot_host::tile_catalogue_info tile_catalogue_published;
} // namespace

void godot_host::request_tile_catalogue( const std::string &spec )
{
    {
        std::lock_guard<std::mutex> lock( tile_catalogue_mutex );
        tile_catalogue_spec = spec;
        tile_catalogue_requested = true;
    }
    // An engine waiting for input pumps on the next event.
    wake();
}

bool godot_host::take_tile_catalogue_request( std::string &spec )
{
    std::lock_guard<std::mutex> lock( tile_catalogue_mutex );
    if( !tile_catalogue_requested ) {
        return false;
    }
    tile_catalogue_requested = false;
    spec = tile_catalogue_spec;
    return true;
}

void godot_host::publish_tile_catalogue( std::vector<tile_catalogue_entry> entries )
{
    map_recording = false;
    {
        std::lock_guard<std::mutex> lock( tileset_mutex );
        map_building.tileset_generation = tileset_published.generation;
    }
    std::lock_guard<std::mutex> lock( tile_catalogue_mutex );
    tile_catalogue_published.snapshot = std::move( map_building );
    map_building = map_snapshot();
    tile_catalogue_published.entries = std::move( entries );
    ++tile_catalogue_published.generation;
}

bool godot_host::take_tile_catalogue( uint64_t &generation, tile_catalogue_info &out )
{
    std::lock_guard<std::mutex> lock( tile_catalogue_mutex );
    if( tile_catalogue_published.generation == generation ) {
        return false;
    }
    out = tile_catalogue_published;
    generation = tile_catalogue_published.generation;
    return true;
}

namespace
{
std::map<std::string, std::string> tile_substitutes;
} // namespace

void godot_host::set_tile_substitutes( std::map<std::string, std::string> substitutes )
{
    tile_substitutes = std::move( substitutes );
}

const std::string *godot_host::tile_substitute( const std::string &id )
{
    const auto it = tile_substitutes.find( id );
    return it != tile_substitutes.end() ? &it->second : nullptr;
}

#endif // CDDA_GODOT
