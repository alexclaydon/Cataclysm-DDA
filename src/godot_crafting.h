#pragma once
#ifndef CATA_SRC_GODOT_CRAFTING_H
#define CATA_SRC_GODOT_CRAFTING_H

#if defined(CDDA_GODOT)

#include <vector>

class recipe;

// The crafting screen's batch count, modeless (godot_crafting.cpp): Batch
// + and - (the pad's LT + right and left) count the selected recipe up and
// down in place, and the count goes back to 1 on another recipe.
namespace godot_crafting
{
void follow_recipe( int &manual_batch, bool batch, const std::vector<const recipe *> &current,
                    int line );
} // namespace godot_crafting

#endif // CDDA_GODOT
#endif // CATA_SRC_GODOT_CRAFTING_H
