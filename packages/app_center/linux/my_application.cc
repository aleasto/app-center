#include "my_application.h"

#include <flutter_linux/flutter_linux.h>
#include <handy.h>

#include "packagekit_session_installer.h"
#include "flutter/generated_plugin_registrant.h"

#ifdef NDEBUG
#define APPLICATION_FLAGS \
  G_APPLICATION_HANDLES_COMMAND_LINE | G_APPLICATION_HANDLES_OPEN
#else
#define APPLICATION_FLAGS G_APPLICATION_NON_UNIQUE
#endif

struct _MyApplication {
  GtkApplication parent_instance;
  char** dart_entrypoint_arguments;
  PackageKitSessionInstaller *pk_session_installer;
};

G_DEFINE_TYPE(MyApplication, my_application, GTK_TYPE_APPLICATION)

// Implements GApplication::activate.
static void my_application_activate(GApplication* application) {
  MyApplication* self = MY_APPLICATION(application);

#ifdef NDEBUG
  GList* windows = gtk_application_get_windows(GTK_APPLICATION(application));
  if (windows) {
    gtk_window_present(GTK_WINDOW(windows->data));
    return;
  }
#endif

  GtkWindow* window = GTK_WINDOW(hdy_application_window_new());
  gtk_window_set_application(window, GTK_APPLICATION(application));

  GdkGeometry geometry;

  // TODO: find better solution; set default window size based on available space
  geometry.min_width = 800 + 52;  // account for shadow from libhandy
  geometry.min_height = 600 + 52;
  gtk_window_set_geometry_hints(window, nullptr, &geometry, GDK_HINT_MIN_SIZE);

  gtk_window_set_default_size(window, 1280 + 52, 800 + 52);
  gtk_widget_show(GTK_WIDGET(window));

  g_autoptr(FlDartProject) project = fl_dart_project_new();
  fl_dart_project_set_dart_entrypoint_arguments(
      project, self->dart_entrypoint_arguments);

  FlView* view = fl_view_new(project);
  gtk_widget_show(GTK_WIDGET(view));
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));

  fl_register_plugins(FL_PLUGIN_REGISTRY(view));

  gtk_widget_grab_focus(GTK_WIDGET(view));
}

// Implements GApplication::local_command_line.
static gboolean my_application_local_command_line(GApplication* application,
                                                  gchar*** arguments,
                                                  int* exit_status) {
  MyApplication* self = MY_APPLICATION(application);
  // Strip out the first argument as it is the binary name.
  self->dart_entrypoint_arguments = g_strdupv(*arguments + 1);

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

#ifdef NDEBUG
// Implements GApplication::command_line.
static gint my_application_command_line(GApplication* application,
                                        GApplicationCommandLine* command_line) {
  gchar** arguments =
      g_application_command_line_get_arguments(command_line, nullptr);
  gint exit_status = 0;
  my_application_local_command_line(application, &arguments, &exit_status);
  return exit_status;
}
#endif

// Implements GApplication::dbus_register.
static gboolean my_application_dbus_register(GApplication* application,
                                             GDBusConnection* connection,
                                             const gchar* object_path,
                                             GError** error) {
  MyApplication *self = MY_APPLICATION(application);
  GApplicationClass *parent_class =
      G_APPLICATION_CLASS(my_application_parent_class);
  g_autoptr(GError) local_error = NULL;

  if (!parent_class->dbus_register(application,
                                   connection,
                                   object_path,
                                   error))
    return FALSE;

  if (!package_kit_session_installer_dbus_register(self->pk_session_installer,
                                                   connection,
                                                   &local_error))
    g_warning ("Failed to register PackageKit session installer: %s", local_error->message);

  return TRUE;
}

// Implements GApplication::dbus_unregister.
static void my_application_dbus_unregister(GApplication* application,
                                           GDBusConnection* connection,
                                           const gchar* object_path) {
  MyApplication *self = MY_APPLICATION(application);
  GApplicationClass *parent_class =
      G_APPLICATION_CLASS(my_application_parent_class);

  package_kit_session_installer_dbus_unregister(self->pk_session_installer,
                                                connection);

  parent_class->dbus_unregister(application, connection, object_path);
}

// Implements GObject::dispose.
static void my_application_dispose(GObject* object) {
  MyApplication* self = MY_APPLICATION(object);
  g_clear_pointer(&self->dart_entrypoint_arguments, g_strfreev);
  g_clear_object(&self->pk_session_installer);
  G_OBJECT_CLASS(my_application_parent_class)->dispose(object);
}

static void my_application_class_init(MyApplicationClass* klass) {
  G_APPLICATION_CLASS(klass)->activate = my_application_activate;
  G_APPLICATION_CLASS(klass)->dbus_register = my_application_dbus_register;
  G_APPLICATION_CLASS(klass)->dbus_unregister = my_application_dbus_unregister;
#ifdef NDEBUG
  G_APPLICATION_CLASS(klass)->command_line = my_application_command_line;
#else
  G_APPLICATION_CLASS(klass)->local_command_line =
      my_application_local_command_line;
#endif
  G_OBJECT_CLASS(klass)->dispose = my_application_dispose;
}

static void my_application_init(MyApplication* self) {
  GApplication *app = G_APPLICATION (self);
  self->pk_session_installer = package_kit_session_installer_new (app);
}

MyApplication* my_application_new() {
  return MY_APPLICATION(g_object_new(my_application_get_type(),
                                     "application-id", APPLICATION_ID, "flags",
                                     APPLICATION_FLAGS, nullptr));
}
