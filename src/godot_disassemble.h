#pragma once
#ifndef CATA_SRC_GODOT_DISASSEMBLE_H
#define CATA_SRC_GODOT_DISASSEMBLE_H

#if defined(CDDA_GODOT)

class Character;
class item;
class uilist;

// Disassemble, several items at once (godot_disassemble.cpp): the
// multi-select item screen, one confirmation, one activity for the lot.
namespace godot_disassemble
{
bool menu( Character &you );
// crafting.cpp's place_craft_or_disassembly asks where an in-progress item
// goes: in a batch from menu(), the first answer stands for the rest.
void query_placement( uilist &menu, const Character &you, const item &craft );
} // namespace godot_disassemble

#endif // CDDA_GODOT
#endif // CATA_SRC_GODOT_DISASSEMBLE_H
