#include "document_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include <string.h>

static constexpr char kChannelName[] = "com.perol.dev/save";

typedef struct {
    gchar* save_directory;
} DocumentPluginData;

static void save_method_call_handler(FlMethodChannel* channel, FlMethodCall* method_call, gpointer user_data) {
    DocumentPluginData* data = (DocumentPluginData*)user_data;
    
    const gchar* method = fl_method_call_get_name(method_call);
    
    if (strcmp(method, "save") == 0) {
        FlValue* args = fl_method_call_get_args(method_call);
        FlValue* data_value = fl_value_lookup_string(args, "data");
        FlValue* name_value = fl_value_lookup_string(args, "name");
        
        if (data_value && name_value && fl_value_get_type(data_value) == FL_VALUE_TYPE_UINT8_LIST) {
            const gchar* filename = fl_value_get_string(name_value);
            const uint8_t* bytes = fl_value_get_uint8_list(data_value);
            size_t length = fl_value_get_length(data_value);
            
            gchar* file_path = g_build_filename(data->save_directory, filename, NULL);
            
            FILE* file = fopen(file_path, "wb");
            if (file) {
                fwrite(bytes, 1, length, file);
                fclose(file);
                
                g_autoptr(FlValue) result = fl_value_new_bool(TRUE);
                fl_method_call_respond_success(method_call, result, NULL);
            } else {
                g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
                fl_method_call_respond_success(method_call, result, NULL);
            }
            
            g_free(file_path);
        } else {
            g_autoptr(FlValue) result = fl_value_new_string("Invalid arguments");
            fl_method_call_respond_success(method_call, result, NULL);
        }
    } else if (strcmp(method, "openSave") == 0) {
        FlValue* args = fl_method_call_get_args(method_call);
        FlValue* data_value = fl_value_lookup_string(args, "data");
        FlValue* name_value = fl_value_lookup_string(args, "name");
        
        if (data_value && name_value && fl_value_get_type(data_value) == FL_VALUE_TYPE_UINT8_LIST) {
            const gchar* filename = fl_value_get_string(name_value);
            const uint8_t* bytes = fl_value_get_uint8_list(data_value);
            size_t length = fl_value_get_length(data_value);
            
            gchar* file_path = g_build_filename(data->save_directory, filename, NULL);
            
            FILE* file = fopen(file_path, "wb");
            if (file) {
                fwrite(bytes, 1, length, file);
                fclose(file);
                
                g_autoptr(FlValue) result = fl_value_new_bool(TRUE);
                fl_method_call_respond_success(method_call, result, NULL);
            } else {
                g_autoptr(FlValue) result = fl_value_new_bool(FALSE);
                fl_method_call_respond_success(method_call, result, NULL);
            }
            
            g_free(file_path);
        } else {
            g_autoptr(FlValue) result = fl_value_new_string("Invalid arguments");
            fl_method_call_respond_success(method_call, result, NULL);
        }
    } else if (strcmp(method, "get_path") == 0) {
        g_autoptr(FlValue) result = fl_value_new_string(data->save_directory);
        fl_method_call_respond_success(method_call, result, NULL);
    } else if (strcmp(method, "choice_folder") == 0) {
        GtkWindow* window = GTK_WINDOW(gtk_application_get_active_window(GTK_APPLICATION(g_application_get_default())));
        
        GtkFileChooserDialog* dialog = GTK_FILE_CHOOSER_DIALOG(gtk_file_chooser_dialog_new(
            "Select Save Directory",
            window,
            GTK_FILE_CHOOSER_ACTION_SELECT_FOLDER,
            "_Cancel",
            GTK_RESPONSE_CANCEL,
            "_Select",
            GTK_RESPONSE_ACCEPT,
            NULL
        ));
        
        if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT) {
            gchar* folder = gtk_file_chooser_get_filename(GTK_FILE_CHOOSER(dialog));
            
            g_free(data->save_directory);
            data->save_directory = folder;
            
            g_autoptr(FlValue) result = fl_value_new_string(folder);
            fl_method_call_respond_success(method_call, result, NULL);
        } else {
            g_autoptr(FlValue) result = fl_value_new_string("");
            fl_method_call_respond_success(method_call, result, NULL);
        }
        
        gtk_widget_destroy(GTK_WIDGET(dialog));
    } else {
        fl_method_call_respond_not_implemented(method_call, NULL);
    }
}

void register_document_plugin(FlPluginRegistrar* registrar) {
    DocumentPluginData* data = g_new0(DocumentPluginData, 1);
    
    const gchar* home_dir = g_get_home_dir();
    data->save_directory = g_build_filename(home_dir, "Pictures", "PixEz", NULL);
    
    GFile* dir = g_file_new_for_path(data->save_directory);
    g_file_make_directory_with_parents(dir, NULL, NULL);
    g_object_unref(dir);
    
    FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
    g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
    
    FlMethodChannel* channel = fl_method_channel_new(messenger, kChannelName, FL_METHOD_CODEC(codec));
    fl_method_channel_set_method_call_handler(channel, save_method_call_handler, data, g_free);
}