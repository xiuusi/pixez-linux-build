#include "document_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

// ─── 配置文件路径 ──────────────────────────────────────────────
// PixEz 在 Linux 下把"保存目录"等信息持久化到 ~/.config/pixez/settings.ini
// 这样下次启动仍能记住用户选择的目录，与 Windows 端 settings.json 对应。
static constexpr char kChannelName[] = "com.perol.dev/save";

typedef struct {
    gchar* save_directory;
} DocumentPluginData;

// 读取持久化的保存目录；若不存在则使用默认值 ~/Pictures/PixEz。
static gchar* load_settings_dir() {
    const gchar* config_dir = g_get_user_config_dir();
    gchar* settings_path =
        g_build_filename(config_dir, "pixez", "settings.ini", NULL);

    g_autoptr(GKeyFile) kf = g_key_file_new();
    if (g_key_file_load_from_file(kf, settings_path, G_KEY_FILE_NONE, NULL)) {
        gchar* folder =
            g_key_file_get_string(kf, "save", "folder", NULL);
        if (folder != NULL && folder[0] != '\0') {
            g_free(settings_path);
            return folder;
        }
        g_free(folder);
    }
    g_free(settings_path);

    const gchar* home = g_get_home_dir();
    return g_build_filename(home, "Pictures", "PixEz", NULL);
}

// 持久化保存目录。
static void save_settings_dir(const gchar* dir) {
    const gchar* config_dir = g_get_user_config_dir();
    gchar* settings_dir = g_build_filename(config_dir, "pixez", NULL);
    g_mkdir_with_parents(settings_dir, 0700);
    gchar* settings_path =
        g_build_filename(settings_dir, "settings.ini", NULL);

    g_autoptr(GKeyFile) kf = g_key_file_new();
    g_key_file_set_string(kf, "save", "folder", dir);

    gsize length = 0;
    g_autofree gchar* data = g_key_file_to_data(kf, &length, NULL);
    if (data != NULL) {
        g_file_set_contents(settings_path, data, length, NULL);
    }

    g_free(settings_path);
    g_free(settings_dir);
}

// 取主 GtkWindow，用于挂接文件对话框。
static GtkWindow* get_main_window() {
    GApplication* app = g_application_get_default();
    if (!GTK_IS_APPLICATION(app)) return NULL;
    GtkWindow* win = gtk_application_get_active_window(GTK_APPLICATION(app));
    return win;
}

// 把 bytes 写到指定路径。成功返回 TRUE。
static gboolean write_bytes_to_path(const gchar* path, const uint8_t* bytes,
                                    size_t length) {
    FILE* file = fopen(path, "wb");
    if (!file) return FALSE;
    size_t written = fwrite(bytes, 1, length, file);
    int ok = fclose(file);
    return written == length && ok == 0;
}

// 取扩展名（含点），如 "a.jpg" -> ".jpg"；无扩展名时返回空串。
static gchar* get_extension(const gchar* filename) {
    const gchar* dot = strrchr(filename, '.');
    return dot ? g_strdup(dot) : g_strdup("");
}

// 把目录里所有文件列表中是否存在名为 [filename] 的文件。
static gboolean file_exists_in_dir(const gchar* dir, const gchar* filename) {
    g_autofree gchar* file_path = g_build_filename(dir, filename, NULL);
    return g_file_test(file_path, G_FILE_TEST_EXISTS);
}

// 将 ARM 通过 GTK FileChooserDialog 选一个保存文件路径并写盘。
static gboolean open_save_dialog(const uint8_t* bytes, size_t length,
                                 const gchar* suggested_name) {
    GtkWindow* parent = get_main_window();

    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Save File", parent, GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel",
        GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
    GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

    // 文件名带目录分隔符在 Linux 上也是非法的，统一过滤
    g_autofree gchar* base_name =
        g_path_get_basename(suggested_name);
    gtk_file_chooser_set_current_name(chooser, base_name);

    // 设置文件过滤器（按扩展名），fallback 用所有文件
    g_autofree gchar* ext = get_extension(base_name);
    if (ext[0] != '\0') {
        GtkFileFilter* filter = gtk_file_filter_new();
        g_autofree gchar* pattern =
            g_strconcat("*", ext, NULL);
        gtk_file_filter_add_pattern(filter, pattern);
        g_autofree gchar* filter_name =
            g_strconcat(ext + 1, " files", NULL);
        gtk_file_filter_set_name(filter, filter_name);
        gtk_file_chooser_add_filter(chooser, filter);
    }
    GtkFileFilter* all_filter = gtk_file_filter_new();
    gtk_file_filter_set_name(all_filter, "All files");
    gtk_file_filter_add_pattern(all_filter, "*");
    gtk_file_chooser_add_filter(chooser, all_filter);
    gtk_file_chooser_set_filter(chooser, all_filter);

    gboolean success = FALSE;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        gchar* chosen = gtk_file_chooser_get_filename(chooser);
        if (chosen) {
            success = write_bytes_to_path(chosen, bytes, length);
            g_free(chosen);
        }
    }
    gtk_widget_destroy(dialog);
    // 让对话框的事件队列真正处理完 destroy，避免视觉残留
    while (gtk_events_pending()) gtk_main_iteration();
    return success;
}

// choice_folder 的 GTK 实现：返回选中的目录字符串（调用方负责 g_free）。
// 用户取消时返回 NULL。
static gchar* choose_folder_dialog() {
    GtkWindow* parent = get_main_window();
    GtkWidget* dialog = gtk_file_chooser_dialog_new(
        "Select Save Directory", parent, GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
        "_Cancel", GTK_RESPONSE_CANCEL, "_Select", GTK_RESPONSE_ACCEPT, NULL);
    gchar* result = NULL;
    if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
        result = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
    }
    gtk_widget_destroy(dialog);
    while (gtk_events_pending()) gtk_main_iteration();
    return result;
}

static void document_method_call_handler(FlMethodChannel* channel,
                                         FlMethodCall* method_call,
                                         gpointer user_data) {
    (void)channel;
    DocumentPluginData* data = (DocumentPluginData*)user_data;

    const gchar* method = fl_method_call_get_name(method_call);

    if (strcmp(method, "save") == 0 || strcmp(method, "saveFromPath") == 0) {
        FlValue* args = fl_method_call_get_args(method_call);
        FlValue* name_value = fl_value_lookup_string(args, "name");
        FlValue* data_value = NULL;
        gboolean is_from_path = strcmp(method, "saveFromPath") == 0;

        if (strcmp(method, "saveFromPath") == 0) {
            data_value = fl_value_lookup_string(args, "source_path");
        } else {
            data_value = fl_value_lookup_string(args, "data");
        }

        if (name_value && fl_value_get_type(name_value) == FL_VALUE_TYPE_STRING) {
            const gchar* filename = fl_value_get_string(name_value);

            gchar* path = g_build_filename(data->save_directory, filename, NULL);
            // 把子目录创建出来，保证写入不会失败
            gchar* dir_part = g_path_get_dirname(path);
            g_mkdir_with_parents(dir_part, 0755);
            g_free(dir_part);

            gboolean ok = FALSE;
            if (is_from_path) {
                if (data_value &&
                    fl_value_get_type(data_value) == FL_VALUE_TYPE_STRING) {
                    const gchar* source_path = fl_value_get_string(data_value);
                    GFile* src = g_file_new_for_path(source_path);
                    GFile* dst = g_file_new_for_path(path);
                    GError* error = NULL;
                    ok = g_file_copy(src, dst, G_FILE_COPY_OVERWRITE, NULL,
                                     NULL, NULL, &error);
                    if (!ok) {
                        g_warning("saveFromPath copy failed: %s",
                                  error ? error->message : "unknown");
                        if (error) g_error_free(error);
                    }
                    g_object_unref(src);
                    g_object_unref(dst);
                }
            } else {
                if (data_value &&
                    fl_value_get_type(data_value) == FL_VALUE_TYPE_UINT8_LIST) {
                    const uint8_t* bytes = fl_value_get_uint8_list(data_value);
                    size_t length = fl_value_get_length(data_value);
                    ok = write_bytes_to_path(path, bytes, length);
                }
            }

            g_autoptr(FlValue) result = fl_value_new_bool(ok);
            fl_method_call_respond_success(method_call, result, NULL);
            g_free(path);
            return;
        }
        // 参数不合法
        g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
        fl_method_call_respond_success(method_call, result, NULL);
        return;
    }

    if (strcmp(method, "openSave") == 0) {
        FlValue* args = fl_method_call_get_args(method_call);
        FlValue* name_value = fl_value_lookup_string(args, "name");
        FlValue* data_value = fl_value_lookup_string(args, "data");

        if (name_value && fl_value_get_type(name_value) == FL_VALUE_TYPE_STRING &&
            data_value &&
            fl_value_get_type(data_value) == FL_VALUE_TYPE_UINT8_LIST) {
            const gchar* suggested = fl_value_get_string(name_value);
            const uint8_t* bytes = fl_value_get_uint8_list(data_value);
            size_t length = fl_value_get_length(data_value);

            gboolean ok = open_save_dialog(bytes, length, suggested);
            g_autoptr(FlValue) result = fl_value_new_bool(ok);
            fl_method_call_respond_success(method_call, result, NULL);
            return;
        }
        g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
        fl_method_call_respond_success(method_call, result, NULL);
        return;
    }

    if (strcmp(method, "exist") == 0) {
        FlValue* args = fl_method_call_get_args(method_call);
        FlValue* name_value = fl_value_lookup_string(args, "name");
        if (name_value && fl_value_get_type(name_value) == FL_VALUE_TYPE_STRING) {
            const gchar* filename = fl_value_get_string(name_value);
            gboolean exists =
                file_exists_in_dir(data->save_directory, filename);
            g_autoptr(FlValue) result = fl_value_new_bool(exists);
            fl_method_call_respond_success(method_call, result, NULL);
            return;
        }
        g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
        fl_method_call_respond_success(method_call, result, NULL);
        return;
    }

    if (strcmp(method, "get_path") == 0) {
        g_autoptr(FlValue) result =
            fl_value_new_string(data->save_directory);
        fl_method_call_respond_success(method_call, result, NULL);
        return;
    }

    if (strcmp(method, "choice_folder") == 0) {
        gchar* folder = choose_folder_dialog();
        if (folder != NULL) {
            g_free(data->save_directory);
            data->save_directory = folder;
            save_settings_dir(folder);
            g_autoptr(FlValue) result = fl_value_new_string(folder);
            fl_method_call_respond_success(method_call, result, NULL);
        } else {
            // 用户取消，返回当前保存目录
            g_autoptr(FlValue) result =
                fl_value_new_string(data->save_directory);
            fl_method_call_respond_success(method_call, result, NULL);
        }
        return;
    }

    if (strcmp(method, "permissionStatus") == 0 ||
        strcmp(method, "requestPermission") == 0) {
        // Linux 桌面端没有存储权限概念，直接同意
        g_autoptr(FlValue) result = fl_value_new_bool(TRUE);
        fl_method_call_respond_success(method_call, result, NULL);
        return;
    }

    fl_method_call_respond_not_implemented(method_call, NULL);
}

void register_document_plugin(FlPluginRegistrar* registrar) {
    DocumentPluginData* data = g_new0(DocumentPluginData, 1);
    data->save_directory = load_settings_dir();
    g_mkdir_with_parents(data->save_directory, 0755);

    FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();

    FlMethodChannel* channel =
        fl_method_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));
    fl_method_channel_set_method_call_handler(channel,
                                              document_method_call_handler, data,
                                              [](gpointer user_data) {
                                                  DocumentPluginData* d =
                                                      (DocumentPluginData*)
                                                          user_data;
                                                  g_free(d->save_directory);
                                                  g_free(d);
                                              });
}