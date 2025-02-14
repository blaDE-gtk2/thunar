/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2009-2011 Jannis Pohlmann <jannis@xfce.org>
 * Copyright (c) 2012      Nick Schermer <nick@xfce.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the Free
 * Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_DBUS
#include <dbus/dbus.h>
#include <dbus/dbus-glib.h>

#include <fmb/fmb-thumbnailer-proxy.h>
#endif

#include <fmb/fmb-marshal.h>
#include <fmb/fmb-private.h>
#include <fmb/fmb-thumbnailer.h>



/**
 * WARNING: The source code in this file may do harm to animals. Dead kittens
 * are to be expected.
 *
 *
 * FmbThumbnailer is a class for requesting thumbnails from org.blade.tumbler.*
 * D-Bus services. This header contains an in-depth description of its design to make
 * it easier to understand why things are the way they are.
 *
 * Please note that all D-Bus calls are performed asynchronously.
 *
 *
 * When a request call is sent out, an internal request ID is created and
 * associated with the corresponding DBusGProxyCall.
 *
 * The D-Bus reply handler then checks if there was an delivery error or
 * not. If the request method was sent successfully, the handle returned by the
 * D-Bus thumbnailer is associated bidirectionally with the internal request ID via
 * the request and handle values in the job structure. In both cases, the
 * association of the internal request ID with the DBusGProxyCall is set to NULL.
 *
 *
 * Ready / Error
 * =============
 *
 * The Ready and Error signal handlers work exactly like Started except that
 * the Ready idle function sets the thumb state of the corresponding
 * FmbFile objects to _READY and the Error signal sets the state to _NONE.
 *
 *
 * Finished
 * ========
 *
 * The Finished signal handler looks up the internal request ID based on
 * the D-Bus thumbnailer handle. It then drops all corresponding information
 * from handle_request_mapping and request_handle_mapping.
 */



#ifdef HAVE_DBUS
typedef enum
{
  FMB_THUMBNAILER_IDLE_ERROR,
  FMB_THUMBNAILER_IDLE_READY,
} FmbThumbnailerIdleType;



typedef struct _FmbThumbnailerJob  FmbThumbnailerJob;
typedef struct _FmbThumbnailerIdle FmbThumbnailerIdle;
#endif

/* Signal identifiers */
enum
{
  REQUEST_FINISHED,
  LAST_SIGNAL,
};



static void                   fmb_thumbnailer_finalize               (GObject                    *object);
#ifdef HAVE_DBUS
static void                   fmb_thumbnailer_init_thumbnailer_proxy (FmbThumbnailer          *thumbnailer,
                                                                         DBusGConnection            *connection);
static gboolean               fmb_thumbnailer_file_is_supported      (FmbThumbnailer          *thumbnailer,
                                                                         FmbFile                 *file);
static void                   fmb_thumbnailer_thumbnailer_finished   (DBusGProxy                 *proxy,
                                                                         guint                       handle,
                                                                         FmbThumbnailer          *thumbnailer);
static void                   fmb_thumbnailer_thumbnailer_error      (DBusGProxy                 *proxy,
                                                                         guint                       handle,
                                                                         const gchar               **uris,
                                                                         gint                        code,
                                                                         const gchar                *message,
                                                                         FmbThumbnailer          *thumbnailer);
static void                   fmb_thumbnailer_thumbnailer_ready      (DBusGProxy                 *proxy,
                                                                         guint32                     handle,
                                                                         const gchar               **uris,
                                                                         FmbThumbnailer          *thumbnailer);
static guint                  fmb_thumbnailer_queue_async            (FmbThumbnailer          *thumbnailer,
                                                                         gchar                     **uris,
                                                                         const gchar               **mime_hints);
static void                   fmb_thumbnailer_idle                   (FmbThumbnailer          *thumbnailer,
                                                                         guint                       handle,
                                                                         FmbThumbnailerIdleType   type,
                                                                         const gchar               **uris);
static gboolean               fmb_thumbnailer_idle_func              (gpointer                    user_data);
static void                   fmb_thumbnailer_idle_free              (gpointer                    data);
#endif

#if GLIB_CHECK_VERSION (2, 32, 0)
#define _thumbnailer_lock(thumbnailer)    g_mutex_lock (&((thumbnailer)->lock))
#define _thumbnailer_unlock(thumbnailer)  g_mutex_unlock (&((thumbnailer)->lock))
#define _thumbnailer_trylock(thumbnailer) g_mutex_trylock (&((thumbnailer)->lock))
#else
#define _thumbnailer_lock(thumbnailer)    g_mutex_lock ((thumbnailer)->lock)
#define _thumbnailer_unlock(thumbnailer)  g_mutex_unlock ((thumbnailer)->lock)
#define _thumbnailer_trylock(thumbnailer) g_mutex_trylock ((thumbnailer)->lock)
#endif



struct _FmbThumbnailerClass
{
  GObjectClass __parent__;
};

struct _FmbThumbnailer
{
  GObject __parent__;

#ifdef HAVE_DBUS
  /* proxies to communicate with D-Bus services */
  DBusGProxy *thumbnailer_proxy;

  /* running jobs */
  GSList     *jobs;

#if GLIB_CHECK_VERSION (2, 32, 0)
  GMutex      lock;
#else
  GMutex     *lock;
#endif

  /* cached MIME types -> URI schemes for which thumbs can be generated */
  GHashTable *supported;

  /* last FmbThumbnailer request ID */
  guint       last_request;

  /* IDs of idle functions */
  GSList     *idles;
#endif
};

#ifdef HAVE_DBUS
struct _FmbThumbnailerJob
{
  FmbThumbnailer *thumbnailer;

  /* if this job is cancelled */
  guint              cancelled : 1;

  /* request number returned by FmbThumbnailer */
  guint              request;

  /* handle returned by the tumbler dbus service */
  guint              handle;

  /* dbus call to get the handle */
  DBusGProxyCall    *handle_call;
};

struct _FmbThumbnailerIdle
{
  FmbThumbnailerIdleType  type;
  FmbThumbnailer          *thumbnailer;
  guint                       id;
  gchar                     **uris;
};
#endif



static guint thumbnailer_signals[LAST_SIGNAL];



G_DEFINE_TYPE (FmbThumbnailer, fmb_thumbnailer, G_TYPE_OBJECT);



static void
fmb_thumbnailer_class_init (FmbThumbnailerClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_thumbnailer_finalize;

  /**
   * FmbThumbnailer:request-finished:
   * @thumbnailer : a #FmbThumbnailer.
   * @request     : id of the request that is finished.
   *
   * Emitted by @thumbnailer, when a request is finished
   * by the thumbnail generator
   **/
  thumbnailer_signals[REQUEST_FINISHED] =
    g_signal_new (I_("request-finished"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL,
                  g_cclosure_marshal_VOID__UINT,
                  G_TYPE_NONE, 1, G_TYPE_UINT);
}



static void
fmb_thumbnailer_init (FmbThumbnailer *thumbnailer)
{
#ifdef HAVE_DBUS
  DBusGConnection *connection;

#if GLIB_CHECK_VERSION (2, 32, 0)
  g_mutex_init (&thumbnailer->lock);
#else
  thumbnailer->lock = g_mutex_new ();
#endif

  /* try to connect to D-Bus */
  connection = dbus_g_bus_get (DBUS_BUS_SESSION, NULL);

  /* initialize the proxies */
  fmb_thumbnailer_init_thumbnailer_proxy (thumbnailer, connection);

  /* release the D-Bus connection if we have one */
  if (connection != NULL)
    dbus_g_connection_unref (connection);
#endif
}



static void
fmb_thumbnailer_finalize (GObject *object)
{
#ifdef HAVE_DBUS
  FmbThumbnailer     *thumbnailer = FMB_THUMBNAILER (object);
  FmbThumbnailerIdle *idle;
  FmbThumbnailerJob  *job;
  GSList                *lp;

  /* acquire the thumbnailer lock */
  _thumbnailer_lock (thumbnailer);

  if (thumbnailer->thumbnailer_proxy != NULL)
    {
      /* disconnect from the thumbnailer proxy */
      g_signal_handlers_disconnect_matched (thumbnailer->thumbnailer_proxy,
                                            G_SIGNAL_MATCH_DATA, 0, 0,
                                            NULL, NULL, thumbnailer);
    }

  /* abort all pending idle functions */
  for (lp = thumbnailer->idles; lp != NULL; lp = lp->next)
    {
      idle = lp->data;
      g_source_remove (idle->id);
    }
  g_slist_free (thumbnailer->idles);

  /* remove all jobs */
  for (lp = thumbnailer->jobs; lp != NULL; lp = lp->next)
    {
      job = lp->data;

      if (thumbnailer->thumbnailer_proxy != NULL)
        {
          if (job->handle_call != NULL)
            dbus_g_proxy_cancel_call (thumbnailer->thumbnailer_proxy, job->handle_call);

          if (job->handle != 0)
            fmb_thumbnailer_proxy_dequeue (thumbnailer->thumbnailer_proxy, job->handle, NULL);
        }

      g_slice_free (FmbThumbnailerJob, job);
    }
  g_slist_free (thumbnailer->jobs);

  /* release the thumbnailer proxy */
  if (thumbnailer->thumbnailer_proxy != NULL)
    g_object_unref (thumbnailer->thumbnailer_proxy);

  /* free the cached URI schemes and MIME types table */
  if (thumbnailer->supported != NULL)
    g_hash_table_unref (thumbnailer->supported);

  /* release the thumbnailer lock */
  _thumbnailer_unlock (thumbnailer);

/* release the mutex */
#if GLIB_CHECK_VERSION (2, 32, 0)
  g_mutex_clear (&thumbnailer->lock);
#else
  g_mutex_free (thumbnailer->lock);
#endif
#endif

  (*G_OBJECT_CLASS (fmb_thumbnailer_parent_class)->finalize) (object);
}



#ifdef HAVE_DBUS
static void
fmb_thumbnailer_init_thumbnailer_proxy (FmbThumbnailer *thumbnailer,
                                           DBusGConnection   *connection)
{
  /* we can't have a proxy without a D-Bus connection */
  if (connection == NULL)
    {
      thumbnailer->thumbnailer_proxy = NULL;
      return;
    }

  /* create the thumbnailer proxy */
  thumbnailer->thumbnailer_proxy =
    dbus_g_proxy_new_for_name (connection,
                               "org.freedesktop.thumbnails.Thumbnailer1",
                               "/org/freedesktop/thumbnails/Thumbnailer1",
                               "org.freedesktop.thumbnails.Thumbnailer1");

  /* TODO this should actually be VOID:UINT,BOXED,INT,STRING */
  dbus_g_object_register_marshaller (_fmb_marshal_VOID__UINT_BOXED_UINT_STRING,
                                     G_TYPE_NONE,
                                     G_TYPE_UINT,
                                     G_TYPE_STRV,
                                     G_TYPE_UINT,
                                     G_TYPE_STRING,
                                     G_TYPE_INVALID);

  dbus_g_object_register_marshaller ((GClosureMarshal) _fmb_marshal_VOID__UINT_BOXED,
                                     G_TYPE_NONE,
                                     G_TYPE_UINT,
                                     G_TYPE_STRV,
                                     G_TYPE_INVALID);

  dbus_g_proxy_add_signal (thumbnailer->thumbnailer_proxy, "Error",
                           G_TYPE_UINT, G_TYPE_STRV, G_TYPE_UINT, G_TYPE_STRING,
                           G_TYPE_INVALID);
  dbus_g_proxy_add_signal (thumbnailer->thumbnailer_proxy, "Finished",
                           G_TYPE_UINT, G_TYPE_INVALID);
  dbus_g_proxy_add_signal (thumbnailer->thumbnailer_proxy, "Ready",
                           G_TYPE_UINT, G_TYPE_STRV, G_TYPE_INVALID);
  dbus_g_proxy_add_signal (thumbnailer->thumbnailer_proxy, "Started",
                           G_TYPE_UINT, G_TYPE_INVALID);

  dbus_g_proxy_connect_signal (thumbnailer->thumbnailer_proxy, "Error",
                               G_CALLBACK (fmb_thumbnailer_thumbnailer_error),
                               thumbnailer, NULL);
  dbus_g_proxy_connect_signal (thumbnailer->thumbnailer_proxy, "Finished",
                               G_CALLBACK (fmb_thumbnailer_thumbnailer_finished),
                               thumbnailer, NULL);
  dbus_g_proxy_connect_signal (thumbnailer->thumbnailer_proxy, "Ready",
                               G_CALLBACK (fmb_thumbnailer_thumbnailer_ready),
                               thumbnailer, NULL);
}



static gint
fmb_thumbnailer_file_schemes_compare (gconstpointer a,
                                         gconstpointer b)
{
  const gchar *scheme_a = *(gconstpointer *) a;
  const gchar *scheme_b = *(gconstpointer *) b;

  /* sort file before other schemes */
  if (strcmp (scheme_a, "file") == 0)
    return -1;
  if (strcmp (scheme_b, "file") == 0)
    return 1;

  /* sort trash before other schemes */
  if (strcmp (scheme_a, "trash") == 0)
    return -1;
  if (strcmp (scheme_b, "trash") == 0)
    return 1;

  /* other order is just fine */
  return 0;
}



static void
fmb_thumbnailer_file_sort_schemes (gpointer mime_type,
                                      gpointer schemes,
                                      gpointer user_data)
{
  g_ptr_array_sort (schemes, fmb_thumbnailer_file_schemes_compare);
}



static void
fmb_thumbnailer_get_supported_types (FmbThumbnailer *thumbnailer)
{
  guint       n;
  gchar     **schemes = NULL;
  gchar     **types = NULL;
  GPtrArray  *schemes_array;

  _fmb_return_if_fail (FMB_IS_THUMBNAILER (thumbnailer));
  _fmb_return_if_fail (DBUS_IS_G_PROXY (thumbnailer->thumbnailer_proxy));
  _fmb_return_if_fail (!_thumbnailer_trylock (thumbnailer));

  /* leave if there already is a hash table */
  if (thumbnailer->supported != NULL)
    return;

  /* prepare table */
  thumbnailer->supported = g_hash_table_new_full (g_str_hash, g_str_equal,
                                                  g_free,
                                                  (GDestroyNotify) g_ptr_array_unref);

  /* request the supported types from the thumbnailer D-Bus service. We only do
   * this once, so using a non-async call should be ok */
  fmb_thumbnailer_proxy_get_supported (thumbnailer->thumbnailer_proxy,
                                          &schemes, &types,
                                          NULL);

  if (G_LIKELY (schemes != NULL && types != NULL))
    {
      /* combine content types and uri schemes */
      for (n = 0; types[n] != NULL; ++n)
        {
          schemes_array = g_hash_table_lookup (thumbnailer->supported, types[n]);
          if (G_UNLIKELY (schemes_array == NULL))
            {
              /* create an array for the uri schemes this content type supports */
              schemes_array = g_ptr_array_new_with_free_func (g_free);
              g_ptr_array_add (schemes_array, schemes[n]);
              g_hash_table_insert (thumbnailer->supported, types[n], schemes_array);
            }
          else
            {
              /* add the uri scheme to the array of the content type */
              g_ptr_array_add (schemes_array, schemes[n]);

              /* cleanup */
              g_free (types[n]);
            }
        }

      /* remove arrays, we stole the values */
      g_free (types);
      g_free (schemes);

      /* sort array to optimize for local files */
      g_hash_table_foreach (thumbnailer->supported, fmb_thumbnailer_file_sort_schemes, NULL);
    }
}



static gboolean
fmb_thumbnailer_file_is_supported (FmbThumbnailer *thumbnailer,
                                      FmbFile        *file)
{
  const gchar *content_type;
  gboolean     supported = FALSE;
  guint        n;
  GPtrArray   *schemes_array;
  const gchar *scheme;

  _fmb_return_val_if_fail (FMB_IS_THUMBNAILER (thumbnailer), FALSE);
  _fmb_return_val_if_fail (FMB_IS_FILE (file), FALSE);
  _fmb_return_val_if_fail (DBUS_IS_G_PROXY (thumbnailer->thumbnailer_proxy), FALSE);
  _fmb_return_val_if_fail (thumbnailer->supported != NULL, FALSE);
  _fmb_return_val_if_fail (!_thumbnailer_trylock (thumbnailer), FALSE);

  /* determine the content type of the passed file */
  content_type = fmb_file_get_content_type (file);

  /* abort if the content type is unknown */
  if (content_type == NULL)
    return FALSE;

  /* lazy lookup the content type, no difficult parent type matching here */
  schemes_array = g_hash_table_lookup (thumbnailer->supported, content_type);
  if (schemes_array != NULL)
    {
      /* go through all the URI schemes this type supports */
      for (n = 0; !supported && n < schemes_array->len; ++n)
        {
          /* check if the file has the current URI scheme */
          scheme = g_ptr_array_index (schemes_array, n);
          if (fmb_file_has_uri_scheme (file, scheme))
            supported = TRUE;
        }
    }

  return supported;
}



static void
fmb_thumbnailer_thumbnailer_error (DBusGProxy        *proxy,
                                      guint              handle,
                                      const gchar      **uris,
                                      gint               code,
                                      const gchar       *message,
                                      FmbThumbnailer *thumbnailer)
{
  _fmb_return_if_fail (DBUS_IS_G_PROXY (proxy));
  _fmb_return_if_fail (FMB_IS_THUMBNAILER (thumbnailer));

  /* check if we have any ready URIs */
  fmb_thumbnailer_idle (thumbnailer,
                           handle,
                           FMB_THUMBNAILER_IDLE_ERROR,
                           uris);
}



static void
fmb_thumbnailer_thumbnailer_ready (DBusGProxy        *proxy,
                                      guint32            handle,
                                      const gchar      **uris,
                                      FmbThumbnailer *thumbnailer)
{
  _fmb_return_if_fail (DBUS_IS_G_PROXY (proxy));
  _fmb_return_if_fail (FMB_IS_THUMBNAILER (thumbnailer));

  fmb_thumbnailer_idle (thumbnailer,
                           handle,
                           FMB_THUMBNAILER_IDLE_READY,
                           uris);
}



static void
fmb_thumbnailer_thumbnailer_finished (DBusGProxy        *proxy,
                                         guint              handle,
                                         FmbThumbnailer *thumbnailer)
{
  FmbThumbnailerJob *job;
  GSList               *lp;

  _fmb_return_if_fail (DBUS_IS_G_PROXY (proxy));
  _fmb_return_if_fail (FMB_IS_THUMBNAILER (thumbnailer));

  for (lp = thumbnailer->jobs; lp != NULL; lp = lp->next)
    {
      job = lp->data;

      if (job->handle == handle)
        {
          /* this job is finished, forget about the handle */
          job->handle = 0;

          /* tell everybody we're done here */
          g_signal_emit (G_OBJECT (thumbnailer), thumbnailer_signals[REQUEST_FINISHED], 0, job->request);

          /* remove job from the list */
          _thumbnailer_lock (thumbnailer);
          thumbnailer->jobs = g_slist_delete_link (thumbnailer->jobs, lp);
          _thumbnailer_unlock (thumbnailer);

          g_slice_free (FmbThumbnailerJob, job);
          break;
        }
    }
}



static void
fmb_thumbnailer_queue_async_reply (DBusGProxy *proxy,
                                      guint       handle,
                                      GError     *error,
                                      gpointer    user_data)
{
  _fmb_return_if_fail (DBUS_IS_G_PROXY (proxy));
  _fmb_return_if_fail (user_data != NULL);

  FmbThumbnailerJob *job = user_data;
  FmbThumbnailer    *thumbnailer = FMB_THUMBNAILER (job->thumbnailer);

  _fmb_return_if_fail (FMB_IS_THUMBNAILER (thumbnailer));

  _thumbnailer_lock (thumbnailer);

  /* the queue call is finished, we can forget about its proxy call */
  job->handle_call = NULL;

  if (job->cancelled)
    {
      /* job is cancelled while there was no handle jet, so dequeue it now */
      fmb_thumbnailer_proxy_dequeue (proxy, handle, NULL);

      /* cleanup */
      thumbnailer->jobs = g_slist_remove (thumbnailer->jobs, job);
      g_slice_free (FmbThumbnailerJob, job);
    }
  else if (error == NULL)
    {
      /* store the handle returned by tumbler */
      job->handle = handle;
    }

  _thumbnailer_unlock (thumbnailer);
}



static guint
fmb_thumbnailer_queue_async (FmbThumbnailer *thumbnailer,
                                gchar            **uris,
                                const gchar      **mime_hints)
{
  FmbThumbnailerJob *job;
  guint                 request_no;

  _fmb_return_val_if_fail (FMB_IS_THUMBNAILER (thumbnailer), 0);
  _fmb_return_val_if_fail (uris != NULL, 0);
  _fmb_return_val_if_fail (mime_hints != NULL, 0);
  _fmb_return_val_if_fail (DBUS_IS_G_PROXY (thumbnailer->thumbnailer_proxy), 0);
  _fmb_return_val_if_fail (!_thumbnailer_trylock (thumbnailer), 0);

  /* compute the next request ID, making sure it's never 0 */
  request_no = thumbnailer->last_request + 1;
  request_no = MAX (request_no, 1);

  /* remember the ID for the next request */
  thumbnailer->last_request = request_no;

  /* allocate a new struct to follow this job until finished */
  job = g_slice_new0 (FmbThumbnailerJob);
  job->thumbnailer = thumbnailer;
  job->request = request_no;

  /* store the job */
  thumbnailer->jobs = g_slist_prepend (thumbnailer->jobs, job);

  /* queue thumbnails for the given URIs asynchronously */
  job->handle_call = fmb_thumbnailer_proxy_queue_async (thumbnailer->thumbnailer_proxy,
                                                           (const gchar **)uris, mime_hints,
                                                           "normal", "foreground", 0,
                                                           fmb_thumbnailer_queue_async_reply,
                                                           job);

  /* return the request ID used for this request */
  return request_no;
}



static void
fmb_thumbnailer_idle (FmbThumbnailer          *thumbnailer,
                         guint                       handle,
                         FmbThumbnailerIdleType   type,
                         const gchar               **uris)
{
  GSList                *lp;
  FmbThumbnailerIdle *idle;
  FmbThumbnailerJob  *job;

  /* leave if there are no uris */
  if (G_UNLIKELY (uris == NULL))
    return;

  /* look for the job so we don't emit unknown handles, the reason
   * we do this is when you have multiple windows opened, you don't
   * want each window (because they all have a connection to the
   * same proxy) emit the file change, only the window that requested
   * the data */
  for (lp = thumbnailer->jobs; lp != NULL; lp = lp->next)
    {
      job = lp->data;

      if (job->handle == handle)
        {
          /* allocate a new idle struct */
          idle = g_slice_new0 (FmbThumbnailerIdle);
          idle->type = type;
          idle->thumbnailer = thumbnailer;

          /* copy the URI array because we need it in the idle function */
          idle->uris = g_strdupv ((gchar **)uris);

          /* remember the idle struct because we might have to remove it in finalize() */
          _thumbnailer_lock (thumbnailer);
          thumbnailer->idles = g_slist_prepend (thumbnailer->idles, idle);
          _thumbnailer_unlock (thumbnailer);

          /* call the idle function when we have the time */
          idle->id = g_idle_add_full (G_PRIORITY_LOW,
                                      fmb_thumbnailer_idle_func, idle,
                                      fmb_thumbnailer_idle_free);

          break;
        }
    }
}



static gboolean
fmb_thumbnailer_idle_func (gpointer user_data)
{
  FmbThumbnailerIdle *idle = user_data;
  FmbFile            *file;
  GFile                 *gfile;
  guint                  n;

  _fmb_return_val_if_fail (idle != NULL, FALSE);
  _fmb_return_val_if_fail (FMB_IS_THUMBNAILER (idle->thumbnailer), FALSE);

  /* iterate over all failed URIs */
  for (n = 0; idle->uris != NULL && idle->uris[n] != NULL; ++n)
    {
      /* look up the corresponding FmbFile from the cache */
      gfile = g_file_new_for_uri (idle->uris[n]);
      file = fmb_file_cache_lookup (gfile);
      g_object_unref (gfile);

      /* check if we have a file for this URI in the cache */
      if (file != NULL)
        {
          if (idle->type == FMB_THUMBNAILER_IDLE_ERROR)
            {
              /* set thumbnail state to none unless the thumbnail has already been created.
               * This is to prevent race conditions with the other idle functions */
              if (fmb_file_get_thumb_state (file) != FMB_FILE_THUMB_STATE_READY)
                fmb_file_set_thumb_state (file, FMB_FILE_THUMB_STATE_NONE);
            }
          else if (idle->type == FMB_THUMBNAILER_IDLE_READY)
            {
              /* set thumbnail state to ready - we now have a thumbnail */
              fmb_file_set_thumb_state (file, FMB_FILE_THUMB_STATE_READY);
            }
          else
            {
              _fmb_assert_not_reached ();
            }
        }

      g_object_unref (file);
    }

  /* remove the idle struct */
  _thumbnailer_lock (idle->thumbnailer);
  idle->thumbnailer->idles = g_slist_remove (idle->thumbnailer->idles, idle);
  _thumbnailer_unlock (idle->thumbnailer);

  /* remove the idle source, which also destroys the idle struct */
  return FALSE;
}



static void
fmb_thumbnailer_idle_free (gpointer data)
{
  FmbThumbnailerIdle *idle = data;

  _fmb_return_if_fail (idle != NULL);

  /* free the URI array if necessary */
  if (idle->type == FMB_THUMBNAILER_IDLE_READY
      || idle->type == FMB_THUMBNAILER_IDLE_ERROR)
    {
      g_strfreev (idle->uris);
    }

  /* free the struct */
  g_slice_free (FmbThumbnailerIdle, idle);
}
#endif /* HAVE_DBUS */



/**
 * fmb_thumbnailer_get:
 *
 * Return a shared new #FmbThumbnailer object, which can be used to
 * generate and store thumbnails for files.
 *
 * The caller is responsible to free the returned
 * object using g_object_unref() when no longer needed.
 *
 * Return value: a #FmbThumbnailer.
 **/
FmbThumbnailer*
fmb_thumbnailer_get (void)
{
  static FmbThumbnailer *thumbnailer = NULL;

  if (G_UNLIKELY (thumbnailer == NULL))
    {
      thumbnailer = g_object_new (FMB_TYPE_THUMBNAILER, NULL);
      g_object_add_weak_pointer (G_OBJECT (thumbnailer), (gpointer) &thumbnailer);
    }
  else
    {
      g_object_ref (G_OBJECT (thumbnailer));
    }

  return thumbnailer;
}



gboolean
fmb_thumbnailer_queue_file (FmbThumbnailer *thumbnailer,
                               FmbFile        *file,
                               guint             *request)
{
  GList files;

  _fmb_return_val_if_fail (FMB_IS_THUMBNAILER (thumbnailer), FALSE);
  _fmb_return_val_if_fail (FMB_IS_FILE (file), FALSE);

  /* fake a file list */
  files.data = file;
  files.next = NULL;
  files.prev = NULL;

  /* queue a thumbnail request for the file */
  return fmb_thumbnailer_queue_files (thumbnailer, FALSE, &files, request);
}



gboolean
fmb_thumbnailer_queue_files (FmbThumbnailer *thumbnailer,
                                gboolean           lazy_checks,
                                GList             *files,
                                guint             *request)
{
  gboolean               success = FALSE;
#ifdef HAVE_DBUS
  const gchar          **mime_hints;
  gchar                **uris;
  GList                 *lp;
  GList                 *supported_files = NULL;
  guint                  n;
  guint                  n_items = 0;
  FmbFileThumbState   thumb_state;
  const gchar           *thumbnail_path;
#endif

  _fmb_return_val_if_fail (FMB_IS_THUMBNAILER (thumbnailer), FALSE);
  _fmb_return_val_if_fail (files != NULL, FALSE);

#ifdef HAVE_DBUS
  /* acquire the thumbnailer lock */
  _thumbnailer_lock (thumbnailer);

  if (thumbnailer->thumbnailer_proxy == NULL)
    {
      /* release the lock and abort */
      _thumbnailer_unlock (thumbnailer);
      return FALSE;
    }

  /* make sure there is a hash table with supported files */
  fmb_thumbnailer_get_supported_types (thumbnailer);

  /* collect all supported files from the list that are neither in the
   * about to be queued (wait queue), nor already queued, nor already
   * processed (and awaiting to be refreshed) */
  for (lp = files; lp != NULL; lp = lp->next)
    {
      /* the icon factory only loads icons for regular files */
      if (!fmb_file_is_regular (lp->data))
        {
          fmb_file_set_thumb_state (lp->data, FMB_FILE_THUMB_STATE_NONE);
          continue;
        }

      /* get the current thumb state */
      thumb_state = fmb_file_get_thumb_state (lp->data);

      if (lazy_checks)
        {
          /* in lazy mode, don't both for files that have already
           * been loaded or are not supported */
          if (thumb_state == FMB_FILE_THUMB_STATE_NONE
              || thumb_state == FMB_FILE_THUMB_STATE_READY)
            continue;
        }

      /* check if the file is supported, assume it is when the state was ready previously */
      if (thumb_state == FMB_FILE_THUMB_STATE_READY
          || fmb_thumbnailer_file_is_supported (thumbnailer, lp->data))
        {
          supported_files = g_list_prepend (supported_files, lp->data);
          n_items++;
        }
      else
        {
          /* still a regular file, but the type is now known to tumbler but
           * maybe the application created a thumbnail */
          thumbnail_path = fmb_file_get_thumbnail_path (lp->data);

          /* test if a thumbnail can be found */
          if (thumbnail_path != NULL && g_file_test (thumbnail_path, G_FILE_TEST_EXISTS))
            fmb_file_set_thumb_state (lp->data, FMB_FILE_THUMB_STATE_READY);
          else
            fmb_file_set_thumb_state (lp->data, FMB_FILE_THUMB_STATE_NONE);
        }
    }

  /* release the thumbnailer lock */
  _thumbnailer_unlock (thumbnailer);

  /* check if we have any supported files */
  if (n_items > 0)
    {
      /* allocate arrays for URIs and mime hints */
      uris = g_new0 (gchar *, n_items + 1);
      mime_hints = g_new0 (const gchar *, n_items + 1);

      /* fill URI and MIME hint arrays with items from the wait queue */
      for (lp = supported_files, n = 0; lp != NULL; lp = lp->next, ++n)
        {
          /* set the thumbnail state to loading */
          fmb_file_set_thumb_state (lp->data, FMB_FILE_THUMB_STATE_LOADING);

          /* save URI and MIME hint in the arrays */
          uris[n] = fmb_file_dup_uri (lp->data);
          mime_hints[n] = fmb_file_get_content_type (lp->data);
        }

      /* NULL-terminate both arrays */
      uris[n] = NULL;
      mime_hints[n] = NULL;

      _thumbnailer_lock (thumbnailer);

      /* queue a thumbnail request for the URIs from the wait queue */
      if (request != NULL)
        *request = fmb_thumbnailer_queue_async (thumbnailer, uris, mime_hints);
      else
        fmb_thumbnailer_queue_async (thumbnailer, uris, mime_hints);

      _thumbnailer_unlock (thumbnailer);

      /* free mime hints array */
      g_free (mime_hints);
      g_strfreev (uris);

      /* free the list of supported files */
      g_list_free (supported_files);

      /* we assume success if we've come so far */
      success = TRUE;
    }
#endif /* HAVE_DBUS */

  return success;
}



void
fmb_thumbnailer_dequeue (FmbThumbnailer *thumbnailer,
                            guint              request)
{
#ifdef HAVE_DBUS
  FmbThumbnailerJob *job;
  GSList               *lp;
#endif

  _fmb_return_if_fail (FMB_IS_THUMBNAILER (thumbnailer));

#ifdef HAVE_DBUS
  /* acquire the thumbnailer lock */
  _thumbnailer_lock (thumbnailer);

  for (lp = thumbnailer->jobs; lp != NULL; lp = lp->next)
    {
      job = lp->data;

      /* find the request in the list */
      if (job->request == request)
        {
          /* this job is cancelled */
          job->cancelled = TRUE;

          if (job->handle != 0)
            {
              /* dequeue the tumbler request */
              if (thumbnailer->thumbnailer_proxy != NULL)
                fmb_thumbnailer_proxy_dequeue (thumbnailer->thumbnailer_proxy, job->handle, NULL);

              /* remove job */
              thumbnailer->jobs = g_slist_delete_link (thumbnailer->jobs, lp);
              g_slice_free (FmbThumbnailerJob, job);
            }

          break;
        }
    }

  /* release the thumbnailer lock */
  _thumbnailer_unlock (thumbnailer);
#endif
}
