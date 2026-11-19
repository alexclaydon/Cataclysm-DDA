#pragma once
#ifndef CATA_SRC_GODOT_SOUND_H
#define CATA_SRC_GODOT_SOUND_H

#if defined(CDDA_GODOT)

#include <string_view>

// Repeated sounds held back during the avatar's activities
// (godot_sound.cpp; the user's call, 2026-11-19).
namespace godot_sound
{
// Whether this play of the sound is held back; sdlsound.cpp's
// play_variant_sound asks before playing.
bool held_back( std::string_view id, std::string_view variant );
} // namespace godot_sound

#endif // CDDA_GODOT
#endif // CATA_SRC_GODOT_SOUND_H
