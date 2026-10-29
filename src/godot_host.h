#pragma once
#ifndef CATA_SRC_GODOT_HOST_H
#define CATA_SRC_GODOT_HOST_H

// Hosting the engine inside another program (CDDA_GODOT builds only): the
// Godot VR host in the cataclysm-dda-vr repo. The host calls run() on a
// thread it owns, in place of main(). The engine renders to SDL's offscreen
// video driver and publishes every presented frame for the host to show;
// the host feeds input by pushing ordinary SDL events at window_id(), so
// the engine's own input path is unchanged.
//
// Deliberately plain types only, so the host never includes engine headers
// or depends on the engine's build defines.

#include <cstdint>
#include <string>
#include <vector>

struct SDL_Renderer;

namespace godot_host
{

// The map view as the engine last drew it (plan 0003, Phase 3): one entry
// per sprite cata_tiles looked up for game::w_terrain, in draw order, so
// terrain comes before furniture, items and creatures on the same tile.
struct map_sprite {
    std::string id;
    std::string variant;
    int category;   // cata_tiles' TILE_CATEGORY
    int light;      // lit_level (MEMORIZED for remembered tiles)
    int x, y, z;    // tripoint_bub_ms
    int subtile;
    int rota;
    int height_3d;  // the tile's stack height so far, as cata_tiles has it
};

// One sprite image the engine blitted for a map_sprite, after looks_like,
// fallbacks, multitile subtiles, weighted variants and animation frames are
// resolved (plan 0004). A lookup can blit several: background first.
// One frame of an animated sprite: its global sprite index, and how many
// 17 ms steps it shows (its weight in the tileset).
struct map_anim_frame {
    int index;
    int weight;
};

struct map_blit {
    int sprite;     // index into map_snapshot::sprites
    int index;      // global sprite index; see tileset_sheet
    int angle;      // clockwise degrees: 0, 90 or -90
    bool flip_h;
    bool flip_v;
    int offset_x;   // tileset pixels from the tile's top left
    int offset_y;   // (tile offset plus draw offset, before height_3d)
    int width;      // sprite size in tileset pixels
    int height;
    float pixelscale;
    int opaque_x;   // the sprite's non-transparent bounds, in its own pixels
    int opaque_y;   // before rotation and flips (all 0 if fully transparent)
    int opaque_w;
    int opaque_h;
    // An animated sprite (anim_count > 0): its frames are
    // map_snapshot::anim_frames[anim_first, anim_first + anim_count). The
    // frame showing at steady_clock time t (ms) is the one at
    // (anim_base + t / 17) % anim_total along their weights, as cata_tiles
    // picks it, so the host can run the animation on between snapshots.
    int anim_first = 0;
    int anim_count = 0;
    int anim_base = 0;
    int anim_total = 0;
};

// The light on one tile of the avatar's storey, from the engine's light map
// (plan 0004, Phase 2). Units are lightmap.h's (LIGHT_AMBIENT_LIT is 10).
struct map_light {
    float level;    // the brightest quadrant of the tile's light
    float r, g, b;  // coloured light energy here; all 0 for white light
    float source;   // the luminance of a bulk source here (fire, lava), or 0
    bool outside;   // open to the sky (map::is_outside), for rain and snow
};

// A creature the avatar sees, for the diorama's coloured bases (a ring
// under each, by attitude, readable when the cards are too small to).
struct map_creature {
    int x, y, z;    // tripoint_bub_ms
    int attitude;   // 0 the avatar, 1 hostile, 2 neutral, 3 friendly
};

struct map_snapshot {
    int center[3];  // the view centre (tripoint_bub_ms)
    int player[3];  // the avatar (tripoint_bub_ms)
    int player_abs[3];  // the avatar (tripoint_abs_ms): the map's place in the world,
                        // for what must not change as the map shifts (a tree's variant)
    int radius;     // tiles around the view centre it covers (snapshot_radius)
    uint64_t tileset_generation;  // see take_tileset
    int storeys_usec;  // the engine's time recording the storeys below
    std::vector<map_sprite> sprites;
    std::vector<map_blit> blits;
    std::vector<map_anim_frame> anim_frames;
    // The avatar's storey's light: lights_size x lights_size tiles,
    // row-major, from (lights_x, lights_y); empty if not recorded.
    float natural_light;
    int lights_x, lights_y, lights_size;
    std::vector<map_light> lights;
    std::vector<map_creature> creatures;
    // The weather, for the diorama's effects: where the wind blows from
    // (degrees clockwise from north, as the sidebar's arrow), its speed in
    // mph (before local shelter), and precipitation (precip_class: 0 none
    // to 3 heavy), falling as rain or not (snow).
    int wind_from;
    int wind_mph;
    int precip;
    bool rains;
    // The sky, for the diorama's sun and moon: each one's direction (degrees
    // clockwise from north) and height above the horizon (degrees; the
    // engine clamps a sun far below it), the moon's phase (moon_phase: 0 new,
    // 4 full) and the weather's light_modifier (sunny 2, cloudy -20, fog -40).
    float sun_from, sun_altitude;
    float moon_from, moon_altitude;
    int moon_phase;
    int light_modifier;
};

// One image of the tileset: global sprite indices [first, first + count)
// are its sprite_width x sprite_height cells, row-major.
struct tileset_sheet {
    std::string path;  // absolute PNG path
    int first;
    int count;
    int sprite_width;
    int sprite_height;
    int color_key[3];  // RGB made transparent, or -1s
};

// Static properties of a terrain or furniture type, for the host to choose
// how to model it (plan 0004, Phase 1). Bits of map_object::flags:
enum map_object_flag : uint32_t {
    MOF_WALL = 1u << 0,
    MOF_DOOR = 1u << 1,
    MOF_WINDOW = 1u << 2,
    MOF_TRANSPARENT = 1u << 3,   // doesn't block sight
    MOF_IMPASSABLE = 1u << 4,    // movecost 0
    MOF_TREE = 1u << 5,
    MOF_SHRUB = 1u << 6,         // shrubs and young trees
    MOF_NO_FLOOR = 1u << 7,      // open air: the level below shows through
    MOF_THIN_OBSTACLE = 1u << 8, // fences, railings
    MOF_SHORT = 1u << 9,
    MOF_FLAT = 1u << 10,
    MOF_FLOWER = 1u << 11,
    MOF_FLAT_SURF = 1u << 12,    // tables, counters, desks
    MOF_CONTAINER = 1u << 13,
    MOF_LOCKED = 1u << 14,       // locked doors, which lack the DOOR flag
    MOF_GOES_UP = 1u << 15,      // stairs and ladders
    MOF_GOES_DOWN = 1u << 16,
};

struct map_object {
    std::string id;
    bool furniture;
    uint32_t flags;
    int movecost;
    int coverage;  // percent
    // A field type (fd_*): only MOF_FLAT is set, for liquids and solids
    // (blood, gibs, rubble), which lie on the ground; gases and plasma
    // (smoke, fire) stand.
    bool field = false;
};

struct map_objects_info {
    uint64_t generation = 0;  // bumped each time the game data is (re)loaded
    std::vector<map_object> objects;
};

struct tileset_info {
    uint64_t generation = 0;  // bumped each time the tileset is (re)loaded
    int tile_width = 0;       // the tileset's grid in pixels
    int tile_height = 0;
    std::vector<tileset_sheet> sheets;
};

// Host side.

// The engine's main(): same arguments, same exit code. Blocks until the
// game quits. Note that several engine paths call exit() outright, which
// ends the host process too.
int run( int argc, const char *argv[] );

// The VR host's virtual gamepad (plan 0005): Touch controllers reach the
// engine as an ordinary SDL gamepad, so the controller scheme works
// unchanged. Ask for it before run(); the engine attaches it when its
// gamepad subsystem starts.
constexpr int virtual_pad_axes = 6;  // SDL_GamepadAxis order; triggers 0..32767
void enable_virtual_pad();
// Host side, any thread: the pad's state. `buttons` has bit n set for
// SDL_GamepadButton n held. Applied on the engine thread when SDL next
// updates its joysticks.
void set_virtual_pad( uint32_t buttons, const int16_t axes[virtual_pad_axes] );
// The latest rumble the game asked the virtual pad for, 0..65535 per
// motor; returns false if unchanged since the last call.
bool take_virtual_rumble( uint16_t &low, uint16_t &high );
// Engine side, from sdl_gamepad::init once SDL_INIT_GAMEPAD is up.
void attach_virtual_pad();

// Which way is "forward" for the player in VR (plan 0005): degrees
// clockwise from map north of the direction from the player's head to the
// avatar on the table. sdl_gamepad turns map-direction stick input by it,
// so pushing a stick forward means "away from me" however the table is
// turned. Host side, any thread; 0 outside VR.
void set_view_yaw( float degrees );
float view_yaw();

// Optional: called on the engine thread with each new frame (RGBA8, tightly
// packed, top row first) instead of keeping it for take_frame, so the host
// can copy it into its own buffers off its main thread. Set before run().
using frame_sink = void ( * )( void *context, const unsigned char *rgba, int width, int height );
void set_frame_sink( frame_sink sink, void *context );

// If a frame newer than `sequence` has been published, move it into `rgba` (RGBA8,
// tightly packed, top row first), update `sequence` and return true.
bool take_frame( uint64_t &sequence, std::vector<unsigned char> &rgba, int &width,
                 int &height );

// The SDL window ID to address pushed events to; 0 until the window exists.
uint32_t window_id();

// The open UIs (ui_adaptor's stack), bottom (the main game) first, as of
// the latest published frame, in its pixels (plan 0006: menus float).
struct ui_window {
    int x, y, width, height;
    bool shim;         // an app-shell adaptor, not a screen
    bool imgui;        // drawn with ImGui
    bool hides_below;  // hides the UIs beneath it
    bool visible;      // on screen (an ImGui window drew itself this pass)
};
// The sidebar (status panels and the message log), in terminal cells: its
// width on the left and on the right (one is 0) of a TERMX x TERMY grid.
// Engine thread sets it as the panels draw; the host reads it any time.
void set_sidebar( int left, int right, int columns, int rows );
void get_sidebar( int &left, int &right, int &columns, int &rows );

// The gamepad's radial menu, drawn straight onto the frame rather than as
// a UI: its panel in frame pixels (0 wide when not shown), published as one
// more window on top of the stack. Engine thread, each present.
void set_radial_window( int x, int y, int width, int height );

// If the list has changed since `sequence`, copy it into `out`, update
// `sequence` and return true.
bool take_ui_windows( uint64_t &sequence, std::vector<ui_window> &out );

// The mouse, from the host (pointing at a window, or desk mode's mouse): in
// frame pixels, buttons as SDL_BUTTON_MASK bits, wheel in notches. Any
// thread; queued in order, and the engine thread applies one per event
// pump (apply_mouse), so no press or release is lost.
void set_mouse( int x, int y, uint32_t buttons, int wheel );
// The mouse on a map tile instead (pointing at the diorama): `dx`, `dy`
// tiles from the avatar. The engine thread turns it into the tile's middle
// in the map view's pixels (cata_tiles::player_to_screen), so it is exact
// at any zoom or view offset.
void set_mouse_tile( int dx, int dy, uint32_t buttons );
// The host's pointer has left the map (the diorama's pointing ended): the
// game hears a mouse move with no tile under it, so the mouse view closes.
// Until the next tile move, mouse_away() (engine thread) is true; a move in
// pixels (a ray on a window) leaves it true too.
void set_mouse_away();
bool mouse_away();

// The VR player pointed at a map tile and asked to look at it (`dx`, `dy`
// tiles from the avatar): game::handle_action opens look-around with its
// cursor there, as if the look action had been taken and the cursor moved.
// Host thread asks (waking the play loop with a mouse move); the engine
// thread takes it.
void request_look( int dx, int dy );
bool take_look_request( int &dx, int &dy );
// A test hook (make vr-mouse "goto TYPE"): teleport the avatar to the
// closest overmap terrain whose id starts with TYPE ("house"), as the
// debug menu's teleport does; handled beside the look request.
void request_goto( const std::string &type );
bool take_goto_request( std::string &type );
// Another (make vr-mouse "time HH"): move the clock on to the next HH:00,
// as the debug menu's Change time does, for night scenes.
void request_time( int hour );
bool take_time_request( int &hour );
// Whether a look, goto or wheel action request waits, without taking it:
// game::handle_mouseview stops there, or the mouse view's loop (and
// get_player_input's, on timeouts) would keep it waiting until a key came.
bool look_pending();

// Whether the game is waiting for typed text (a string-input or
// string-editor context on top, or a focused ImGui text field), for the
// host's virtual keyboard. The engine thread sets it each event pump; the
// host reads it any time.
void set_text_wanted( bool wanted );
bool text_wanted();
// Whether a game is being played (main's do_turn loop), not the title
// screen: the VR host hides the board and the HUD outside one, since the
// last game's map and sidebar would otherwise stay up, stale.
void set_in_game( bool playing );
bool in_game();


// Wheels on the pad's shoulders, drawn by the host (ADR 0009). In play, a
// bare shoulder held opens a wheel: LB the local one (what to do at the
// aimed tile), RB the global one (every action by category). The engine
// captures the pad while one is open and sends what it does as wheel
// events; the host draws the wheel, picks, and answers with wheel_done. A
// tap is the shoulder's binding, as without wheels.
//
// Host side: ask for them before run(); without that the shoulders are as
// in the flat game.
void enable_wheels();
bool wheels_enabled();

// One action a wheel can offer: one of the action menu's (action.cpp).
struct wheel_action {
    std::string id;             // the action's id, as keybindings.json has it
    std::string name;           // its display name
    std::string category;       // the action menu's category: "look", "interact"...
    std::string category_name;  // its display name
    bool applies;               // possible now (for a tile verb: at the aim)
    bool at_tile;               // takes the aimed tile as its target
};

struct wheel_event {
    enum kind_t : int {
        opened = 0,    // a wheel opened: `aim_*` and `actions` are set
        stick = 1,     // the picking stick moved: `x`, `y`
        confirm = 2,   // A
        back = 3,      // B
        released = 4,  // the shoulder was let go: pick what is highlighted
        closed = 5,    // the engine closed it (the game left play): drop it
    };
    kind_t kind = opened;
    int serial = 0;  // which opening this belongs to, for wheel_done
    int wheel = 0;   // 0 local (LB), 1 global (RB)
    // The picking stick (whichever is pushed further and not spent): -1..1,
    // x to the right, y up, as on the pad (not turned by the view).
    float x = 0.0f;
    float y = 0.0f;
    // The aimed tile, from the avatar, turned like the left stick; 0, 0
    // with the stick at rest when the shoulder went down.
    int aim_dx = 0;
    int aim_dy = 0;
    std::vector<wheel_action> actions;
};

// Host side, any thread: the events since the last call, oldest first.
void take_wheel_events( std::vector<wheel_event> &out );
// Host side, any thread: the wheel opened as `serial` is done. `action` is
// an id from its actions to perform (at the aim, for a tile verb), or empty
// for none. Ignored if that wheel has already closed and another opened.
void wheel_done( int serial, const std::string &action );

// Engine side. The gamepad layer opens and feeds a wheel; the catalogue is
// built by game code (godot_wheels.cpp), as the wheel opens.
void push_wheel_event( wheel_event event );
std::vector<wheel_action> wheel_catalogue( int aim_dx, int aim_dy );
// Whether the action `id` takes the aimed tile as its target.
bool wheel_action_at_tile( const std::string &id );
// The aim the wheel opened as `serial` targets, for its action request.
void set_wheel_aim( int serial, int aim_dx, int aim_dy );
// Whether the host has finished the wheel opened as `serial` (and takes
// that answer). The action itself, if any, waits for the play loop:
bool take_wheel_done( int serial );
// game::handle_action takes it as if its key had been pressed, with the
// aim to use as the target. Counted by look_pending().
bool take_action_request( std::string &action, int &aim_dx, int &aim_dy );

// If the map has been drawn since `sequence`, move the newest snapshot into
// `out`, update `sequence` and return true.
bool take_map_snapshot( uint64_t &sequence, map_snapshot &out );

// The tileset the snapshots' sprite indices refer to. Returns false if it
// is unchanged since `generation`.
bool take_tileset( uint64_t &generation, tileset_info &out );

// Every terrain and furniture type's properties. Returns false if unchanged
// since `generation`.
bool take_map_objects( uint64_t &generation, map_objects_info &out );

// Engine side.

void publish_frame( SDL_Renderer *renderer );
void set_window_id( uint32_t id );
// Apply the host's next mouse command (set_mouse and co.): warp SDL's
// mouse in the engine's window (the game reads the pointer from SDL's mouse
// state, not the event) and push button and wheel events. `tile_to_pixels`
// places a set_mouse_tile (false: nowhere). Called from CheckMessages.
void apply_mouse( bool ( *tile_to_pixels )( int dx, int dy, int &x, int &y ) );

// How far around the avatar the snapshot reaches, and how many storeys below
// and above the avatar's it adds from map memory (plan 0004, Phase 2). The radius is
// the VR player's zoom: the game's zoom actions step it (zoom_view), so
// zooming out shows more of the surroundings on the same table, as the 2D
// zoom shows more on the same screen. 60 is the loaded map's reach.
int snapshot_radius();
constexpr int snapshot_levels_below = 2;
constexpr int snapshot_levels_above = 2;
// Steps in (+, a smaller radius) or out (-) through the radius levels.
// Engine thread (the zoom actions), which then redraws the map.
void zoom_view( int steps );

// Whether a map snapshot is being recorded (between begin and end).
bool recording_map();

// While one of these exists, cata_tiles records sprites without drawing
// them: for the storeys below, which the 2D view never shows.
class record_only_scope
{
    public:
        record_only_scope();
        ~record_only_scope();
        record_only_scope( const record_only_scope & ) = delete;
        record_only_scope &operator=( const record_only_scope & ) = delete;
};
bool record_only();
void note_storeys_usec( int usec );
// The light grid for the snapshot being recorded: sized size x size, zeroed,
// for cata_tiles::draw to fill.
std::vector<map_light> &record_map_lights( float natural_light, int x, int y, int size );
void record_map_weather( int wind_from, int wind_mph, int precip, bool rains );
void record_map_sky( float sun_from, float sun_altitude, float moon_from, float moon_altitude,
                     int moon_phase, int light_modifier );
// The creatures list for the snapshot being recorded, cleared, to fill.
std::vector<map_creature> &record_map_creatures();

// Around the map view's draw: record sprite lookups between begin and end,
// then publish them. A draw that recorded nothing (aborted, or nothing
// visible) publishes nothing.
void begin_map_snapshot( int center_x, int center_y, int center_z, int player_x, int player_y,
                         int player_z, int player_abs_x, int player_abs_y );
void end_map_snapshot();

// Held for the duration of each cata_tiles sprite lookup. Lookups recurse
// for fallback ids, so only the outermost one is recorded.
class sprite_lookup_scope
{
    public:
        sprite_lookup_scope();
        ~sprite_lookup_scope();
        sprite_lookup_scope( const sprite_lookup_scope & ) = delete;
        sprite_lookup_scope &operator=( const sprite_lookup_scope & ) = delete;
        // True for the outermost lookup during a map snapshot.
        bool recording() const;
};
void record_map_sprite( const std::string &id, const std::string &variant, int category,
                        int light, int x, int y, int z, int subtile, int rota, int height_3d );
// A sprite image blitted for the most recently recorded map sprite. Called
// from cata_tiles::draw_sprite_at; ignored outside a recorded lookup.
void record_map_blit( int index, int angle, bool flip_h, bool flip_v, int offset_x,
                      int offset_y, int width, int height, float pixelscale, int opaque_x, int opaque_y,
                      int opaque_w, int opaque_h );
// The animation of the blit just recorded (see map_blit::anim_*).
void record_map_blit_animation( const std::vector<map_anim_frame> &frames, int base, int total );
// Called at each map draw with the tileset in use; `key` identifies it, so
// the sheets are only copied when it changes.
bool tileset_changed( const void *key );
void publish_tileset( tileset_info info );
// Likewise for the game data: called at each map draw with a key for the
// loaded terrain and furniture, published only when it changes.
bool map_objects_changed( uint64_t key );
void publish_map_objects( map_objects_info info );

} // namespace godot_host

#endif // CATA_SRC_GODOT_HOST_H
