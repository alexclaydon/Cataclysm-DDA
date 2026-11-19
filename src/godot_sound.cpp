#if defined(CDDA_GODOT)

// Repeated sounds held back during the avatar's activities (the user's
// call, 2026-11-19, from play on the phone: a long task's sound fired
// every turn, a wait or a craft a string of the same thump). While the
// avatar has an activity (waiting included), a sound (id and variant)
// plays at most once every MIN_GAP_MS of real time, and after REPEATS
// plays in a row with no other sound between, not again until another
// sound plays or the activity changes or ends. Out of an activity nothing
// is held back: a burst of fire keeps its shots. What is held back is
// logged once per sound per run of it (debug.log), so the culprit can be
// named.

#include <chrono>
#include <string>
#include <string_view>

#include "activity_type.h"
#include "avatar.h"
#include "debug.h"
#include "godot_sound.h"
#include "player_activity.h"

namespace
{
constexpr int MIN_GAP_MS = 750;
constexpr int REPEATS = 5;

std::string last_sound;
int in_a_row = 0;
bool logged = false;
activity_id last_activity = activity_id::NULL_ID();
std::chrono::steady_clock::time_point last_played;
} // namespace

bool godot_sound::held_back( std::string_view id, std::string_view variant )
{
    const activity_id act = get_avatar().activity.id();
    const std::string sound = std::string( id ) + "/" + std::string( variant );
    const auto now = std::chrono::steady_clock::now();
    if( act.is_null() || act != last_activity || sound != last_sound ) {
        last_activity = act;
        last_sound = sound;
        in_a_row = 1;
        logged = false;
        last_played = now;
        return false;
    }
    const long long gap = std::chrono::duration_cast<std::chrono::milliseconds>( now - last_played ).count();
    if( in_a_row >= REPEATS || gap < MIN_GAP_MS ) {
        if( !logged ) {
            DebugLog( D_WARNING, D_SDL ) << "Sound held back during " << act.str() << ": " << sound <<
                                      ( in_a_row >= REPEATS ? " (repeated)" : " (too soon)" );
            logged = true;
        }
        return true;
    }
    ++in_a_row;
    last_played = now;
    return false;
}

#endif // CDDA_GODOT
