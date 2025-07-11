#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include <pk-modify2.h>

#include "packagekit_session_installer.h"

#define APP_CENTER_DESKTOP "snap-store_snap-store.desktop"

struct _PackageKitSessionInstaller {
    GObject parent;
    GApplication *app;
    PackageKitModify2 *skeleton;
    guint bus_owner_id;
};
G_DEFINE_TYPE (PackageKitSessionInstaller, package_kit_session_installer, G_TYPE_OBJECT)

#define PACKAGE_KIT_TYPE_MODIFY2_REPLY_CLOSURE (package_kit_modify2_reply_closure_get_type ())
G_DECLARE_FINAL_TYPE (PackageKitModify2ReplyClosure, package_kit_modify2_reply_closure, PACKAGE_KIT, MODIFY2_REPLY_CLOSURE, GObject)

typedef void (*PackageKitModify2ReplyFunc)(PackageKitModify2 *, GDBusMethodInvocation *);
struct _PackageKitModify2ReplyClosure {
    GObject parent;
    PackageKitModify2ReplyFunc func;
    PackageKitSessionInstaller *service;
    GDBusMethodInvocation *invocation;
};
G_DEFINE_TYPE (PackageKitModify2ReplyClosure, package_kit_modify2_reply_closure, G_TYPE_OBJECT)

static PackageKitModify2ReplyClosure *
package_kit_modify2_reply_closure_new (PackageKitModify2ReplyFunc  func,
                                       PackageKitSessionInstaller *service,
                                       GDBusMethodInvocation      *invocation)
{
    PackageKitModify2ReplyClosure *self;

    self = PACKAGE_KIT_MODIFY2_REPLY_CLOSURE (
            g_object_new (PACKAGE_KIT_TYPE_MODIFY2_REPLY_CLOSURE, NULL));
    self->func = func;
    self->service = g_object_ref (service);
    self->invocation = g_object_ref (invocation);

    return self;
}

static void
package_kit_modify2_reply_closure_init (PackageKitModify2ReplyClosure *self)
{
}

static void
package_kit_modify2_reply_closure_dispose (GObject *object)
{
    PackageKitModify2ReplyClosure *self = PACKAGE_KIT_MODIFY2_REPLY_CLOSURE (object);

    g_clear_object (&self->service);
    g_clear_object (&self->invocation);
    G_OBJECT_CLASS (package_kit_session_installer_parent_class)->dispose (object);
}

static void
package_kit_modify2_reply_closure_class_init (PackageKitModify2ReplyClosureClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = package_kit_modify2_reply_closure_dispose;
}

static gboolean
handle_install_gstreamer_resources (PackageKitModify2      *interface,
                                    GDBusMethodInvocation  *invocation,
                                    char                  **arg_resources,
                                    const char             *arg_interaction,
                                    const char             *arg_desktop_id,
                                    GVariant               *arg_platform_data,
                                    gpointer                user_data)
{
    PackageKitSessionInstaller *service = PACKAGE_KIT_SESSION_INSTALLER (user_data);
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_auto (GStrv) argv = NULL;
    g_autoptr (PackageKitModify2ReplyClosure) reply = NULL;
    GApplicationClass *app_class = G_APPLICATION_GET_CLASS (service->app);
    const char *sni = NULL;
    int status;

    if (!arg_resources[0])
        return TRUE;

    if (g_variant_lookup (arg_platform_data, "desktop-startup-id", "&s", &sni)) {
        // TODO: set SNI
    }

    for (int i = 0; arg_resources[i]; i++) {
        g_strv_builder_add (builder, "--gst");
        g_strv_builder_add (builder, arg_resources[i]);
    }
    argv = g_strv_builder_end (builder);

    reply = package_kit_modify2_reply_closure_new (
        package_kit_modify2_complete_install_gstreamer_resources,
        service,
        invocation);

    app_class->local_command_line (service->app, &argv, &status);
    if (status != 0) {
        g_error ("Failed to launch App Center");
        g_dbus_method_invocation_return_error (invocation,
                                               G_DBUS_ERROR,
                                               G_DBUS_ERROR_INVALID_ARGS,
                                               "Failed to launch App Center");
        return TRUE;
    }

    // TODO: Reply after install complete
    reply->func (reply->service->skeleton, reply->invocation);

    return TRUE;
}

gboolean
package_kit_session_installer_dbus_register (PackageKitSessionInstaller *self,
                                             GDBusConnection            *connection,
                                             GError                    **error)
{
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON (self->skeleton);

    if (!g_dbus_interface_skeleton_export (skeleton,
                                           connection,
                                           "/org/freedesktop/PackageKit",
                                           error))
        return FALSE;

    self->bus_owner_id =
        g_bus_own_name_on_connection (connection,
                                      "org.freedesktop.PackageKit",
                                      G_BUS_NAME_OWNER_FLAGS_NONE,
                                      NULL, NULL, NULL, NULL);

    return TRUE;
}

void
package_kit_session_installer_dbus_unregister (PackageKitSessionInstaller *self,
                                               GDBusConnection            *connection)
{
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON (self->skeleton);

    if (g_dbus_interface_skeleton_has_connection (skeleton, connection))
        g_dbus_interface_skeleton_unexport_from_connection (skeleton, connection);
}

static void
package_kit_session_installer_dispose (GObject *object)
{
    PackageKitSessionInstaller *self = PACKAGE_KIT_SESSION_INSTALLER (object);

    g_clear_object (&self->skeleton);
    G_OBJECT_CLASS (package_kit_session_installer_parent_class)->dispose (object);
}

static void
package_kit_session_installer_class_init (PackageKitSessionInstallerClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = package_kit_session_installer_dispose;
}

static void
package_kit_session_installer_init (PackageKitSessionInstaller *self)
{
    g_autoptr (GDesktopAppInfo) app_info = NULL;
    const char* display_name = NULL;

    app_info = g_desktop_app_info_new (APP_CENTER_DESKTOP);
    display_name = g_app_info_get_name (G_APP_INFO (app_info));
    self->skeleton = package_kit_modify2_skeleton_new ();

    package_kit_modify2_set_display_name (self->skeleton, display_name);
    g_signal_connect (self->skeleton,
                      "handle-install-gstreamer-resources",
                      G_CALLBACK(handle_install_gstreamer_resources),
                      self);
}

PackageKitSessionInstaller *
package_kit_session_installer_new (GApplication *app)
{
    PackageKitSessionInstaller *self = PACKAGE_KIT_SESSION_INSTALLER (
            g_object_new (PACKAGE_KIT_TYPE_SESSION_INSTALLER, NULL));

    self->app = app;
    return self;
}
