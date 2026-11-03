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

namespace streamgallery {
namespace {
class PlayerView {
public:
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    GtkWidget* area = gtk_gl_area_new();
    GtkWidget* status = gtk_label_new("Open a local video or a direct media URL");
    GtkWidget* position = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 1, 1);
    GtkWidget* play = gtk_button_new_with_label("Play / Pause");
    mpv_handle* mpv = nullptr;
    mpv_render_context* renderer = nullptr;
    guint timer = 0;
    bool updating = false;
    bool loaded = false;
    double duration = 0;

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
        auto* volume = gtk_scale_new_with_range(GTK_ORIENTATION_HORIZONTAL, 0, 100, 1);
        gtk_range_set_value(GTK_RANGE(volume), 100);
        gtk_widget_set_size_request(volume, 120, -1);
        auto* speed = gtk_combo_box_text_new();
        for (const char* value : {"0.5", "1.0", "1.5", "2.0"})
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(speed), value);
        gtk_combo_box_set_active(GTK_COMBO_BOX(speed), 1);
        auto* full = gtk_button_new_with_label("Fullscreen");
        gtk_box_pack_start(GTK_BOX(controls), play, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), gtk_label_new("Volume"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), volume, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), speed, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(controls), full, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(root), controls, FALSE, FALSE, 0);
        gtk_label_set_ellipsize(GTK_LABEL(status), PANGO_ELLIPSIZE_MIDDLE);
        gtk_box_pack_start(GTK_BOX(root), status, FALSE, FALSE, 0);
        gtk_widget_set_name(root, "player-view");
        gtk_widget_set_name(status, "player-status");
        gtk_widget_set_name(url, "player-url");
        gtk_widget_set_name(load, "player-open-url");
        gtk_widget_set_sensitive(position, FALSE);
        mpv = mpv_create();
        if (mpv) {
            mpv_set_option_string(mpv, "vo", "libmpv");
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
                message(mpv_error_string(result));
                mpv_terminate_destroy(mpv);
                mpv = nullptr;
            }
        } else message("Unable to create libmpv player");
        if (mpv) {
            mpv_observe_property(mpv, 1, "duration", MPV_FORMAT_DOUBLE);
            mpv_observe_property(mpv, 2, "time-pos", MPV_FORMAT_DOUBLE);
            mpv_observe_property(mpv, 3, "pause", MPV_FORMAT_FLAG);
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
            const char* args[] = {"cycle", "pause", nullptr}; static_cast<PlayerView*>(p)->command(args);
        })), this);
        g_signal_connect(position, "value-changed", G_CALLBACK((+[](GtkRange* range, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            if (!self->updating && self->duration > 0) {
                std::string value = std::to_string(gtk_range_get_value(range));
                const char* args[] = {"seek", value.c_str(), "absolute", nullptr}; self->command(args);
            }
        })), this);
        g_signal_connect(volume, "value-changed", G_CALLBACK((+[](GtkRange* range, gpointer p) {
            static_cast<PlayerView*>(p)->set("volume", gtk_range_get_value(range));
        })), this);
        g_signal_connect(speed, "changed", G_CALLBACK((+[](GtkComboBox* combo, gpointer p) {
            const double speeds[] = {0.5, 1, 1.5, 2};
            int index = gtk_combo_box_get_active(combo);
            if (index >= 0 && index < 4) static_cast<PlayerView*>(p)->set("speed", speeds[index]);
        })), this);
        g_signal_connect(full, "clicked", G_CALLBACK((+[](GtkButton*, gpointer p) {
            auto* self = static_cast<PlayerView*>(p);
            auto* window = GTK_WINDOW(gtk_widget_get_toplevel(self->root));
            GdkWindow* native = gtk_widget_get_window(GTK_WIDGET(window));
            if (native && (gdk_window_get_state(native) & GDK_WINDOW_STATE_FULLSCREEN))
                gtk_window_unfullscreen(window);
            else gtk_window_fullscreen(window);
        })), this);
        timer = g_timeout_add(33, +[](gpointer p) -> gboolean { static_cast<PlayerView*>(p)->poll(); return G_SOURCE_CONTINUE; }, this);
        g_signal_connect(root, "destroy", G_CALLBACK((+[](GtkWidget*, gpointer p) { delete static_cast<PlayerView*>(p); })), this);
    }
    ~PlayerView() {
        if (timer) g_source_remove(timer);
        g_signal_handlers_disconnect_by_data(area, this);
        unrealize();
        g_object_unref(area);
        if (mpv) mpv_terminate_destroy(mpv);
    }
    void message(const char* text) { gtk_label_set_text(GTK_LABEL(status), text); }
    void command(const char** args) {
        if (!mpv) return;
        int result = mpv_command_async(mpv, 0, args);
        if (result < 0) message(mpv_error_string(result));
    }
    void set(const char* property, double value) {
        if (mpv) mpv_set_property_async(mpv, 0, property, MPV_FORMAT_DOUBLE, &value);
    }
    void load(const char* path) {
        if (!mpv || !renderer) { message("Video renderer unavailable; check OpenGL support"); return; }
        if (loaded) { const char* save[] = {"write-watch-later-config", nullptr}; command(save); }
        loaded = false;
        duration = 0;
        gtk_widget_set_sensitive(position, FALSE);
        const char* args[] = {"loadfile", path, "replace", nullptr}; command(args);
        message("Loading…");
    }
    void realize() {
        if (!mpv || renderer) return;
        gtk_gl_area_make_current(GTK_GL_AREA(area));
        if (gtk_gl_area_get_error(GTK_GL_AREA(area))) { message(gtk_gl_area_get_error(GTK_GL_AREA(area))->message); return; }
        mpv_opengl_init_params init{+[](void* display, const char* name) -> void* {
            if (GDK_IS_WAYLAND_DISPLAY(display)) return reinterpret_cast<void*>(eglGetProcAddress(name));
            return reinterpret_cast<void*>(glXGetProcAddressARB(reinterpret_cast<const GLubyte*>(name)));
        }, gtk_widget_get_display(area)};
        mpv_render_param params[] = {{MPV_RENDER_PARAM_API_TYPE, const_cast<char*>(MPV_RENDER_API_TYPE_OPENGL)},
            {MPV_RENDER_PARAM_OPENGL_INIT_PARAMS, &init}, {MPV_RENDER_PARAM_INVALID, nullptr}};
        int result = mpv_render_context_create(&renderer, mpv, params);
        if (result < 0) message(mpv_error_string(result));
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
                        updating = false;
                    }
                } else if (property->data && event->reply_userdata == 3 && property->format == MPV_FORMAT_FLAG) {
                    gtk_button_set_label(GTK_BUTTON(play), *static_cast<int*>(property->data) ? "Play" : "Pause");
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
}
