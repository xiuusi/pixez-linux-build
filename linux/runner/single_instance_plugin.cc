#include "single_instance_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gio/gio.h>
#include <glib.h>
#include <glib-unix.h>

#include <cstring>
#include <string>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

// 抽象 Unix 套接字名（首字节为 \0 表示进入抽象命名空间）。
// 抽象命名空间不占用磁盘文件，进程退出后内核自动回收，最适合用于单实例检测。
constexpr char kSocketName[] = "pixez";
constexpr char kChannelName[] = "pixez/single_instance";

struct InstanceData {
    int listen_fd = -1;
    guint source_id = 0;
    FlEventChannel* channel = nullptr;
};

InstanceData* g_data = nullptr;

socklen_t fill_abstract_addr(struct sockaddr_un* addr) {
    std::memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    // 抽象地址：sun_path[0] = '\0'，紧接其后是 "pixez"
    addr->sun_path[0] = '\0';
    std::memcpy(&addr->sun_path[1], kSocketName, std::strlen(kSocketName));
    return offsetof(struct sockaddr_un, sun_path) + 1 +
           static_cast<socklen_t>(std::strlen(kSocketName));
}

// 把 argv（跳过程序名）拼接成与 Windows 端一致的格式：每个参数后跟一个 "\n"
std::string join_args(int argc, char** argv) {
    std::string s;
    for (int i = 1; i < argc; ++i) {
        s += (argv[i] ? argv[i] : "");
        s += "\n";
    }
    return s;
}

gboolean accept_cb(int fd, GIOCondition condition, gpointer user_data) {
    if ((condition & G_IO_IN) == 0) return G_SOURCE_CONTINUE;

    struct sockaddr_un client{};
    socklen_t clen = sizeof(client);
    int conn = accept(fd, reinterpret_cast<struct sockaddr*>(&client), &clen);
    if (conn < 0) {
        // EAGAIN/EINTR 等暂时性错误，继续监听
        return G_SOURCE_CONTINUE;
    }

    char buf[4096];
    ssize_t n;
    std::string acc;
    while ((n = read(conn, buf, sizeof(buf))) > 0) {
        acc.append(buf, static_cast<size_t>(n));
    }
    close(conn);

    if (!acc.empty() && g_data != nullptr && g_data->channel != nullptr) {
        g_autoptr(FlValue) event = fl_value_new_string(acc.c_str());
        g_autoptr(GError) error = nullptr;
        fl_event_channel_send(g_data->channel, event, nullptr, &error);
        if (error != nullptr) {
            g_warning("SingleInstance send event failed: %s", error->message);
        }
    }
    return G_SOURCE_CONTINUE;
}

FlMethodErrorResponse* on_listen(FlEventChannel* channel, FlValue* args,
                                 gpointer user_data) {
    auto* data = static_cast<InstanceData*>(user_data);
    (void)args;
    if (data->listen_fd < 0) {
        // 本实例不是主实例；也不会有事件可发，直接返回即可
        return nullptr;
    }
    if (data->source_id == 0) {
        data->channel = channel;
        data->source_id =
            g_unix_fd_add(data->listen_fd, G_IO_IN, accept_cb, data);
    }
    return nullptr;
}

FlMethodErrorResponse* on_cancel(FlEventChannel* channel, FlValue* args,
                                 gpointer user_data) {
    auto* data = static_cast<InstanceData*>(user_data);
    (void)channel;
    (void)args;
    if (data->source_id != 0) {
        g_source_remove(data->source_id);
        data->source_id = 0;
    }
    return nullptr;
}

}  // namespace

int single_instance_try_lock_or_forward(int argc, char** argv) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        // 创建失败仅是失去单实例保护，不影响启动
        return 0;
    }

    struct sockaddr_un addr{};
    socklen_t alen = fill_abstract_addr(&addr);

    // 1) 尝试 connect：成功说明已经有主实例在 listen
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), alen) == 0) {
        std::string payload = join_args(argc, argv);
        if (!payload.empty()) {
            ssize_t total = 0;
            while (total < static_cast<ssize_t>(payload.size())) {
                ssize_t n =
                    write(fd, payload.data() + total, payload.size() - total);
                if (n <= 0) break;
                total += n;
            }
        }
        shutdown(fd, SHUT_WR);
        close(fd);
        return 1;  // 调用方应当退出
    }

    // 2) connect 失败 → 自己 bind + listen 成为主实例
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), alen) == 0 &&
        listen(fd, 16) == 0) {
        if (g_data == nullptr) {
            g_data = new InstanceData();
        }
        g_data->listen_fd = fd;
        return 0;
    }

    // bind 失败但又没法 connect（边角情形）：放弃单例保护，照常运行
    close(fd);
    return 0;
}

void register_single_instance_plugin(FlPluginRegistrar* registrar) {
    FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();

    FlEventChannel* channel =
        fl_event_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));

    if (g_data == nullptr) {
        g_data = new InstanceData();
    }
    fl_event_channel_set_stream_handlers(channel, on_listen, on_cancel, g_data,
                                         nullptr);
}