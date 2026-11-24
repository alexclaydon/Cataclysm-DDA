#if defined(CDDA_GODOT)

// The native menus and dialogs' bridges (plans 0017, 0018): uilist and
// query_popup and input_popup published to Dark Forest, and its answers
// taken. Member functions of those classes, defined here rather than in
// upstream's files (plan 0022); the short hooks that call them stay there.

#include <cstddef>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "godot_host.h"
#include "godot_menus.h"
#include "input.h"
#include "input_popup.h"
#include "input_context.h"
#include "popup.h"
#include "sdl_gamepad.h"
#include "uilist.h"


// ---- uilist (plan 0017) ----

// Whether the host draws this list: its switch on, and no callback that
// draws beside the list, which the host can't show (one asking for room
// at a side: the spell menu's details, the advanced inventory's
// destination). A callback that only acts on the selection (a map tile
// highlighted for each creature or turret: pointmenu_cb) or takes keys
// (the item action menu) works the same under the host (2026-11-23).
bool uilist::host_menu()
{
    return godot_host::native_menus() && !entries.empty() &&
           ( callback == nullptr || ( callback->desired_extra_space_left() <= 0.0f &&
                                      callback->desired_extra_space_right() <= 0.0f ) );
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

// Before each wait in uilist::query: drawn by the host (published, the
// engine's window hidden) or not (nothing published).
void uilist::host_before_wait( uilist_impl &ui )
{
    const bool host = host_menu();
    // Hidden every pass while ours draws it, not only when that changes: a
    // list queried a pass at a time (query( false ), the item menu's) gets a
    // new window each pass, which drew beside our native copy (the menu
    // catalogue, 2026-11-24). Shown again only on the switch back, so a
    // window the engine hid itself stays hidden.
    if( host || host != host_drawn ) {
        ui.hide_ui = host;
    }
    host_drawn = host;
    if( host ) {
        publish_host_menu();
    } else {
        godot_host::clear_menu( this );
    }
    godot_host::set_list_filters( filtering );
}

// After each wait: a tab changed from the pad applied (the hidden window's
// tab bar doesn't run), and the list taken down once answered.
void uilist::host_after_wait()
{
    if( host_drawn && switch_to_category < categories.size() &&
        switch_to_category != current_category ) {
        current_category = switch_to_category;
        switch_to_category = static_cast<size_t>( -1 );
        filterlist();
    }
    if( ret != UILIST_WAIT_INPUT ) {
        godot_host::clear_menu( this );
    }
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
                // As the list's own moves and choices tell its callback.
                if( callback != nullptr ) {
                    callback->select( this );
                }
                if( r.kind == godot_host::menu_request_kind::confirm && allow_confirm &&
                    ( entries[selected].enabled || allow_disabled ) ) {
                    ret = entries[selected].retval;
                    if( callback != nullptr ) {
                        callback->confirm( this );
                    }
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

// As the wait begins: drawn by the host while its switch is on, published
// (each option by its name, no key in it: the host shows the pad button
// answering it as a glyph, find_pad_answers).
void query_popup::open_host_dialog( const input_context &ctxt )
{
    host_drawn = godot_host::native_menus();
    if( !host_drawn ) {
        return;
    }
    godot_host::dialog_state s;
    s.kind = 0;
    s.message = godot_host::bbcode( text );
    for( size_t i = 0; i < options.size(); ++i ) {
        const int at = static_cast<int>( i );
        s.options.push_back( ctxt.get_action_name( options[i].action ) );
        s.option_glyphs.emplace_back( at == pad_b_option ? "B" :
                                      at == pad_x_option ? "X" : at == pad_y_option ? "Y" :
                                      at == pad_a_option && ( pad_all || options[i].action == "YES" ) ? "A" : "" );
    }
    s.cancel = cancel;
    s.anykey = anykey;
    godot_host::publish_dialog( this, std::move( s ) );
}

// Each pass: the option the keys are on, outlined by the host; none while
// the pad's face buttons answer every option (its d-pad moves nothing
// there: query_once's pad_arrow).
void query_popup::follow_host_dialog( int keyboard_selected ) const
{
    if( !host_drawn ) {
        return;
    }
    const bool pad_answers = gamepad::last_input_was_pad() && pad_all;
    godot_host::set_dialog_selected( this, pad_answers ? -1 : keyboard_selected );
}

// The host's answer, as the matching input: an option chosen (a click on
// its button; by_mouse for any option chosen), a cancel (Escape, or the option the pad's B
// answers where nothing cancels), any key. True if one came.
bool query_popup::take_host_dialog( result &res, bool &by_mouse )
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
                by_mouse = true;
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
                by_mouse = true;  // chosen, not a key the pad's rules map
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

// ---- the contract check (godot_menus.h) ----

namespace
{
struct check_log {
    int checks = 0;
    std::vector<std::string> fails;
    void expect( bool ok, const std::string &what ) {
        ++checks;
        if( !ok ) {
            fails.push_back( what );
        }
    }
};

bool contains( const std::string &s, const std::string &part )
{
    return s.find( part ) != std::string::npos;
}

// Requests the check didn't send (a stray tap) would be taken as its own.
void drain_requests()
{
    godot_host::menu_request m;
    while( godot_host::take_menu_request( m ) ) {}
    godot_host::dialog_request d;
    while( godot_host::take_dialog_request( d ) ) {}
}

void request_menu( godot_host::menu_request_kind kind, int value, const std::string &text = "" )
{
    godot_host::menu_request r;
    r.kind = kind;
    r.value = value;
    r.text = text;
    godot_host::request_menu( r );
}

// A callback that only acts on the selection (as pointmenu_cb), and one
// that draws beside the list (as the spell menu's): counted, and its room.
struct test_callback : uilist_callback {
    float room = 0.0f;
    int selects = 0;
    int confirms = 0;
    void select( uilist * ) override {
        ++selects;
    }
    void confirm( uilist * ) override {
        ++confirms;
    }
    float desired_extra_space_right() override {
        return room;
    }
};

void request_dialog( godot_host::dialog_request_kind kind, int value )
{
    godot_host::dialog_request r;
    r.kind = kind;
    r.value = value;
    godot_host::request_dialog( r );
}
} // namespace

int godot_menus_check::run()
{
    check_log log;
    drain_requests();
    // The dialogs publish only with the host's switch on; the lists are
    // published here directly.
    const bool native = godot_host::native_menus();
    godot_host::set_native_menus( true );

    // A list: two tabs, a header, a footer, descriptions, a hotkey, a
    // second column, a disabled entry, colour tags.
    {
        uilist m;
        m.title = "<color_light_red>Contract</color> list";
        m.text = "The header";
        m.footer_text = "The footer";
        m.desc_enabled = true;
        m.allow_cancel = true;
        m.filtering = true;
        m.addentry_desc( 10, true, 'a', "Alpha", "Alpha's description" );
        m.entries.back().ctxt = "col";
        m.addentry_desc( 11, false, 'b', "Beta", "Beta's description" );
        m.addentry_desc( 12, true, 'c', "Gamma", "Gamma's description" );
        m.categories = { { "first", "First" }, { "second", "Second" } };
        m.set_category_filter( []( const uilist_entry & e, const std::string & id ) {
            return ( e.retval == 12 ) == ( id == "second" );
        } );
        m.filterlist();
        m.publish_host_menu();
        godot_host::menu_state s;
        log.expect( godot_host::get_menu( s ), "list: published" );
        log.expect( contains( s.title, "Contract" ) && contains( s.title, "[color=" ),
                    "list: title as BBCode (" + s.title + ")" );
        log.expect( s.header == "The header", "list: header (" + s.header + ")" );
        log.expect( s.footer == "The footer", "list: footer (" + s.footer + ")" );
        log.expect( s.entries.size() == 2, "list: the first tab's 2 entries (" +
                    std::to_string( s.entries.size() ) + ")" );
        if( s.entries.size() == 2 ) {
            log.expect( s.entries[0].text == "Alpha", "list: entry text (" + s.entries[0].text + ")" );
            log.expect( s.entries[0].desc == "Alpha's description", "list: entry description" );
            log.expect( s.entries[0].hotkey == "a", "list: hotkey (" + s.entries[0].hotkey + ")" );
            log.expect( s.entries[0].column == "col", "list: second column" );
            log.expect( s.entries[0].enabled && !s.entries[1].enabled, "list: enabled and disabled" );
        }
        log.expect( s.selected == 0, "list: selected" );
        log.expect( s.tabs == std::vector<std::string> { "First", "Second" } && s.tab == 0, "list: tabs" );
        log.expect( s.desc && s.cancel && s.filter, "list: descriptions, cancel, filter" );

        // The host's requests, as its taps send them.
        m.ret = UILIST_WAIT_INPUT;
        request_menu( godot_host::menu_request_kind::select, 1 );
        log.expect( m.take_host_requests() && m.fselected == 1 && m.selected == 1,
                    "list: select moves the selection" );
        request_menu( godot_host::menu_request_kind::confirm, 1 );
        m.take_host_requests();
        log.expect( m.ret == UILIST_WAIT_INPUT, "list: a disabled entry isn't chosen" );
        request_menu( godot_host::menu_request_kind::tab, 1 );
        m.take_host_requests();
        log.expect( m.current_category == 1 && m.fentries.size() == 1 &&
                    m.entries[m.fentries[0]].txt == "Gamma", "list: a tab shows its entries" );
        request_menu( godot_host::menu_request_kind::tab, 0 );
        request_menu( godot_host::menu_request_kind::filter, 0, "alp" );
        m.take_host_requests();
        log.expect( m.current_category == 0 && m.fentries.size() == 1 &&
                    m.entries[m.fentries[0]].txt == "Alpha", "list: the filter" );
        request_menu( godot_host::menu_request_kind::confirm, 0 );
        m.take_host_requests();
        log.expect( m.ret == 10, "list: confirm chooses (" + std::to_string( m.ret ) + ")" );
        m.ret = UILIST_WAIT_INPUT;
        request_menu( godot_host::menu_request_kind::cancel, 0 );
        m.take_host_requests();
        log.expect( m.ret == UILIST_CANCEL, "list: cancel" );
        godot_host::clear_menu( &m );
        log.expect( !godot_host::get_menu( s ), "list: cleared" );
    }

    // Lists with callbacks: one acting on the selection is the host's, and
    // hears of the host's selections and choices; one drawing beside the
    // list stays the engine's.
    {
        uilist m;
        test_callback cb;
        m.callback = &cb;
        m.addentry( 20, true, 'a', "One" );
        m.addentry( 21, true, 'b', "Two" );
        m.filterlist();
        log.expect( m.host_menu(), "callback: a selection-only callback's list is the host's" );
        cb.room = 120.0f;
        log.expect( !m.host_menu(), "callback: a side pane's list stays the engine's" );
        cb.room = 0.0f;
        m.ret = UILIST_WAIT_INPUT;
        const int before = cb.selects;
        request_menu( godot_host::menu_request_kind::select, 1 );
        m.take_host_requests();
        log.expect( cb.selects == before + 1 && m.selected == 1, "callback: told of a host selection" );
        request_menu( godot_host::menu_request_kind::confirm, 0 );
        m.take_host_requests();
        log.expect( cb.confirms == 1 && m.ret == 20, "callback: told of a host choice" );
        m.callback = nullptr;
        godot_host::clear_menu( &m );
    }

    // A yes/no prompt: what the pad's buttons answer, the options' names,
    // the host's choice and its cancel (B's No, nothing cancelling).
    {
        query_popup p;
        p.context( "YESNO" ).message( "%s", "<color_yellow>Sure?</color>" ).option( "YES" ).option( "NO" );
        input_context ctxt( "YESNO" );
        ctxt.register_action( "CONFIRM" );
        ctxt.register_action( "YES" );
        ctxt.register_action( "NO" );
        p.find_pad_answers( ctxt );
        p.open_host_dialog( ctxt );
        godot_host::dialog_state d;
        log.expect( p.host_drawn && godot_host::get_dialog( d ), "prompt: published" );
        log.expect( contains( d.message, "Sure?" ) && contains( d.message, "[color=" ),
                    "prompt: message as BBCode (" + d.message + ")" );
        log.expect( d.options.size() == 2 && d.options[0] == ctxt.get_action_name( "YES" ) &&
                    !d.options[0].empty() && d.options[0] != "YES", "prompt: options by name" );
        log.expect( d.option_glyphs == std::vector<std::string> { "A", "B" }, "prompt: A Yes, B No" );
        log.expect( !d.cancel && !d.anykey, "prompt: no cancel, not any key" );
        p.follow_host_dialog( 1 );
        godot_host::get_dialog( d );
        const bool pad_answers = gamepad::last_input_was_pad() && p.pad_all;
        log.expect( d.selected == ( pad_answers ? -1 : 1 ), "prompt: the option the keys are on" );

        query_popup::result res;
        bool by_mouse = false;
        request_dialog( godot_host::dialog_request_kind::choose, 1 );
        log.expect( p.take_host_dialog( res, by_mouse ) && res.action == "CONFIRM" && p.cur == 1 &&
                    by_mouse, "prompt: an option chosen" );
        res = query_popup::result();
        by_mouse = false;
        p.cur = 0;
        request_dialog( godot_host::dialog_request_kind::cancel, 0 );
        log.expect( p.take_host_dialog( res, by_mouse ) && res.action == "CONFIRM" && p.cur == 1,
                    "prompt: a cancel answers B's No" );
    }
    {
        godot_host::dialog_state gone;
        log.expect( !godot_host::get_dialog( gone ), "prompt: cleared with its popup" );
    }

    // A message any key dismisses.
    {
        query_popup q;
        q.message( "%s", "Noted." ).allow_anykey( true );
        input_context ctxt( "default" );
        q.find_pad_answers( ctxt );
        q.open_host_dialog( ctxt );
        godot_host::dialog_state d;
        log.expect( godot_host::get_dialog( d ) && d.anykey && d.options.empty(), "message: any key" );
        query_popup::result res;
        bool by_mouse = false;
        request_dialog( godot_host::dialog_request_kind::dismiss, 0 );
        log.expect( q.take_host_dialog( res, by_mouse ) && res.action == "ANY_INPUT",
                    "message: dismissed" );
    }

    godot_host::set_native_menus( native );
    drain_requests();
    for( const std::string &f : log.fails ) {
        std::printf( "Menus contract: FAIL: %s\n", f.c_str() );
    }
    std::printf( "Menus contract: %s, %d checks, %d failed\n", log.fails.empty() ? "PASS" : "FAIL",
                 log.checks, static_cast<int>( log.fails.size() ) );
    std::fflush( stdout );
    return static_cast<int>( log.fails.size() );
}

#endif // CDDA_GODOT
