#if defined(CDDA_GODOT)

// The crafting screen's batch count, modeless (the user's call,
// 2026-11-15). Upstream's Batch + and - jump into batch mode, which swaps
// the recipe list for a list of batch sizes; here they count the selected
// recipe up and down in place (crafting_gui.cpp no longer enters the mode
// for them), the title showing "xN" and the components and time for N, and
// Confirm crafts N. The count belongs to the recipe it was set on: on
// another recipe it goes back to 1. The radial's Batch keeps upstream's
// batch mode.

#include <vector>

#include "godot_crafting.h"

void godot_crafting::follow_recipe( int &manual_batch, bool batch,
                                    const std::vector<const recipe *> &current, int line )
{
    static const recipe *counted = nullptr;
    const recipe *selected = line >= 0 && line < static_cast<int>( current.size() ) ?
                             current[line] : nullptr;
    if( !batch && selected != counted ) {
        manual_batch = 1;
    }
    counted = selected;
}
#endif // CDDA_GODOT
