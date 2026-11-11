#if defined(CDDA_GODOT)

// cata_tiles::draw's additions for Dark Forest (godot_tiles.h). Moved here
// from cata_tiles.cpp unchanged (2026-11-09, ADR 0018), where upstream's
// lighting rework kept conflicting with them.

#include <algorithm>
#include <array>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "avatar.h"
#include "calendar.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "cata_utility.h"
#include "coordinates.h"
#include "creature.h"
#include "debug.h"
#include "field_type.h"
#include "game.h"
#include "godot_host.h"
#include "godot_tiles.h"
#include "init.h"
#include "item_factory.h"
#include "itype.h"
#include "level_cache.h"
#include "lightmap.h"
#include "map.h"
#include "map_memory.h"
#include "map_scale_constants.h"
#include "mapdata.h"
#include "mod_manager.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "point.h"
#include "string_id.h"
#include "submap.h"
#include "trap.h"
#include "type_id.h"
#include "units.h"
#include "veh_type.h"
#include "weather.h"
#include "weather_type.h"
#include "worldfactory.h"

static const std::string empty_string;

bool godot_tiles::animating( const cata_tiles &ct )
{
    return ct.do_draw_bullet || ct.do_draw_explosion || ct.do_draw_custom_explosion ||
           ct.do_draw_hit || ct.do_draw_async_anim;
}

void godot_tiles::drop_one_shots( cata_tiles &ct )
{
    ct.void_line();
    ct.void_cursor();
    ct.void_highlight();
    ct.void_weather();
    ct.void_sct();
    ct.void_zones();
}

void godot_tiles::publish_tileset_and_objects( cata_tiles &ct )
{
    // The sheets behind the snapshot's sprite indices, for the VR host.
    if( godot_host::tileset_changed( ct.tileset_ptr.get() ) ) {
        godot_host::tileset_info info;
        info.tile_width = ct.tileset_ptr->get_tile_width();
        info.tile_height = ct.tileset_ptr->get_tile_height();
        for( const atlas_replay_descriptor &d : ct.tileset_ptr->get_atlas_descriptors() ) {
            info.sheets.push_back( { d.image_path_u8, d.atlas_offset, d.expected_tilecount,
                                     d.sprite_width, d.sprite_height, { d.color_key_r, d.color_key_g, d.color_key_b } } );
        }
        godot_host::publish_tileset( std::move( info ) );
    }
    // Terrain and furniture properties, for its modelling rules. The key
    // changes when a world with different data is loaded.
    if( godot_host::map_objects_changed( ( static_cast<uint64_t>( ter_t::count() ) << 32 ) ^
                                         furn_t::count() ^ reinterpret_cast<uintptr_t>( &ter_id( 0 ).obj() ) ) ) {
        // Terrain is impassable at movecost 0; furniture's movecost is a
        // modifier (move_cost_mod), impassable when negative.
        const auto props = []( const map_data_common_t &t, bool furniture ) {
            uint32_t f = 0;
            const auto set = [&]( ter_furn_flag flag, uint32_t bit ) {
                if( t.has_flag( flag ) ) {
                    f |= bit;
                }
            };
            set( ter_furn_flag::TFLAG_WALL, godot_host::MOF_WALL );
            set( ter_furn_flag::TFLAG_DOOR, godot_host::MOF_DOOR );
            set( ter_furn_flag::TFLAG_WINDOW, godot_host::MOF_WINDOW );
            set( ter_furn_flag::TFLAG_TRANSPARENT, godot_host::MOF_TRANSPARENT );
            set( ter_furn_flag::TFLAG_TREE, godot_host::MOF_TREE );
            set( ter_furn_flag::TFLAG_SHRUB, godot_host::MOF_SHRUB );
            set( ter_furn_flag::TFLAG_YOUNG, godot_host::MOF_SHRUB );
            set( ter_furn_flag::TFLAG_NO_FLOOR, godot_host::MOF_NO_FLOOR );
            set( ter_furn_flag::TFLAG_THIN_OBSTACLE, godot_host::MOF_THIN_OBSTACLE );
            set( ter_furn_flag::TFLAG_SHORT, godot_host::MOF_SHORT );
            set( ter_furn_flag::TFLAG_FLAT, godot_host::MOF_FLAT );
            set( ter_furn_flag::TFLAG_FLOWER, godot_host::MOF_FLOWER );
            set( ter_furn_flag::TFLAG_FLAT_SURF, godot_host::MOF_FLAT_SURF );
            set( ter_furn_flag::TFLAG_CONTAINER, godot_host::MOF_CONTAINER );
            set( ter_furn_flag::TFLAG_LOCKED, godot_host::MOF_LOCKED );
            set( ter_furn_flag::TFLAG_GOES_UP, godot_host::MOF_GOES_UP );
            set( ter_furn_flag::TFLAG_GOES_DOWN, godot_host::MOF_GOES_DOWN );
            if( furniture ? t.movecost < 0 : t.movecost == 0 ) {
                f |= godot_host::MOF_IMPASSABLE;
            }
            return f;
        };
        godot_host::map_objects_info objects;
        for( size_t i = 0; i < ter_t::count(); ++i ) {
            const ter_t &t = ter_id( static_cast<int>( i ) ).obj();
            objects.objects.push_back( { t.id.str(), false, props( t, false ), t.movecost, t.coverage } );
        }
        for( size_t i = 0; i < furn_t::count(); ++i ) {
            const furn_t &f = furn_id( static_cast<int>( i ) ).obj();
            objects.objects.push_back( { f.id.str(), true, props( f, true ), f.movecost, f.coverage } );
        }
        for( const field_type &ft : field_types::get_all() ) {
            const bool flat = ft.phase == phase_id::LIQUID || ft.phase == phase_id::SOLID;
            objects.objects.push_back( { ft.id.str(), false, flat ? godot_host::MOF_FLAT : 0u, 0, 0, true } );
        }
        godot_host::publish_map_objects( std::move( objects ) );
    }
}

void godot_tiles::record_extras( cata_tiles &ct, map &here, const tripoint_bub_ms &center,
                                 const tripoint_bub_ms &you_pos )
{
    // The storeys below the avatar's, for the VR host's 3D view: what the
    // avatar remembers of them (the 2D view never shows them), recorded by
    // the same layer functions in their memory mode, without drawing.
    if( godot_host::recording_map() ) {
        const int r = godot_host::snapshot_radius();
        // The square the snapshot covers: about the view's centre, as the
        // board is (the avatar's storey, though: look-around's cursor can
        // change storeys, the board doesn't).
        const point mid = center.xy().raw();
        {
            const weather_manager &weather = get_weather();
            const weather_type &type = weather.weather_id.obj();
            godot_host::record_map_weather( weather.winddirection, weather.windspeed,
                                            static_cast<int>( type.precip ), type.rains );
            // The engine's azimuth runs from the south, positive to the
            // west; the host wants compass bearings. The moon trails the sun
            // by its phase (new with the sun, full opposite it): where the
            // sun was that long ago is a fair stand-in for where it is.
            const time_point now = calendar::turn;
            const int phase = static_cast<int>( get_moon_phase( now ) );
            const auto sun = sun_azimuth_altitude( now );
            const auto moon = sun_azimuth_altitude( now - time_duration::from_seconds( phase * 24 * 3600 / 8 ) );
            godot_host::record_map_sky( static_cast<float>( units::to_degrees( sun.first ) + 180.0 ),
                                        static_cast<float>( units::to_degrees( sun.second ) ),
                                        static_cast<float>( units::to_degrees( moon.first ) + 180.0 ),
                                        static_cast<float>( units::to_degrees( moon.second ) ),
                                        phase, type.light_modifier );
        }
        {
            // Everyone the avatar sees within the snapshot, by attitude
            // towards it, for the 3D view's bases.
            avatar &you = get_avatar();
            std::vector<godot_host::map_creature> &creatures = godot_host::record_map_creatures();
            for( Creature &c : g->all_creatures() ) {
                const tripoint_bub_ms p = c.pos_bub();
                if( std::abs( p.x() - mid.x ) > r || std::abs( p.y() - mid.y ) > r ||
                    ( !c.is_avatar() && !you.sees( here, c ) ) ) {
                    continue;
                }
                int attitude = 0;
                if( !c.is_avatar() ) {
                    switch( c.attitude_to( you ) ) {
                        case Creature::Attitude::HOSTILE:
                            attitude = 1;
                            break;
                        case Creature::Attitude::FRIENDLY:
                            attitude = 3;
                            break;
                        default:
                            attitude = 2;
                            break;
                    }
                }
                creatures.push_back( { p.x(), p.y(), p.z(), attitude } );
            }
        }
        {
            // The light on the avatar's storey, for lighting the 3D view.
            const level_cache &cache = here.access_cache( you_pos.z() );
            const int size = 2 * r + 1;
            std::vector<godot_host::map_light> &lights = godot_host::record_map_lights(
                        cache.natural_light_level_cache, mid.x - r, mid.y - r, size );
            for( int y = std::max( 0, mid.y - r ); y <= std::min( MAPSIZE_Y - 1, mid.y + r ); ++y ) {
                for( int x = std::max( 0, mid.x - r ); x <= std::min( MAPSIZE_X - 1, mid.x + r ); ++x ) {
                    const light_color_rgb &color = cache.light_color_cache[x][y];
                    lights[( y - mid.y + r ) * size + x - mid.x + r] = {
                        cache.lm[x][y].max(), color.r, color.g, color.b,
                        cache.light_source_buffer[x][y].luminance,
                        here.is_outside( tripoint_bub_ms( x, y, you_pos.z() ) )
                    };
                }
            }
        }
        const auto storeys_start = std::chrono::steady_clock::now();
        const godot_host::timed_scope storeys_timed( godot_host::timer::storeys );
        godot_host::record_only_scope record_only;
        const std::array<bool, 5> unseen = { true, true, true, true, true };
        // Every storey, the world's whole height (godot_host.h).
        static_assert( godot_host::snapshot_levels_below >= OVERMAP_DEPTH + OVERMAP_HEIGHT &&
                       godot_host::snapshot_levels_above >= OVERMAP_DEPTH + OVERMAP_HEIGHT );
        // The square around the view's centre, a submap at a time: most
        // of the other storeys' submaps hold nothing to record (unexplored
        // below, open air above), told by one look at each (2026-11-10:
        // every tile of 20 storeys each way was 5-7 ms a redraw on the
        // Deck).
        const int x0 = std::max( 0, mid.x - r );
        const int x1 = std::min( MAPSIZE_X - 1, mid.x + r );
        const int y0 = std::max( 0, mid.y - r );
        const int y1 = std::min( MAPSIZE_Y - 1, mid.y + r );
        const auto blocks = [&]( const auto & visit ) {
            for( int by = y0 - y0 % SEEY; by <= y1; by += SEEY ) {
                for( int bx = x0 - x0 % SEEX; bx <= x1; bx += SEEX ) {
                    visit( std::max( bx, x0 ), std::min( bx + SEEX - 1, x1 ), std::max( by, y0 ),
                           std::min( by + SEEY - 1, y1 ), bx, by );
                }
            }
        };
        // Below the avatar: what it remembers there.
        for( int dz = 1; dz <= godot_host::snapshot_levels_below; ++dz ) {
            const int z = you_pos.z() - dz;
            if( z < -OVERMAP_DEPTH ) {
                break;
            }
            blocks( [&]( int bx0, int bx1, int by0, int by1, int bx, int by ) {
                // A submap the avatar remembers nothing of answers with the
                // shared default tile (mm_submap::get_tile).
                if( &get_avatar().get_memorized_tile( here.get_abs( tripoint_bub_ms( bx, by, z ) ) ) ==
                    &mm_submap::default_tile ) {
                    return;
                }
            for( int y = by0; y <= by1; ++y ) {
                for( int x = bx0; x <= bx1; ++x ) {
                    const tripoint_bub_ms p( x, y, z );
                    // Most of these storeys' tiles the avatar never saw: one
                    // memory lookup, not four layer calls finding nothing
                    // (each step paid them on every storey to the world's
                    // bottom).
                    const memorized_tile &mt = get_avatar().get_memorized_tile( here.get_abs( p ) );
                    if( mt.get_ter_id().empty() && mt.get_dec_id().empty() ) {
                        continue;
                    }
                    int height_3d = 0;
                    ct.draw_terrain( p, lit_level::MEMORIZED, height_3d, unseen, false );
                    ct.draw_furniture( p, lit_level::MEMORIZED, height_3d, unseen, false );
                    ct.draw_trap( p, lit_level::MEMORIZED, height_3d, unseen, false );
                    ct.draw_vpart_no_roof( p, lit_level::MEMORIZED, height_3d, unseen, false );
                }
            }
            } );
        }
        // Above it: the terrain itself (upper walls and floors, roofs), as
        // a building's shell is seen from outside; nothing on it, and never
        // memorized (draw_terrain's visible path would memorize). From the
        // ground the avatar sees almost none of it, so memory would show
        // nothing. The host keeps only what stands on something it shows,
        // and cuts away what would hide the avatar.
        {
            const lit_level above_ll = here.access_cache( you_pos.z() ).natural_light_level_cache >=
                                       LIGHT_AMBIENT_LIT ? lit_level::LIT : lit_level::LOW;
            const std::array<bool, 5> seen = { false, false, false, false, false };
            // None in the host's 2D view, which doesn't draw them.
            const int levels_above = godot_host::flat_view() ? 0 : godot_host::snapshot_levels_above;
            for( int dz = 1; dz <= levels_above; ++dz ) {
                const int z = you_pos.z() + dz;
                if( z > OVERMAP_HEIGHT ) {
                    break;
                }
                blocks( [&]( int bx0, int bx1, int by0, int by1, int bx, int by ) {
                // A submap of one terrain (open air, most of them) with no
                // floor holds nothing to show.
                const submap *sm = here.get_submap_at( tripoint_bub_ms( bx, by, z ) );
                if( sm != nullptr && sm->is_uniform() ) {
                    const ter_id ut = sm->get_ter( point_sm_ms() );
                    if( !ut || ut.obj().has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) ) {
                        return;
                    }
                }
                for( int y = by0; y <= by1; ++y ) {
                    for( int x = bx0; x <= bx1; ++x ) {
                        const tripoint_bub_ms p( x, y, z );
                        const ter_id &t = here.ter( p );
                        if( !t || t.obj().has_flag( ter_furn_flag::TFLAG_NO_FLOOR ) ) {
                            continue;
                        }
                        int subtile = 0;
                        int rotation = 0;
                        const std::bitset<NUM_TERCONN> &connect_group = t.obj().connect_to_groups;
                        const std::bitset<NUM_TERCONN> &rotate_group = t.obj().rotate_to_groups;
                        if( connect_group.any() ) {
                            ct.get_connect_values( p, subtile, rotation, connect_group, rotate_group, {} );
                        } else {
                            ct.get_terrain_orientation( p, rotation, subtile, {}, seen, rotate_group );
                        }
                        int height_3d = 0;
                        ct.draw_from_id_string( t.id().str(), TILE_CATEGORY::TERRAIN, empty_string, p, subtile,
                                             rotation, above_ll, false, height_3d );
                    }
                }
                } );
            }
        }
        godot_host::note_storeys_usec( static_cast<int>( std::chrono::duration_cast<std::chrono::microseconds>
                                       ( std::chrono::steady_clock::now() - storeys_start ).count() ) );
    }
}

void godot_tiles::record_blit( const cata_tiles &ct, const texture &sprite_tex, int sprite_index,
                               double render_angle, CataFlipMode render_flip, const point &blit_at,
                               int width, int height, float pixelscale,
                               const weighted_int_list<std::vector<int>> &svlist, int rota,
                               unsigned int loc_rand )
{
    const SDL_Rect opaque = sprite_tex.get_opaque_rect();
    godot_host::record_map_blit( sprite_index, static_cast<int>( render_angle ),
                                 ( render_flip & SDL_FLIP_HORIZONTAL ) != 0, ( render_flip & SDL_FLIP_VERTICAL ) != 0,
                                 blit_at.x, blit_at.y, width, height, pixelscale,
                                 opaque.x, opaque.y, opaque.w, opaque.h );
    // An animated tile's list cycles by the clock: every frame (its
    // sprite, picked for this rotation as above, and how many 17 ms
    // steps it shows), and where the cycle stood at this draw, so the
    // host can run it on without a redraw.
    const int total = svlist.get_weight();
    if( ct.godot_anim_counter_ >= 0 && total > 1 ) {
        std::vector<godot_host::map_anim_frame> frames;
        for( const std::pair<std::vector<int>, int> &f : svlist ) {
            if( f.first.empty() || f.second <= 0 ) {
                continue;
            }
            const int n = f.first.size() == 1 ? 0 : rota % static_cast<int>( f.first.size() );
            frames.push_back( { f.first[n], f.second } );
        }
        const int at = static_cast<int>( loc_rand % total );
        const int base = static_cast<int>( ( ( at - ct.godot_anim_counter_ ) % total + total ) % total );
        godot_host::record_map_blit_animation( frames, base, total );
    }
}

void godot_tiles::record_catalogue( cata_tiles &ct, const std::string &spec )
{
    // Finalizing shows a loading screen, which pumps events, which would
    // come back here for a request the host repeated meanwhile.
    static bool recording = false;
    if( recording ) {
        return;
    }
    const restore_on_out_of_scope<bool> restore_recording( recording );
    recording = true;
    // The categories asked for, each with how many to draw (0: all).
    std::vector<std::pair<std::string, int>> wanted;
    std::string floor = "t_floor";
    std::vector<mod_id> mods;
    for( const std::string &part : string_split( spec, ',' ) ) {
        if( string_starts_with( part, "floor=" ) ) {
            floor = part.substr( 6 );
            continue;
        }
        if( string_starts_with( part, "mods=" ) ) {
            for( const std::string &m : string_split( part.substr( 5 ), '+' ) ) {
                mods.emplace_back( m );
            }
            continue;
        }
        const size_t colon = part.find( ':' );
        wanted.emplace_back( part.substr( 0, colon ),
                             colon == std::string::npos ? 0 : std::atoi( part.substr( colon + 1 ).c_str() ) );
    }
    // At the main menu no world's data is loaded (the menu loads the bare
    // core, unfinalized; a world's start loads it again with the world's
    // mods, dda among them, and finalizes it). Load it as a game's start
    // does (game::setup), for a world made for the purpose with the mods
    // asked for (dda when none), deleted again after. The catalogue's run
    // quits after; a game started later loads its data afresh.
    if( !g->is_core_data_loaded() ) {
        world_generator->set_active_world( nullptr );
        world_generator->init();
        WORLD *world = world_generator->make_new_world( mods );
        world_generator->set_active_world( world );
        g->load_core_data();
        world_generator->get_mod_manager().check_mods_list( world );
        g->load_world_modfiles();
        world_generator->delete_world( world->world_name, true );
    }
    // The tileset and the map objects: the snapshot names the tileset's
    // generation, and the host's rules read the objects.
    publish_tileset_and_objects( ct );
    struct candidate {
        std::string id;
        TILE_CATEGORY category;
        std::string variant;
    };
    std::vector<candidate> candidates;
    for( const auto &[name, most] : wanted ) {
        std::vector<candidate> found;
        if( name == "terrain" ) {
            for( size_t i = 0; i < ter_t::count(); ++i ) {
                found.push_back( { ter_id( static_cast<int>( i ) ).obj().id.str(), TILE_CATEGORY::TERRAIN, "" } );
            }
        } else if( name == "furniture" ) {
            for( size_t i = 0; i < furn_t::count(); ++i ) {
                found.push_back( { furn_id( static_cast<int>( i ) ).obj().id.str(), TILE_CATEGORY::FURNITURE, "" } );
            }
        } else if( name == "field" ) {
            for( const field_type &ft : field_types::get_all() ) {
                found.push_back( { ft.id.str(), TILE_CATEGORY::FIELD, "" } );
            }
        } else if( name == "trap" ) {
            for( size_t i = 0; i < trap::count(); ++i ) {
                found.push_back( { trap_id( static_cast<int>( i ) ).obj().id.str(), TILE_CATEGORY::TRAP, "" } );
            }
        } else if( name == "vehicle_part" ) {
            for( const vpart_info &vp : vehicles::parts::get_all() ) {
                if( vp.variants.empty() ) {
                    found.push_back( { "vp_" + vp.id.str(), TILE_CATEGORY::VEHICLE_PART, "" } );
                }
                for( const auto &[variant, v] : vp.variants ) {
                    found.push_back( { "vp_" + vp.id.str(), TILE_CATEGORY::VEHICLE_PART, variant } );
                }
            }
        } else if( name == "monster" ) {
            for( const mtype &m : MonsterGenerator::generator().get_all_mtypes() ) {
                found.push_back( { m.id.str(), TILE_CATEGORY::MONSTER, "" } );
            }
        } else if( name == "item" ) {
            for( const itype *t : item_controller->all() ) {
                found.push_back( { t->get_id().str(), TILE_CATEGORY::ITEM, "" } );
            }
        } else {
            debugmsg( "catalogue: unknown category '%s'", name );
            continue;
        }
        // A sample: every Kth, in the engine's own order.
        const size_t step = most > 0 && found.size() > static_cast<size_t>( most ) ?
                            found.size() / most : 1;
        for( size_t i = 0; i < found.size(); i += step ) {
            candidates.push_back( std::move( found[i] ) );
        }
    }

    // Each candidate, and each connection shape of a multitile, on a tile
    // of its own: rows of WIDTH from (0, 0).
    static constexpr int WIDTH = 64;
    static const std::array<std::string, 8> shapes = { {
            "center", "corner", "edge", "t_connection", "end_piece", "unconnected", "open", "broken"
        }
    };
    std::vector<godot_host::tile_catalogue_entry> entries;
    godot_host::begin_map_snapshot( 0, 0, 0, 0, 0, 0, 0, 0 );
    {
        const godot_host::record_only_scope record_only;
        int n = 0;
        for( const candidate &c : candidates ) {
            std::optional<tile_lookup_res> res = ct.find_tile_looks_like( c.id, c.category, c.variant );
            // The plain sprite, or each connection shape a multitile has
            // (center to unconnected: they stand in for the plain one), then
            // its open and broken states (doors, vehicle parts).
            std::vector<int> subtiles;
            if( res && res->tile().multitile ) {
                const std::vector<std::string> &have = res->tile().available_subtiles;
                for( size_t k = 0; k < shapes.size(); ++k ) {
                    if( std::find( have.begin(), have.end(), shapes[k] ) != have.end() ) {
                        if( k >= 6 && subtiles.empty() ) {
                            subtiles.push_back( -1 );
                        }
                        subtiles.push_back( static_cast<int>( k ) );
                    }
                }
            }
            if( subtiles.empty() ) {
                subtiles.push_back( -1 );
            }
            for( const int subtile : subtiles ) {
                godot_host::tile_catalogue_entry e;
                e.id = c.id;
                e.category = static_cast<int>( c.category );
                e.variant = c.variant;
                e.subtile = subtile;
                e.shape = subtile >= 0 ? shapes[subtile] : std::string();
                e.x = n % WIDTH;
                e.y = n / WIDTH;
                e.found = res.has_value();
                if( res && res->id() != c.id ) {
                    e.looks_like = res->id();
                }
                const tripoint_bub_ms pos( e.x, e.y, 0 );
                int height_3d = 0;
                if( c.category != TILE_CATEGORY::TERRAIN && !floor.empty() ) {
                    ct.draw_from_id_string( floor, TILE_CATEGORY::TERRAIN, empty_string, pos, -1, 0,
                                            lit_level::LIT, false, height_3d );
                }
                // A vehicle part or a monster drawn by an override (as the
                // vehicle part and monster previews do), so their variant
                // seeds don't look for a vehicle or a creature on the map.
                if( c.category == TILE_CATEGORY::VEHICLE_PART ) {
                    ct.init_draw_vpart_override( pos, vpart_id( c.id.substr( 3 ) ), 0, 0_degrees, false,
                                                 point_rel_ms::zero );
                } else if( c.category == TILE_CATEGORY::MONSTER ) {
                    ct.init_draw_monster_override( pos, mtype_id( c.id ), 1, false, Creature::Attitude::HOSTILE );
                }
                ct.draw_from_id_string( c.id, c.category, empty_string, pos, subtile, 0, lit_level::LIT,
                                        false, height_3d, 0, c.variant );
                entries.push_back( std::move( e ) );
                ++n;
            }
        }
        ct.void_vpart_override();
        ct.void_monster_override();
    }
    godot_host::publish_tile_catalogue( std::move( entries ) );
    // Requests repeated while this ran are answered by it.
    std::string repeated;
    godot_host::take_tile_catalogue_request( repeated );
}

#endif // CDDA_GODOT
