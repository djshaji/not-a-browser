#include "ui/MainWindow.hpp"
#include <initializer_list>
#include <functional>
#ifdef HAVE_PLAYER_TESTS
#include "ui/PlayerView.hpp"
#include <clocale>
#include <glib/gstdio.h>
#include <string>
#endif
#ifdef HAVE_LIBRARY_TESTS
#include "database/LibraryRepository.hpp"
#include <filesystem>
#include <functional>
#include <string>
#endif

namespace {
GtkWidget* findWidget(GtkWidget* root, GType type) {
    if (G_TYPE_CHECK_INSTANCE_TYPE(root, type)) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* found = nullptr;
    for (GList* child = children; child && !found; child = child->next) {
        found = findWidget(GTK_WIDGET(child->data), type);
    }
    g_list_free(children);
    return found;
}
#if defined(HAVE_LIBRARY_TESTS) || defined(HAVE_PLAYER_TESTS)
GtkWidget* findNamed(GtkWidget* root, const char* name) {
    if (g_strcmp0(gtk_widget_get_name(root),name)==0) return root;
    if (!GTK_IS_CONTAINER(root)) return nullptr;
    GList* children = gtk_container_get_children(GTK_CONTAINER(root));
    GtkWidget* found = nullptr;
    for (auto* child = children; child && !found; child = child->next)
        found = findNamed(GTK_WIDGET(child->data),name);
    g_list_free(children);
    return found;
}
#endif
#if defined(HAVE_LIBRARY_TESTS) || defined(HAVE_PLAYER_TESTS)
void waitUntil(int line, const std::function<bool()>& predicate) {
    const gint64 deadline = g_get_monotonic_time() + 10 * G_TIME_SPAN_SECOND;
    while (!predicate() && g_get_monotonic_time() < deadline) {
        while (gtk_events_pending()) gtk_main_iteration();
        g_usleep(1000);
    }
    if (!predicate()) g_error("UI check at line %d timed out", line);
}
#define waitFor(...) waitUntil(__LINE__, __VA_ARGS__)
#endif
}
int main(int argc, char** argv) {
    // Keep direct test runs independent of third-party desktop theme warnings.
    g_setenv("GTK_THEME", "Adwaita", TRUE);
#ifdef HAVE_LIBRARY_TESTS
    gchar* testDirectory = g_dir_make_tmp("stream-gallery-ui-XXXXXX",nullptr);
    g_assert_nonnull(testDirectory);
    g_setenv("XDG_DATA_HOME",testDirectory,TRUE);
    g_setenv("XDG_CACHE_HOME",testDirectory,TRUE);
#endif
    if (!gtk_init_check(&argc, &argv)) {
#ifdef HAVE_LIBRARY_TESTS
        std::filesystem::remove_all(testDirectory);
        g_free(testDirectory);
#endif
        return 77;
    }
#ifdef HAVE_LIBRARY_TESTS
    const auto libraryDirectory = std::filesystem::path(testDirectory) / "stream-gallery";
    std::filesystem::create_directories(libraryDirectory);
    {
        streamgallery::Database db((libraryDirectory / "library.db").string());
        streamgallery::LibraryRepository library(db);
        for (int index = 0; index < 30; ++index)
            library.bookmark("https://example.com/" + std::to_string(index),
                index == 0 ? "Unique concert" : "Video " + std::to_string(index));
    }
#endif
    auto* app = gtk_application_new("org.streamgallery.UiTest", G_APPLICATION_NON_UNIQUE);
    GError* error = nullptr;
    g_assert_true(g_application_register(G_APPLICATION(app), nullptr, &error));
    g_assert_no_error(error);
#ifdef HAVE_PLAYER_TESTS
    // A desktop locale with decimal commas used to make mpv_create() fail.
    if (!std::setlocale(LC_NUMERIC, "de_DE.UTF-8"))
        g_test_message("Decimal-comma locale unavailable; still checking C numeric initialization");
#endif
    for (int iteration = 0; iteration < 2; ++iteration) {
        GtkWidget* window = streamgallery::createMainWindow(app);
#ifdef HAVE_PLAYER_TESTS
        g_assert_cmpstr(std::setlocale(LC_NUMERIC, nullptr), ==, "C");
#endif
        gtk_widget_show_all(window);
        auto* stack = GTK_STACK(findWidget(window, GTK_TYPE_STACK));
        auto* sidebar = GTK_STACK_SIDEBAR(findWidget(window, GTK_TYPE_STACK_SIDEBAR));
        g_assert_nonnull(stack);
        g_assert_true(gtk_stack_sidebar_get_stack(sidebar) == stack);
        g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "gallery");
        const char* shortcutPages[] = {"gallery", "browser", "player", "files"};
        for (guint index = 0; index < 4; ++index) {
            GdkEventKey navigationKey{};
            navigationKey.type = GDK_KEY_PRESS;
            navigationKey.keyval = GDK_KEY_1 + index;
            navigationKey.state = GDK_MOD1_MASK;
            gboolean handled = FALSE;
            g_signal_emit_by_name(window, "key-press-event", &navigationKey, &handled);
            g_assert_true(handled);
            g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, shortcutPages[index]);
        }

        for (const char* page : {"browser", "player", "files", "gallery"}) {
            g_assert_nonnull(gtk_stack_get_child_by_name(stack, page));
            gtk_stack_set_visible_child_name(stack, page);
            g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, page);
        }
#ifdef HAVE_LIBRARY_TESTS
        auto* status = GTK_LABEL(findNamed(window,"library-status"));
        auto* flow = GTK_CONTAINER(findNamed(window,"library-videos"));
        g_assert_nonnull(status); g_assert_nonnull(flow);
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"1–24 of 30 videos")==0; });
        auto childCount = [&] {
            GList* children = gtk_container_get_children(flow);
            const guint count = g_list_length(children); g_list_free(children); return count;
        };
        g_assert_cmpuint(childCount(),==,24);
        gtk_button_clicked(GTK_BUTTON(findNamed(window,"library-next")));
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"25–30 of 30 videos")==0; });
        g_assert_cmpuint(childCount(),==,6);
        gtk_combo_box_set_active(GTK_COMBO_BOX(findNamed(window,"library-mode")),1);
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"25–30 of 30 videos")==0; });
        g_assert_cmpuint(gtk_flow_box_get_max_children_per_line(GTK_FLOW_BOX(flow)),==,1);
        gtk_entry_set_text(GTK_ENTRY(findNamed(window,"library-search")),"Unique");
        waitFor([&] { return g_strcmp0(gtk_label_get_text(status),"1–1 of 1 videos")==0; });
        g_assert_cmpuint(childCount(),==,1);
        auto* edit = findNamed(window, "library-edit");
        g_assert_false(gtk_widget_get_sensitive(edit));
        gtk_flow_box_select_child(GTK_FLOW_BOX(flow), gtk_flow_box_get_child_at_index(GTK_FLOW_BOX(flow), 0));
        g_assert_true(gtk_widget_get_sensitive(edit));
        gtk_button_clicked(GTK_BUTTON(edit));
        auto findEditor = [] {
            GtkWidget* editor = nullptr;
            GList* toplevels = gtk_window_list_toplevels();
            for (auto* item = toplevels; item; item = item->next)
                if (g_strcmp0(gtk_widget_get_name(GTK_WIDGET(item->data)), "library-editor") == 0)
                    editor = GTK_WIDGET(item->data);
            g_list_free(toplevels);
            return editor;
        };
        auto* editor = findEditor();
        g_assert_nonnull(editor);
        std::string originalTitle = gtk_entry_get_text(GTK_ENTRY(findNamed(editor, "library-edit-title")));
        gtk_entry_set_text(GTK_ENTRY(findNamed(editor, "library-edit-title")), "Discard this edit");
        gtk_dialog_response(GTK_DIALOG(editor), GTK_RESPONSE_CANCEL);
        g_assert_null(findEditor());
        gtk_button_clicked(GTK_BUTTON(edit));
        editor = findEditor();
        g_assert_nonnull(editor);
        g_assert_cmpstr(gtk_entry_get_text(GTK_ENTRY(findNamed(editor, "library-edit-title"))), ==, originalTitle.c_str());
        const char* editedTitle = iteration == 0 ? "Unique edited concert" : "Renamed concert";
        gtk_entry_set_text(GTK_ENTRY(findNamed(editor, "library-edit-title")), editedTitle);
        auto* description = GTK_TEXT_VIEW(findNamed(editor, "library-edit-description"));
        gtk_text_buffer_set_text(gtk_text_view_get_buffer(description), "Persisted description", -1);
        if (const char* screenshot = g_getenv("STREAM_GALLERY_EDITOR_SCREENSHOT")) {
            while (gtk_events_pending()) gtk_main_iteration();
            GdkPixbuf* pixels = gdk_pixbuf_get_from_window(gtk_widget_get_window(editor), 0, 0,
                gtk_widget_get_allocated_width(editor), gtk_widget_get_allocated_height(editor));
            g_assert_nonnull(pixels);
            g_assert_true(gdk_pixbuf_save(pixels, screenshot, "png", nullptr, nullptr));
            g_object_unref(pixels);
        }
        gtk_dialog_response(GTK_DIALOG(editor), GTK_RESPONSE_ACCEPT);
        // Changing pages during a save must not discard it or target another record.
        gtk_stack_set_visible_child_name(stack, "files");
        waitFor([&] {
            streamgallery::Database db((libraryDirectory / "library.db").string());
            auto page = streamgallery::LibraryRepository(db).page("Persisted", streamgallery::VideoSort::Newest, 0);
            return page.total == 1 && page.items[0].title == editedTitle;
        });
        waitFor([&] { return !findEditor() && !gtk_widget_get_sensitive(edit) &&
            (iteration == 0 ? g_strcmp0(gtk_label_get_text(status), "1–1 of 1 videos") == 0 :
                g_str_has_prefix(gtk_label_get_text(status), "No videos found.")); });
        g_assert_cmpuint(childCount(), ==, iteration == 0 ? 1 : 0);
        gtk_stack_set_visible_child_name(stack, "gallery");
#endif
#ifdef HAVE_PLAYER_TESTS
        g_assert_nonnull(findWidget(window, GTK_TYPE_GL_AREA));
        auto* url = GTK_ENTRY(findNamed(window, "player-url"));
        auto* openUrl = GTK_BUTTON(findNamed(window, "player-open-url"));
        auto* playerStatus = GTK_LABEL(findNamed(window, "player-status"));
        gtk_stack_set_visible_child_name(stack, "player");
        while (gtk_events_pending()) gtk_main_iteration();
        g_test_message("Player renderer status: %s", gtk_label_get_text(playerStatus));
        g_assert_cmpstr(gtk_label_get_text(playerStatus), ==, "Player ready");
        g_assert_nonnull(url);
        g_assert_nonnull(openUrl);
        auto sendKey = [&](guint key, GdkModifierType modifiers, guint16 hardware = 0, guint8 group = 0) {
            GdkEventKey event{};
            event.type = GDK_KEY_PRESS;
            event.window = gtk_widget_get_window(window);
            event.keyval = key;
            event.state = modifiers;
            event.hardware_keycode = hardware;
            event.group = group;
            gboolean handled = FALSE;
            g_signal_emit_by_name(window, "key-press-event", &event, &handled);
            return handled;
        };
        auto* playPause = GTK_BUTTON(findNamed(window, "player-play-pause"));
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Pause") == 0; });
        // Text entry takes priority over single-key playback and quit shortcuts.
        gtk_stack_set_visible_child_name(stack, "gallery");
#ifdef HAVE_LIBRARY_TESTS
        gtk_widget_grab_focus(findNamed(window, "library-search"));
#endif
        gtk_stack_set_visible_child_name(stack, "player");
        gtk_widget_grab_focus(GTK_WIDGET(url));
        for (guint key : {GDK_KEY_p, GDK_KEY_m, GDK_KEY_f, GDK_KEY_q, GDK_KEY_1,
                         GDK_KEY_plus, GDK_KEY_Left}) {
            sendKey(key, static_cast<GdkModifierType>(0));
            g_assert_false(gtk_widget_in_destruction(window));
            g_assert_cmpstr(gtk_label_get_text(playerStatus), ==, "Player ready");
            g_assert_cmpstr(gtk_button_get_label(playPause), ==, "Pause");
            g_assert_true(gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(findNamed(window, "player-mute"))));
        }
        g_assert_true(sendKey(GDK_KEY_1, GDK_MOD1_MASK));
        g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "gallery");
        gtk_window_set_focus(GTK_WINDOW(window), nullptr);
        g_assert_true(sendKey(GDK_KEY_P, GDK_SHIFT_MASK));
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Play") == 0; });
        g_assert_true(sendKey(GDK_KEY_p, static_cast<GdkModifierType>(0)));
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Pause") == 0; });
        for (guint key : {GDK_KEY_Up, GDK_KEY_Down, GDK_KEY_Left, GDK_KEY_Right,
                         GDK_KEY_1, GDK_KEY_2, GDK_KEY_3, GDK_KEY_4, GDK_KEY_5,
                         GDK_KEY_6, GDK_KEY_7, GDK_KEY_8, GDK_KEY_9, GDK_KEY_KP_5}) {
            g_assert_true(sendKey(key, static_cast<GdkModifierType>(0)));
            g_assert_true(sendKey(key, GDK_SHIFT_MASK));
        }
        // Exercise real shifted digit symbols instead of synthetic Shift+digit keyvals.
        GdkKeymap* keymap = gdk_keymap_get_for_display(gtk_widget_get_display(window));
        auto* muteButton = GTK_TOGGLE_BUTTON(findNamed(window, "player-mute"));
        auto* volumeSlider = GTK_RANGE(findNamed(window, "player-volume"));
        g_assert_true(gtk_toggle_button_get_active(muteButton));
        g_assert_true(sendKey(GDK_KEY_m, static_cast<GdkModifierType>(0)));
        waitFor([&] { return !gtk_toggle_button_get_active(muteButton); });
        g_assert_true(sendKey(GDK_KEY_M, GDK_SHIFT_MASK));
        waitFor([&] { return gtk_toggle_button_get_active(muteButton); });
        // Volume changes are reflected by libmpv property observations.
        g_assert_true(sendKey(GDK_KEY_plus, GDK_SHIFT_MASK));
        g_assert_true(sendKey(GDK_KEY_minus, static_cast<GdkModifierType>(0)));
        waitFor([&] { return gtk_range_get_value(volumeSlider) == 95; });
        GdkKeymapKey* plusKeys = nullptr;
        gint plusCount = 0;
        g_assert_true(gdk_keymap_get_entries_for_keyval(keymap, GDK_KEY_plus, &plusKeys, &plusCount));
        g_assert_true(sendKey(GDK_KEY_plus, GDK_SHIFT_MASK, plusKeys[0].keycode, plusKeys[0].group));
        g_free(plusKeys);
        waitFor([&] { return gtk_range_get_value(volumeSlider) == 100; });
        gtk_range_set_value(volumeSlider, 0);
        g_assert_true(sendKey(GDK_KEY_KP_Subtract, static_cast<GdkModifierType>(0)));
        g_assert_true(sendKey(GDK_KEY_KP_Add, static_cast<GdkModifierType>(0)));
        waitFor([&] { return gtk_range_get_value(volumeSlider) == 5; });
        g_assert_true(gtk_toggle_button_get_active(muteButton));
        gtk_button_clicked(GTK_BUTTON(muteButton));
        waitFor([&] { return !gtk_toggle_button_get_active(muteButton); });
        for (guint digit = GDK_KEY_1; digit <= GDK_KEY_9; ++digit) {
            GdkKeymapKey* keys = nullptr;
            gint count = 0;
            g_assert_true(gdk_keymap_get_entries_for_keyval(keymap, digit, &keys, &count));
            guint shifted = 0;
            g_assert_true(gdk_keymap_translate_keyboard_state(keymap, keys[0].keycode,
                GDK_SHIFT_MASK, keys[0].group, &shifted, nullptr, nullptr, nullptr));
            g_assert_true(sendKey(shifted, GDK_SHIFT_MASK, keys[0].keycode, keys[0].group));
            g_free(keys);
        }
        auto* playerView = findNamed(window, "player-view");
        auto* glArea = findWidget(playerView, GTK_TYPE_GL_AREA);
        int unrealizeCount = 0;
        g_signal_connect(glArea, "unrealize", G_CALLBACK(+[](GtkWidget*, gpointer value) {
            ++*static_cast<int*>(value);
        }), &unrealizeCount);
        for (guint key : {GDK_KEY_F, GDK_KEY_f}) {
            g_assert_true(sendKey(key, key == GDK_KEY_F ? GDK_SHIFT_MASK : static_cast<GdkModifierType>(0)));
            g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "player");
            g_assert_false(gtk_widget_get_visible(GTK_WIDGET(sidebar)));
            g_assert_false(gtk_widget_get_visible(gtk_window_get_titlebar(GTK_WINDOW(window))));
            g_assert_false(gtk_widget_get_visible(GTK_WIDGET(playPause)) &&
                gtk_widget_get_visible(gtk_widget_get_parent(GTK_WIDGET(playPause))));
            g_assert_false(gtk_widget_get_visible(GTK_WIDGET(playerStatus)));
            g_assert_true(gtk_widget_get_visible(glArea));
            g_assert_false(gtk_window_get_decorated(GTK_WINDOW(window)));
            if (key == GDK_KEY_F) g_assert_true(sendKey(GDK_KEY_Escape, static_cast<GdkModifierType>(0)));
            else g_assert_true(sendKey(GDK_KEY_f, static_cast<GdkModifierType>(0)));
            g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "gallery");
            g_assert_true(gtk_widget_get_visible(GTK_WIDGET(sidebar)));
            g_assert_true(gtk_widget_get_visible(GTK_WIDGET(playerStatus)));
            g_assert_true(gtk_widget_get_visible(gtk_widget_get_parent(GTK_WIDGET(playPause))));
            g_assert_true(gtk_window_get_decorated(GTK_WINDOW(window)));
            g_assert_cmpint(unrealizeCount, ==, 0);
        }
        // A tiny YUV video exercises EOF retention and replay without external tools.
        gchar* fixtureDirectory = g_dir_make_tmp("stream-gallery-player-XXXXXX", nullptr);
        g_assert_nonnull(fixtureDirectory);
        gchar* fixture = g_build_filename(fixtureDirectory, "short.y4m", nullptr);
        std::string video = "YUV4MPEG2 W16 H16 F2:1 Ip A1:1 C420jpeg\n";
        for (int frame = 0; frame < 2; ++frame) {
            video += "FRAME\n";
            video += std::string(256, static_cast<char>(frame ? 180 : 80));
            video += std::string(128, static_cast<char>(128));
        }
        g_assert_true(g_file_set_contents(fixture, video.data(), video.size(), nullptr));
        gtk_stack_set_visible_child_name(stack, "player");
        streamgallery::openPlayerFile(playerView, fixture);
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Replay") == 0; });
        auto* seek = findNamed(window, "player-position");
        g_assert_true(gtk_widget_get_sensitive(seek));
        g_assert_cmpfloat(gtk_range_get_value(GTK_RANGE(seek)), >, 0);
        g_assert_cmpint(unrealizeCount, ==, 0);
        g_assert_true(sendKey(GDK_KEY_p, static_cast<GdkModifierType>(0)));
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Pause") == 0; });
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Replay") == 0; });
        g_assert_true(gtk_widget_get_sensitive(seek));
        // A seekable ten-minute fixture verifies the new transport controls.
        std::string longVideo = "YUV4MPEG2 W16 H16 F2:1 Ip A1:1 C420jpeg\n";
        for (int frame = 0; frame < 1200; ++frame)
            longVideo += "FRAME\n" + std::string(256, static_cast<char>(100)) + std::string(128, static_cast<char>(128));
        gchar* longFixture = g_build_filename(fixtureDirectory, "transport.y4m", nullptr);
        g_assert_true(g_file_set_contents(longFixture, longVideo.data(), longVideo.size(), nullptr));
        streamgallery::openPlayerFile(playerView, longFixture);
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Pause") == 0 &&
            gtk_adjustment_get_upper(gtk_range_get_adjustment(GTK_RANGE(seek))) > 590; });
        auto click = [&](const char* name) { gtk_button_clicked(GTK_BUTTON(findNamed(window, name))); };
        auto* reverse = GTK_TOGGLE_BUTTON(findNamed(window, "player-reverse"));
        auto* fast = GTK_TOGGLE_BUTTON(findNamed(window, "player-fast-forward"));
        auto* speed = GTK_COMBO_BOX(findNamed(window, "player-speed"));
        click("player-forward");
        waitFor([&] { return gtk_range_get_value(GTK_RANGE(seek)) > 290; });
        click("player-rewind");
        waitFor([&] { return gtk_range_get_value(GTK_RANGE(seek)) < 10; });
        click("player-forward");
        waitFor([&] { return gtk_range_get_value(GTK_RANGE(seek)) > 290; });
        const double beforeReverse = gtk_range_get_value(GTK_RANGE(seek));
        click("player-reverse");
        waitFor([&] { return gtk_toggle_button_get_active(reverse) && gtk_combo_box_get_active(speed) == 3; });
        waitFor([&] { return gtk_range_get_value(GTK_RANGE(seek)) < beforeReverse - 1; });
        click("player-fast-forward");
        waitFor([&] { return gtk_toggle_button_get_active(fast) && !gtk_toggle_button_get_active(reverse); });
        click("player-fast-forward");
        waitFor([&] { return !gtk_toggle_button_get_active(fast) && gtk_combo_box_get_active(speed) == 1; });
        click("player-play-pause");
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Play") == 0; });
        click("player-play-pause");
        waitFor([&] { return g_strcmp0(gtk_button_get_label(playPause), "Pause") == 0; });
        if (const char* screenshot = g_getenv("STREAM_GALLERY_PLAYER_SCREENSHOT")) {
            GdkPixbuf* pixels = gdk_pixbuf_get_from_window(gtk_widget_get_window(window), 0, 0,
                gtk_widget_get_allocated_width(window), gtk_widget_get_allocated_height(window));
            g_assert_nonnull(pixels);
            g_assert_true(gdk_pixbuf_save(pixels, screenshot, "png", nullptr, nullptr));
            g_object_unref(pixels);
        }
        // Returning from playback must keep the file browser's location and selection.
        gtk_stack_set_visible_child_name(stack, "files");
        auto* chooser = GTK_FILE_CHOOSER(findNamed(window, "files-chooser"));
        GSList* filters = gtk_file_chooser_list_filters(chooser);
        gtk_file_chooser_set_filter(chooser, GTK_FILE_FILTER(g_slist_last(filters)->data));
        g_slist_free(filters);
        g_assert_true(gtk_file_chooser_set_filename(chooser, longFixture));
        auto selectedFile = [&] {
            gchar* selected = gtk_file_chooser_get_filename(chooser);
            const bool matches = g_strcmp0(selected, longFixture) == 0;
            g_free(selected);
            return matches;
        };
        waitFor(selectedFile);
        gtk_button_clicked(GTK_BUTTON(findNamed(window, "files-play")));
        g_assert_cmpstr(gtk_stack_get_visible_child_name(stack), ==, "player");
        gtk_stack_set_visible_child_name(stack, "files");
        waitFor(selectedFile);
        gchar* folder = gtk_file_chooser_get_current_folder(chooser);
        g_assert_cmpstr(folder, ==, fixtureDirectory);
        g_free(folder);
        // Also preserve browsing state through a page switch without playback.
        gtk_stack_set_visible_child_name(stack, "gallery");
        gtk_stack_set_visible_child_name(stack, "files");
        waitFor(selectedFile);
        gtk_stack_set_visible_child_name(stack, "player");
        gtk_window_set_focus(GTK_WINDOW(window), nullptr);
        g_remove(longFixture); g_free(longFixture);
        g_remove(fixture);
        g_rmdir(fixtureDirectory);
        g_free(fixture);
        g_free(fixtureDirectory);
        g_assert_false(sendKey(GDK_KEY_p, GDK_CONTROL_MASK));
        g_assert_false(sendKey(GDK_KEY_f, GDK_MOD1_MASK));
        // GTK's focused entry can consume Super+1 after our handler declines it.
        GdkEventKey reservedKey{};
        reservedKey.keyval = GDK_KEY_1;
        reservedKey.state = GDK_SUPER_MASK;
        g_assert_false(streamgallery::handlePlayerKey(findNamed(window, "player-view"), &reservedKey));
        reservedKey.keyval = GDK_KEY_q;
        reservedKey.state = 0;
        g_assert_false(streamgallery::handlePlayerKey(findNamed(window, "player-view"), &reservedKey));
        for (const char* invalid : {"file:///etc/passwd", "https://user:password@example.com/video", "not a URL"}) {
            gtk_entry_set_text(url, invalid);
            gtk_button_clicked(openUrl);
            g_assert_cmpstr(gtk_label_get_text(playerStatus), ==,
                "Enter an HTTP/HTTPS media URL without credentials");
        }
#endif
        auto* toggle = GTK_SWITCH(findWidget(window, GTK_TYPE_SWITCH));
        g_assert_nonnull(toggle);
        g_assert_cmpstr(atk_object_get_name(gtk_widget_get_accessible(GTK_WIDGET(toggle))),
                        ==, "Dark theme");
        for (gboolean dark : {1, 0}) {
            gtk_switch_set_active(toggle, dark);
            gboolean actual = !dark;
            g_object_get(gtk_settings_get_default(), "gtk-application-prefer-dark-theme", &actual, nullptr);
            g_assert_true(actual == dark);
        }
        while (gtk_events_pending()) gtk_main_iteration();
        // Exercise immediate quit with both q and Shift+Q, preserving cleanup.
        gtk_window_set_focus(GTK_WINDOW(window), nullptr);
#ifdef HAVE_PLAYER_TESTS
        if (iteration == 1) g_assert_true(sendKey(GDK_KEY_f, static_cast<GdkModifierType>(0)));
#endif
        bool destroyed = false;
        g_signal_connect(window, "destroy", G_CALLBACK(+[](GtkWidget*, gpointer value) {
            *static_cast<bool*>(value) = true;
        }), &destroyed);
        GdkEventKey quitKey{};
        quitKey.type = GDK_KEY_PRESS;
        quitKey.window = gtk_widget_get_window(window);
        quitKey.keyval = iteration == 0 ? GDK_KEY_q : GDK_KEY_Q;
        quitKey.state = iteration == 0 ? 0 : GDK_SHIFT_MASK;
        gboolean handled = FALSE;
        g_object_ref(window);
        g_signal_emit_by_name(window, "key-press-event", &quitKey, &handled);
        g_assert_true(handled);
        g_assert_true(destroyed);
        g_object_unref(window);
    }
    g_object_unref(app);
#ifdef HAVE_LIBRARY_TESTS
    std::filesystem::remove_all(testDirectory);
    g_free(testDirectory);
#endif
    return 0;
}
