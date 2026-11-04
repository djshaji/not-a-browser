#include "ui/PlayerView.hpp"
#include <epoxy/gl.h>
#include <epoxy/glx.h>
#include <epoxy/egl.h>
#include <gdk/gdkwayland.h>
#include <mpv/client.h>
#include <mpv/render_gl.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <optional>

namespace streamgallery {
namespace {
class PlayerView {
public:
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* area = gtk_gl_area_new();
    GtkWidget* status = gtk_label_new("Open a local video or a direct media URL");
    GtkWidget* position = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 1);
    GtkWidget* volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
    GtkWidget* mute = gtk_toggle_button_new_with_label("Muted");
    GtkWidget* play = gtk_button_new_with_label("Play");
    GtkWidget* speed = gtk_combo_box_text_new();
    GtkWidget* reverse = gtk_toggle_button_new();
    GtkWidget* fast = gtk_toggle_button_new();
    bool paused = false;
    bool backwards = false;
    double playbackSpeed = 1;

    mpv_handle* mpv = nullptr;
    mpv_render_context* renderer = nullptr;
    guint timer = 0;
    bool updating = false;
    bool loaded = false;
    bool atEnd = false;
    bool fullscreen = false;
    bool decorated = true;
    GtkWidget* previousPage = nullptr;
    GtkStackTransitionType previousTransition = GTK_STACK_TRANSITION_TYPE_NONE;
    struct HiddenWidget { GtkWidget* widget; bool visible; bool noShowAll; };
    std::vector<HiddenWidget> hiddenWidgets;
    double duration = 0;
    std::string rendererError;
    struct PendingLoad { std::string path, referrer, userAgent; };
    std::optional<PendingLoad> pendingLoad;

    PlayerView() {
        auto* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        auto* open = gtk_button_new_with_label("Open file…");
        auto* url = gtk_entry_new();
        gtk_entry_set_placeholder_text(GTK_ENTRY(url), "Direct HTTP/HTTPS media URL");
        auto* load = gtk_button_new_with_label("Open URL");
        gtk_box_pack_start(GTK_BOX(toolbar), open, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(toolbar), url, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(toolbar), load, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(root), toolbar, FALSE, FALSE, 0);
        gtk_gl_area_set_required_version(GTK_GL_AREA(area), 3, 2);
        gtk_gl_area_set_auto_render(GTK_GL_AREA(area), FALSE);
        gtk_widget_set_size_request(area, 320, 180);
        gtk_box_pack_start(GTK_BOX(root), area, TRUE, TRUE, 0);
        g_object_ref(area);
        gtk_box_pack_start(GTK_BOX(root), position, FALSE, FALSE, 0);
        gtk_scale_set_draw_value(GTK_SCALE(position), FALSE);
        auto* controls = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        gtk_range_set_value(GTK_RANGE(volume), 100);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mute), TRUE);
        gtk_widget_set_name(position, "player-position");
        gtk_widget_set_name(volume, "player-volume");
        gtk_widget_set_name(mute, "player-mute");
        gtk_widget_set_size_request(volume, 120, -1);
        gtk_widget_set_name(speed, "player-speed");
        for (const char* value : {"0.5", "1.0", "1.5", "2.0"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(speed), value);
        gtk_combo_box_set_active(GTK_COMBO_BOX(speed), 1);
        auto* full = gtk_button_new_with_label("Fullscreen");
        auto* transport = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 14);
        gtk_widget_set_halign(transport, GTK_ALIGN_CENTER);
        gtk_widget_set_name(transport, "player-transport");
        auto* rewind = gtk_button_new();
        auto* forward = gtk_button_new();
        auto styleButton = [](GtkWidget* button, const char* icon, const char* label,
                              const char* name, const char* tooltip, int size) {
            gtk_button_set_label(GTK_BUTTON(button), label);
            auto* image = gtk_image_new_from_icon_name(icon, GTK_ICON_SIZE_BUTTON);
            gtk_image_set_pixel_size(GTK_IMAGE(image), size == 96 ? 34 : 22);
            gtk_button_set_image(GTK_BUTTON(button), image);
            gtk_button_set_always_show_image(GTK_BUTTON(button), TRUE);
            gtk_button_set_image_position(GTK_BUTTON(button), GTK_POS_TOP);
            gtk_widget_set_name(button, name);
            gtk_widget_set_tooltip_text(button, tooltip);
            atk_object_set_name(gtk_widget_get_accessible(button), tooltip);
            gtk_style_context_add_class(gtk_widget_get_style_context(button), "transport-button");
            gtk_widget_set_size_request(button, size, size);
            gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
        };
        styleButton(rewind, "media-skip-backward-symbolic", "−5 min", "player-rewind", "Rewind 5 minutes", 68);
        styleButton(reverse, "media-seek-backward-symbolic", "2×", "player-reverse", "Play backwards at 2×; click again for normal playback", 68);
        styleButton(play, "media-playback-start-symbolic", "Play", "player-play-pause", "Play", 96);
        styleButton(fast, "media-seek-forward-symbolic", "2×", "player-fast-forward", "Play forwards at 2×; click again for normal playback", 68);
        styleButton(forward, "media-skip-forward-symbolic", "+5 min", "player-forward", "Forward 5 minutes", 68);
        gtk_style_context_add_class(gtk_widget_get_style_context(play), "transport-primary");
        for (auto* button : {rewind, reverse, play, fast, forward})
            gtk_box_pack_start(GTK_BOX(transport), button, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(root), transport, FALSE, FALSE, 0);
        gtk_widget_set_halign(controls, GTK_ALIGN_CENTER);
        for (auto* button : {rewind, forward}) {
            g_signal_connect(button, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer p) {
                const char* offset = g_strcmp0(gtk_widget_get_name(GTK_WIDGET(button)), "player-rewind") == 0 ? "-300" : "300";
                const char* args[] = {"seek", offset, "relative", nullptr};
                static_cast<PlayerView*>(p)->command(args);
            })), this);
        }
        for (auto* button : {reverse, fast}) {
            g_signal_connect(button, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer p) {
                auto* self = static_cast<PlayerView*>(p);
                if (self->updating) return;
                const bool reverse = GTK_WIDGET(button) == self->reverse;
                const bool normal = self->playbackSpeed == 2 && self->backwards == reverse;
                self->setPlayback(!normal && reverse, normal ? 1 : 2);
                const char* args[] = {"set", "pause", "no", nullptr}; self->command(args);
            })), this);
        }
        gtk_box_pack_start(GTK_BOX(controls), mute, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("Volume"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), volume, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), speed, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), full, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(root), controls, FALSE, FALSE, 0);
        gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_pack_start(GTK_BOX(root), status, FALSE, FALSE, 0);
        g_object_set_data(G_OBJECT(root), "player-controller", this);
        gtk_widget_set_name(root, "player-view");
        gtk_widget_set_name(play, "player-play-pause");
        gtk_widget_set_name(status, "player-status");
        gtk_widget_set_name(url, "player-url");
        gtk_widget_set_name(load, "player-open-url");
        gtk_widget_set_sensitive(position, FALSE);
        mpv = mpv_create();
        if (mpv) {
            mpv_set_option_string(mpv, "vo", "libmpv");
            mpv_set_option_string(mpv, "keep-open", "yes");
            mpv_set_option_string(mpv, "mute", "yes");
            mpv_set_option_string(mpv, "volume", "100");
            mpv_set_option_string(mpv, "volume-max", "100");
            mpv_set_option_string(mpv, "hwdec", "auto-safe");
            mpv_set_option_string(mpv, "ytdl", "no");
            mpv_set_option_string(mpv, "load-scripts", "no");
            mpv_set_option_string(mpv, "resume-playback", "yes");
            mpv_set_option_string(mpv, "save-position-on-quit", "yes");
            gchar* directory = g_build_filename(g_get_user_state_dir(), "stream-gallery", "watch-later", nullptr);
            mpv_set_option_string(mpv, "watch-later-directory", directory);
            g_free(directory);
            int result = mpv_initialize(mpv);
            if (result < 0) {
                failRenderer(std::string("libmpv initialization failed: ") + mpv_error_string(result));
                mpv_terminate_destroy(mpv);
                mpv = nullptr;
            }
        } else failRenderer("Unable to create libmpv player (check numeric locale and available memory)");
        if (mpv) {
            mpv_observe_property(mpv, 1, "duration", MPV_FORMAT_DOUBLE);
            mpv_observe_property(mpv, 2, "time-pos", MPV_FORMAT_DOUBLE);
            mpv_observe_property(mpv, 3, "pause", MPV_FORMAT_FLAG);
            mpv_observe_property(mpv, 4, "volume", MPV_FORMAT_DOUBLE);
            mpv_observe_property(mpv, 5, "mute", MPV_FORMAT_FLAG);
            mpv_observe_property(mpv, 6, "eof-reached", MPV_FORMAT_FLAG);
            mpv_observe_property(mpv, 7, "speed", MPV_FORMAT_DOUBLE);
            mpv_observe_property(mpv, 8, "play-direction", MPV_FORMAT_STRING);
        }
        g_signal_connect(area, "realize", G_CALLBACK((+[](GtkWidget*, gpointer p) { static_cast<PlayerView*>(p)->realize(); })), this);
        g_signal_connect(area, "unrealize", G_CALLBACK((+[](GtkWidget*, gpointer p) { static_cast<PlayerView*>(p)->unrealize(); })), this);
        g_signal_connect(area, "render", G_CALLBACK((+[](GtkGLArea*, GdkGLContext*, gpointer p) -> gboolean { return static_cast<PlayerView*>(p)->render(); })), this);
        g_signal_connect(open, "clicked", G_CALLBACK((+[](GtkButton*, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            auto* dialog = gtk_file_chooser_dialog_new("Open video", GTK_WINDOW(gtk_widget_get_toplevel(self->root)),
                GTK_FILE_CHOOSER_ACTION_OPEN, "Cancel", GTK_RESPONSE_CANCEL, "Open", GTK_RESPONSE_ACCEPT, nullptr);
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
                gchar* path = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
                if (path) self->load(path);
                g_free(path);
            }
            gtk_widget_destroy(dialog);
        })), this);
        g_object_set_data(G_OBJECT(load), "url-entry", url);
        g_signal_connect(load, "clicked", G_CALLBACK((+[](GtkButton* button, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            const char* value = gtk_entry_get_text(GTK_ENTRY(g_object_get_data(G_OBJECT(button), "url-entry")));
            GError* error = nullptr;
            GUri* uri = g_uri_parse(value, G_URI_FLAGS_NONE, &error);
            const char* scheme = uri ? g_uri_get_scheme(uri) : nullptr;
            bool valid = scheme && (g_ascii_strcasecmp(scheme, "http") == 0 || g_ascii_strcasecmp(scheme, "https") == 0)
                && g_uri_get_host(uri) && *g_uri_get_host(uri) && !g_uri_get_userinfo(uri);
            if (valid) self->load(value);
            else self->message("Enter an HTTP/HTTPS media URL without credentials");
            if (uri) g_uri_unref(uri);
            g_clear_error(&error);
        })), this);
        g_signal_connect(play, "clicked", G_CALLBACK((+[](GtkButton*, gpointer p) {
            static_cast<PlayerView*>(p)->togglePlayback();
        })), this);
        g_signal_connect(position, "value-changed", G_CALLBACK((+[](GtkRange* range, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            if (!self->updating && self->duration > 0) {
                std::string value = std::to_string(gtk_range_get_value(range));
                const char* args[] = {"seek", value.c_str(), "absolute", nullptr}; self->command(args);
            }
        })), this);
        g_signal_connect(volume, "value-changed", G_CALLBACK((+[](GtkRange* range, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            if (!self->updating) self->set("volume", gtk_range_get_value(range));
        })), this);
        g_signal_connect(mute, "toggled", G_CALLBACK((+[](GtkToggleButton* button, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            if (self->mpv && !self->updating) {
                int muted = gtk_toggle_button_get_active(button);
                mpv_set_property_async(self->mpv, 0, "mute", MPV_FORMAT_FLAG, &muted);
            }
        })), this);
        g_signal_connect(speed, "changed", G_CALLBACK((+[](GtkComboBox* combo, gpointer p) {
            const double speeds[] = {0.5, 1, 1.5, 2};
            int index = gtk_combo_box_get_active(combo);
            auto* self = static_cast<PlayerView*>(p);
            if (!self->updating && index >= 0 && index < 4) self->setPlayback(false, speeds[index]);
        })), this);
        g_signal_connect(full, "clicked", G_CALLBACK((+[](GtkButton*, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            self->toggleFullscreen();
        })), this);
        timer = g_timeout_add(33, +[](gpointer p) -> gboolean { static_cast<PlayerView*>(p)->poll(); return G_SOURCE_CONTINUE; }, this);
        g_signal_connect(root, "destroy", G_CALLBACK((+[](GtkWidget*, gpointer p) { delete static_cast<PlayerView*>(p); })), this);
    }
    void hideForFullscreen(GtkWidget* widget) {
        if (!widget) return;
        hiddenWidgets.push_back({widget, gtk_widget_get_visible(widget) != FALSE,
            gtk_widget_get_no_show_all(widget) != FALSE});
        gtk_widget_set_no_show_all(widget, TRUE);
        gtk_widget_hide(widget);
    }
    void setFullscreen(bool active) {
        if (fullscreen == active) return;
        fullscreen = active;
        auto* window = GTK_WINDOW(gtk_widget_get_toplevel(root));
        auto* stackWidget = gtk_widget_get_ancestor(root, GTK_TYPE_STACK);
        auto* stack = stackWidget ? GTK_STACK(stackWidget) : nullptr;
        if (active) {
            decorated = gtk_window_get_decorated(window);
            if (stack) {
                previousPage = gtk_stack_get_visible_child(stack);
                previousTransition = gtk_stack_get_transition_type(stack);
                gtk_stack_set_transition_type(stack, GTK_STACK_TRANSITION_TYPE_NONE);
                GtkWidget* page = root;
                while (gtk_widget_get_parent(page) != stackWidget) page = gtk_widget_get_parent(page);
                gtk_stack_set_visible_child(stack, page);
                auto* parent = gtk_widget_get_parent(stackWidget);
                if (GTK_IS_PANED(parent)) {
                    auto* first = gtk_paned_get_child1(GTK_PANED(parent));
                    auto* second = gtk_paned_get_child2(GTK_PANED(parent));
                    hideForFullscreen(first == stackWidget ? second : first);
                }
            }
            hideForFullscreen(gtk_window_get_titlebar(window));
            GList* children = gtk_container_get_children(GTK_CONTAINER(root));
            for (auto* child = children; child; child = child->next)
                if (child->data != area) hideForFullscreen(GTK_WIDGET(child->data));
            g_list_free(children);
            gtk_window_set_decorated(window, FALSE);
            gtk_widget_grab_focus(area);
            gtk_window_fullscreen(window);
        } else {
            gtk_window_unfullscreen(window);
            gtk_window_set_decorated(window, decorated);
            for (const auto& hidden : hiddenWidgets) {
                gtk_widget_set_no_show_all(hidden.widget, hidden.noShowAll);
                gtk_widget_set_visible(hidden.widget, hidden.visible);
            }
            hiddenWidgets.clear();
            if (stack) {
                if (previousPage) gtk_stack_set_visible_child(stack, previousPage);
                gtk_stack_set_transition_type(stack, previousTransition);
            }
            previousPage = nullptr;
        }
    }
    void toggleFullscreen() { setFullscreen(!fullscreen); }
    void updatePlaybackControls() {
        updating = true;
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(reverse), backwards && playbackSpeed == 2);
        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(fast), !backwards && playbackSpeed == 2);
        const double speeds[] = {0.5, 1, 1.5, 2};
        int selected = -1;
        for (int i = 0; i < 4; ++i) if (playbackSpeed == speeds[i]) selected = i;
        gtk_combo_box_set_active(GTK_COMBO_BOX(speed), selected);
        updating = false;
    }
    void setPlayback(bool backward, double rate) {
        const char* direction[] = {"set", "play-direction", backward ? "backward" : "forward", nullptr};
        command(direction); set("speed", rate);
    }
    void updatePlayButton() {
        const char* label = atEnd ? "Replay" : (paused ? "Play" : "Pause");
        gtk_button_set_label(GTK_BUTTON(play), label);
        auto* image = gtk_button_get_image(GTK_BUTTON(play));
        gtk_image_set_from_icon_name(GTK_IMAGE(image), atEnd ? "media-playlist-repeat-symbolic" :
            (paused ? "media-playback-start-symbolic" : "media-playback-pause-symbolic"), GTK_ICON_SIZE_BUTTON);
        gtk_image_set_pixel_size(GTK_IMAGE(image), 34);
        gtk_widget_set_tooltip_text(play, label);
        atk_object_set_name(gtk_widget_get_accessible(play), label);
    }
    void togglePlayback() {
        if (backwards || playbackSpeed == 2) {
            setPlayback(false, 1);
        }
        if (atEnd) {
            const char* seek[] = {"seek", "0", "absolute+exact", nullptr}; command(seek);
            const char* resume[] = {"set", "pause", "no", nullptr}; command(resume);
        } else {
            const char* args[] = {"cycle", "pause", nullptr}; command(args);
        }
    }
    gboolean handleKey(GdkEventKey* event) {
        // Shift and Caps Lock are accepted; preserve Ctrl/Alt/Super shortcuts.
        constexpr guint reserved = GDK_CONTROL_MASK | GDK_MOD1_MASK | GDK_MOD3_MASK |
            GDK_MOD4_MASK | GDK_MOD5_MASK | GDK_SUPER_MASK | GDK_HYPER_MASK | GDK_META_MASK;
        if (event->state & reserved) return FALSE;
        // Test the produced plus symbol before removing Shift (usually Shift+=).
        if (event->keyval == GDK_KEY_plus || event->keyval == GDK_KEY_KP_Add) {
            const char* args[] = {"add", "volume", "5", nullptr}; command(args); return TRUE;
        }
        guint key = event->keyval;
        // Resolve Shift+1 (!), Shift+2 (@), etc. through the active keymap.
        guint unshifted = 0;
        if (event->hardware_keycode && gdk_keymap_translate_keyboard_state(
                gdk_keymap_get_for_display(gtk_widget_get_display(root)), event->hardware_keycode,
                static_cast<GdkModifierType>(event->state & ~(GDK_SHIFT_MASK | GDK_LOCK_MASK)),
                event->group, &unshifted, nullptr, nullptr, nullptr))
            key = unshifted;
        key = gdk_keyval_to_lower(key);
        if (key == GDK_KEY_minus || key == GDK_KEY_KP_Subtract) {
            const char* args[] = {"add", "volume", "-5", nullptr}; command(args); return TRUE;
        }
        if (key == GDK_KEY_m) {
            const char* args[] = {"cycle", "mute", nullptr}; command(args); return TRUE;
        }
        if (key == GDK_KEY_f) { toggleFullscreen(); return TRUE; }
        if (key == GDK_KEY_p) {
            togglePlayback(); return TRUE;
        }
        const char* seconds = nullptr;
        switch (key) {
        case GDK_KEY_Up: seconds = "300"; break;
        case GDK_KEY_Down: seconds = "-300"; break;
        case GDK_KEY_Right: seconds = "60"; break;
        case GDK_KEY_Left: seconds = "-60"; break;
        default: break;
        }
        if (seconds) {
            const char* args[] = {"seek", seconds, "relative", nullptr}; command(args); return TRUE;
        }
        if (key >= GDK_KEY_KP_1 && key <= GDK_KEY_KP_9) key = GDK_KEY_1 + key - GDK_KEY_KP_1;
        if (key >= GDK_KEY_1 && key <= GDK_KEY_9) {
            std::string percent = std::to_string((key - GDK_KEY_0) * 10);
            const char* args[] = {"seek", percent.c_str(), "absolute-percent", nullptr};
            command(args); return TRUE;
        }
        return FALSE;
    }
    ~PlayerView() {
        g_object_set_data(G_OBJECT(root), "player-controller", nullptr);
        if (timer) g_source_remove(timer);
        g_signal_handlers_disconnect_by_data(area, this);
        unrealize();
        g_object_unref(area);
        if (mpv) mpv_terminate_destroy(mpv);
    }
    void message(const char* text) {
        gtk_label_set_text(GTK_LABEL(status), text);
        gtk_widget_set_tooltip_text(status, text);
    }
    void failRenderer(const std::string& error) {
        rendererError = error;
        message(rendererError.c_str());
        g_printerr("Stream Gallery player: %s\n", rendererError.c_str());
    }
    void command(const char** args) {
        if (!mpv) return;
        int result = mpv_command_async(mpv, 0, args);
        if (result < 0) message(mpv_error_string(result));
    }
    void set(const char* property, double value) {
        if (mpv) mpv_set_property_async(mpv, 0, property, MPV_FORMAT_DOUBLE, &value);
    }
    void load(const char* path, const char* referrer = "", const char* userAgent = "StreamGallery/0.1") {
        if (mpv && !gtk_widget_get_realized(area)) {
            pendingLoad = PendingLoad{path, referrer, userAgent};
            message("Starting video renderer…");
            return;
        }
        if (!renderer && mpv && gtk_widget_get_realized(area)) realize();
        if (!mpv || !renderer) {
            message(rendererError.empty() ? "Video renderer is not ready; open the Player page first" : rendererError.c_str());
            return;
        }
        if (loaded) { const char* save[] = {"write-watch-later-config", nullptr}; command(save); }
        loaded = false;
        atEnd = false;
        setPlayback(false, 1);
        duration = 0;
        gtk_widget_set_sensitive(position, FALSE);
        mpv_set_property_async(mpv, 0, "referrer", MPV_FORMAT_STRING, &referrer);
        mpv_set_property_async(mpv, 0, "user-agent", MPV_FORMAT_STRING, &userAgent);
        const char* args[] = {"loadfile", path, "replace", nullptr}; command(args);
        const char* resume[] = {"set", "pause", "no", nullptr}; command(resume);
        message("Loading…");
    }
    void realize() {
        if (!mpv || renderer) return;
        gtk_gl_area_make_current(GTK_GL_AREA(area));
        if (gtk_gl_area_get_error(GTK_GL_AREA(area))) {
            failRenderer(std::string("OpenGL context failed: ") + gtk_gl_area_get_error(GTK_GL_AREA(area))->message);
            return;
        }
        mpv_opengl_init_params init{+[](void* display, const char* name) -> void* {
            if (GDK_IS_WAYLAND_DISPLAY(display)) return reinterpret_cast<void*>(eglGetProcAddress(name));
            return reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
        }, gtk_widget_get_display(area)};
        mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init}, {MPV_RENDER_PARAM_INVALID, nullptr}};
        int result = mpv_render_context_create(&renderer, mpv, params);
        if (result < 0) failRenderer(std::string("libmpv OpenGL renderer failed: ") + mpv_error_string(result));
        else {
            rendererError.clear(); message("Player ready");
            if (pendingLoad) {
                auto request = std::move(*pendingLoad);
                pendingLoad.reset();
                load(request.path.c_str(), request.referrer.c_str(), request.userAgent.c_str());
            }
        }
    }
    void unrealize() {
        if (!renderer) return;
        gtk_gl_area_make_current(GTK_GL_AREA(area));
        mpv_render_context_free(renderer);
        renderer = nullptr;
    }
    gboolean render() {
        if (!renderer) return FALSE;
        GLint framebuffer = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &framebuffer);
        int scale = gtk_widget_get_scale_factor(area);
        mpv_opengl_fbo fbo{framebuffer, gtk_widget_get_allocated_width(area) * scale,
            gtk_widget_get_allocated_height(area) * scale, 0};
        int flip = 1;
        int block = 0;
        mpv_render_param params[] = {{MPV_RENDER_PARAM_OPENGL_FBO, &fbo}, {MPV_RENDER_PARAM_FLIP_Y, &flip},
            {MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &block}, {MPV_RENDER_PARAM_INVALID, nullptr}};
        mpv_render_context_render(renderer, params);
        return TRUE;
    }
    void poll() {
        if (!mpv) return;
        for (int count = 0; count < 64; ++count) {
            mpv_event* event = mpv_wait_event(mpv, 0);
            if (event->event_id == MPV_EVENT_NONE) break;
            if (event->event_id == MPV_EVENT_PROPERTY_CHANGE) {
                auto* property = static_cast<mpv_event_property*>(event->data);
                if (property->data && property->format == MPV_FORMAT_DOUBLE) {
                    double value = *static_cast<double*>(property->data);
                    if (std::isfinite(value)) {
                        updating = true;
                        if (event->reply_userdata == 1) {
                            duration = std::max(0.0, value);
                            gtk_widget_set_sensitive(position, duration > 0);
                            gtk_range_set_range(GTK_RANGE(position), 0, std::max(1.0, duration));
                        } else if (event->reply_userdata == 2)
                            gtk_range_set_value(GTK_RANGE(position), value);
                        else if (event->reply_userdata == 4)
                            gtk_range_set_value(GTK_RANGE(volume), value);
                        updating = false;
                        if (event->reply_userdata == 7) {
                            playbackSpeed = value; updatePlaybackControls();
                        }
                    }
                } else if (property->data && property->format == MPV_FORMAT_FLAG) {
                    bool active = *static_cast<int*>(property->data) != 0;
                    if (event->reply_userdata == 3) {
                        paused = active; updatePlayButton();
                    }
                    else if (event->reply_userdata == 5) {
                        updating = true;
                        gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(mute), active);
                        gtk_button_set_label(GTK_BUTTON(mute), active ? "Muted" : "Mute");
                        updating = false;
                    } else if (event->reply_userdata == 6) {
                        bool wasAtEnd = atEnd;
                        atEnd = active;
                        if (!atEnd && wasAtEnd && loaded) {
                            message("Playing");
                            updatePlayButton();
                        }
                        if (atEnd) {
                            message("Playback ended — video kept open");
                            updatePlayButton();
                        }
                    }
                }
            }
            if (event->event_id == MPV_EVENT_PROPERTY_CHANGE && event->reply_userdata == 8) {
                auto* property = static_cast<mpv_event_property*>(event->data);
                if (property->data && property->format == MPV_FORMAT_STRING) {
                    backwards = g_strcmp0(*static_cast<char**>(property->data), "backward") == 0 ||
                        g_strcmp0(*static_cast<char**>(property->data), "-") == 0;
                    updatePlaybackControls();
                }
            }
            if (event->event_id == MPV_EVENT_FILE_LOADED) { loaded = true; message("Playing"); }
            if (event->event_id == MPV_EVENT_END_FILE) {
                auto* end = static_cast<mpv_event_end_file*>(event->data);
                message(end->reason == MPV_END_FILE_REASON_ERROR ? mpv_error_string(end->error) : "Playback ended");
            }
            if ((event->event_id == MPV_EVENT_COMMAND_REPLY || event->event_id == MPV_EVENT_SET_PROPERTY_REPLY) && event->error < 0)
                message(mpv_error_string(event->error));
        }
        if (renderer && gtk_widget_get_mapped(area)) gtk_gl_area_queue_render(GTK_GL_AREA(area));
    }
};
}
GtkWidget* createPlayerView() { return (new PlayerView())->root; }
void setPlayerFullscreen(GtkWidget* player, bool fullscreen) {
    auto* controller = static_cast<PlayerView*>(g_object_get_data(G_OBJECT(player), "player-controller"));
    if (controller) controller->setFullscreen(fullscreen);
}
void openPlayerFile(GtkWidget* player, const char* filename) {
    auto* controller = static_cast<PlayerView*>(g_object_get_data(G_OBJECT(player), "player-controller"));
    if (controller && filename) controller->load(filename);
}
void openPlayerStream(GtkWidget* player, const char* url, const char* referrer, const char* userAgent) {
    auto* controller = static_cast<PlayerView*>(g_object_get_data(G_OBJECT(player), "player-controller"));
    if (controller && url && referrer && userAgent) controller->load(url, referrer, userAgent);
}
gboolean handlePlayerKey(GtkWidget* player, GdkEventKey* event) {
    auto* controller = static_cast<PlayerView*>(g_object_get_data(G_OBJECT(player), "player-controller"));
    return controller ? controller->handleKey(event) : FALSE;
}
}
