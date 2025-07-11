#include <gio/gio.h>

#define PACKAGE_KIT_TYPE_SESSION_INSTALLER (package_kit_session_installer_get_type ())
G_DECLARE_FINAL_TYPE (PackageKitSessionInstaller, package_kit_session_installer, PACKAGE_KIT, SESSION_INSTALLER, GObject)

gboolean
package_kit_session_installer_dbus_register (PackageKitSessionInstaller *self,
                                             GDBusConnection            *connection,
                                             GError                    **error);

void
package_kit_session_installer_dbus_unregister (PackageKitSessionInstaller *self,
                                               GDBusConnection            *connection);

PackageKitSessionInstaller *
package_kit_session_installer_new (GApplication *app);
