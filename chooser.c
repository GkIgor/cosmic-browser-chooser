#include <gtk/gtk.h>
#include <gio/gdesktopappinfo.h>
#include <string.h>

#define APP_ID "dev.local.BrowserChooser"
#define DESKTOP_ID APP_ID ".desktop"

static const char *handled_types[] = {
    "x-scheme-handler/http", "x-scheme-handler/https",
    "text/html", "application/xhtml+xml", NULL
};

typedef struct {
    char *target;
    GPtrArray *browsers;
    GtkWidget *error_label;
} Chooser;

typedef struct {
    Chooser *chooser;
    GAppInfo *browser;
    GtkWindow *window;
} BrowserButton;

static gboolean has_browser_category(GAppInfo *app) {
    if (!G_IS_DESKTOP_APP_INFO(app)) return FALSE;
    const char *categories = g_desktop_app_info_get_categories(G_DESKTOP_APP_INFO(app));
    if (!categories) return FALSE;
    char **parts = g_strsplit(categories, ";", -1);
    gboolean found = FALSE;
    for (char **part = parts; *part; part++) {
        if (strcmp(*part, "WebBrowser") == 0) { found = TRUE; break; }
    }
    g_strfreev(parts);
    return found;
}

static gint compare_browsers(gconstpointer a, gconstpointer b) {
    GAppInfo *left = *(GAppInfo *const *)a;
    GAppInfo *right = *(GAppInfo *const *)b;
    return g_utf8_collate(g_app_info_get_display_name(left), g_app_info_get_display_name(right));
}

static GPtrArray *available_browsers(void) {
    GPtrArray *browsers = g_ptr_array_new_with_free_func(g_object_unref);
    GHashTable *seen = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, NULL);
    for (const char **type = handled_types; *type; type++) {
        GList *apps = g_app_info_get_all_for_type(*type);
        for (GList *item = apps; item; item = item->next) {
            GAppInfo *app = G_APP_INFO(item->data);
            const char *id = g_app_info_get_id(app);
            if (!id || strcmp(id, DESKTOP_ID) == 0 || g_hash_table_contains(seen, id) ||
                !g_app_info_should_show(app) || !g_app_info_supports_uris(app) ||
                !has_browser_category(app)) continue;
            g_hash_table_add(seen, g_strdup(id));
            g_ptr_array_add(browsers, g_object_ref(app));
        }
        g_list_free_full(apps, g_object_unref);
    }
    g_hash_table_unref(seen);
    g_ptr_array_sort(browsers, compare_browsers);
    return browsers;
}

static char *normalize_target(const char *raw, const char **error) {
    if (!raw || !*raw) { *error = "Informe um link ou arquivo."; return NULL; }
    for (const unsigned char *p = (const unsigned char *)raw; *p; p++) {
        if (*p < 32 || *p == 127) {
            *error = "O link contém caracteres de controle."; return NULL;
        }
    }
    GError *parse_error = NULL;
    GUri *uri = g_uri_parse(raw, G_URI_FLAGS_NONE, &parse_error);
    if (uri) {
        const char *scheme = g_uri_get_scheme(uri);
        if (scheme && (g_ascii_strcasecmp(scheme, "http") == 0 ||
                       g_ascii_strcasecmp(scheme, "https") == 0)) {
            const char *host = g_uri_get_host(uri);
            gboolean valid = host && *host;
            for (const char *p = raw; *p; p++) {
                if (g_ascii_isspace(*p)) { valid = FALSE; break; }
            }
            g_uri_unref(uri);
            if (valid) { g_clear_error(&parse_error); return g_strdup(raw); }
            *error = "O endereço HTTP/HTTPS não é válido.";
            g_clear_error(&parse_error);
            return NULL;
        }
        if (scheme && g_ascii_strcasecmp(scheme, "file") == 0) {
            GFile *file = g_file_new_for_uri(raw);
            char *path = g_file_get_path(file);
            gboolean valid = path && g_file_test(path, G_FILE_TEST_IS_REGULAR);
            g_free(path);
            g_object_unref(file);
            g_uri_unref(uri);
            g_clear_error(&parse_error);
            if (valid) return g_strdup(raw);
            *error = "O arquivo local não foi encontrado.";
            return NULL;
        }
        g_uri_unref(uri);
    }
    g_clear_error(&parse_error);
    if (g_file_test(raw, G_FILE_TEST_IS_REGULAR)) {
        GFile *file = g_file_new_for_commandline_arg(raw);
        char *result = g_file_get_uri(file);
        g_object_unref(file);
        return result;
    }
    *error = "Abra um link HTTP/HTTPS ou um arquivo local.";
    return NULL;
}

static char *target_label(const char *target) {
    if (g_str_has_prefix(target, "file:")) {
        GFile *file = g_file_new_for_uri(target);
        char *name = g_file_get_basename(file);
        g_object_unref(file);
        return name ? name : g_strdup(target);
    }
    GUri *uri = g_uri_parse(target, G_URI_FLAGS_NONE, NULL);
    char *label = uri && g_uri_get_host(uri) ? g_strdup(g_uri_get_host(uri)) : g_strdup(target);
    if (uri) g_uri_unref(uri);
    return label;
}

static void open_browser(GtkButton *button, gpointer user_data) {
    (void)button;
    BrowserButton *data = user_data;
    GdkDisplay *display = gdk_display_get_default();
    GdkAppLaunchContext *context = gdk_display_get_app_launch_context(display);
    GList uri = { .data = data->chooser->target, .next = NULL, .prev = NULL };
    GError *error = NULL;
    gboolean launched = g_app_info_launch_uris(data->browser, &uri,
                                               G_APP_LAUNCH_CONTEXT(context), &error);
    g_object_unref(context);
    if (launched) {
        gtk_window_close(data->window);
    } else {
        char *message = g_strdup_printf("Não foi possível abrir %s: %s",
                                        g_app_info_get_display_name(data->browser),
                                        error ? error->message : "o navegador recusou a abertura");
        gtk_label_set_text(GTK_LABEL(data->chooser->error_label), message);
        gtk_widget_set_visible(data->chooser->error_label, TRUE);
        g_free(message);
    }
    g_clear_error(&error);
}

static gboolean on_key_pressed(GtkEventControllerKey *controller, guint keyval,
                               guint keycode, GdkModifierType state, gpointer user_data) {
    (void)controller; (void)keycode; (void)state;
    if (keyval != GDK_KEY_Escape) return FALSE;
    gtk_window_close(GTK_WINDOW(user_data));
    return TRUE;
}

static void on_cancel(GtkButton *button, gpointer user_data) {
    (void)button;
    gtk_window_close(GTK_WINDOW(user_data));
}

static void free_browser_button(gpointer data, GClosure *closure) {
    (void)closure;
    g_free(data);
}

static void apply_system_theme(GSettings *settings) {
    char *scheme = g_settings_get_string(settings, "color-scheme");
    gboolean prefer_dark = g_strcmp0(scheme, "prefer-dark") == 0;
    if (g_strcmp0(scheme, "default") == 0) {
        char *theme = g_settings_get_string(settings, "gtk-theme");
        prefer_dark = g_str_has_suffix(theme, "-dark");
        g_free(theme);
    }
    g_object_set(gtk_settings_get_default(),
                 "gtk-application-prefer-dark-theme", prefer_dark, NULL);
    g_free(scheme);
}

static void on_system_theme_changed(GSettings *settings, const char *key, gpointer user_data) {
    (void)key;
    (void)user_data;
    apply_system_theme(settings);
}

static GSettings *watch_system_theme(void) {
    GSettingsSchemaSource *source = g_settings_schema_source_get_default();
    if (!source) return NULL;
    GSettingsSchema *schema = g_settings_schema_source_lookup(
        source, "org.gnome.desktop.interface", TRUE);
    if (!schema) return NULL;
    gboolean supported = g_settings_schema_has_key(schema, "color-scheme") &&
                         g_settings_schema_has_key(schema, "gtk-theme");
    g_settings_schema_unref(schema);
    if (!supported) return NULL;
    GSettings *settings = g_settings_new("org.gnome.desktop.interface");
    apply_system_theme(settings);
    g_signal_connect(settings, "changed::color-scheme",
                     G_CALLBACK(on_system_theme_changed), NULL);
    g_signal_connect(settings, "changed::gtk-theme",
                     G_CALLBACK(on_system_theme_changed), NULL);
    return settings;
}

static void activate(GtkApplication *app, gpointer user_data) {
    Chooser *chooser = user_data;
    GtkWidget *window = gtk_application_window_new(app);
    gtk_window_set_title(GTK_WINDOW(window), "Escolher navegador");
    gtk_window_set_default_size(GTK_WINDOW(window), 600, 800);
    gtk_window_set_titlebar(GTK_WINDOW(window), gtk_header_bar_new());

    GtkEventController *keys = gtk_event_controller_key_new();
    g_signal_connect(keys, "key-pressed", G_CALLBACK(on_key_pressed), window);
    gtk_widget_add_controller(window, keys);

    GtkWidget *outer = gtk_box_new(GTK_ORIENTATION_VERTICAL, 16);
    gtk_widget_set_margin_top(outer, 20);
    gtk_widget_set_margin_bottom(outer, 20);
    gtk_widget_set_margin_start(outer, 20);
    gtk_widget_set_margin_end(outer, 20);
    gtk_window_set_child(GTK_WINDOW(window), outer);

    GtkWidget *heading = gtk_label_new("Abrir link com");
    gtk_label_set_xalign(GTK_LABEL(heading), 0);
    gtk_widget_add_css_class(heading, "title-2");
    gtk_box_append(GTK_BOX(outer), heading);

    char *short_target = target_label(chooser->target);
    GtkWidget *destination = gtk_label_new(short_target);
    g_free(short_target);
    gtk_label_set_xalign(GTK_LABEL(destination), 0);
    gtk_label_set_ellipsize(GTK_LABEL(destination), PANGO_ELLIPSIZE_END);
    gtk_widget_set_tooltip_text(destination, chooser->target);
    gtk_widget_add_css_class(destination, "dim-label");
    gtk_box_append(GTK_BOX(outer), destination);

    GtkWidget *scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_box_append(GTK_BOX(outer), scroll);
    GtkWidget *list = gtk_list_box_new();
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list), GTK_SELECTION_NONE);
    gtk_widget_add_css_class(list, "boxed-list");
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);

    if (chooser->browsers->len == 0) {
        GtkWidget *empty = gtk_label_new("Nenhum navegador registrado foi encontrado.");
        gtk_label_set_wrap(GTK_LABEL(empty), TRUE);
        gtk_widget_set_margin_top(empty, 20);
        gtk_widget_set_margin_bottom(empty, 20);
        gtk_list_box_append(GTK_LIST_BOX(list), empty);
    }
    for (guint i = 0; i < chooser->browsers->len; i++) {
        GAppInfo *browser = g_ptr_array_index(chooser->browsers, i);
        GtkWidget *button = gtk_button_new();
        gtk_widget_add_css_class(button, "flat");
        gtk_widget_set_margin_top(button, 5);
        gtk_widget_set_margin_bottom(button, 5);
        gtk_widget_set_margin_start(button, 5);
        gtk_widget_set_margin_end(button, 5);
        GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_margin_top(row, 7);
        gtk_widget_set_margin_bottom(row, 7);
        gtk_widget_set_margin_start(row, 8);
        gtk_widget_set_margin_end(row, 8);
        GIcon *icon = g_app_info_get_icon(browser);
        GtkWidget *image = icon ? gtk_image_new_from_gicon(icon) :
                                  gtk_image_new_from_icon_name("web-browser-symbolic");
        gtk_image_set_pixel_size(GTK_IMAGE(image), 28);
        gtk_box_append(GTK_BOX(row), image);
        GtkWidget *name = gtk_label_new(g_app_info_get_display_name(browser));
        gtk_label_set_xalign(GTK_LABEL(name), 0);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_box_append(GTK_BOX(row), name);
        gtk_box_append(GTK_BOX(row), gtk_image_new_from_icon_name("go-next-symbolic"));
        gtk_button_set_child(GTK_BUTTON(button), row);
        BrowserButton *data = g_new(BrowserButton, 1);
        *data = (BrowserButton){ chooser, browser, GTK_WINDOW(window) };
        g_signal_connect_data(button, "clicked", G_CALLBACK(open_browser), data,
                              free_browser_button, 0);
        gtk_list_box_append(GTK_LIST_BOX(list), button);
    }

    chooser->error_label = gtk_label_new(NULL);
    gtk_label_set_xalign(GTK_LABEL(chooser->error_label), 0);
    gtk_label_set_wrap(GTK_LABEL(chooser->error_label), TRUE);
    gtk_widget_add_css_class(chooser->error_label, "error");
    gtk_widget_set_visible(chooser->error_label, FALSE);
    gtk_box_append(GTK_BOX(outer), chooser->error_label);
    GtkWidget *footer = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_halign(footer, GTK_ALIGN_END);
    GtkWidget *cancel = gtk_button_new_with_label("Cancelar");
    g_signal_connect(cancel, "clicked", G_CALLBACK(on_cancel), window);
    gtk_box_append(GTK_BOX(footer), cancel);
    gtk_box_append(GTK_BOX(outer), footer);
    gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
    GPtrArray *browsers = available_browsers();
    if (argc == 2 && strcmp(argv[1], "--list-browsers") == 0) {
        for (guint i = 0; i < browsers->len; i++) {
            GAppInfo *browser = g_ptr_array_index(browsers, i);
            g_print("%s\t%s\n", g_app_info_get_id(browser), g_app_info_get_display_name(browser));
        }
        g_ptr_array_unref(browsers);
        return 0;
    }
    if (argc != 2) {
        g_printerr("Uso: %s URL-ou-arquivo | --list-browsers\n", argv[0]);
        g_ptr_array_unref(browsers);
        return 2;
    }
    const char *error = NULL;
    char *target = normalize_target(argv[1], &error);
    if (!target) {
        g_printerr("%s\n", error);
        g_ptr_array_unref(browsers);
        return 2;
    }
    if (!gtk_init_check() || !gdk_display_get_default()) {
        g_printerr("Não foi possível acessar uma sessão gráfica GTK.\n");
        g_free(target);
        g_ptr_array_unref(browsers);
        return 1;
    }
    GSettings *theme_settings = watch_system_theme();
    Chooser chooser = { .target = target, .browsers = browsers, .error_label = NULL };
    GtkApplication *app = gtk_application_new(APP_ID, G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(activate), &chooser);
    int result = g_application_run(G_APPLICATION(app), 0, NULL);
    g_object_unref(app);
    if (theme_settings) g_object_unref(theme_settings);
    g_ptr_array_unref(browsers);
    g_free(target);
    return result;
}
