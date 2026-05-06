/*
 * Copyright (c) 2023-2026, Tim Flynn <trflynn89@ladybird.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/HashMap.h>
#include <AK/Vector.h>
#include <LibWebView/Application.h>
#include <LibWebView/HistoryStore.h>
#include <LibWebView/ViewImplementation.h>

#import <Application/ApplicationDelegate.h>
#import <Interface/InfoBar.h>
#import <Interface/LadybirdWebView.h>
#import <Interface/Menu.h>
#import <Interface/Tab.h>
#import <Interface/TabController.h>
#import <Utilities/Conversions.h>

#if !__has_feature(objc_arc)
#    error "This project requires ARC"
#endif

static NSString* const REOPEN_RECENTLY_CLOSED_TAB_MENU_ITEM_TITLE = @"Reopen Recently Closed Tab";
static NSString* const REOPEN_RECENTLY_CLOSED_WINDOW_MENU_ITEM_TITLE = @"Reopen Recently Closed Window";

struct RecentlyClosedTabGroupSnapshot {
    Vector<URL::URL> urls;
    size_t active_tab_index { 0 };
};

struct PendingRecentlyClosedTabGroup {
    Vector<URL::URL> urls;
    Vector<URL::URL> individually_closed_urls;
    Vector<void*> window_keys;
    size_t active_tab_index { 0 };
    size_t expected_close_count { 0 };
    bool finalization_scheduled { false };
};

static constexpr int RECENTLY_CLOSED_TAB_GROUP_FINALIZATION_DELAY_MS = 250;

static void* recently_closed_tab_window_key(Tab* tab)
{
    return (__bridge void*)tab;
}

static void* recently_closed_tab_group_key(Tab* tab)
{
    if (auto* tab_group = [tab tabGroup]; tab_group != nil)
        return (__bridge void*)tab_group;

    return (__bridge void*)tab;
}

static RecentlyClosedTabGroupSnapshot snapshot_recently_closed_tab_group(Tab* tab)
{
    auto* tab_group = [tab tabGroup];
    NSArray<NSWindow*>* windows = tab_group != nil ? [tab_group windows] : @[ (NSWindow*)tab ];
    NSWindow* selected_window = tab_group != nil ? [tab_group selectedWindow] : tab;

    RecentlyClosedTabGroupSnapshot snapshot;
    snapshot.urls.ensure_capacity([windows count]);

    for (NSWindow* window in windows) {
        if (![window isKindOfClass:[Tab class]])
            continue;

        snapshot.urls.append([[(Tab*)window web_view] view].url());
        if (window == selected_window)
            snapshot.active_tab_index = snapshot.urls.size() - 1;
    }

    return snapshot;
}

@interface ApplicationDelegate ()

@property (nonatomic, strong) NSMutableArray<TabController*>* managed_tabs;
@property (nonatomic, weak) Tab* active_tab;

@property (nonatomic, strong) NSMenu* bookmarks_menu;
@property (nonatomic, strong) NSMenuItem* reopen_recently_closed_tab_item;

@property (nonatomic, strong) InfoBar* info_bar;

- (NSMenuItem*)createApplicationMenu;
- (NSMenuItem*)createFileMenu;
- (NSMenuItem*)createEditMenu;
- (NSMenuItem*)createViewMenu;
- (NSMenuItem*)createHistoryMenu;
- (NSMenuItem*)createBookmarksMenu;
- (NSMenuItem*)createInspectMenu;
- (NSMenuItem*)createDebugMenu;
- (NSMenuItem*)createWindowMenu;
- (NSMenuItem*)createHelpMenu;

@end

@implementation ApplicationDelegate
{
    HashMap<void*, PendingRecentlyClosedTabGroup> m_pending_recently_closed_tab_groups;
    HashMap<void*, void*> m_pending_recently_closed_tab_window_group_keys;
}

- (instancetype)init
{
    if (self = [super init]) {
        [NSApp setMainMenu:[[NSMenu alloc] init]];

        [[NSApp mainMenu] addItem:[self createApplicationMenu]];
        [[NSApp mainMenu] addItem:[self createFileMenu]];
        [[NSApp mainMenu] addItem:[self createEditMenu]];
        [[NSApp mainMenu] addItem:[self createViewMenu]];
        [[NSApp mainMenu] addItem:[self createHistoryMenu]];
        [[NSApp mainMenu] addItem:[self createBookmarksMenu]];
        [[NSApp mainMenu] addItem:[self createInspectMenu]];
        [[NSApp mainMenu] addItem:[self createDebugMenu]];
        [[NSApp mainMenu] addItem:[self createWindowMenu]];
        [[NSApp mainMenu] addItem:[self createHelpMenu]];

        self.managed_tabs = [[NSMutableArray alloc] init];

        // Reduce the tooltip delay, as the default delay feels quite long.
        [[NSUserDefaults standardUserDefaults] setObject:@100 forKey:@"NSInitialToolTipDelay"];
    }

    return self;
}

#pragma mark - Public methods

- (nonnull TabController*)createNewTab:(Web::HTML::ActivateTab)activate_tab
                               fromTab:(nullable Tab*)tab
{
    auto* controller = [[TabController alloc] init];
    [self initializeTabController:controller
                      activateTab:activate_tab
                          fromTab:tab];

    return controller;
}

- (TabController*)createNewTab:(Optional<URL::URL> const&)url
                       fromTab:(Tab*)tab
                   activateTab:(Web::HTML::ActivateTab)activate_tab
{
    auto* controller = [self createNewTab:activate_tab fromTab:tab];

    if (url.has_value()) {
        [controller loadURL:*url];

        if (*url != WebView::Application::settings().new_tab_page_url())
            [controller focusWebView];
    }

    return controller;
}

- (nonnull TabController*)createChildTab:(Optional<URL::URL> const&)url
                                 fromTab:(nonnull Tab*)tab
                             activateTab:(Web::HTML::ActivateTab)activate_tab
                               pageIndex:(u64)page_index
{
    auto* controller = [self createChildTab:activate_tab fromTab:tab pageIndex:page_index];

    if (url.has_value()) {
        [controller loadURL:*url];
    }

    [controller focusWebView];

    return controller;
}

- (void)setActiveTab:(Tab*)tab
{
    if (tab == self.activeTab)
        return;

    self.active_tab = tab;

    if (self.info_bar) {
        [self.info_bar tabBecameActive:self.active_tab];
    }

    WebView::Application::the().update_bookmark_action_for_current_web_view();
}

- (Tab*)activeTab
{
    return self.active_tab;
}

- (void)recordClosingTabController:(TabController*)controller
{
    auto* tab = (Tab*)[controller window];
    auto window_key = recently_closed_tab_window_key(tab);
    if (auto group_key = m_pending_recently_closed_tab_window_group_keys.get(window_key); group_key.has_value()) {
        auto pending_tab_group_iterator = m_pending_recently_closed_tab_groups.find(*group_key);
        if (pending_tab_group_iterator != m_pending_recently_closed_tab_groups.end()) {
            auto* pending_tab_group = &pending_tab_group_iterator->value;
            pending_tab_group->individually_closed_urls.append([[tab web_view] view].url());
            if (pending_tab_group->finalization_scheduled)
                return;

            pending_tab_group->finalization_scheduled = true;
            dispatch_after(dispatch_time(DISPATCH_TIME_NOW, RECENTLY_CLOSED_TAB_GROUP_FINALIZATION_DELAY_MS * NSEC_PER_MSEC), dispatch_get_main_queue(), ^{
                auto pending_tab_group_iterator = m_pending_recently_closed_tab_groups.find(*group_key);
                if (pending_tab_group_iterator == m_pending_recently_closed_tab_groups.end())
                    return;
                auto* pending_tab_group = &pending_tab_group_iterator->value;

                auto finalized_tab_group = move(*pending_tab_group);
                m_pending_recently_closed_tab_groups.remove(*group_key);
                for (auto pending_window_key : finalized_tab_group.window_keys)
                    m_pending_recently_closed_tab_window_group_keys.remove(pending_window_key);

                if (finalized_tab_group.individually_closed_urls.size() == finalized_tab_group.expected_close_count) {
                    WebView::Application::history_store().record_closed_window(move(finalized_tab_group.urls), finalized_tab_group.active_tab_index);
                } else {
                    for (auto& url : finalized_tab_group.individually_closed_urls)
                        WebView::Application::history_store().record_closed_tab(url);
                }

                [self updateReopenRecentlyClosedMenuItem];
            });
            return;
        }

        m_pending_recently_closed_tab_window_group_keys.remove(window_key);
    }

    WebView::Application::history_store().record_closed_tab([[tab web_view] view].url());
    [self updateReopenRecentlyClosedMenuItem];
}

- (void)noteCloseRequestedForTabController:(TabController*)controller
{
    auto* tab = (Tab*)[controller window];
    auto* tab_group = [tab tabGroup];
    if (tab_group == nil)
        return;

    auto snapshot = snapshot_recently_closed_tab_group(tab);
    if (snapshot.urls.size() <= 1)
        return;

    auto group_key = recently_closed_tab_group_key(tab);
    if (m_pending_recently_closed_tab_groups.contains(group_key))
        return;

    auto expected_close_count = snapshot.urls.size();
    PendingRecentlyClosedTabGroup pending_tab_group {
        .urls = move(snapshot.urls),
        .active_tab_index = snapshot.active_tab_index,
        .expected_close_count = expected_close_count,
    };

    NSArray<NSWindow*>* windows = [tab_group windows];
    pending_tab_group.window_keys.ensure_capacity([windows count]);

    for (NSWindow* window in windows) {
        if (![window isKindOfClass:[Tab class]])
            continue;

        auto pending_window_key = recently_closed_tab_window_key((Tab*)window);
        pending_tab_group.window_keys.append(pending_window_key);
        m_pending_recently_closed_tab_window_group_keys.set(pending_window_key, group_key);
    }

    m_pending_recently_closed_tab_groups.set(group_key, move(pending_tab_group));
}

- (void)openNewWindowWithURLs:(Vector<URL::URL> const&)urls
               activeTabIndex:(size_t)active_tab_index
{
    if (urls.is_empty())
        return;

    auto clamped_active_tab_index = active_tab_index < urls.size() ? active_tab_index : urls.size() - 1;
    Tab* tab = nil;
    TabController* active_controller = nil;

    for (size_t index = 0; index < urls.size(); ++index) {
        auto activate_tab = index == 0 ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No;
        auto* controller = [self createNewTab:urls[index]
                                      fromTab:tab
                                  activateTab:activate_tab];

        if (index == clamped_active_tab_index)
            active_controller = controller;

        tab = (Tab*)[controller window];
    }

    if (active_controller == nil)
        return;

    auto* active_window = [active_controller window];
    if (auto* tab_group = [active_window tabGroup]; tab_group != nil)
        [tab_group setSelectedWindow:active_window];

    [active_window orderFrontRegardless];
    [active_controller focusWebView];
}

- (void)removeTab:(TabController*)controller
{
    [self.managed_tabs removeObject:controller];
}

- (void)rebuildBookmarksMenu
{
    Ladybird::repopulate_application_menu(self.bookmarks_menu, WebView::Application::the().bookmarks_menu());

    for (TabController* controller in self.managed_tabs) {
        auto* tab = (Tab*)[controller window];
        [tab rebuildBookmarksBar];
    }
}

- (void)updateBookmarksBarDisplay:(bool)show_bookmarks_bar
{
    for (TabController* controller in self.managed_tabs) {
        if (auto* tab = (Tab*)[controller window]; ([tab styleMask] & NSWindowStyleMaskFullScreen) == 0) {
            [tab updateBookmarksBarDisplay:show_bookmarks_bar];
        }
    }
}

- (void)onDevtoolsEnabled
{
    if (!self.info_bar) {
        self.info_bar = [[InfoBar alloc] init];
    }

    auto message = MUST(String::formatted("DevTools is enabled on port {}", WebView::Application::browser_options().devtools_port));

    [self.info_bar showWithMessage:Ladybird::string_to_ns_string(message)
                dismissButtonTitle:@"Disable"
              dismissButtonClicked:^{
                  MUST(WebView::Application::the().toggle_devtools_enabled());
              }
                         activeTab:self.active_tab];
}

- (void)onDevtoolsDisabled
{
    if (self.info_bar) {
        [self.info_bar hide];
        self.info_bar = nil;
    }
}

#pragma mark - Private methods

- (void)openLocation:(id)sender
{
    auto* current_tab = [NSApp keyWindow];

    if (![current_tab isKindOfClass:[Tab class]]) {
        return;
    }

    auto* controller = (TabController*)[current_tab windowController];
    [controller focusLocationToolbarItem];
}

- (nonnull TabController*)createChildTab:(Web::HTML::ActivateTab)activate_tab
                                 fromTab:(nonnull Tab*)tab
                               pageIndex:(u64)page_index
{
    auto* controller = [[TabController alloc] initAsChild:tab pageIndex:page_index];
    [self initializeTabController:controller
                      activateTab:activate_tab
                          fromTab:tab];

    return controller;
}

- (void)initializeTabController:(TabController*)controller
                    activateTab:(Web::HTML::ActivateTab)activate_tab
                        fromTab:(nullable Tab*)tab
{
    [controller showWindow:nil];

    if (tab) {
        [[tab tabGroup] addWindow:controller.window];

        // FIXME: Can we create the tabbed window above without it becoming active in the first place?
        if (activate_tab == Web::HTML::ActivateTab::No) {
            [tab orderFront:nil];
        }
    }

    if (activate_tab == Web::HTML::ActivateTab::Yes) {
        [[controller window] orderFrontRegardless];
        [controller focusLocationToolbarItem];
    }

    [self.managed_tabs addObject:controller];
}

- (void)closeCurrentTab:(id)sender
{
    auto* current_window = [NSApp keyWindow];
    [current_window performClose:self];
}

- (void)clearHistory:(id)sender
{
    WebView::Application::the().clear_history();
}

- (void)updateReopenRecentlyClosedMenuItem
{
    auto recently_closed_entry = WebView::Application::history_store().most_recently_closed_entry();
    auto* title = recently_closed_entry.has_value() && recently_closed_entry->was_window
        ? REOPEN_RECENTLY_CLOSED_WINDOW_MENU_ITEM_TITLE
        : REOPEN_RECENTLY_CLOSED_TAB_MENU_ITEM_TITLE;

    [self.reopen_recently_closed_tab_item setTitle:title];
    [self.reopen_recently_closed_tab_item setEnabled:recently_closed_entry.has_value()];
}

- (NSMenuItem*)createApplicationMenu
{
    auto* menu = [[NSMenuItem alloc] init];

    auto* process_name = [[NSProcessInfo processInfo] processName];
    auto* submenu = [[NSMenu alloc] initWithTitle:process_name];

    [submenu addItem:Ladybird::create_application_menu_item(WebView::Application::the().open_about_page_action())];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:Ladybird::create_application_menu_item(WebView::Application::the().open_settings_page_action())];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Hide %@", process_name]
                                                action:@selector(hide:)
                                         keyEquivalent:@"h"]];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:[NSString stringWithFormat:@"Quit %@", process_name]
                                                action:@selector(terminate:)
                                         keyEquivalent:@"q"]];

    [menu setSubmenu:submenu];
    return menu;
}

- (NSMenuItem*)createFileMenu
{
    auto* menu = [[NSMenuItem alloc] init];
    auto* submenu = [[NSMenu alloc] initWithTitle:@"File"];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"New Tab"
                                                action:@selector(createNewTab:)
                                         keyEquivalent:@"t"]];
    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Close Tab"
                                                action:@selector(closeCurrentTab:)
                                         keyEquivalent:@"w"]];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Open Location"
                                                action:@selector(openLocation:)
                                         keyEquivalent:@"l"]];

    [menu setSubmenu:submenu];
    return menu;
}

- (NSMenuItem*)createEditMenu
{
    auto* menu = [[NSMenuItem alloc] init];
    auto* submenu = [[NSMenu alloc] initWithTitle:@"Edit"];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Undo"
                                                action:@selector(undo:)
                                         keyEquivalent:@"z"]];
    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Redo"
                                                action:@selector(redo:)
                                         keyEquivalent:@"y"]];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Cut"
                                                action:@selector(cut:)
                                         keyEquivalent:@"x"]];

    [submenu addItem:Ladybird::create_application_menu_item(WebView::Application::the().copy_selection_action())];
    [submenu addItem:Ladybird::create_application_menu_item(WebView::Application::the().paste_action())];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:Ladybird::create_application_menu_item(WebView::Application::the().select_all_action())];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Find..."
                                                action:@selector(find:)
                                         keyEquivalent:@"f"]];
    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Find Next"
                                                action:@selector(findNextMatch:)
                                         keyEquivalent:@"g"]];
    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Find Previous"
                                                action:@selector(findPreviousMatch:)
                                         keyEquivalent:@"G"]];
    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Use Selection for Find"
                                                action:@selector(useSelectionForFind:)
                                         keyEquivalent:@"e"]];

    [menu setSubmenu:submenu];
    return menu;
}

- (NSMenuItem*)createViewMenu
{
    auto* menu = [[NSMenuItem alloc] init];
    auto* submenu = [[NSMenu alloc] initWithTitle:@"View"];

    auto* zoom_menu = Ladybird::create_application_menu(WebView::Application::the().zoom_menu());
    auto* zoom_menu_item = [[NSMenuItem alloc] initWithTitle:[zoom_menu title]
                                                      action:nil
                                               keyEquivalent:@""];
    [zoom_menu_item setSubmenu:zoom_menu];

    auto* color_scheme_menu = Ladybird::create_application_menu(WebView::Application::the().color_scheme_menu());
    auto* color_scheme_menu_item = [[NSMenuItem alloc] initWithTitle:[color_scheme_menu title]
                                                              action:nil
                                                       keyEquivalent:@""];
    [color_scheme_menu_item setSubmenu:color_scheme_menu];

    auto* contrast_menu = Ladybird::create_application_menu(WebView::Application::the().contrast_menu());
    auto* contrast_menu_item = [[NSMenuItem alloc] initWithTitle:[contrast_menu title]
                                                          action:nil
                                                   keyEquivalent:@""];
    [contrast_menu_item setSubmenu:contrast_menu];

    auto* motion_menu = Ladybird::create_application_menu(WebView::Application::the().motion_menu());
    auto* motion_menu_item = [[NSMenuItem alloc] initWithTitle:[motion_menu title]
                                                        action:nil
                                                 keyEquivalent:@""];
    [motion_menu_item setSubmenu:motion_menu];

    [submenu addItem:zoom_menu_item];
    [submenu addItem:[NSMenuItem separatorItem]];
    [submenu addItem:color_scheme_menu_item];
    [submenu addItem:contrast_menu_item];
    [submenu addItem:motion_menu_item];
    [submenu addItem:[NSMenuItem separatorItem]];

    [menu setSubmenu:submenu];
    return menu;
}

- (NSMenuItem*)createHistoryMenu
{
    auto* menu = [[NSMenuItem alloc] init];

    auto* submenu = [[NSMenu alloc] initWithTitle:@"History"];
    [submenu setAutoenablesItems:NO];

    [submenu addItem:Ladybird::create_application_menu_item(WebView::Application::the().reload_action())];
    self.reopen_recently_closed_tab_item = [[NSMenuItem alloc] initWithTitle:REOPEN_RECENTLY_CLOSED_TAB_MENU_ITEM_TITLE
                                                                      action:@selector(reopenRecentlyClosedTab:)
                                                               keyEquivalent:@"T"];
    [self updateReopenRecentlyClosedMenuItem];
    [submenu addItem:self.reopen_recently_closed_tab_item];
    [submenu addItem:[NSMenuItem separatorItem]];

    [submenu addItem:[[NSMenuItem alloc] initWithTitle:@"Clear History"
                                                action:@selector(clearHistory:)
                                         keyEquivalent:@""]];

    [menu setSubmenu:submenu];
    return menu;
}

- (NSMenuItem*)createBookmarksMenu
{
    auto* menu = [[NSMenuItem alloc] init];

    self.bookmarks_menu = Ladybird::create_application_menu(WebView::Application::the().bookmarks_menu());
    [menu setSubmenu:self.bookmarks_menu];

    return menu;
}

- (NSMenuItem*)createInspectMenu
{
    auto* menu = [[NSMenuItem alloc] init];

    auto* submenu = Ladybird::create_application_menu(WebView::Application::the().inspect_menu());
    [menu setSubmenu:submenu];

    return menu;
}

- (NSMenuItem*)createDebugMenu
{
    auto* menu = [[NSMenuItem alloc] init];

    auto* submenu = Ladybird::create_application_menu(WebView::Application::the().debug_menu());
    [menu setSubmenu:submenu];

    return menu;
}

- (NSMenuItem*)createWindowMenu
{
    auto* menu = [[NSMenuItem alloc] init];
    auto* submenu = [[NSMenu alloc] initWithTitle:@"Window"];

    [NSApp setWindowsMenu:submenu];

    [menu setSubmenu:submenu];
    return menu;
}

- (NSMenuItem*)createHelpMenu
{
    auto* menu = [[NSMenuItem alloc] init];
    auto* submenu = [[NSMenu alloc] initWithTitle:@"Help"];

    [NSApp setHelpMenu:submenu];

    [menu setSubmenu:submenu];
    return menu;
}

#pragma mark - NSApplicationDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification
{
    auto const& browser_options = WebView::Application::browser_options();

    if (browser_options.devtools_port.has_value())
        [self onDevtoolsEnabled];

    Tab* tab = nil;

    for (auto const& url : browser_options.urls) {
        auto activate_tab = tab == nil ? Web::HTML::ActivateTab::Yes : Web::HTML::ActivateTab::No;

        auto* controller = [self createNewTab:url
                                      fromTab:tab
                                  activateTab:activate_tab];

        tab = (Tab*)[controller window];
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification
{
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender
{
    return YES;
}

- (void)applicationDidChangeScreenParameters:(NSNotification*)notification
{
    for (TabController* controller in self.managed_tabs) {
        auto* tab = (Tab*)[controller window];
        [[tab web_view] handleDisplayRefreshRateChange];
    }
}

- (BOOL)validateMenuItem:(NSMenuItem*)menu
{
    SEL action = [menu action];

    if (action == @selector(closeCurrentTab:)) {
        return [[NSApp keyWindow] isKindOfClass:[Tab class]];
    }

    return YES;
}

@end
