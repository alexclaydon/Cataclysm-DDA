#pragma once
#ifndef CATA_SRC_GODOT_SCREENS_H
#define CATA_SRC_GODOT_SCREENS_H

#if defined(CDDA_GODOT)

// The engine's own screens shaped for Dark Forest (plan 0028, the menu
// catalogue's principles, the user's calls of 2026-11-24): hooks in the
// screens' layout code call these, the rules living here.
struct godot_screens {
    // The widest an engine screen is drawn: a share of the terminal,
    // centred, so a screen keeps a margin and its columns stay near
    // their names.
    static constexpr double max_share = 0.85;
    // The fewest rows a picker is drawn with (its header, a few rows,
    // the footer), however few items it holds.
    static constexpr int min_picker_rows = 12;

    // F: a picker (inventory_selector, unless it was given a fixed size)
    // as wide as its columns need up to max_share of the terminal, and as
    // tall as its rows, at least min_picker_rows, at most the terminal
    // (upstream: at least FULL_SCREEN_HEIGHT, and snapped to the
    // terminal's width or height when within a few cells of it).
    static void picker_size( int &width, int &height, int content_width, int content_height );

    // H: the columns a screen leaves for the engine's sidebar on one side
    // (messages, construction). Dark Forest draws the sidebar itself, so
    // the room left was empty and those screens sat to one side: they
    // leave the same margin on both sides instead, centred at max_share.
    static int beside_sidebar( int panel_width );

    // H, for ImGui screens laid out in pixels (the conversation, which was
    // the viewport's width less the sidebar's, at its left edge): the
    // width max_share of the viewport's, and the left that centres it.
    static float centred_width( float viewport_width );
    static float centred_x( float viewport_width, float width );
};

#endif // CDDA_GODOT
#endif // CATA_SRC_GODOT_SCREENS_H
