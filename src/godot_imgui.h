#pragma once
#ifndef CATA_SRC_GODOT_IMGUI_H
#define CATA_SRC_GODOT_IMGUI_H

#if defined(CDDA_GODOT)

struct ImDrawData;

// ImGui screens drawn only when they change (godot_imgui.cpp; P24, the
// user's report of sluggish menus, 2026-11-19).
namespace godot_imgui
{
// Whether this frame's ImGui draw data is what was last rasterised, with
// nothing drawn under it since: then cata_imgui.cpp's end_frame skips the
// software render, and the present (skipped()).
bool unchanged( const ImDrawData *data );
// A curses window drew to the frame (sdltiles.cpp's curses_drawwindow):
// the next ImGui frame is rasterised whatever it holds.
void note_drawn_under();
// The last ImGui frame was skipped: nothing to present.
bool skipped();
} // namespace godot_imgui

#endif // CDDA_GODOT
#endif // CATA_SRC_GODOT_IMGUI_H
