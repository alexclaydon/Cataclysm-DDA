#if defined(CDDA_GODOT)

// The native menus and dialogs' bridges (plans 0017, 0018): uilist and
// query_popup and input_popup published to Dark Forest, and its answers
// taken. Member functions of those classes, defined here rather than in
// upstream's files (plan 0022); the short hooks that call them stay there.

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "godot_host.h"
#include "input.h"
#include "input_popup.h"
#include "popup.h"
#include "uilist.h"


// ---- uilist (plan 0017) ----

// Whether the host draws this list: its switch on, and no callback (a
// callback may draw beside the list, which the host can't show).
bool uilist::host_menu()
{
    return godot_host::native_menus() && callback == nullptr && !entries.empty();
}

void uilist::publish_host_menu()
{
    godot_host::menu_state s;
    s.title = godot_host::bbcode( title );
    s.header = godot_host::bbcode( text );
    s.footer = godot_host::bbcode( footer_text );
    s.entries.reserve( fentries.size() );
    for( const int i : fentries ) {
        const uilist_entry &e = entries[i];
        godot_host::menu_entry m;
        m.text = godot_host::bbcode( e.txt );
        m.column = godot_host::bbcode( e.ctxt );
        m.desc = godot_host::bbcode( e.desc );
        if( e.hotkey.has_value() && e.hotkey.value() != input_event() ) {
            m.hotkey = e.hotkey.value().short_description();
        }
        m.enabled = e.enabled || e.force_color;
        s.entries.push_back( std::move( m ) );
    }
    s.selected = fselected;
    for( const std::pair<std::string, std::string> &cat : categories ) {
        s.tabs.push_back( cat.second );
    }
    s.tab = static_cast<int>( current_category );
    s.desc = desc_enabled;
    s.cancel = allow_cancel;
    s.filter = filtering;
    s.filter_text = filter;
    godot_host::publish_menu( this, std::move( s ) );
}

// The host's requests, applied as the matching keys are: true if one was.
bool uilist::take_host_requests()
{
    bool any = false;
    godot_host::menu_request r;
    while( ret == UILIST_WAIT_INPUT && godot_host::take_menu_request( r ) ) {
        any = true;
        switch( r.kind ) {
            case godot_host::menu_request_kind::select:
            case godot_host::menu_request_kind::confirm:
                if( r.value < 0 || r.value >= static_cast<int>( fentries.size() ) ) {
                    break;
                }
                fselected = r.value;
                selected = previewing = fentries[fselected];
                need_to_scroll = true;
                if( r.kind == godot_host::menu_request_kind::confirm && allow_confirm &&
                    ( entries[selected].enabled || allow_disabled ) ) {
                    ret = entries[selected].retval;
                }
                break;
            case godot_host::menu_request_kind::cancel:
                if( allow_cancel ) {
                    ret = UILIST_CANCEL;
                }
                break;
            case godot_host::menu_request_kind::tab:
                if( r.value >= 0 && r.value < static_cast<int>( categories.size() ) ) {
                    current_category = r.value;
                    switch_to_category = static_cast<size_t>( -1 );
                    filterlist();
                }
                break;
            case godot_host::menu_request_kind::filter:
                if( filtering ) {
                    filter = r.text;
                    filterlist();
                }
                break;
        }
    }
    if( any ) {
        ret_act.clear();
        ret_evt = input_event();
    }
    return any;
}

// ---- query_popup (plan 0018) ----

// The host's answer, as the matching input: an option chosen (a click on
// its button), a cancel (Escape, or the option the pad's B answers where
// nothing cancels), any key. True if one came.
bool query_popup::take_host_dialog( result &res )
{
    godot_host::dialog_request r;
    if( !godot_host::take_dialog_request( r ) ) {
        return false;
    }
    switch( r.kind ) {
        case godot_host::dialog_request_kind::choose:
            if( r.value >= 0 && r.value < static_cast<int>( options.size() ) ) {
                cur = static_cast<size_t>( r.value );
                res.action = "CONFIRM";
                res.evt = input_event();
                return true;
            }
            break;
        case godot_host::dialog_request_kind::cancel:
            if( cancel ) {
                res.action = "QUIT";
                res.evt = input_event( KEY_ESCAPE, input_event_t::keyboard_code );
                return true;
            }
            if( pad_b_option >= 0 ) {
                cur = static_cast<size_t>( pad_b_option );
                res.action = "CONFIRM";
                res.evt = input_event();
                return true;
            }
            if( anykey ) {
                // A message any key dismisses: so does a cancel.
                res.action = "ANY_INPUT";
                res.evt = input_event( KEY_ESCAPE, input_event_t::keyboard_code );
                return true;
            }
            break;
        case godot_host::dialog_request_kind::dismiss:
        case godot_host::dialog_request_kind::submit:
            if( anykey ) {
                res.action = "ANY_INPUT";
                res.evt = input_event( ' ', input_event_t::keyboard_char );
                return true;
            }
            break;
    }
    return false;
}

// ---- input_popup (plan 0018) ----

void input_popup::publish_host( int kind, const std::string &value )
{
    host_drawn = godot_host::native_menus();
    hide_ui = host_drawn;
    if( !host_drawn ) {
        godot_host::clear_dialog( this );
        return;
    }
    godot_host::dialog_state s;
    s.kind = kind;
    s.title = host_title;
    s.message = godot_host::bbcode( description );
    s.label = godot_host::bbcode( label );
    s.value = value;
    s.cancel = true;
    s.max_length = max_input_length;
    godot_host::publish_dialog( this, std::move( s ) );
}

#endif // CDDA_GODOT
