#if defined(CDDA_GODOT)

#if !defined(TILES) || !defined(USE_SDL3)
#error "CDDA_GODOT needs the SDL3 tiles build"
#endif

#include "godot_host.h"

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
std::atomic<float> view_yaw_degrees{ 0.0f };

// The open UIs as of the latest frame (publish_frame), and the host's mouse.
std::mutex ui_windows_mutex;
std::vector<godot_host::ui_window> ui_windows;
uint64_t ui_windows_sequence = 0;
// The host's mouse commands, in order, applied one per CheckMessages: the
// game keeps only the last input event of each poll, and a press and its
// release must each have their own (a held state lost quick clicks while
// the engine was busy).
struct mouse_command {
    enum kind_t { pixels, tile, away } kind;
    int x;
    int y;
    uint32_t buttons;
    int wheel;
};
std::atomic<uint64_t> sidebar_cells{ 0 };  // set_sidebar, packed in 16 bits each
std::mutex mouse_mutex;
std::deque<mouse_command> mouse_queue;
uint32_t mouse_buttons_applied = 0;  // engine thread only
bool mouse_is_away = false;  // engine thread only

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

bool godot_host::mouse_away()
{
    return mouse_is_away;
}

void godot_host::apply_mouse( bool ( *tile_to_pixels )( int dx, int dy, int &x, int &y ) )
{
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
    if( command.kind == mouse_command::away ) {
        // A move where the mouse already is, for handle_mouseview to see.
        float x = 0.0f;
        float y = 0.0f;
        SDL_GetMouseState( &x, &y );
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = window_id();
        event.motion.x = x;
        event.motion.y = y;
        SDL_PushEvent( &event );
        mouse_is_away = true;
        return;
    }
    if( command.kind == mouse_command::tile &&
        !tile_to_pixels( command.x, command.y, command.x, command.y ) ) {
        return;
    }
    mouse_is_away = false;
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

void godot_host::set_view_yaw( float degrees )
{
    view_yaw_degrees.store( degrees );
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
                                     int player_y, int player_z )
{
    map_building.center[0] = center_x;
    map_building.center[1] = center_y;
    map_building.center[2] = center_z;
    map_building.player[0] = player_x;
    map_building.player[1] = player_y;
    map_building.player[2] = player_z;
    map_building.sprites.clear();
    map_building.blits.clear();
    map_building.storeys_usec = 0;
    map_building.natural_light = 0.0f;
    map_building.lights_x = 0;
    map_building.lights_y = 0;
    map_building.lights_size = 0;
    map_building.lights.clear();
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

godot_host::record_only_scope::record_only_scope()
{
    recording_without_drawing = true;
}

godot_host::record_only_scope::~record_only_scope()
{
    recording_without_drawing = false;
}

bool godot_host::record_only()
{
    return recording_without_drawing;
}

void godot_host::note_storeys_usec( int usec )
{
    map_building.storeys_usec = usec;
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

#endif // CDDA_GODOT
