/*
 *     Project: Remmina Plugin RDESKTOP
 * Description: Remmina protocol plugin to open a RDP connection with rdesktop.
 *      Author: Fabio Castelli (Muflone) <muflone@vbsimple.net>
 *   Copyright: 2013-2016 Fabio Castelli (Muflone)
 *     License: GPL-2+
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of ERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "plugin_config.h"
#include <remmina/remmina_plugin.h>
#if GTK_VERSION == 3
  # include <gtk/gtkx.h>
#endif

typedef struct _RemminaPluginData
{
  GtkWidget *socket;
  gint socket_id;
  GPid pid;
} RemminaPluginData;

static RemminaPluginService *remmina_plugin_service = NULL;

static void remmina_plugin_rdesktop_on_plug_added(GtkSocket *socket, RemminaProtocolWidget *gp)
{
  RemminaPluginData *gpdata;
  gpdata = (RemminaPluginData*) g_object_get_data(G_OBJECT(gp), "plugin-data");
  remmina_plugin_service->log_printf("[%s] remmina_plugin_rdesktop_on_plug_added socket %d\n", PLUGIN_NAME, gpdata->socket_id);
  remmina_plugin_service->protocol_plugin_emit_signal(gp, "connect");
  return;
}

static void remmina_plugin_rdesktop_on_plug_removed(GtkSocket *socket, RemminaProtocolWidget *gp)
{
  remmina_plugin_service->log_printf("[%s] remmina_plugin_rdesktop_on_plug_removed\n", PLUGIN_NAME);
  remmina_plugin_service->protocol_plugin_close_connection(gp);
}

static void remmina_plugin_rdesktop_init(RemminaProtocolWidget *gp)
{
  remmina_plugin_service->log_printf("[%s] remmina_plugin_rdesktop_init\n", PLUGIN_NAME);
  RemminaPluginData *gpdata;

  gpdata = g_new0(RemminaPluginData, 1);
  g_object_set_data_full(G_OBJECT(gp), "plugin-data", gpdata, g_free);

  gpdata->socket = gtk_socket_new();
  remmina_plugin_service->protocol_plugin_register_hostkey(gp, gpdata->socket);
  gtk_widget_show(gpdata->socket);
  g_signal_connect(G_OBJECT(gpdata->socket), "plug-added", G_CALLBACK(remmina_plugin_rdesktop_on_plug_added), gp);
  g_signal_connect(G_OBJECT(gpdata->socket), "plug-removed", G_CALLBACK(remmina_plugin_rdesktop_on_plug_removed), gp);
  gtk_container_add(GTK_CONTAINER(gp), gpdata->socket);
}

static gboolean remmina_plugin_rdesktop_open_connection(RemminaProtocolWidget *gp)
{
  remmina_plugin_service->log_printf("[%s] remmina_plugin_rdesktop_open_connection\n", PLUGIN_NAME);
  #define GET_PLUGIN_STRING(value) \
    g_strdup(remmina_plugin_service->file_get_string(remminafile, value))
  #define GET_PLUGIN_BOOLEAN(value) \
    remmina_plugin_service->file_get_int(remminafile, value, FALSE)
  #define GET_PLUGIN_INT(value, default_value) \
    remmina_plugin_service->file_get_int(remminafile, value, default_value)
  #define GET_PLUGIN_PASSWORD(value) \
    g_strdup(remmina_plugin_service->file_get_secret(remminafile, value))
  #define ADD_ARGUMENT(name, value) \
    { \
      argv[argc] = g_strdup(name); \
      argv_debug[argc] = g_strdup(name); \
      argc++; \
      if (value != NULL) \
      { \
        argv[argc] = value; \
        argv_debug[argc++] = g_strdup(g_strcmp0(name, "-p") != 0 ? value : "XXXXX"); \
      } \
    }
  RemminaPluginData *gpdata;
  RemminaFile *remminafile;
  gboolean ret;
  GError *error = NULL;
  gchar *argv[50];  // Contains all the arguments included the password
  gchar *argv_debug[50]; // Contains all the arguments, excluding the password
  gchar *command_line; // The whole command line obtained from argv_debug
  gint argc;
  gint i;
  
  gchar *option_str;
  gint option_int;

  gpdata = (RemminaPluginData*) g_object_get_data(G_OBJECT(gp), "plugin-data");
  remminafile = remmina_plugin_service->protocol_plugin_get_file(gp);

  if (!GET_PLUGIN_BOOLEAN("detached"))
  {
    remmina_plugin_service->protocol_plugin_set_width(gp, 640);
    remmina_plugin_service->protocol_plugin_set_height(gp, 480);
    gtk_widget_set_size_request(GTK_WIDGET(gp), 640, 480);
    gpdata->socket_id = gtk_socket_get_id(GTK_SOCKET(gpdata->socket));
  }

  argc = 0;
  // Main executable name
  ADD_ARGUMENT("rdesktop", NULL);
  // Username for authentication on the server
  option_str = GET_PLUGIN_STRING("username");
  if (option_str)
    ADD_ARGUMENT("-u", option_str);
  // Domain for authentication
  option_str = GET_PLUGIN_STRING("domain");
  if (option_str)
    ADD_ARGUMENT("-d", option_str);
  // The  password  to  authenticate  with
  option_str = GET_PLUGIN_PASSWORD("password");
  if (option_str)
    ADD_ARGUMENT("-p", option_str);
  // Client hostname
  option_str = GET_PLUGIN_STRING("clientname");
  if (option_str)
    ADD_ARGUMENT("-n", option_str);
  // Startup shell to execute on the server
  option_str = GET_PLUGIN_STRING("exec");
  if (option_str)
    ADD_ARGUMENT("-s", option_str);
  // The initial working directory for the user
  option_str = GET_PLUGIN_STRING("execpath");
  if (option_str)
    ADD_ARGUMENT("-c", option_str);
  // Sets the window title
  option_str = GET_PLUGIN_STRING("title");
  if (option_str)
    ADD_ARGUMENT("-T", option_str);
  // Keyboard layout to emulate
  option_str = GET_PLUGIN_STRING("keymap");
  if (option_str)
    ADD_ARGUMENT("-k", option_str);
  // Attach to the console of the server (requires Windows Server 2003 or newer)
  if (GET_PLUGIN_BOOLEAN("console"))
    ADD_ARGUMENT("-0", NULL);
  // Enable compression of the RDP datastream
  if (GET_PLUGIN_BOOLEAN("compression"))
    ADD_ARGUMENT("-z", NULL);
  // Enable caching of bitmaps to disk (persistent bitmap caching)
  if (GET_PLUGIN_BOOLEAN("bitmapcaching"))
    ADD_ARGUMENT("-P", NULL);
  // Redirects a path to the share \\tsclient\<sharename> on the server
  option_str = GET_PLUGIN_STRING("sharefolder");
  if (option_str)
  {
    ADD_ARGUMENT("-r", g_strdup_printf("disk:share=%s", option_str));
    g_free(option_str);
  }

  if (GET_PLUGIN_BOOLEAN("fullscreen"))
  {
    // Enable fullscreen mode
    ADD_ARGUMENT("-f", NULL);
  }
  else
  {
    // The SeamlessRDP option cannot be combined with screen resolutions
    if (GET_PLUGIN_BOOLEAN("seamlessrdp"))
    {
      option_str = GET_PLUGIN_STRING("seamlessrdpshell");
      if (option_str)
      {
        ADD_ARGUMENT("-A", g_strdup_printf("\"%s\" %s", option_str, GET_PLUGIN_STRING("exec")));
        g_free(option_str);
      }
    }
    else {
      // Desktop geometry (WxH)
      ADD_ARGUMENT("-g", g_strdup_printf("%ix%i", 
        GET_PLUGIN_INT("resolution_width", 1024), 
        GET_PLUGIN_INT("resolution_height", 768))
      );
    }
  }
  // Sets the colour depth for the connection
  option_int = GET_PLUGIN_INT("colordepth", 0);
  if (option_int != 0)
    ADD_ARGUMENT("-a", g_strdup_printf("%i", option_int));
  // Changes default bandwidth performance behaviour for RDP5
  option_str = GET_PLUGIN_STRING("experience");
  if (option_str)
    ADD_ARGUMENT("-x", option_str);
  // Redirects sound generated on the server to the client
  option_str = GET_PLUGIN_STRING("sound");
  if (option_str)
  {
    ADD_ARGUMENT("-r", g_strdup_printf("sound:%s", option_str));
    g_free(option_str);
  }
  // Hide window manager decorations, by using MWM hints
  if (GET_PLUGIN_BOOLEAN("hidedecorations"))
    ADD_ARGUMENT("-D", NULL);
  // Do not override window manager key bindings
  if (GET_PLUGIN_BOOLEAN("nograbkeyboard"))
    ADD_ARGUMENT("-K", NULL);
  // Disable  encryption  from  client to server
  if (GET_PLUGIN_BOOLEAN("noencryption"))
    ADD_ARGUMENT("-E", NULL);
  // Enable  numlock  syncronization between the Xserver and the remote RDP session
  if (GET_PLUGIN_BOOLEAN("syncnumlock"))
    ADD_ARGUMENT("-N", NULL);
  // Use RDP version 4
  if (GET_PLUGIN_BOOLEAN("rdp4"))
    ADD_ARGUMENT("-4", NULL);
  // Use RDP version 5
  if (GET_PLUGIN_BOOLEAN("rdp5"))
    ADD_ARGUMENT("-5", NULL);
  // Do not send mouse motion events
  if (GET_PLUGIN_BOOLEAN("nomousemotion"))
    ADD_ARGUMENT("-m", NULL);
  // Embed rdesktop-window in another window
  if (gpdata->socket_id != 0)
    ADD_ARGUMENT("-X", g_strdup_printf("%i", gpdata->socket_id));
  // Server address
  option_str = GET_PLUGIN_STRING("server");
  ADD_ARGUMENT(option_str, NULL);
  g_free(option_str);
  // End of the arguments list
  ADD_ARGUMENT(NULL, NULL);
  // Retrieve the whole command line
  command_line = g_strjoinv(g_strdup(" "), (gchar **)&argv_debug[0]);
  remmina_plugin_service->log_printf("[RDESKTOP] starting %s\n", command_line);
  g_free(command_line);
  // Execute the external process rdesktop
  ret = g_spawn_async(NULL, argv, NULL, G_SPAWN_SEARCH_PATH,
    NULL, NULL, &gpdata->pid, &error);
  remmina_plugin_service->log_printf(
    "[RDESKTOP] started rdesktop with GPid %d\n", &gpdata->pid);
  // Free the arguments list
  for (i = 0; i < argc; i++)
  {
    g_free(argv_debug[i]);
    g_free(argv[i]);
  }
  // Show error message
  if (!ret)
    remmina_plugin_service->protocol_plugin_set_error(gp, "%s", error->message);
  // Show attached window socket ID
  if (!GET_PLUGIN_BOOLEAN("detached"))
  {
    remmina_plugin_service->log_printf("[RDESKTOP] attached window to socket %d\n", gpdata->socket_id);
    return TRUE;
  }
  else
  {
    return FALSE;
  }
}

static gboolean remmina_plugin_rdesktop_close_connection(RemminaProtocolWidget *gp)
{
  remmina_plugin_service->log_printf("[%s] remmina_plugin_rdesktop_close_connection\n", PLUGIN_NAME);
  remmina_plugin_service->protocol_plugin_emit_signal(gp, "disconnect");
  return FALSE;
}

static gpointer colordepth_list[] =
{
  "8", N_("256 colors (8 bpp)"),
  "15", N_("High color (15 bpp)"),
  "16", N_("High color (16 bpp)"),
  "24", N_("True color (24 bpp)"),
  "32", N_("True color (32 bpp)"),
  NULL
};

static gpointer experience_list[] =
{
  "", N_("Default"),
  "m", N_("Modem (no wallpaper, full window drag, animations, theming)"),
  "b", N_("Broadband (remove wallpaper)"),
  "l", N_("LAN (show all details)"),
  "0x8F", N_("Modem with font smoothing"),
  "0x81", N_("Broadband with font smoothing"),
  "0x80", N_("LAN with font smoothing"),
  "0x01", N_("Disable wallpaper"),
  "0x02", N_("Disable full window drag"),
  "0x03", N_("Disable wallpaper, full window drag"),
  "0x04", N_("Disable animations"),
  "0x05", N_("Disable animations, wallpaper"),
  "0x06", N_("Disable animations, full window drag"),
  "0x07", N_("Disable animations, wallpaper, full window drag"),
  "0x08", N_("Disable theming"),
  "0x09", N_("Disable theming, wallpaper"),
  "0x0a", N_("Disable theming, full window drag"),
  "0x0b", N_("Disable theming, wallpaper, full window drag"),
  "0x0c", N_("Disable theming, animations"),
  "0x0d", N_("Disable theming, animations, wallpaper"),
  "0x0e", N_("Disable theming, animations, full window drag"),
  "0x0f", N_("Disable everything"),
  NULL
};

static gpointer sound_list[] =
{
  "off", N_("Off"),
  "local", N_("Local"),
  "local,11025,1", N_("Local - low quality"),
  "local,22050,2", N_("Local - medium quality"),
  "local,44100,2", N_("Local - high quality"),
  "remote", N_("Remote"),
  NULL
};

static const RemminaProtocolSetting remmina_plugin_rdesktop_basic_settings[] =
{
  { REMMINA_PROTOCOL_SETTING_TYPE_SERVER, NULL, NULL, FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_TEXT, "username", N_("User name"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_PASSWORD, NULL, NULL, FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_TEXT, "domain", N_("Domain"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_RESOLUTION, NULL, NULL, FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_SELECT, "colordepth", N_("Color depth"), FALSE, colordepth_list, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_SELECT, "experience", N_("Experience"), FALSE, experience_list, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_SELECT, "sound", N_("Sound"), FALSE, sound_list, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_FOLDER, "sharefolder", N_("Share folder"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_END, NULL, NULL, FALSE, NULL, NULL }
};

static const RemminaProtocolSetting remmina_plugin_rdesktop_advanced_settings[] =
{
  { REMMINA_PROTOCOL_SETTING_TYPE_TEXT, "title", N_("Window title"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_TEXT, "clientname", N_("Client name"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_TEXT, "exec", N_("Startup program"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_TEXT, "execpath", N_("Startup path"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_COMBO, "keymap", N_("Keyboard map"), FALSE, 
    "ar,cs,da,de,de-ch,en-dv,en-gb,en-us,es,et,fi,fo,fr,fr-be,fr-ca,fr-ch,he,hr,hu,is,it,ja,ko,lt,lv,mk,nl,nl-be,no,pl,pt,pt-br,ru,sl,sv,th,tr", NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "fullscreen", N_("Fullscreen"), TRUE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "seamlessrdp", N_("Seamless RDP"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_TEXT, "seamlessrdpshell", N_("Seamless RDP Shell Path"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "console", N_("Attach to console (Windows 2003 / 2003 R2)"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "compression", N_("RDP datastream compression"), TRUE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "bitmapcaching", N_("Bitmap caching"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "nomousemotion", N_("Don't send mouse motion events"), TRUE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "hidedecorations", N_("Hide WM decorations"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "detached", N_("Detached window"), TRUE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "nograbkeyboard", N_("Don't grab keyboard"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "rdp4", N_("Force RDP version 4"), TRUE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "rdp5", N_("Force RDP version 5"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "syncnumlock", N_("Numlock syncronization"), TRUE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_CHECK, "noencryption", N_("Disable encryption"), FALSE, NULL, NULL },
  { REMMINA_PROTOCOL_SETTING_TYPE_END, NULL, NULL, FALSE, NULL, NULL }
};

static RemminaProtocolPlugin remmina_plugin =
{
  REMMINA_PLUGIN_TYPE_PROTOCOL,
  PLUGIN_NAME,
  PLUGIN_DESCRIPTION,
  GETTEXT_PACKAGE,
  PLUGIN_VERSION,
  PLUGIN_APPICON,
  PLUGIN_APPICON,
  remmina_plugin_rdesktop_basic_settings,
  remmina_plugin_rdesktop_advanced_settings,
  REMMINA_PROTOCOL_SSH_SETTING_NONE,
  NULL,
  remmina_plugin_rdesktop_init,
  remmina_plugin_rdesktop_open_connection,
  remmina_plugin_rdesktop_close_connection,
  NULL,
  NULL
};

G_MODULE_EXPORT gboolean remmina_plugin_entry(RemminaPluginService *service)
{
  remmina_plugin_service = service;

  if (!service->register_plugin((RemminaPlugin *) &remmina_plugin))
  {
    return FALSE;
  }
  return TRUE;
}
