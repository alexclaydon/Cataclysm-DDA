#if defined(CDDA_GODOT)

#if !defined(TILES) || !defined(USE_SDL3)
#error "CDDA_GODOT needs the SDL3 tiles build"
#endif

#include "godot_host.h"

#include <atomic>
#include <cstring>
#include <mutex>

#include <SDL3/SDL.h>

namespace
{

std::mutex frame_mutex;
std::vector<unsigned char> frame_rgba;
int frame_width = 0;
int frame_height = 0;
uint64_t frame_sequence = 0;

std::atomic<uint32_t> engine_window_id{ 0 };

} // namespace

void godot_host::publish_frame( SDL_Renderer *renderer )
{
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
    {
        std::lock_guard<std::mutex> lock( frame_mutex );
        const size_t row = static_cast<size_t>( rgba->w ) * 4;
        frame_rgba.resize( row * rgba->h );
        const unsigned char *src = static_cast<const unsigned char *>( rgba->pixels );
        for( int y = 0; y < rgba->h; ++y ) {
            std::memcpy( frame_rgba.data() + row * y, src + static_cast<size_t>( rgba->pitch ) * y, row );
        }
        frame_width = rgba->w;
        frame_height = rgba->h;
        ++frame_sequence;
    }
    SDL_DestroySurface( rgba );
}

bool godot_host::take_frame( uint64_t &sequence, std::vector<unsigned char> &rgba, int &width,
                             int &height )
{
    std::lock_guard<std::mutex> lock( frame_mutex );
    if( frame_sequence == sequence ) {
        return false;
    }
    rgba = frame_rgba;
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

#endif // CDDA_GODOT
