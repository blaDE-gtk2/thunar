/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2006 Benedikt Meurer <benny@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 59 Temple
 * Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#include <stdio.h>
#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_LIBSM
#include <X11/SM/SMlib.h>
#endif

#include <glib/gstdio.h>

#include <fmb/fmb-application.h>
#include <fmb/fmb-ice.h>
#include <fmb/fmb-private.h>
#include <fmb/fmb-session-client.h>
#include <fmb/fmb-file.h>



static void     fmb_session_client_finalize            (GObject                  *object);
#ifdef HAVE_LIBSM
static gboolean fmb_session_client_connect             (FmbSessionClient      *session_client,
                                                           const gchar              *previous_id);
static void     fmb_session_client_restore             (FmbSessionClient      *session_client);
static void     fmb_session_client_die                 (SmcConn                   connection,
                                                           FmbSessionClient      *session_client);
static void     fmb_session_client_save_complete       (SmcConn                   connection,
                                                           FmbSessionClient      *session_client);
static void     fmb_session_client_save_yourself       (SmcConn                   connection,
                                                           FmbSessionClient      *session_client,
                                                           gint                      save_type,
                                                           Bool                      shutdown,
                                                           gint                      interact_style,
                                                           Bool                      fast);
static void     fmb_session_client_shutdown_cancelled  (SmcConn                   connection,
                                                           FmbSessionClient      *session_client);
#endif /* !HAVE_LIBSM */



struct _FmbSessionClientClass
{
  GObjectClass __parent__;
};

struct _FmbSessionClient
{
  GObject __parent__;

  gchar  *path;
  gchar  *id;

#ifdef HAVE_LIBSM
  SmcConn connection;
#endif
};



G_DEFINE_TYPE (FmbSessionClient, fmb_session_client, G_TYPE_OBJECT)



static void
fmb_session_client_class_init (FmbSessionClientClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_session_client_finalize;
}



static void
fmb_session_client_init (FmbSessionClient *session_client)
{
  /* initialize the ICE layer */
  fmb_ice_init ();
}



static void
fmb_session_client_finalize (GObject *object)
{
  FmbSessionClient *session_client = FMB_SESSION_CLIENT (object);

#ifdef HAVE_LIBSM
  /* disconnect from the session manager */
  if (G_LIKELY (session_client->connection != NULL))
    SmcCloseConnection (session_client->connection, 0, NULL);
#endif /* !HAVE_LIBSM */

  /* release internal state */
  g_free (session_client->path);
  g_free (session_client->id);

  (*G_OBJECT_CLASS (fmb_session_client_parent_class)->finalize) (object);
}



#ifdef HAVE_LIBSM
static gboolean
fmb_session_client_connect (FmbSessionClient *session_client,
                               const gchar         *previous_id)
{
  SmcCallbacks session_callbacks;
  SmPropValue  value_discard[3];
  SmPropValue  value_restart[4];
  SmPropValue  value_userid[1];
  SmPropValue  value_priority[1];
  SmProp      *properties[6];
  SmProp       prop_clone;
  SmProp       prop_discard;
  SmProp       prop_program;
  SmProp       prop_restart;
  SmProp       prop_userid;
  SmProp       prop_priority;
  gchar        error_string[1];
  gchar       *spec;
  gchar       *id = NULL;

  /* setup the session client callbacks */
  session_callbacks.die.callback = (SmcDieProc) fmb_session_client_die;
  session_callbacks.die.client_data = session_client;
  session_callbacks.save_complete.callback = (SmcSaveCompleteProc) fmb_session_client_save_complete;
  session_callbacks.save_complete.client_data = session_client;
  session_callbacks.save_yourself.callback = (SmcSaveYourselfProc) fmb_session_client_save_yourself;
  session_callbacks.save_yourself.client_data = session_client;
  session_callbacks.shutdown_cancelled.callback = (SmcShutdownCancelledProc) fmb_session_client_shutdown_cancelled;
  session_callbacks.shutdown_cancelled.client_data = session_client;

  /* try to establish a connection to the session manager */
  session_client->connection = SmcOpenConnection (NULL, NULL, SmProtoMajor, SmProtoMinor, SmcDieProcMask
                                                  | SmcSaveCompleteProcMask | SmcSaveYourselfProcMask
                                                  | SmcShutdownCancelledProcMask, &session_callbacks,
                                                  (gchar *) previous_id, &id, sizeof (error_string), error_string);
  if (G_UNLIKELY (session_client->connection == NULL))
    return FALSE;

  /* tell GDK about our new session id */
  gdk_set_sm_client_id (id);
#if GLIB_CHECK_VERSION (2, 46, 0)
  /* just use the memory */
  session_client->id = id;
#else
  /* remember the returned client id */
  if (g_mem_is_system_malloc ())
    {
      /* just use the memory */
      session_client->id = id;
    }
  else
    {
      /* use GLib memory management */
      session_client->id = g_strdup (id);
      free (id);
    }
#endif

  /* determine the session file path */
  spec = g_strconcat ("sessions/Fmb-", session_client->id, NULL);
  session_client->path = xfce_resource_save_location (XFCE_RESOURCE_CACHE, spec, TRUE);
  g_free (spec);

  /* verify that we were able to create the path to the file */
  if (G_UNLIKELY (session_client->path == NULL))
    {
      /* disconnect from the session manager */
      SmcCloseConnection (session_client->connection, 0, NULL);
      session_client->connection = NULL;
      return FALSE;
    }

  /* SmCloneCommand (see SmRestartCommand) */
  prop_clone.name = SmCloneCommand;
  prop_clone.type = SmLISTofARRAY8;
  prop_clone.num_vals = 1;
  prop_clone.vals = &value_restart[0];

  /* SmDiscardCommand */
  prop_discard.name = SmDiscardCommand;
  prop_discard.type = SmLISTofARRAY8;
  prop_discard.num_vals = G_N_ELEMENTS (value_discard);
  prop_discard.vals = &value_discard[0];
  value_discard[0].value = "rm";
  value_discard[0].length = sizeof ("rm") - 1;
  value_discard[1].value = "-f";
  value_discard[1].length = sizeof ("-f") - 1;
  value_discard[2].value = session_client->path;
  value_discard[2].length = strlen (session_client->path);

  /* SmProgram (see SmRestartCommand) */
  prop_program.name = SmProgram;
  prop_program.type = SmARRAY8;
  prop_program.num_vals = 1;
  prop_program.vals = &value_restart[0];

  /* SmRestartCommand */
  prop_restart.name = SmRestartCommand;
  prop_restart.type = SmLISTofARRAY8;
  prop_restart.num_vals = G_N_ELEMENTS (value_restart);
  prop_restart.vals = &value_restart[0];
  value_restart[0].value = "Fmb";
  value_restart[0].length = sizeof ("Fmb") - 1;
  value_restart[1].value = "--sm-client-id";
  value_restart[1].length = sizeof ("--sm-client-id") - 1;
  value_restart[2].value = session_client->id;
  value_restart[2].length = strlen (session_client->id);
  value_restart[3].value = "--daemon";
  value_restart[3].length = sizeof ("--daemon") - 1;

  /* SmUserID */
  prop_userid.name = SmUserID;
  prop_userid.type = SmARRAY8;
  prop_userid.num_vals = G_N_ELEMENTS (value_userid);
  prop_userid.vals = &value_userid[0];
  value_userid[0].value = (gchar *) g_get_user_name ();
  value_userid[0].length = strlen (value_userid[0].value);

  /* _GSM_Priority */
  prop_priority.name = "_GSM_Priority";
  prop_priority.type = SmCARD8;
  prop_priority.num_vals = G_N_ELEMENTS (value_priority);
  prop_priority.vals = &value_priority[0];
  value_priority[0].value = "\30";
  value_priority[0].length = 1;

  /* setup the properties list */
  properties[0] = &prop_clone;
  properties[1] = &prop_discard;
  properties[2] = &prop_program;
  properties[3] = &prop_restart;
  properties[4] = &prop_userid;
  properties[5] = &prop_priority;

  /* send the properties to the session manager */
  SmcSetProperties (session_client->connection, G_N_ELEMENTS (properties), properties);

  return TRUE;
}



static void
fmb_session_client_restore (FmbSessionClient *session_client)
{
  FmbApplication *application;
  gchar            **uris;
  GtkWidget         *window;
  XfceRc            *rc;
  gchar            **roles;
  guint              n;
  gint               active_tab;

  /* try to open the session file */
  rc = xfce_rc_simple_open (session_client->path, TRUE);
  if (G_UNLIKELY (rc == NULL))
    return;

  /* grab a reference on the application */
  application = fmb_application_get ();

  /* determine all window roles */
  roles = xfce_rc_get_groups (rc);
  for (n = 0; roles[n] != NULL; ++n)
    {
      /* skip the null group */
      if (strcmp (roles[n], "[NULL]") == 0)
        continue;

      /* enter the group */
      xfce_rc_set_group (rc, roles[n]);

      /* determine the URI for the new window */
      uris = xfce_rc_read_list_entry (rc, "URI", ";");
      if (G_UNLIKELY (uris == NULL))
        continue;

      /* active tab */
      active_tab = xfce_rc_read_int_entry (rc, "PAGE", -1);

      /* open the new window */
      window = g_object_new (FMB_TYPE_WINDOW, "role", roles[n], NULL);
      fmb_application_take_window (application, GTK_WINDOW (window));
      gtk_widget_show (window);

      /* open tabs */
      if (!fmb_window_set_directories (FMB_WINDOW (window), uris, active_tab))
        {
          /* no tabs were opened */
          gtk_widget_destroy (window);
        }

      /* cleanup */
      g_strfreev (uris);
    }

  /* cleanup */
  g_object_unref (G_OBJECT (application));
  xfce_rc_close (rc);
  g_strfreev (roles);
}



static void
fmb_session_client_die (SmcConn              connection,
                           FmbSessionClient *session_client)
{
  _fmb_return_if_fail (FMB_IS_SESSION_CLIENT (session_client));
  _fmb_return_if_fail (session_client->connection == connection);

  /* terminate the application */
  gtk_main_quit ();
}



static void
fmb_session_client_save_complete (SmcConn              connection,
                                     FmbSessionClient *session_client)
{
  _fmb_return_if_fail (FMB_IS_SESSION_CLIENT (session_client));
  _fmb_return_if_fail (session_client->connection == connection);
}



static void
fmb_session_client_save_yourself (SmcConn              connection,
                                     FmbSessionClient *session_client,
                                     gint                 save_type,
                                     Bool                 shutdown,
                                     gint                 interact_style,
                                     Bool                 fast)
{
  FmbApplication *application;
  const gchar       *role;
  gchar            **uris;
  GList             *windows;
  GList             *lp;
  guint              n;
  FILE              *fp;
  gint               active_page;

  _fmb_return_if_fail (FMB_IS_SESSION_CLIENT (session_client));
  _fmb_return_if_fail (session_client->connection == connection);

  /* check if we should save our current state */
  if (save_type == SmSaveLocal || save_type == SmSaveBoth)
    {
      /* save the active windows */
      application = fmb_application_get ();
      windows = fmb_application_get_windows (application);

      if (windows != NULL)
        {
          /* try to open the session file for writing */
          fp = fopen (session_client->path, "w");
          if (G_LIKELY (fp != NULL))
            {
              for (lp = windows; lp != NULL; lp = lp->next)
                {
                  /* determine the role for the window */
                  role = gtk_window_get_role (lp->data);
                  if (G_UNLIKELY (role == NULL))
                    continue;

                  /* determine the directories for the window */
                  uris = fmb_window_get_directories (lp->data, &active_page);
                  if (G_UNLIKELY (uris == NULL))
                    continue;

                  /* save the window */
                  fprintf (fp, "[%s]\n", role);
                  fprintf (fp, "PAGE=%d\n", active_page);
                  fprintf (fp, "URI=");
                  for (n = 0; uris[n] != NULL; n++)
                    {
                      fprintf (fp, "%s", uris[n]);
                      if (G_LIKELY (uris[n + 1] != NULL))
                        fprintf (fp, ";");
                    }
                  fprintf (fp, "\n\n");

                  /* cleanup */
                  g_strfreev (uris);
                }

              /* cleanup */
              fclose (fp);
            }

          g_list_free (windows);
        }
      else
        {
          /* no windows, remove the file */
          g_unlink (session_client->path);
        }

      g_object_unref (G_OBJECT (application));
    }

  /* tell the session manager that we're done */
  SmcSaveYourselfDone (connection, True);
}



static void
fmb_session_client_shutdown_cancelled (SmcConn              connection,
                                          FmbSessionClient *session_client)
{
  _fmb_return_if_fail (FMB_IS_SESSION_CLIENT (session_client));
  _fmb_return_if_fail (session_client->connection == connection);
}
#endif /* !HAVE_LIBSM */



/**
 * fmb_session_client_new:
 * @session_id : the previous session id, or %NULL.
 *
 * Allocates a new #FmbSessionClient with the specified
 * @session_id. If @session_id is %NULL the session manager
 * will assign a new session id, otherwise the session for
 * the specified id will be restored.
 *
 * Return value: the newly allocated #FmbSessionClient.
 **/
FmbSessionClient*
fmb_session_client_new (const gchar *session_id)
{
  FmbSessionClient *session_client;

  /* allocate a new session client */
  session_client = g_object_new (FMB_TYPE_SESSION_CLIENT, NULL);

#ifdef HAVE_LIBSM
  /* try to connect to the session manager and restore the previous session */
  if (fmb_session_client_connect (session_client, session_id) && session_id != NULL)
    fmb_session_client_restore (session_client);
#endif /* !HAVE_LIBSM */

  return session_client;
}

