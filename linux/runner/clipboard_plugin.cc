#include "clipboard_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

static constexpr char kChannelName[] = "com.perol.dev/clipboard";

static void copy_image_method_call_handler(FlMethodChannel* channel, FlMethodCall* method_call, gpointer user_data) {
    const gchar* method = fl_method_call_get_name(method_call);
    
    if (strcmp(method, "copyImageFromByteArray") == 0) {
        FlValue* args = fl_method_call_get_args(method_call);
        FlValue* data_value = fl_value_lookup_string(args, "data");
        
        if (data_value && fl_value_get_type(data_value) == FL_VALUE_TYPE_UINT8_LIST) {
            const uint8_t* bytes = fl_value_get_uint8_list(data_value);
            size_t length = fl_value_get_length(data_value);
            
            GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
            
            GdkPixbufLoader* loader = gdk_pixbuf_loader_new();
            GError* error = NULL;
            
            gdk_pixbuf_loader_write(loader, bytes, length, &error);
            
            if (error != NULL) {
                g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
                fl_method_call_respond_success(method_call, result, NULL);
                g_error_free(error);
            } else {
                gdk_pixbuf_loader_close(loader, &error);
                
                GdkPixbuf* pixbuf = gdk_pixbuf_loader_get_pixbuf(loader);
                if (pixbuf) {
                    g_object_ref(pixbuf);
                    gtk_clipboard_set_image(clipboard, pixbuf);
                    g_object_unref(pixbuf);
                    
                    g_autoptr(FlValue) result = fl_value_new_bool(TRUE);
                    fl_method_call_respond_success(method_call, result, NULL);
                } else {
                    g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
                    fl_method_call_respond_success(method_call, result, NULL);
                }
            }
            
            g_object_unref(loader);
        } else {
            g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
            fl_method_call_respond_success(method_call, result, NULL);
        }
    } else {
        fl_method_call_respond_not_implemented(method_call, NULL);
    }
}

void register_clipboard_plugin(FlPluginRegistrar* registrar) {
    FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
    
    FlMethodChannel* channel = fl_method_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));
    fl_method_channel_set_method_call_handler(channel, copy_image_method_call_handler, NULL, NULL);
}