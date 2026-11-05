#include "ui/NameSuggestions.hpp"
#include "database/LibraryRepository.hpp"
#include <filesystem>
#include <memory>

namespace streamgallery {
namespace {
struct Suggestions {
    GtkWidget* entry;
    GtkWidget* combo;
    bool folders, closed = false, updating = false;
    guint debounce = 0;
    unsigned generation = 0;
    GCancellable* cancellable = nullptr;
    ~Suggestions() { g_clear_object(&cancellable); }
};
using State = std::shared_ptr<Suggestions>;
struct Request {
    State state;
    unsigned generation;
    std::string path, prefix;
};
State stateFor(GtkWidget* entry) {
    return *static_cast<State*>(g_object_get_data(G_OBJECT(entry),"name-suggestions"));
}
void lookup(const State& state) {
    if (state->closed) return;
    auto path = std::filesystem::path(g_get_user_data_dir()) / "stream-gallery" / "library.db";
    auto* request = new Request{state,state->generation,path.string(),gtk_entry_get_text(GTK_ENTRY(state->entry))};
    state->cancellable = g_cancellable_new();
    auto* task = g_task_new(nullptr,state->cancellable,+[](GObject*, GAsyncResult* result, gpointer) {
        auto* task = G_TASK(result);
        auto* request = static_cast<Request*>(g_task_get_task_data(task));
        auto state = request->state;
        GError* error = nullptr;
        std::unique_ptr<std::vector<std::string>> names(static_cast<std::vector<std::string>*>(g_task_propagate_pointer(task,&error)));
        if (!state->closed && request->generation == state->generation) {
            if (error && !g_error_matches(error,G_IO_ERROR,G_IO_ERROR_CANCELLED))
                gtk_widget_set_tooltip_text(state->entry,error->message);
            if (names) {
                gtk_widget_set_tooltip_text(state->entry,"Up to 50 existing names. Type a case-sensitive prefix to narrow the choices; new names are allowed.");
                state->updating = true;
                if (state->combo) {
                    const std::string text = gtk_entry_get_text(GTK_ENTRY(state->entry));
                    const int position = gtk_editable_get_position(GTK_EDITABLE(state->entry));
                    gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(state->combo));
                    for (const auto& name : *names) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(state->combo),name.c_str());
                    gtk_entry_set_text(GTK_ENTRY(state->entry),text.c_str());
                    gtk_editable_set_position(GTK_EDITABLE(state->entry),position);
                } else {
                    auto* completion = gtk_entry_get_completion(GTK_ENTRY(state->entry));
                    auto* model = gtk_list_store_new(1,G_TYPE_STRING);
                    for (const auto& name : *names) {
                        GtkTreeIter row;
                        gtk_list_store_insert_with_values(model,&row,-1,0,name.c_str(),-1);
                    }
                    gtk_entry_completion_set_model(completion,GTK_TREE_MODEL(model));
                    g_object_unref(model);
                    gtk_entry_completion_complete(completion);
                }
                state->updating = false;
            }
        }
        g_clear_error(&error);
    },nullptr);
    g_task_set_task_data(task,request,+[](gpointer data) { delete static_cast<Request*>(data); });
    g_task_run_in_thread(task,+[](GTask* task, gpointer, gpointer data, GCancellable*) {
        auto* request = static_cast<Request*>(data);
        try {
            if (g_task_return_error_if_cancelled(task)) return;
            std::filesystem::create_directories(std::filesystem::path(request->path).parent_path());
            Database db(request->path);
            auto names = std::make_unique<std::vector<std::string>>(LibraryRepository(db).assignmentNames(request->state->folders,request->prefix));
            g_task_return_pointer(task,names.release(),+[](gpointer data) { delete static_cast<std::vector<std::string>*>(data); });
        } catch (const std::exception& error) {
            g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_FAILED,"%s",error.what());
        }
    });
    g_object_unref(task);
}
}
void refreshNameSuggestions(GtkWidget* entry) {
    auto state = stateFor(entry);
    if (state->closed || state->updating) return;
    ++state->generation;
    if (state->cancellable) g_cancellable_cancel(state->cancellable);
    g_clear_object(&state->cancellable);
    if (state->debounce) g_source_remove(state->debounce);
    // Clear obsolete suggestions immediately, before the replacement lookup completes.
    if (!state->combo) gtk_entry_completion_set_model(gtk_entry_get_completion(GTK_ENTRY(entry)),nullptr);
    state->debounce = g_timeout_add(150,+[](gpointer data) -> gboolean {
        auto* entry = GTK_WIDGET(data);
        auto state = stateFor(entry);
        state->debounce = 0;
        lookup(state);
        return G_SOURCE_REMOVE;
    },entry);
}
void installNameSuggestions(GtkWidget* entry, bool folders, GtkWidget* combo) {
    auto state = std::make_shared<Suggestions>();
    state->entry = entry; state->combo = combo; state->folders = folders;
    g_object_set_data_full(G_OBJECT(entry),"name-suggestions",new State(state),+[](gpointer data) { delete static_cast<State*>(data); });
    if (!combo) {
        auto* completion = gtk_entry_completion_new();
        gtk_entry_completion_set_text_column(completion,0);
        gtk_entry_completion_set_minimum_key_length(completion,1);
        gtk_entry_completion_set_match_func(completion,+[](GtkEntryCompletion*, const gchar*, GtkTreeIter*, gpointer) -> gboolean {
            return TRUE; // The worker already applies a literal, case-sensitive prefix.
        },nullptr,nullptr);
        gtk_entry_set_completion(GTK_ENTRY(entry),completion);
        g_object_unref(completion);
    }
    g_signal_connect(entry,"changed",G_CALLBACK(+[](GtkEditable* entry, gpointer) {
        refreshNameSuggestions(GTK_WIDGET(entry));
    }),nullptr);
    g_signal_connect(entry,"focus-in-event",G_CALLBACK(+[](GtkWidget* entry, GdkEventFocus*, gpointer) -> gboolean {
        refreshNameSuggestions(entry); return FALSE;
    }),nullptr);
    g_signal_connect(entry,"destroy",G_CALLBACK(+[](GtkWidget* entry, gpointer) {
        auto state = stateFor(entry);
        state->closed = true;
        if (state->debounce) { g_source_remove(state->debounce); state->debounce = 0; }
        if (state->cancellable) g_cancellable_cancel(state->cancellable);
    }),nullptr);
    refreshNameSuggestions(entry);
}
}
