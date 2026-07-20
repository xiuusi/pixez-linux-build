#include "encode_plugin.h"

#include <gif_lib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>
#include <flutter_linux/flutter_linux.h>
#include <glib.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// ─── 方法通道名称（与 ugoira_store.dart 保持一致） ──────────────────────
static constexpr char kChannelName[] = "samples.flutter.dev/battery";
static constexpr char kMethodName[] = "getBatteryLevel";

// ─── 3-3-2 均匀量化调色板（共 256 色） ─────────────────────────────
// R: 8 级(3bit), G: 8 级(3bit), B: 4 级(2bit)
static const int kPaletteR[8] = {0, 36, 73, 109, 146, 182, 219, 255};
static const int kPaletteG[8] = {0, 36, 73, 109, 146, 182, 219, 255};
static const int kPaletteB[4] = {0, 85, 170, 255};

// 将 RGB 映射到调色板索引 (0..255)
static GifPixelType rgb_to_index(int r, int g, int b) {
    int ri = (r * 7 + 128) >> 8; // 0..7
    int gi = (g * 7 + 128) >> 8; // 0..7
    int bi = (b * 3 + 128) >> 8; // 0..3
    if (ri < 0) ri = 0; if (ri > 7) ri = 7;
    if (gi < 0) gi = 0; if (gi > 7) gi = 7;
    if (bi < 0) bi = 0; if (bi > 3) bi = 3;
    return static_cast<GifPixelType>((ri << 5) | (gi << 2) | bi);
}

// 构建 256 色调色板
static ColorMapObject* create_uniform_palette() {
    GifColorType colors[256];
    for (int ri = 0; ri < 8; ++ri) {
        for (int gi = 0; gi < 8; ++gi) {
            for (int bi = 0; bi < 4; ++bi) {
                int idx = (ri << 5) | (gi << 2) | bi;
                colors[idx].Red   = static_cast<GifByteType>(kPaletteR[ri]);
                colors[idx].Green = static_cast<GifByteType>(kPaletteG[gi]);
                colors[idx].Blue  = static_cast<GifByteType>(kPaletteB[bi]);
            }
        }
    }
    return GifMakeMapObject(256, colors);
}

// ─── 从 GdkPixbuf 生成索引行（返回的数组长度 = width） ──────────────
static std::vector<GifPixelType> quantize_row(const GdkPixbuf* pb, int y) {
    int width  = gdk_pixbuf_get_width(pb);
    int nchan  = gdk_pixbuf_get_n_channels(pb);
    int stride = gdk_pixbuf_get_rowstride(pb);
    const guint8* pixels = gdk_pixbuf_get_pixels(pb) + y * stride;

    std::vector<GifPixelType> row(width);
    for (int x = 0; x < width; ++x) {
        int r = pixels[x * nchan + 0];
        int g = pixels[x * nchan + 1];
        int b = pixels[x * nchan + 2];
        // 如果有 alpha 通道且透明度 > 50%，映射到色 0（纯黑）
        if (nchan >= 4 && pixels[x * nchan + 3] < 128) {
            r = g = b = 0;
        }
        row[x] = rgb_to_index(r, g, b);
    }
    return row;
}

// ─── 列出目录下所有图片文件（按文件名排序） ─────────────────────────
static std::vector<std::string> list_image_files(const gchar* dir_path) {
    std::vector<std::string> files;
    GDir* dir = g_dir_open(dir_path, 0, nullptr);
    if (!dir) return files;

    const gchar* name;
    while ((name = g_dir_read_name(dir)) != nullptr) {
        const gchar* ext = strrchr(name, '.');
        if (ext) {
            gchar* lower = g_utf8_strdown(ext + 1, -1);
            bool is_image = (strcmp(lower, "png") == 0 ||
                             strcmp(lower, "jpg") == 0 ||
                             strcmp(lower, "jpeg") == 0);
            g_free(lower);
            if (is_image) {
                files.push_back(g_build_filename(dir_path, name, NULL));
            }
        }
    }
    g_dir_close(dir);

    std::sort(files.begin(), files.end());
    return files;
}

// ─── MethodChannel 处理器 ─────────────────────────────────────────────
static void encode_method_call_handler(FlMethodChannel* channel,
                                        FlMethodCall* method_call,
                                        gpointer user_data) {
    (void)channel;
    const gchar* method = fl_method_call_get_name(method_call);

    if (strcmp(method, kMethodName) != 0) {
        fl_method_call_respond_not_implemented(method_call, NULL);
        return;
    }

    FlValue* args = fl_method_call_get_args(method_call);
    if (!args || fl_value_get_type(args) != FL_VALUE_TYPE_MAP) {
        g_autoptr(FlValue) err = fl_value_new_string("invalid args");
        fl_method_call_respond_error(method_call, "BAD_ARGS", NULL, err, NULL);
        return;
    }

    // 提取 path
    FlValue* path_val = fl_value_lookup_string(args, "path");
    if (!path_val || fl_value_get_type(path_val) != FL_VALUE_TYPE_STRING) {
        g_autoptr(FlValue) err = fl_value_new_string("missing path");
        fl_method_call_respond_error(method_call, "BAD_ARGS", NULL, err, NULL);
        return;
    }
    const gchar* dir_path = fl_value_get_string(path_val);

    // 提取 delay（首帧延迟，ms）
    int first_delay = 100;
    FlValue* delay_val = fl_value_lookup_string(args, "delay");
    if (delay_val && fl_value_get_type(delay_val) == FL_VALUE_TYPE_INT) {
        first_delay = fl_value_get_int(delay_val);
    }

    // 提取 delay_array
    FlValue* delays_val = fl_value_lookup_string(args, "delay_array");
    std::vector<int> delays;
    if (delays_val && fl_value_get_type(delays_val) == FL_VALUE_TYPE_LIST) {
        size_t len = fl_value_get_length(delays_val);
        delays.reserve(len);
        for (size_t i = 0; i < len; ++i) {
            FlValue* d = fl_value_get_list_value(delays_val, i);
            if (d && fl_value_get_type(d) == FL_VALUE_TYPE_INT) {
                delays.push_back(fl_value_get_int(d));
            } else {
                delays.push_back(first_delay);
            }
        }
    } else {
        delays.push_back(first_delay);
    }

    // ── 列出图片文件 ──
    auto files = list_image_files(dir_path);
    if (files.empty()) {
        g_autoptr(FlValue) err = fl_value_new_string("no images found");
        fl_method_call_respond_error(method_call, "NO_IMAGES", NULL, err, NULL);
        return;
    }

    // ── 输出路径 ──
    g_autofree gchar* output_name =
        g_strdup_printf("pixez_ugoira_%ld.gif", static_cast<long>(g_get_monotonic_time()));
    g_autofree gchar* output_dir = g_build_filename(g_get_tmp_dir(), "pixez", NULL);
    g_mkdir_with_parents(output_dir, 0700);
    g_autofree gchar* output_path = g_build_filename(output_dir, output_name, NULL);

    // ── 读取第一帧获取尺寸 ──
    GError* error = nullptr;
    GdkPixbuf* first_pb = gdk_pixbuf_new_from_file(files[0].c_str(), &error);
    if (!first_pb) {
        g_autoptr(FlValue) err = fl_value_new_string(error->message);
        fl_method_call_respond_error(method_call, "IMAGE_LOAD", NULL, err, NULL);
        g_error_free(error);
        return;
    }
    int width  = gdk_pixbuf_get_width(first_pb);
    int height = gdk_pixbuf_get_height(first_pb);
    g_object_unref(first_pb);

    if (width <= 0 || height <= 0) {
        g_autoptr(FlValue) err = fl_value_new_string("invalid image dimensions");
        fl_method_call_respond_error(method_call, "BAD_IMAGE", NULL, err, NULL);
        return;
    }

    // ── 创建 GIF 文件 ──
    int gif_error = 0;
    GifFileType* gif = EGifOpenFileName(output_path, false, &gif_error);
    if (!gif) {
        g_autofree gchar* errmsg = g_strdup_printf("EGifOpenFileName failed: error %d", gif_error);
        g_autoptr(FlValue) err = fl_value_new_string(errmsg);
        fl_method_call_respond_error(method_call, "GIF_CREATE", NULL, err, NULL);
        return;
    }

    // 调色板
    ColorMapObject* cmap = create_uniform_palette();
    EGifSetGifVersion(gif, true); // GIF89a — 支持控制扩展
    EGifPutScreenDesc(gif, width, height, 8, 0, cmap);
    GifFreeMapObject(cmap);

    // ── 逐帧编码 ──
    bool all_ok = true;
    for (size_t i = 0; i < files.size(); ++i) {
        // 读取帧
        GdkPixbuf* frame = gdk_pixbuf_new_from_file(files[i].c_str(), &error);
        if (!frame) {
            g_warning("encode_plugin: skip frame %zu: %s", i, error->message);
            g_error_free(error);
            error = nullptr;
            continue;
        }

        // 检查尺寸是否一致（不一致时跳过，不中断）
        int fw = gdk_pixbuf_get_width(frame);
        int fh = gdk_pixbuf_get_height(frame);
        if (fw != width || fh != height) {
            g_warning("encode_plugin: frame %zu size mismatch (%dx%d != %dx%d), skip",
                      i, fw, fh, width, height);
            g_object_unref(frame);
            continue;
        }

        // 当前帧延迟（ms）
        int delay_ms = (i < delays.size()) ? delays[i] : delays.back();
        if (delay_ms < 10) delay_ms = 10; // 最小 10ms

        // Graphics Control Extension
        unsigned short delay_cs = static_cast<unsigned short>(delay_ms / 10);
        unsigned char gce_data[4];
        gce_data[0] = 0;                        // 标志: 无透明, 无用户输入, 还原
        gce_data[1] = delay_cs & 0xFF;           // 延迟低位
        gce_data[2] = (delay_cs >> 8) & 0xFF;    // 延迟高位
        gce_data[3] = 0;                         // 透明色索引(无)
        EGifPutExtensionLeader(gif, GRAPHICS_EXT_FUNC_CODE);
        EGifPutExtensionBlock(gif, 4, gce_data);
        EGifPutExtensionTrailer(gif);

        // Image Descriptor（使用全局色表）
        if (EGifPutImageDesc(gif, 0, 0, width, height, 0, NULL) == GIF_ERROR) {
            g_warning("encode_plugin: EGifPutImageDesc failed at frame %zu", i);
            all_ok = false;
            g_object_unref(frame);
            break;
        }

        // 逐行写入
        for (int y = 0; y < height; ++y) {
            auto row = quantize_row(frame, y);
            if (EGifPutLine(gif, row.data(), width) == GIF_ERROR) {
                g_warning("encode_plugin: EGifPutLine failed at frame %zu line %d", i, y);
                all_ok = false;
                break;
            }
        }

        g_object_unref(frame);
        if (!all_ok) break;
    }

    // ── 关闭 GIF ──
    int close_err = 0;
    EGifCloseFile(gif, &close_err);

    if (!all_ok || files.empty()) {
        std::remove(output_path);
        g_autoptr(FlValue) err = fl_value_new_string("encoding failed");
        fl_method_call_respond_error(method_call, "ENCODE_FAILED", NULL, err, NULL);
        return;
    }

    // ── 返回路径 ──
    g_autoptr(FlValue) result = fl_value_new_string(output_path);
    fl_method_call_respond_success(method_call, result, NULL);
}

// ─── 注册插件 ────────────────────────────────────────────────────────
void register_encode_plugin(FlPluginRegistrar* registrar) {
    FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
    FlMethodChannel* channel =
        fl_method_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));
    fl_method_channel_set_method_call_handler(channel,
                                               encode_method_call_handler,
                                               nullptr,
                                               nullptr);
}
