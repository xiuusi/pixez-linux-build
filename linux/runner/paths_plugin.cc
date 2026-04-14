#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <filesystem>

namespace fs = std::filesystem;

static void handle_path_method_call(FlMethodChannel* channel, FlMethodCall* method_call, gpointer user_data) {
    // 消除警告
    (void)channel;
    (void)user_data;

    const gchar* method = fl_method_call_get_name(method_call);

    if (strcmp(method, "getDatabaseFolderPath") == 0) {
        const gchar* data_dir = g_get_user_data_dir();
        fs::path db_path = fs::path(data_dir) / "pixez" / "databases";

        if (!fs::exists(db_path)) {
            fs::create_directories(db_path);
        }

        g_autoptr(FlValue) result = fl_value_new_string(db_path.c_str());
        fl_method_call_respond_success(method_call, result, nullptr);
    } else {
        fl_method_call_respond_not_implemented(method_call, nullptr);
    }
}

void register_paths_plugin(FlPluginRegistrar* registrar) {
    FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
    FlMethodChannel* channel = fl_method_channel_new(messenger, "com.perol.dev/paths", FL_METHOD_CODEC(codec));
    fl_method_channel_set_method_call_handler(channel, handle_path_method_call, nullptr, nullptr);
}
