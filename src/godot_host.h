#pragma once
#ifndef CATA_SRC_GODOT_HOST_H
#define CATA_SRC_GODOT_HOST_H

// Hosting the engine inside another program (CDDA_GODOT builds only): the
// Godot host of Cataclysm: Dark Forest, the cataclysm-df repo. The host calls run() on a
// thread it owns, in place of main(). The engine renders to SDL's offscreen
// video driver and publishes every presented frame for the host to show;
// the host feeds input by pushing ordinary SDL events at window_id(), so
// the engine's own input path is unchanged.
//
// Deliberately plain types only, so the host never includes engine headers
// or depends on the engine's build defines.

#include <array>
#include <chrono>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace catacurses
{
class window;
} // namespace catacurses

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

// A creature the avatar sees, for the 3D view's coloured bases (a ring
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
    // The weather, for the 3D view's effects: where the wind blows from
    // (degrees clockwise from north, as the sidebar's arrow), its speed in
    // mph (before local shelter), and precipitation (precip_class: 0 none
    // to 3 heavy), falling as rain or not (snow).
    int wind_from;
    int wind_mph;
    int precip;
    bool rains;
    // The sky, for the 3D view's sun and moon: each one's direction (degrees
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
// Whether the virtual pad is the host's touch pad (iOS, --touchpad), not
// the VR or desk host's Touch controllers (ADR 0014): its X then always
// opens the action list. Host side, any thread.
void set_touch_pad( bool on );
bool touch_pad();
// The pad is the host's (the hotbar focused from d-pad down, its picker,
// Dark Forest's settings panel): the pad layer swallows every button and
// stick until the host lets go, reading the pad itself meanwhile.
void set_pad_captured( bool on );
bool pad_captured();
// Start held in play (the pad layer's start_hold_ms): the host opens Dark
// Forest's settings panel. A tap is still the game's menu.
void request_df_panel();
bool take_df_panel_request();
// The overmap, for the touch pad's map buttons: 0 closed, 1 open, 2 open
// with a route plotted to the cursor (the Route button reads Travel;
// godot_actions::publish_overmap_state).
// The right stick turns Dark Forest's camera in play (the user's call):
// the pad layer sends none of its tilts to the game there, and R3 is a tap
// (look-around) or a hold (the camera's zoom and reset, the host's).
void set_stick_camera( bool on );
bool stick_camera();
void set_overmap_state( int state );
int overmap_state();
// One of the open screen's own actions by id (the touch pad's map
// buttons: CHOOSE_DESTINATION, CENTER), taken by that screen's loop in
// place of its next input. The overmap takes it.
void request_screen_action( const std::string &id );
std::string take_screen_action();
// A slide on the open map (the touch pad, 2026-11-11), in the engine's
// frame pixels, added up until the map's loop takes it
// (godot_actions::overmap_drag): the map follows the finger.
void request_map_drag( float dx, float dy );
std::pair<float, float> take_map_drag();
// The map's zoom (uistate's overmap tileset zoom: 4 farthest, doubling to
// 64 closest), for the touch pad's RADIUS notches on the map.
int overmap_zoom();
// Engine side: whether the joystick `id` is the virtual pad.
bool is_virtual_pad( uint32_t id );

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
// The mouse on a map tile instead (pointing at the 3D view): `dx`, `dy`
// tiles from the avatar. The engine thread turns it into the tile's middle
// in the map view's pixels (cata_tiles::player_to_screen), so it is exact
// at any zoom or view offset.
void set_mouse_tile( int dx, int dy, uint32_t buttons );
// The host's pointer has left the map (the 3D view's pointing ended): the
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
// The host clicked a tile on its 3D board (tap-to-move, a mouse click on
// the board), `dx`, `dy` tiles from the avatar: game::handle_action takes
// it as a left click on that tile (a route preview, then its walk),
// without the round trip through a mouse position in the map window,
// which in portrait is a row tall under the sidebar band.
void request_click( int dx, int dy );
bool take_click_request( int &dx, int &dy );
// Look-around's cursor, for Dark Forest's touch drag (the map slides under
// a cursor fixed at the centre). The engine thread reports each pass of
// game::look_around whether it runs and where its cursor is, `dx`, `dy`
// tiles from the avatar; the host reads it any time. The host moves the
// cursor (and the view centre with it) by request, taken at the end of a
// pass.
void set_look_cursor( bool looking, int dx, int dy );
bool look_cursor( int &dx, int &dy );
void request_look_cursor( int dx, int dy );
bool take_look_cursor_request( int &dx, int &dy );
// The touch pad's look-around buttons (ADR 0014): leave look-around and
// plan a route to the cursor's tile (go_here: the route waits as a tapped
// one does, for Confirm), or leave it centred on the avatar, the offset
// cleared (recenter). Host side, any thread; look_around takes it at the
// top of its next pass (engine side: none for no request).
enum class look_exit : int { none = 0, go_here = 1, recenter = 2 };
void request_look_exit( look_exit how );
look_exit take_look_exit_request();
// Go here leaves look-around with the view where it was looking (the
// user's call): the view to go back to (the one before look-around) is
// held, armed once Go here's click is taken, and given back by
// take_settled_view once no route preview is pending (walked, cancelled,
// or never made). Engine thread only.
void hold_view( int dx, int dy, int dz );
void arm_held_view();
bool take_settled_view( bool preview_pending, int &dx, int &dy, int &dz );
// The host wants the terminal at `columns` x `rows` cells (Dark Forest on a
// phone turned between landscape and portrait, ADR 0012). Host thread
// asks (waking the event pump); the engine thread takes it in
// CheckMessages and resizes its window, as a desktop window resize does.
void request_terminal( int columns, int rows );
bool take_terminal_request( int &columns, int &rows );
// Wake the engine's event pump (a mouse nudge), for work it does on its
// next pass (the redraw after a resize).
void wake();
// The sidebar as a band of `rows` across the top of the terminal (Dark
// Forest in portrait), 0 for the usual column: the map below it, the
// panels flowed into columns of the narrow layout (game::draw_panels).
// Set by the host before the terminal request it goes with.
void set_sidebar_band( int rows );
int sidebar_band();
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
// Whether the game is in its menu plane (a menu, a prompt, look-around or
// aiming on top of play: the pad layer's is_in_menu, the UI stack deeper
// than the game), for the host's Confirm, Cancel and Actions buttons. The
// engine thread sets it each event pump; the host reads it any time.
void set_in_menu( bool menu );
bool in_menu();
// Whether the screen waiting for input pages (its input context has
// PAGE_UP: the menus, the inventory, crafting, the log, help...), for the
// touch pad's PAGE strip (2026-11-11). Set each event pump.
void set_menu_pages( bool pages );
bool menu_pages();
// The input category of the screen waiting for input ("INVENTORY",
// "BODY_STATUS_UI", "YESNO"...), set each event pump: the hotbar decides
// from it whether it can switch away from the open screen (2026-11-11).
void set_waiting_category( const std::string &category );
std::string waiting_category();
// A conversation is open (avatar::talk_to, trade included: its panes are
// inventory screens), for the same decision.
struct talk_scope {
    talk_scope();
    ~talk_scope();
    talk_scope( const talk_scope & ) = delete;
    talk_scope &operator=( const talk_scope & ) = delete;
};
bool talking();
// Whether aiming (target_ui) is up: a map screen, where the host's taps
// move the target cursor; other screens of the menu plane take a tap on
// bare screen as Escape. The engine thread sets it; the host reads it.
void set_aiming( bool aiming );
bool aiming();
// The game's safe mode (game::safe_mode: 0 off, 1 on, 2 stopped by a newly
// spotted hostile; and ours, 3: on, having just refused to attack a neutral
// creature, note_safe_refusal), for the host's touch Safe mode control. The engine
// thread sets it each event pump; the host reads it any time.
void set_safe_mode( int mode );
// Safe mode refused to attack a neutral creature (avatar_action.cpp): until
// the next turn, with safe mode still on, safe_mode() reads 3, and the
// touch pad and the context cues offer Safe off (2026-11-11).
void note_safe_refusal();
int safe_refusal_turn();
// The avatar's chosen movement mode id (walk, run, crouch, prone, or a
// mod's; the desired one, which the next step takes up), empty out of play, for the touch pad's figure on its centre. The engine
// thread sets it each event pump; the host reads it any time.
// `pending`: chosen but not yet taken up (the current mode differs).
void set_move_mode( const std::string &id, bool pending );
std::string move_mode();
bool move_mode_pending();
int safe_mode();
// Whether the avatar's activity (crafting, waiting, reading...) or its
// auto-move would stop on the pause action, as handle_key_blocking_activity
// asks (an unfinished activity that takes a keypress, or a destination),
// for the touch pad's Cancel during one: the wait popup isn't a menu, so
// the pad showed none (2026-11-11). Asleep is no activity, so not here.
// The engine thread sets it each event pump; the host reads it any time.
void set_activity_stoppable( bool stoppable );
bool activity_stoppable();
// Which of the LT wheels have anything on the screen waiting for input
// (bit 0 the left, bit 1 the right), for the touch pad: a finger on the
// side of an empty wheel opens the other (ADR 0014, a stopgap until LT
// leaves the touch layout). The engine thread sets it each event pump.
void set_radial_sides( int sides );
int radial_sides();
// Whether a click-to-move route preview is pending (a tap on the board),
// for the touch pad's Confirm (walk it: the pad's A) and Cancel (drop it:
// the pad's B). The play loop sets it before it waits for input.
void set_route_preview( bool pending );
bool route_preview();
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
    int wheel = 0;   // 0 the game's (LB, ADR 0017), 1 global (RB)
    int stick_index = -1;  // stick events: which stick (0 left, 1 right)
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
// The game's wheel (LB, ADR 0017): the waiting input context's radial sets,
// sixteen slots ("radial:L:0".."radial:L:7", then R; clockwise from north),
// each its bound action's name, empty where unbound. Engine thread.
std::vector<wheel_action> radial_catalogue();
// Each event pump (sdltiles.cpp CheckMessages), engine thread: the host's
// mouse applied, and what the host reads published (text wanted, the menu
// plane, safe mode, the movement mode, which radial sides the waiting
// context fills). Game code, in godot_pump.cpp.
void pump_engine_state();
// The pad layer: the slot the host answered the game's wheel with (a
// "radial:L:n" / "radial:R:n" answer to wheel_done keeps it here instead of
// handing it to the play loop), as side 0/1 and index 0..7; false if none.
bool take_wheel_radial( int &side, int &index );
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

// The hotbar (ADR 0013), drawn by the host. Host side, any thread: do
// action `id` as if its key had been pressed, with no aim (a directional
// verb asks), taken by the play loop as a wheel's pick is. Only sent in
// play with no menu open; one sent otherwise waits for the play loop.
void request_action( const std::string &id );
// Host side, any thread: ask for the action catalogue (the wheels', aimed
// nowhere), for the bar's labels and its picker; the play loop builds it
// and the host takes it with take_catalogue. Counted by look_pending().
void request_catalogue();
bool take_catalogue( std::vector<wheel_action> &out );
// Engine side, the play loop: whether the host asked, and its answer.
bool take_catalogue_request();
void publish_catalogue( std::vector<wheel_action> catalogue );
// The host's own action "stairs" (ADR 0013), in the catalogue beside go
// up and go down: whichever of the two applies where the avatar stands.
// Where both do (a ladder going both ways, deep water) or neither does by
// the terrain (climbing, levitating, or nothing), a menu asks which. The
// play loop calls it on the engine thread: "LEVEL_UP", "LEVEL_DOWN", or
// "" if the menu was cancelled.
extern const char *const stairs_action_id;
std::string stairs_action();

// The touch pad's context row (ADR 0014): the actions that apply where the
// avatar stands, in a fixed priority order (up, down, open, close, pick up,
// talk, drive, butcher, examine). The play loop builds them before it waits
// for input (context_actions_here, game code) and publishes them; the host
// takes them when they change.
std::vector<std::string> context_actions_here();
void publish_context_actions( std::vector<std::string> actions );
bool take_context_actions( std::vector<std::string> &out );
// Dark Forest's context cues (desktop play): how each context action, and
// safe mode's three for a stop (ignore_enemy, safemode, whitelist_enemy),
// is bound in play (the default mode's context): its pad bindings by their
// portable names ("A", "ALT_X", "L_RADIAL_SW"), its keys as the game shows
// them. Built with the context actions (engine thread: the bindings live
// there), published when they change.
struct action_bindings {
    std::vector<std::string> pad;
    std::vector<std::string> keys;
    bool operator==( const action_bindings &o ) const {
        return pad == o.pad && keys == o.keys;
    }
};
using bindings_map = std::map<std::string, action_bindings>;
bindings_map context_bindings_here( const std::vector<std::string> &actions );
void publish_context_bindings( bindings_map bindings );
bool take_context_bindings( bindings_map &out );

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
// Storeys: every one, down to the world's bottom and up to its top (the
// user's call, 2026-11-07; it was 2 each way, which cut tall buildings
// and deep basements off): from any storey the farthest is the world's
// whole height away, and the passes stop at its ends (OVERMAP_DEPTH,
// OVERMAP_HEIGHT in map_scale_constants.h, both 10).
int snapshot_radius();
constexpr int snapshot_levels_below = 20;  // OVERMAP_DEPTH + OVERMAP_HEIGHT
constexpr int snapshot_levels_above = 20;
// Steps in (+, a smaller radius) or out (-) through the radius levels.
// Engine thread (the zoom actions), which then redraws the map.
void zoom_view( int steps );
// The radius's level (0 the smallest) and how many levels there are, for
// the host's view radius slider. Any thread.
int snapshot_radius_level();
int snapshot_radius_level_count();
// Held walking the game falls behind (ADR 0016): the 3D view's snapshot is
// skipped while the next step is already due, at most 250 ms; a skipped
// one is owed until the next recording or, with no input for a moment (the
// move let go), a flush. Engine thread.
// owe: whether this redraw skipped. take_flush: once after a request.
void set_snapshot_owed( bool owed );
bool snapshot_owed();
void request_snapshot_flush();
bool take_snapshot_flush();
// The host's 2D view: the storeys above aren't drawn there, so the
// snapshot skips recording them. A change asks for a fresh snapshot (taken
// by the play loop's idle timeout, as an owed one is). Set from the host;
// read on the engine thread.
// The glyph style for the host's own button glyphs (gamepad::
// host_glyph_style), kept each frame by the engine.
void set_glyph_style( int style );
int glyph_style();
void set_flat_view( bool flat );
bool flat_view();
bool take_flat_view_changed();
// The touch pad's view radius slider: `steps` in (+) or out (-), added up
// until the engine takes them, in the play loop or in look-around (as the
// zoom actions are), so a quick drag loses none. Host side, any thread;
// counted by look_pending().
void request_zoom_view( int steps );
// Engine side: the steps waiting, taken (0 for none).
int take_zoom_view_request();

// Whether a map snapshot is being recorded (between begin and end).
bool recording_map();

// While one of these exists (`on`), cata_tiles records sprites without
// drawing them: for the storeys below, which the 2D view never shows, and
// the whole map while the 3D view shows it (map_unseen). They nest.
class record_only_scope
{
    public:
        explicit record_only_scope( bool on = true );
        ~record_only_scope();
        record_only_scope( const record_only_scope & ) = delete;
        record_only_scope &operator=( const record_only_scope & ) = delete;
    private:
        bool was;
};
bool record_only();
// Whether the engine's 2D map goes unseen: a snapshot is being recorded for
// the host's board, which both its views draw (its 2D view is the board
// seen straight down, not this frame). Then cata_tiles::draw
// records the map without drawing it: the software renderer scaled every
// sprite into a frame nobody saw, seconds a turn on the Deck in open grass
// (2026-11-10). The host's windows and sidebar are drawn as ever.
bool map_unseen();
// The host shows its board (its play, desk or VR views), never the engine's
// 2D map: set by the host. The map is then recorded, never drawn, even
// when a redraw skips its snapshot (held walking), and an unchanged map may
// skip its pass (sdltiles.cpp's map_unchanged).
void set_board_shown( bool shown );
bool board_shown();
// This redraw's map pass is skipped (the map is unchanged since the last
// full one): cata_tiles::draw returns at once. Engine thread.
void set_map_draw_skipped( bool skipped );
bool map_draw_skipped();

// What the engine's redraws cost, for the host's perf line (2026-11-10):
// each timer adds a scope's time on the engine thread; the host takes and
// resets the totals (take_redraw_stats). draw is the whole of game::draw
// (the map pass, the other storeys and the sidebar are inside it); map is
// cata_tiles::draw, storeys the other storeys' recording within it; panels
// the sidebar, minimap the pixel minimap within it; frame publish_frame's
// read-back for the host.
enum class timer : int { draw, map, storeys, panels, minimap, frame, count };
class timed_scope
{
    public:
        explicit timed_scope( timer which );
        ~timed_scope();
        timed_scope( const timed_scope & ) = delete;
        timed_scope &operator=( const timed_scope & ) = delete;
    private:
        timer which;
        std::chrono::steady_clock::time_point start;
};
// The game turn the play loop waits at (engine thread), for turns passed.
void note_turn( int turn );
struct redraw_stats {
    int draws = 0;  // game::draw calls
    int draw_max_usec = 0;
    std::array<int64_t, static_cast<int>( timer::count )> usec = {};
    int turns = 0;  // game turns passed
};
redraw_stats take_redraw_stats();
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

// Native menus (plan 0017): with the host's switch on, a uilist the engine
// runs publishes its state and isn't drawn; the host draws it and sends
// requests back, taken in the list's loop as the matching keys are. Text
// is BBCode ([color=#rrggbb]), the engine's colour tags converted.
struct menu_entry {
    std::string text;
    std::string column;   // the second column
    std::string desc;
    std::string hotkey;   // its key, as the engine names it ("" none)
    bool enabled = true;
};
struct menu_state {
    uint32_t serial = 0;  // bumped on each change
    std::string title;
    std::string header;
    std::string footer;   // shown in place of the description when set
    std::vector<menu_entry> entries;  // in the list's (filtered) order
    int selected = 0;     // into entries
    std::vector<std::string> tabs;
    int tab = 0;
    bool desc = false;    // the list shows descriptions
    bool cancel = true;   // cancelling is allowed
    bool filter = false;  // filtering is allowed
    std::string filter_text;
};
enum class menu_request_kind : int { select, confirm, cancel, tab, filter };
struct menu_request {
    menu_request_kind kind = menu_request_kind::select;
    int value = 0;        // an entry (select, confirm) or a tab
    std::string text;     // the filter
};
// Host: the switch; the state (false when no bridged list is up); a
// request (wakes the engine).
void set_native_menus( bool on );
bool native_menus();
bool get_menu( menu_state &out );
void request_menu( const menu_request &r );
// Engine thread: a list's state, its owner the list; cleared by its owner.
void publish_menu( const void *owner, menu_state state );
void clear_menu( const void *owner );
bool take_menu_request( menu_request &out );
// The engine's colour tags as BBCode ([color=#rrggbb]), brackets escaped.
std::string bbcode( const std::string &tagged );

// The native sidebar (plan 0020): with its switch on, the engine's sidebar
// panels (the portrait band, the landscape column) are not shown in the
// frame but published for the host to draw: each panel's id, name, cell
// rectangle in the terminal and rows as BBCode. game::draw_panels starts
// a pass (sidebar_begin), records each shown panel's place
// (sidebar_place), captures those drawn this pass from their windows
// (sidebar_capture, in godot_sidebar.cpp, the palette through palette_rgb; the window is then
// blanked), and publishes (sidebar_end). A panel not drawn this pass keeps
// its last rows.
struct sidebar_panel {
    std::string id;
    std::string name;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    std::vector<std::string> rows;
};
void set_native_sidebar( bool on );
bool native_sidebar();
// The host says the engine's pixel minimap goes unseen (its own sidebar
// drawn, no peek at the engine's, not VR): cata_tiles::draw_minimap then
// draws nothing (2026-11-10: a few ms a redraw on the Deck, never shown).
void set_minimap_unseen( bool unseen );
bool minimap_unseen();
// Whether the switch changed since last asked (the panels then redraw in
// full, into the frame or to the host).
bool take_sidebar_switched();
// Whether the game owes a redraw for the switch (the play loop's idle
// timeout takes it), so the panels show at once, not after a turn.
bool take_sidebar_redraw();
// The log's zoom for the host's log box, a percentage (100 as the panel):
// the log drawn that much narrower, so the host's larger font fits the
// box with the messages wrapped to it. A change redraws the panels.
void set_log_zoom( int percent );
int log_zoom();
void sidebar_begin();
void sidebar_place( const std::string &id, const std::string &name, int x, int y, int width,
                    int height );
// The palette's colour (r, g, b) for a curses colour index; sdltiles.cpp
// (godot_sidebar.cpp's capture uses it).
std::array<uint8_t, 3> palette_rgb( int color );
void sidebar_capture( const std::string &id, const catacurses::window &w );
void sidebar_rows( const std::string &id, std::vector<std::string> rows );
void sidebar_end( int columns, int rows );
// The host: the panels in drawing order, the terminal's size; false when
// nothing changed since `serial`, which it updates.
bool get_sidebar_panels( std::vector<sidebar_panel> &out, int &columns, int &rows, int &serial );

// Native dialogs (plan 0018), the same way: a query popup (a message with
// buttons, or "any key") or a text or number input, drawn by the host while
// its switch (native_menus) is on. Requests: choose an option, cancel,
// dismiss (any key), submit text.
struct dialog_state {
    uint32_t serial = 0;
    int kind = 0;            // 0 a popup, 1 text input, 2 number input
    std::string title;
    std::string message;     // BBCode
    std::string label;       // an input's label, BBCode
    std::string value;       // an input's starting text
    std::vector<std::string> options;  // a popup's buttons, their names
    std::vector<std::string> option_glyphs;  // the pad button answering each ("A", "B", "")
    bool cancel = false;     // may be cancelled
    bool anykey = false;     // a popup any key dismisses
    int max_length = 0;      // an input's, 0 none
};
enum class dialog_request_kind : int { choose, cancel, dismiss, submit };
struct dialog_request {
    dialog_request_kind kind = dialog_request_kind::cancel;
    int value = 0;           // the option chosen
    std::string text;        // the text submitted
};
bool get_dialog( dialog_state &out );
void request_dialog( const dialog_request &r );
void publish_dialog( const void *owner, dialog_state state );
void clear_dialog( const void *owner );
bool take_dialog_request( dialog_request &out );

} // namespace godot_host

#endif // CATA_SRC_GODOT_HOST_H
