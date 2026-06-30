#include "my_application.h"

#include <flutter_linux/flutter_linux.h>
#ifdef GDK_WINDOWING_X11
#include <gdk/gdkx.h>
#endif

#include "flutter/generated_plugin_registrant.h"

#include "single_instance_plugin.h"

void register_paths_plugin(FlPluginRegistrar* registrar);
void register_single_instance_plugin(FlPluginRegistrar* registrar);
void register_document_plugin(FlPluginRegistrar* registrar);
void register_clipboard_plugin(FlPluginRegistrar* registrar);
void register_weiss_plugin(FlPluginRegistrar* registrar);
void register_saf_plugin(FlPluginRegistrar* registrar);

struct _MyApplication {
  GtkApplication parent_instance;
  char** dart_entrypoint_arguments;
  GtkWindow* window;
  FlView* view;
};

G_DEFINE_TYPE(MyApplication, my_application, GTK_TYPE_APPLICATION)

static void first_frame_cb(MyApplication* self, FlView* view) {
  gtk_widget_show(gtk_widget_get_toplevel(GTK_WIDGET(view)));
}

static gboolean my_application_delete_event(GtkWidget* widget, GdkEvent* event, gpointer user_data) {
  MyApplication* self = MY_APPLICATION(user_data);
  // Destroy the view before the window to prevent crashes during shutdown
  if (self->view != nullptr && GTK_IS_WIDGET(self->view)) {
    gtk_widget_destroy(GTK_WIDGET(self->view));
    self->view = nullptr;
  }
  // Return FALSE to allow the default handler to destroy the window
  return FALSE;
}

static void my_application_activate(GApplication* application) {
  MyApplication* self = MY_APPLICATION(application);
  self->window =
  GTK_WINDOW(gtk_application_window_new(GTK_APPLICATION(application)));
  GtkWindow* window = self->window;

  gboolean use_header_bar = TRUE;
  #ifdef GDK_WINDOWING_X11
  GdkScreen* screen = gtk_window_get_screen(window);
  if (GDK_IS_X11_SCREEN(screen)) {
    const gchar* wm_name = gdk_x11_screen_get_window_manager_name(screen);
    if (g_strcmp0(wm_name, "GNOME Shell") != 0) {
      use_header_bar = FALSE;
    }
  }
  #endif
  if (use_header_bar) {
    GtkHeaderBar* header_bar = GTK_HEADER_BAR(gtk_header_bar_new());
    gtk_widget_show(GTK_WIDGET(header_bar));
    gtk_header_bar_set_title(header_bar, "pixez");
    gtk_header_bar_set_show_close_button(header_bar, TRUE);
    gtk_window_set_titlebar(window, GTK_WIDGET(header_bar));
  } else {
    gtk_window_set_title(window, "pixez");
  }

  gtk_window_set_default_size(window, 1280, 720);

  g_autoptr(FlDartProject) project = fl_dart_project_new();
  fl_dart_project_set_dart_entrypoint_arguments(
    project, self->dart_entrypoint_arguments);

  self->view = fl_view_new(project);
  GdkRGBA background_color;
  gdk_rgba_parse(&background_color, "#000000");
  fl_view_set_background_color(self->view, &background_color);
  gtk_widget_show(GTK_WIDGET(self->view));
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(self->view));

  g_signal_connect_swapped(self->view, "first-frame", G_CALLBACK(first_frame_cb),
                           self);
  gtk_widget_realize(GTK_WIDGET(self->view));

  fl_register_plugins(FL_PLUGIN_REGISTRY(self->view));

  FlPluginRegistrar* paths_registrar =
  fl_plugin_registry_get_registrar_for_plugin(
    FL_PLUGIN_REGISTRY(self->view), "PathsPlugin");
  register_paths_plugin(paths_registrar);

  FlPluginRegistrar* single_instance_registrar =
  fl_plugin_registry_get_registrar_for_plugin(
    FL_PLUGIN_REGISTRY(self->view), "SingleInstancePlugin");
  register_single_instance_plugin(single_instance_registrar);

  FlPluginRegistrar* document_registrar =
  fl_plugin_registry_get_registrar_for_plugin(
    FL_PLUGIN_REGISTRY(self->view), "DocumentPlugin");
  register_document_plugin(document_registrar);

  FlPluginRegistrar* clipboard_registrar =
  fl_plugin_registry_get_registrar_for_plugin(
    FL_PLUGIN_REGISTRY(self->view), "ClipboardPlugin");
  register_clipboard_plugin(clipboard_registrar);

  FlPluginRegistrar* weiss_registrar =
  fl_plugin_registry_get_registrar_for_plugin(
    FL_PLUGIN_REGISTRY(self->view), "WeissPlugin");
  register_weiss_plugin(weiss_registrar);

  FlPluginRegistrar* saf_registrar =
      fl_plugin_registry_get_registrar_for_plugin(
          FL_PLUGIN_REGISTRY(self->view), "SafPlugin");
  register_saf_plugin(saf_registrar);

  gtk_widget_grab_focus(GTK_WIDGET(self->view));

  // Connect delete-event to properly handle window close and prevent crashes
  g_signal_connect(self->window, "delete-event", G_CALLBACK(my_application_delete_event), self);
}

static gboolean my_application_local_command_line(GApplication* application,
                                                  gchar*** arguments,
                                                  int* exit_status) {
  MyApplication* self = MY_APPLICATION(application);
  self->dart_entrypoint_arguments = g_strdupv(*arguments + 1);

  // 单实例检测：如已有实例在运行，转发命令行参数后立即退出本进程。
  // 抽象 Unix 套接字在内核自动管理生命周期，干净无残留。
  gint argc = g_strv_length(*arguments);
  if (single_instance_try_lock_or_forward(argc, *arguments) == 1) {
    *exit_status = 0;
    return TRUE;
  }

  g_autoptr(GError) error = nullptr;
  if (!g_application_register(application, nullptr, &error)) {
    g_warning("Failed to register: %s", error->message);
    *exit_status = 1;
    return TRUE;
  }

  g_application_activate(application);
  *exit_status = 0;

  return TRUE;
                                                  }

                                                  static void my_application_startup(GApplication* application) {
                                                    G_APPLICATION_CLASS(my_application_parent_class)->startup(application);
                                                  }

                                                  static void my_application_shutdown(GApplication* application) {
                                                    G_APPLICATION_CLASS(my_application_parent_class)->shutdown(application);
                                                  }

                                                  static void my_application_dispose(GObject* object) {
                                                    MyApplication* self = MY_APPLICATION(object);
                                                    g_clear_pointer(&self->dart_entrypoint_arguments, g_strfreev);
                                                    G_OBJECT_CLASS(my_application_parent_class)->dispose(object);
                                                  }

                                                  static void my_application_class_init(MyApplicationClass* klass) {
                                                    G_APPLICATION_CLASS(klass)->activate = my_application_activate;
                                                    G_APPLICATION_CLASS(klass)->local_command_line =
                                                    my_application_local_command_line;
                                                    G_APPLICATION_CLASS(klass)->startup = my_application_startup;
                                                    G_APPLICATION_CLASS(klass)->shutdown = my_application_shutdown;
                                                    G_OBJECT_CLASS(klass)->dispose = my_application_dispose;
                                                  }

static void my_application_init(MyApplication* self) {
  self->window = nullptr;
  self->view = nullptr;
}

MyApplication* my_application_new() {
                                                    g_set_prgname(APPLICATION_ID);
                                                    return MY_APPLICATION(g_object_new(my_application_get_type(),
                                                                                       "application-id", APPLICATION_ID, "flags",
                                                                                       G_APPLICATION_NON_UNIQUE, nullptr));
                                                  }
