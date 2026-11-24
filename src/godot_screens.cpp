#if defined(CDDA_GODOT)

#include "godot_screens.h"

#include <algorithm>
#include <cmath>

#include "output.h"

static int share_of_terminal()
{
    return static_cast<int>( std::lround( TERMX * godot_screens::max_share ) );
}

void godot_screens::picker_size( int &width, int &height, const int content_width,
                                 const int content_height )
{
    width = std::min( { content_width, share_of_terminal(), TERMX } );
    height = std::clamp( content_height, std::min( min_picker_rows, TERMY ), TERMY );
}

int godot_screens::beside_sidebar( int /* panel_width */ )
{
    return std::max( 0, ( TERMX - share_of_terminal() ) / 2 );
}

float godot_screens::centred_width( const float viewport_width )
{
    return viewport_width * static_cast<float>( max_share );
}

float godot_screens::centred_x( const float viewport_width, const float width )
{
    return std::max( 0.0f, ( viewport_width - width ) / 2.0f );
}

#endif // CDDA_GODOT
