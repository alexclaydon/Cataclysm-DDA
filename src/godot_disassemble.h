#pragma once
#ifndef CATA_SRC_GODOT_DISASSEMBLE_H
#define CATA_SRC_GODOT_DISASSEMBLE_H

#if defined(CDDA_GODOT)

class Character;

// Disassemble, several items at once (godot_disassemble.cpp): the
// multi-select item screen, one confirmation, one activity for the lot.
namespace godot_disassemble
{
bool menu( Character &you );
} // namespace godot_disassemble

#endif // CDDA_GODOT
#endif // CATA_SRC_GODOT_DISASSEMBLE_H
