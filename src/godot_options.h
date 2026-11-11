#pragma once
#ifndef CATA_SRC_GODOT_OPTIONS_H
#define CATA_SRC_GODOT_OPTIONS_H

#if defined(CDDA_GODOT)

class options_manager;

// Dark Forest's hold on the engine's options (2026-11-11).
struct godot_options {
    // Hides the options Dark Forest's display profile sets at every launch
    // (display/*-dark-forest.json: the terminal grid, the fonts' sizes,
    // fullscreen, the scaling factor, the display): changed in the game,
    // they'd last only until the next launch, and meanwhile break the
    // engine's frame, the touch pad's layout and the sidebar, which are
    // laid out for the profile's. Called at the end of options_manager::
    // init (a friend there and of cOpt).
    static void hide_hosted( options_manager &om );
};

#endif // CDDA_GODOT

#endif // CATA_SRC_GODOT_OPTIONS_H
