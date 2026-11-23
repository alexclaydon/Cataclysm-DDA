#pragma once
#ifndef CATA_SRC_GODOT_MENUS_H
#define CATA_SRC_GODOT_MENUS_H

#if defined(CDDA_GODOT)

// The native menus and dialogs' contract check (godot_menus.cpp): a list
// and two prompts built with known contents, published as the bridges
// publish them, read back as the host reads them, and answered with the
// host's requests, each result compared with what the engine should do.
// Upstream can change what a uilist or query_popup member means without
// any compile error; this is what notices. Run in play from Nova (make
// df-mac-menus-check: godot_host::request_menus_check); a friend of
// uilist and query_popup. Prints "Menus contract: PASS ..." or a FAIL line
// per difference, and returns the number of failures.
struct godot_menus_check {
    static int run();
};

#endif // CDDA_GODOT
#endif // CATA_SRC_GODOT_MENUS_H
