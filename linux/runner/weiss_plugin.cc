#include "weiss_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <string.h>

static constexpr char kChannelName[] = "com.perol.dev/weiss";

static void weiss_method_call_handler(FlMethodChannel* channel,
                                      FlMethodCall* method_call,
                                      gpointer user_data) {
    (void)channel;
    (void)user_data;

    const gchar* method = fl_method_call_get_name(method_call);

    // PixEz 的 DoH/代理功能在 Linux 暂未实现，保留 channel 以避免
    // Dart 侧调用时抛出 MissingPluginException。
    if (strcmp(method, "start") == 0 || strcmp(method, "stop") == 0 ||
        strcmp(method, "proxy") == 0) {
        fl_method_call_respond_success(method_call, nullptr, nullptr);
    } else {
        fl_method_call_respond_not_implemented(method_call, nullptr);
    }
}

void register_weiss_plugin(FlPluginRegistrar* registrar) {
    FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();

    FlMethodChannel* channel =
        fl_method_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));
    fl_method_channel_set_method_call_handler(channel, weiss_method_call_handler,
                                              nullptr, nullptr);
}