/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2025 Canonical Ltd. */

#include <gio/gio.h>
#include <gio/gdesktopappinfo.h>
#include "pk-modify2.h"

#define BUS_NAME "org.freedesktop.PackageKit"
#define APP_CENTER_DESKTOP "snap-store_snap-store.desktop"

#define APP_CENTER_TYPE_PACKAGEKIT_SERVICE (app_center_packagekit_service_get_type ())
G_DECLARE_FINAL_TYPE (AppCenterPackagekitService, app_center_packagekit_service, APP_CENTER, PACKAGEKIT_SERVICE, GApplication)

struct _AppCenterPackagekitService {
    GApplication parent;
    PackageKitModify2 *skeleton;
    const char *display_name;
};
G_DEFINE_TYPE (AppCenterPackagekitService, app_center_packagekit_service, G_TYPE_APPLICATION)

#define PACKAGE_KIT_TYPE_MODIFY2_REPLY_CLOSURE (package_kit_modify2_reply_closure_get_type ())
G_DECLARE_FINAL_TYPE (PackageKitModify2ReplyClosure, package_kit_modify2_reply_closure, PACKAGE_KIT, MODIFY2_REPLY_CLOSURE, GObject)

typedef void (*PackageKitModify2ReplyFunc)(PackageKitModify2 *, GDBusMethodInvocation *);
struct _PackageKitModify2ReplyClosure {
    GObject parent;
    PackageKitModify2ReplyFunc func;
    AppCenterPackagekitService *service;
    GDBusMethodInvocation *invocation;
};
G_DEFINE_TYPE (PackageKitModify2ReplyClosure, package_kit_modify2_reply_closure, G_TYPE_OBJECT)

static PackageKitModify2ReplyClosure *
package_kit_modify2_reply_closure_new (PackageKitModify2ReplyFunc  func,
                                       AppCenterPackagekitService *service,
                                       GDBusMethodInvocation      *invocation)
{
    PackageKitModify2ReplyClosure *self;

    self = g_object_new (PACKAGE_KIT_TYPE_MODIFY2_REPLY_CLOSURE, NULL);
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
    G_OBJECT_CLASS (app_center_packagekit_service_parent_class)->dispose (object);
}

static void
package_kit_modify2_reply_closure_class_init (PackageKitModify2ReplyClosureClass *klass)
{
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    object_class->dispose = package_kit_modify2_reply_closure_dispose;
}

static void
child_watch_cb (GPid     pid,
                gint     status,
                gpointer user_data)
{
    g_autoptr (PackageKitModify2ReplyClosure) reply = PACKAGE_KIT_MODIFY2_REPLY_CLOSURE (user_data);

    reply->func (reply->service->skeleton, reply->invocation);
    g_application_release (G_APPLICATION (reply->service));
    g_spawn_close_pid (pid);
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
    AppCenterPackagekitService *service = APP_CENTER_PACKAGEKIT_SERVICE (user_data);
    g_autoptr (GStrvBuilder) builder = g_strv_builder_new ();
    g_auto (GStrv) envp = g_get_environ ();
    g_auto (GStrv) argv = NULL;
    g_autoptr (GError) error = NULL;
    g_autoptr (PackageKitModify2ReplyClosure) reply = NULL;
    const char *sni = NULL;
    GPid child_pid;

    if (!arg_resources[0])
        return TRUE;

    if (g_variant_lookup (arg_platform_data, "desktop-startup-id", "&s", &sni)) {
        envp = g_environ_setenv (envp, "STARTUP_NOTIFY_ID", sni, TRUE);
        envp = g_environ_setenv (envp, "XDG_ACTIVATION_TOKEN", sni, TRUE);
    }

    g_strv_builder_add (builder, "snap-store");
    for (int i = 0; arg_resources[i]; i++) {
        g_strv_builder_add (builder, "--gst");
        g_strv_builder_add (builder, arg_resources[i]);
    }
    argv = g_strv_builder_end (builder);

    if (!g_spawn_async (g_get_home_dir (), argv, envp,
                        G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD,
                        NULL, NULL, &child_pid, &error)) {
        g_warning ("Failed to launch App Center: %s", error->message);
        g_dbus_method_invocation_return_gerror (invocation, error);
        return TRUE;
    }

    reply = package_kit_modify2_reply_closure_new (
        package_kit_modify2_complete_install_gstreamer_resources,
        service,
        invocation);
    g_child_watch_add (child_pid, child_watch_cb, g_steal_pointer (&reply));
    g_application_hold (G_APPLICATION (service));
    return TRUE;
}

static gboolean
app_center_packagekit_service_dbus_register (GApplication     *app,
                                             GDBusConnection  *connection,
                                             const gchar      *object_path,
                                             GError          **error)
{
    GApplicationClass *parent_class = G_APPLICATION_CLASS (app_center_packagekit_service_parent_class);
    AppCenterPackagekitService *self = APP_CENTER_PACKAGEKIT_SERVICE (app);
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON (self->skeleton);

    if (!parent_class->dbus_register (app, connection, object_path, error))
        return FALSE;

    return g_dbus_interface_skeleton_export (skeleton,
                                             connection,
                                             "/org/freedesktop/PackageKit",
                                             error);
}

static void
app_center_packagekit_service_dbus_unregister (GApplication    *app,
                                               GDBusConnection *connection,
                                               const gchar     *object_path)
{
    GApplicationClass *parent_class = G_APPLICATION_CLASS (app_center_packagekit_service_parent_class);
    AppCenterPackagekitService *self = APP_CENTER_PACKAGEKIT_SERVICE (app);
    GDBusInterfaceSkeleton *skeleton = G_DBUS_INTERFACE_SKELETON (self->skeleton);

    if (g_dbus_interface_skeleton_has_connection (skeleton, connection))
        g_dbus_interface_skeleton_unexport_from_connection (skeleton, connection);

    parent_class->dbus_unregister (app, connection, object_path);
}

static void
app_center_packagekit_service_dispose (GObject *object)
{
    AppCenterPackagekitService *self = APP_CENTER_PACKAGEKIT_SERVICE (object);

    g_clear_object (&self->skeleton);
    G_OBJECT_CLASS (app_center_packagekit_service_parent_class)->dispose (object);
}

static void
app_center_packagekit_service_class_init (AppCenterPackagekitServiceClass *klass)
{
    GApplicationClass *app_class = G_APPLICATION_CLASS (klass);
    GObjectClass *object_class = G_OBJECT_CLASS (klass);

    app_class->dbus_register = app_center_packagekit_service_dbus_register;
    app_class->dbus_unregister = app_center_packagekit_service_dbus_unregister;

    object_class->dispose = app_center_packagekit_service_dispose;
}

static void
app_center_packagekit_service_init (AppCenterPackagekitService *self)
{
    g_autoptr (GDesktopAppInfo) app_info = NULL;

    app_info = g_desktop_app_info_new (APP_CENTER_DESKTOP);
    self->display_name = g_strdup (g_app_info_get_name (G_APP_INFO (app_info)));
    self->skeleton = package_kit_modify2_skeleton_new ();

    package_kit_modify2_set_display_name (self->skeleton, self->display_name);
    g_signal_connect (self->skeleton,
                      "handle-install-gstreamer-resources",
                      G_CALLBACK(handle_install_gstreamer_resources),
                      self);
}

int
main (int argc, char *argv[])
{
    g_autoptr (AppCenterPackagekitService) service = NULL;

    service = g_object_new (APP_CENTER_TYPE_PACKAGEKIT_SERVICE,
                            "application-id", BUS_NAME,
                            "flags", G_APPLICATION_IS_SERVICE,
                            NULL);
    g_application_run (G_APPLICATION (service), argc, argv);
    return 0;
}
