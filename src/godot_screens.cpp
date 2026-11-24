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

#endif // CDDA_GODOT
