/*
 * Compile with:
 *
 * /Users/frida/Buildbot/frida-android/build/build/frida-cc-wrapper-android-arm64.sh
 * /usr/local/opt/android-ndk-r15c/toolchains/llvm/prebuilt/darwin-x86_64/bin/clang
 * --gcc-toolchain=/usr/local/opt/android-ndk-r15c/toolchains/aarch64-linux-android-4.9/prebuilt/darwin-x86_64
 * --target=aarch64-none-linux-android
 * -no-canonical-prefixes
 * -fPIC
 * -ffunction-sections
 * -fdata-sections
 * -DANDROID
 * -D__ANDROID_API__=21
 * -I/Users/frida/Buildbot/frida-android/build/build/frida-android-arm64/include
 * -I/Users/frida/Buildbot/frida-android/build/build/sdk-android-arm64/include
 * -Wall
 * -Os
 * -pipe
 * -g3
 * frida-core-example.c -o frida-core-example
 * -L. -lfrida-core -llog -ldl
 * -Wl,--export-dynamic
 * -fuse-ld=gold
 * -Wl,--gc-sections,-z,noexecstack,-z,relro,-z,now
 * -lgcc
 * -L/Users/frida/Buildbot/frida-android/build/build/frida-android-arm64/lib
 * -L/Users/frida/Buildbot/frida-android/build/build/sdk-android-arm64/lib
 *
 * Visit www.frida.re to learn more about Frida.
 */

#include "frida-core.h"

#include <stdlib.h>
#include <string.h>

static void on_message (FridaScript * script, const gchar * message, GBytes * data, gpointer user_data);
static void on_signal (int signo);
static gboolean stop (gpointer user_data);

static GMainLoop * loop = NULL;

int
main (int argc,
      char * argv[])
{
  guint target_pid;
  FridaDeviceManager * manager;
  GError * error = NULL;
  FridaDeviceList * devices;
  gint num_devices, i;
  FridaDevice * local_device;
  FridaSession * session;

  frida_init ();

  if (argc != 2 || (target_pid = atoi (argv[1])) == 0)
  {
    g_printerr ("Usage: %s <pid>\n", argv[0]);
    return 1;
  }

  loop = g_main_loop_new (NULL, TRUE);

  signal (SIGINT, on_signal);
  signal (SIGTERM, on_signal);

  manager = frida_device_manager_new ();

  devices = frida_device_manager_enumerate_devices_sync (manager, &error);
  g_assert (error == NULL);

  local_device = NULL;
  num_devices = frida_device_list_size (devices);
  for (i = 0; i != num_devices; i++)
  {
    FridaDevice * device = frida_device_list_get (devices, i);

    g_print ("[*] Found device: \"%s\"\n", frida_device_get_name (device));

    if (frida_device_get_dtype (device) == FRIDA_DEVICE_TYPE_LOCAL)
      local_device = g_object_ref (device);

    g_object_unref (device);
  }
  g_assert (local_device != NULL);

  frida_unref (devices);
  devices = NULL;

  session = frida_device_attach_sync (local_device, target_pid, &error);
  if (error == NULL)
  {
    FridaScript * script;

    g_print ("[*] Attached\n");

    script = frida_session_create_script_sync (session, "example",
        "Interceptor.attach(Module.findExportByName(null, 'open'), {\n"
        "  onEnter: function (args) {\n"
        "    console.log('[*] open(\"' + Memory.readUtf8String(args[0]) + '\")');\n"
        "  }\n"
        "});\n"
        "Interceptor.attach(Module.findExportByName(null, 'close'), {\n"
        "  onEnter: function (args) {\n"
        "    console.log('[*] close(' + args[0].toInt32() + ')');\n"
        "  }\n"
        "});",
        &error);
    g_assert (error == NULL);

    g_signal_connect (script, "message", G_CALLBACK (on_message), NULL);

    frida_script_load_sync (script, &error);
    g_assert (error == NULL);

    g_print ("[*] Script loaded\n");

    if (g_main_loop_is_running (loop))
      g_main_loop_run (loop);

    g_print ("[*] Stopped\n");

    frida_script_unload_sync (script, NULL);
    frida_unref (script);
    g_print ("[*] Unloaded\n");

    frida_session_detach_sync (session);
    frida_unref (session);
    g_print ("[*] Detached\n");
  }
  else
  {
    g_printerr ("Failed to attach: %s\n", error->message);
    g_error_free (error);
  }

  frida_unref (local_device);

  frida_device_manager_close_sync (manager);
  frida_unref (manager);
  g_print ("[*] Closed\n");

  g_main_loop_unref (loop);

  return 0;
}

static void
on_message (FridaScript * script,
            const gchar * message,
            GBytes * data,
            gpointer user_data)
{
  JsonParser * parser;
  JsonObject * root;
  const gchar * type;

  parser = json_parser_new ();
  json_parser_load_from_data (parser, message, -1, NULL);
  root = json_node_get_object (json_parser_get_root (parser));

  type = json_object_get_string_member (root, "type");
  if (strcmp (type, "log") == 0)
  {
    const gchar * log_message;

    log_message = json_object_get_string_member (root, "payload");
    g_print ("%s\n", log_message);
  }
  else
  {
    g_print ("on_message: %s\n", message);
  }

  g_object_unref (parser);
}

static void
on_signal (int signo)
{
  g_idle_add (stop, NULL);
}

static gboolean
stop (gpointer user_data)
{
  g_main_loop_quit (loop);

  return FALSE;
}
