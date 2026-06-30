#include "saf_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

// ─── SAF (Storage Access Framework) 的 Linux 桌面端实现 ──────────────────
//
// PixEz 在 Android 端通过 SAF 完成数据导入/导出（标签历史、屏蔽词、浏览记录等）。
// Dart 侧（lib/saf_plugin.dart）期望三个方法：
//
//   1. createFile(name, mimeType) -> String?   打开"另存为"对话框，返回选中的路径。
//   2. writeUri(uri, data)                       按上一部返回的 uri（即路径）写入字节。
//   3. openFile(type) -> Uint8List?              打开文件选择框，读回整个文件字节。
//
// 在 Linux 桌面上没有 SAF 的 Uri 概念，这里直接把 Android 的 Uri 折叠成本地文件路径。
// 这样 createFile / writeUri 的两步配合在 Dart 侧无需任何改动即可工作。
//
// 参考自同目录 document_plugin.cc 的 GTK FileChooser 用法，保持风格一致。

static constexpr char kChannelName[] = "com.perol.dev/saf";

// 取主 GtkWindow，用于挂接文件对话框。
static GtkWindow* saf_get_main_window() {
  GApplication* app = g_application_get_default();
  if (!GTK_IS_APPLICATION(app)) return NULL;
  return gtk_application_get_active_window(GTK_APPLICATION(app));
}

// ─── createFile：另存为对话框 ──────────────────────────────────────────
// 返回调用者需 g_free 的路径字符串；用户取消时返回 NULL。
static gchar* saf_create_file_dialog(const gchar* suggested_name) {
  GtkWindow* parent = saf_get_main_window();

  GtkWidget* dialog = gtk_file_chooser_dialog_new(
      "Save File", parent, GTK_FILE_CHOOSER_ACTION_SAVE, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Save", GTK_RESPONSE_ACCEPT, NULL);
  GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

  // 仅取文件名部分，避免上游传入路径分隔符
  g_autofree gchar* base_name = g_path_get_basename(suggested_name);
  gtk_file_chooser_set_current_name(chooser, base_name);
  gtk_file_chooser_set_do_overwrite_confirmation(chooser, TRUE);

  gchar* result = NULL;
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    result = gtk_file_chooser_get_filename(chooser);
  }
  gtk_widget_destroy(dialog);
  // 让对话框的事件队列真正处理完 destroy，避免视觉残留
  while (gtk_events_pending()) gtk_main_iteration();
  return result;
}

// ─── openFile：打开文件对话框 ──────────────────────────────────────────
// 返回调用者需 g_free 的路径字符串；用户取消时返回 NULL。
static gchar* saf_open_file_dialog() {
  GtkWindow* parent = saf_get_main_window();

  GtkWidget* dialog = gtk_file_chooser_dialog_new(
      "Open File", parent, GTK_FILE_CHOOSER_ACTION_OPEN, "_Cancel",
      GTK_RESPONSE_CANCEL, "_Open", GTK_RESPONSE_ACCEPT, NULL);
  GtkFileChooser* chooser = GTK_FILE_CHOOSER(dialog);

  // 默认过滤器：JSON 文件（导出文件都是 .json），fallback 为所有文件
  GtkFileFilter* json_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(json_filter, "JSON files");
  gtk_file_filter_add_pattern(json_filter, "*.json");
  gtk_file_chooser_add_filter(chooser, json_filter);

  GtkFileFilter* all_filter = gtk_file_filter_new();
  gtk_file_filter_set_name(all_filter, "All files");
  gtk_file_filter_add_pattern(all_filter, "*");
  gtk_file_chooser_add_filter(chooser, all_filter);

  gtk_file_chooser_set_filter(chooser, json_filter);

  gchar* result = NULL;
  if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
    result = gtk_file_chooser_get_filename(chooser);
  }
  gtk_widget_destroy(dialog);
  while (gtk_events_pending()) gtk_main_iteration();
  return result;
}

// 读取整个文件到新分配的 GBytes；失败返回 NULL。
static GBytes* saf_read_file_to_bytes(const gchar* path) {
  gchar* contents = NULL;
  gsize length = 0;
  GError* error = NULL;
  if (!g_file_get_contents(path, &contents, &length, &error)) {
    g_warning("saf openFile read failed: %s", error ? error->message : "unknown");
    if (error) g_error_free(error);
    return NULL;
  }
  // g_file_get_contents 分配的内存由 GBytes 接管
  return g_bytes_new_take(contents, length);
}

static void saf_method_call_handler(FlMethodChannel* channel,
                                    FlMethodCall* method_call,
                                    gpointer user_data) {
  (void)channel;
  (void)user_data;

  const gchar* method = fl_method_call_get_name(method_call);

  // ─── createFile(name, mimeType) -> String? ──────────────────────────
  // Dart 侧 mimeType 在桌面端无对应概念，直接忽略。
  if (strcmp(method, "createFile") == 0) {
    FlValue* args = fl_method_call_get_args(method_call);
    FlValue* name_value = fl_value_lookup_string(args, "name");

    if (name_value && fl_value_get_type(name_value) == FL_VALUE_TYPE_STRING) {
      const gchar* suggested = fl_value_get_string(name_value);
      gchar* path = saf_create_file_dialog(suggested);
      if (path != NULL) {
        g_autoptr(FlValue) result = fl_value_new_string(path);
        fl_method_call_respond_success(method_call, result, NULL);
        g_free(path);
        return;
      }
      // 用户取消，返回 null
      fl_method_call_respond_success(method_call, nullptr, NULL);
      return;
    }
    fl_method_call_respond_success(method_call, nullptr, NULL);
    return;
  }

  // ─── writeUri(uri, data) ─────────────────────────────────────────────
  // uri 在桌面端即文件路径。
  if (strcmp(method, "writeUri") == 0) {
    FlValue* args = fl_method_call_get_args(method_call);
    FlValue* uri_value = fl_value_lookup_string(args, "uri");
    FlValue* data_value = fl_value_lookup_string(args, "data");

    if (uri_value && fl_value_get_type(uri_value) == FL_VALUE_TYPE_STRING &&
        data_value &&
        fl_value_get_type(data_value) == FL_VALUE_TYPE_UINT8_LIST) {
      const gchar* path = fl_value_get_string(uri_value);
      const uint8_t* bytes = fl_value_get_uint8_list(data_value);
      size_t length = fl_value_get_length(data_value);

      GError* error = NULL;
      gboolean ok = g_file_set_contents(path, (const gchar*)bytes, length, &error);
      if (!ok) {
        g_warning("saf writeUri failed: %s", error ? error->message : "unknown");
        if (error) g_error_free(error);
      }

      g_autoptr(FlValue) result = fl_value_new_bool(ok);
      fl_method_call_respond_success(method_call, result, NULL);
      return;
    }
    g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
    fl_method_call_respond_success(method_call, result, NULL);
    return;
  }

  // ─── openFile(type) -> Uint8List? ────────────────────────────────────
  // Dart 侧 type 在桌面端无对应概念，直接忽略。
  if (strcmp(method, "openFile") == 0) {
    gchar* path = saf_open_file_dialog();
    if (path == NULL) {
      // 用户取消，返回 null
      fl_method_call_respond_success(method_call, nullptr, NULL);
      return;
    }

    GBytes* bytes = saf_read_file_to_bytes(path);
    g_free(path);

    if (bytes == NULL) {
      fl_method_call_respond_success(method_call, nullptr, NULL);
      return;
    }

    gsize length = 0;
    const uint8_t* data = (const uint8_t*)g_bytes_get_data(bytes, &length);
    g_autoptr(FlValue) result = fl_value_new_uint8_list(data, length);
    fl_method_call_respond_success(method_call, result, NULL);
    g_bytes_unref(bytes);
    return;
  }

  fl_method_call_respond_not_implemented(method_call, NULL);
}

void register_saf_plugin(FlPluginRegistrar* registrar) {
  FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();

  FlMethodChannel* channel =
      fl_method_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(channel, saf_method_call_handler,
                                            nullptr, nullptr);
}
