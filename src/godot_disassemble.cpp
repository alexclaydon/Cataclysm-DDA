#if defined(CDDA_GODOT)

// Disassemble, several items at once (the user's call, 2026-11-15): the
// game's multi-select item screen (as pickup and drop: a stack marked
// whole, or one at a time), one confirmation for the lot, and one
// disassembly activity running them back to back, as Butcher's
// "Disassemble everything once" does. Reached from Character::disassemble()
// (crafting.cpp's hook); upstream's one-item menu is left as it is.

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "activity_actor_definitions.h"
#include "avatar.h"
#include "calendar.h"
#include "character.h"
#include "color.h"
#include "game.h"
#include "game_inventory.h"
#include "godot_disassemble.h"
#include "inventory_ui.h"
#include "item.h"
#include "item_location.h"
#include "itype.h"
#include "options.h"
#include "output.h"
#include "player_activity.h"
#include "recipe.h"
#include "recipe_dictionary.h"
#include "requirements.h"
#include "string_formatter.h"
#include "translations.h"
#include "type_id.h"

namespace
{

const recipe &uncraft_of( const item &it )
{
    return recipe_dictionary::get_uncraft( it.typeId() );
}

// How many of the item one selection takes apart: charges for an item
// counted by them, else one (the selector gives each item its own entry).
int amount_of( const drop_location &sel )
{
    return sel.first->count_by_charges() ? std::max( 1, sel.second ) : 1;
}

int64_t moves_of( const Character &you, const drop_location &sel )
{
    return uncraft_of( *sel.first ).time_to_craft_moves( you, {},
            recipe_time_flag::ignore_proficiencies ) * amount_of( sel );
}

// As upstream's one-item menu (game_inventory.cpp's
// disassemble_inventory_preset): each item's yield and time, and why one
// can't be taken apart.
class multi_disassemble_preset : public inventory_selector_preset
{
    public:
        multi_disassemble_preset( const Character &you, const temp_crafting_inventory &inv ) :
            you( you ), inv( inv ) {
            check_components = true;
            append_cell( []( const item_location & loc ) {
                if( uncraft_of( *loc ).disassembly_requirements().is_empty() ) {
                    return std::string();
                }
                std::vector<item_comp> components = loc->get_uncraft_components();
                std::sort( components.begin(), components.end() );
                auto it = components.begin();
                while( ( it = std::adjacent_find( it, components.end() ) ) != components.end() ) {
                    ++( it->count );
                    components.erase( std::next( it ) );
                }
                return enumerate_as_string( components.begin(), components.end(),
                []( const item_comp & comp ) {
                    return comp.to_string();
                } );
            }, _( "YIELD" ) );
            append_cell( []( const item_location & loc ) {
                return to_string_clipped( uncraft_of( *loc ).time_to_craft( get_player_character(),
                                          {}, recipe_time_flag::ignore_proficiencies ) );
            }, _( "TIME" ) );
        }

        bool is_shown( const item_location &loc ) const override {
            return !uncraft_of( *loc ).is_null();
        }

        std::string get_denial( const item_location &loc ) const override {
            const ret_val<void> ret = you.can_disassemble( *loc, inv );
            return ret.success() ? std::string() : ret.str();
        }

    private:
        const Character &you;
        const temp_crafting_inventory &inv;
};

// Why an item needs asking about first, as upstream's disassemble() asks
// one at a time: favourited, worn, or someone else's. Empty: none.
std::string caution_of( const Character &you, const item &it )
{
    if( it.is_favorite ) {
        return _( "favorited" );
    }
    if( it.is_worn_by_player() ) {
        return _( "worn" );
    }
    if( !it.is_owned_by( you, true ) ) {
        return _( "not yours" );
    }
    return std::string();
}

std::string named( const drop_location &sel )
{
    const int n = amount_of( sel );
    return colorize( sel.first->tname( n ), sel.first->color_in_inventory() ) +
           ( sel.first->count_by_charges() && n > 1 ? string_format( " (%d)", n ) : std::string() );
}

} // namespace

bool godot_disassemble::menu( Character &you )
{
    const temp_crafting_inventory &inv = you.crafting_inventory();
    const multi_disassemble_preset preset( you, inv );
    auto stats = [&you]( const std::vector<std::pair<item_location, int>> &chosen ) {
        int64_t moves = 0;
        for( const drop_location &sel : chosen ) {
            moves += moves_of( you, sel );
        }
        const std::string time = colorize( to_string_clipped( time_duration::from_moves( moves ) ),
                                           c_light_gray );
        return inventory_selector::convert_stats_to_header_stats( inventory_selector::stats{{
                {{ _( "Estimated time" ), time }}
            }} );
    };
    inventory_multiselector inv_s( you, preset, _( "ITEMS TO DISASSEMBLE" ), stats );
    inv_s.add_character_items( you );
    inv_s.add_nearby_items( PICKUP_RANGE );
    inv_s.set_title( _( "Disassemble items" ) );
    inv_s.set_hint( _( "Select a whole stack, or one at a time; confirm to start." ) );
    if( inv_s.empty() ) {
        popup( std::string( _( "You don't have any items you could disassemble." ) ), PF_GET_KEY );
        return false;
    }
    drop_locations chosen = inv_s.execute();
    if( chosen.empty() ) {
        add_msg( _( "Never mind." ) );
        return false;
    }

    // Asked once for the lot: favourited, worn and others' items, which
    // upstream asks about one by one. No leaves them out.
    std::string cautions;
    for( const drop_location &sel : chosen ) {
        const std::string why = caution_of( you, *sel.first );
        if( !why.empty() ) {
            cautions += string_format( "- %s (%s)\n", named( sel ), why );
        }
    }
    if( !cautions.empty() &&
        !query_yn( _( "Some of these need a second thought:\n%s\nDisassemble them too?" ), cautions ) ) {
        chosen.erase( std::remove_if( chosen.begin(), chosen.end(), [&you]( const drop_location & sel ) {
            return !caution_of( you, *sel.first ).empty();
        } ), chosen.end() );
        if( chosen.empty() ) {
            add_msg( _( "Never mind." ) );
            return false;
        }
    }

    // Part of a stack counted by charges goes first (the activity's
    // position); any other such stack is taken whole (its value 0), so only
    // one can be part-taken at a time.
    auto partial = []( const drop_location & sel ) {
        return sel.first->count_by_charges() && sel.second < sel.first->charges;
    };
    if( std::count_if( chosen.begin(), chosen.end(), partial ) > 1 ) {
        popup( _( "Take only part of one stack at a time: mark the other stacks whole, or leave them for a second round." ) );
        return false;
    }

    // One summary (upstream's "last chance to back out", under the same
    // option): the items, what they yield between them, the time, and
    // whether one might teach its recipe.
    if( get_option<bool>( "QUERY_DISASSEMBLE" ) ) {
        std::map<itype_id, int> yields;
        std::vector<std::string> items;
        int64_t moves = 0;
        bool learn = false;
        for( const drop_location &sel : chosen ) {
            const int n = amount_of( sel );
            for( const item_comp &comp : sel.first->get_uncraft_components() ) {
                yields[comp.type] += comp.count * n;
            }
            items.push_back( named( sel ) );
            moves += moves_of( you, sel );
            const recipe &r = uncraft_of( *sel.first );
            learn = learn || ( !r.learn_by_disassembly.empty() && !you.knows_recipe( &r ) &&
                               you.can_decomp_learn( r ) );
        }
        std::string yield_list;
        for( const std::pair<const itype_id, int> &y : yields ) {
            yield_list += "- " + item_comp( y.first, y.second ).to_string() + "\n";
        }
        std::string item_list;
        for( const std::string &name : items ) {
            item_list += "- " + name + "\n";
        }
        std::string question = string_format(
                                   _( "Disassemble:\n%s\nMay yield:\n%s\nTime: %s\nReally disassemble?" ),
                                   item_list, yield_list,
                                   to_string_clipped( time_duration::from_moves( moves ) ) );
        if( learn ) {
            question += "\n";
            question += _( "You feel you may be able to understand how some of these are made." );
        }
        if( !query_yn( question ) ) {
            add_msg( _( "Never mind." ) );
            return false;
        }
    }

    for( const drop_location &sel : chosen ) {
        if( !sel.first->is_owned_by( you, true ) && sel.first->get_owner() ) {
            g->on_witness_theft( *sel.first );
        }
    }

    // The activity takes its targets from the back: the part-taken stack
    // last in the list, so it runs first with the activity's position.
    std::stable_partition( chosen.begin(), chosen.end(), [&partial]( const drop_location & sel ) {
        return !partial( sel );
    } );
    const drop_location &first = chosen.back();
    player_activity act( disassemble_activity_actor( moves_of( you, first ) ) );
    for( const drop_location &sel : chosen ) {
        act.targets.emplace_back( sel.first );
    }
    // index: no recursive round; value 0: later stacks counted by charges
    // are taken whole; position: how many of the first.
    act.index = false;
    act.position = amount_of( first );
    act.values.push_back( 1 );
    you.assign_activity( act );
    for( const drop_location &sel : chosen ) {
        uncraft_of( *sel.first ).apply_negative_morale_mods( you );
    }
    return true;
}
#endif // CDDA_GODOT
