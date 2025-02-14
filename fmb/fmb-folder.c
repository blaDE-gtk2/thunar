/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>
 * Copyright (c) 2009 Jannis Pohlmann <jannis@xfce.org>
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

#include <fmb/fmb-file-monitor.h>
#include <fmb/fmb-folder.h>
#include <fmb/fmb-gobject-extensions.h>
#include <fmb/fmb-io-jobs.h>
#include <fmb/fmb-job.h>
#include <fmb/fmb-private.h>

#define DEBUG_FILE_CHANGES FALSE



/* property identifiers */
enum
{
  PROP_0,
  PROP_CORRESPONDING_FILE,
  PROP_LOADING,
};

/* signal identifiers */
enum
{
  DESTROY,
  ERROR,
  FILES_ADDED,
  FILES_REMOVED,
  LAST_SIGNAL,
};



static void     fmb_folder_dispose                     (GObject                *object);
static void     fmb_folder_finalize                    (GObject                *object);
static void     fmb_folder_get_property                (GObject                *object,
                                                           guint                   prop_id,
                                                           GValue                 *value,
                                                           GParamSpec             *pspec);
static void     fmb_folder_set_property                (GObject                *object,
                                                           guint                   prop_uid,
                                                           const GValue           *value,
                                                           GParamSpec             *pspec);
static void     fmb_folder_real_destroy                (FmbFolder           *folder);
static void     fmb_folder_error                       (BlxoJob                 *job,
                                                           GError                 *error,
                                                           FmbFolder           *folder);
static gboolean fmb_folder_files_ready                 (FmbJob              *job,
                                                           GList                  *files,
                                                           FmbFolder           *folder);
static void     fmb_folder_finished                    (BlxoJob                 *job,
                                                           FmbFolder           *folder);
static void     fmb_folder_file_changed                (FmbFileMonitor      *file_monitor,
                                                           FmbFile             *file,
                                                           FmbFolder           *folder);
static void     fmb_folder_file_destroyed              (FmbFileMonitor      *file_monitor,
                                                           FmbFile             *file,
                                                           FmbFolder           *folder);
static void     fmb_folder_monitor                     (GFileMonitor           *monitor,
                                                           GFile                  *file,
                                                           GFile                  *other_file,
                                                           GFileMonitorEvent       event_type,
                                                           gpointer                user_data);



struct _FmbFolderClass
{
  GObjectClass __parent__;

  /* signals */
  void (*destroy)       (FmbFolder *folder);
  void (*error)         (FmbFolder *folder,
                         const GError *error);
  void (*files_added)   (FmbFolder *folder,
                         GList        *files);
  void (*files_removed) (FmbFolder *folder,
                         GList        *files);
};

struct _FmbFolder
{
  GObject __parent__;

  FmbJob         *job;

  FmbFile        *corresponding_file;
  GList             *new_files;
  GList             *files;
  gboolean           reload_info;

  GList             *content_type_ptr;
  guint              content_type_idle_id;

  guint              in_destruction : 1;

  FmbFileMonitor *file_monitor;

  GFileMonitor      *monitor;
};



static guint  folder_signals[LAST_SIGNAL];
static GQuark fmb_folder_quark;



G_DEFINE_TYPE (FmbFolder, fmb_folder, G_TYPE_OBJECT)



static void
fmb_folder_class_init (FmbFolderClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = fmb_folder_dispose;
  gobject_class->finalize = fmb_folder_finalize;
  gobject_class->get_property = fmb_folder_get_property;
  gobject_class->set_property = fmb_folder_set_property;

  klass->destroy = fmb_folder_real_destroy;

  /**
   * FmbFolder::corresponding-file:
   *
   * The #FmbFile referring to the #FmbFolder.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_CORRESPONDING_FILE,
                                   g_param_spec_object ("corresponding-file",
                                                        "corresponding-file",
                                                        "corresponding-file",
                                                        FMB_TYPE_FILE,
                                                        G_PARAM_READABLE
                                                        | G_PARAM_WRITABLE
                                                        | G_PARAM_CONSTRUCT_ONLY));

  /**
   * FmbFolder::loading:
   *
   * Tells whether the contents of the #FmbFolder are
   * currently being loaded.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_LOADING,
                                   g_param_spec_boolean ("loading",
                                                         "loading",
                                                         "loading",
                                                         FALSE,
                                                         BLXO_PARAM_READABLE));
  /**
   * FmbFolder::destroy:
   * @folder : a #FmbFolder.
   *
   * Emitted when the #FmbFolder is destroyed.
   **/
  folder_signals[DESTROY] =
    g_signal_new (I_("destroy"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_CLEANUP | G_SIGNAL_NO_RECURSE | G_SIGNAL_NO_HOOKS,
                  G_STRUCT_OFFSET (FmbFolderClass, destroy),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);

  /**
   * FmbFolder::error:
   * @folder : a #FmbFolder.
   * @error  : the #GError describing the problem.
   *
   * Emitted when the #FmbFolder fails to completly
   * load the directory content because of an error.
   **/
  folder_signals[ERROR] =
    g_signal_new (I_("error"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (FmbFolderClass, error),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  /**
   * FmbFolder::files-added:
   *
   * Emitted by the #FmbFolder whenever new files have
   * been added to a particular folder.
   **/
  folder_signals[FILES_ADDED] =
    g_signal_new (I_("files-added"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (FmbFolderClass, files_added),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);

  /**
   * FmbFolder::files-removed:
   *
   * Emitted by the #FmbFolder whenever a bunch of files
   * is removed from the folder, which means they are not
   * necessarily deleted from disk. This can be used to implement
   * the reload of folders, which take longer to load.
   **/
  folder_signals[FILES_REMOVED] =
    g_signal_new (I_("files-removed"),
                  G_TYPE_FROM_CLASS (gobject_class),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (FmbFolderClass, files_removed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__POINTER,
                  G_TYPE_NONE, 1, G_TYPE_POINTER);
}



static void
fmb_folder_init (FmbFolder *folder)
{
  /* connect to the FmbFileMonitor instance */
  folder->file_monitor = fmb_file_monitor_get_default ();
  g_signal_connect (G_OBJECT (folder->file_monitor), "file-changed", G_CALLBACK (fmb_folder_file_changed), folder);
  g_signal_connect (G_OBJECT (folder->file_monitor), "file-destroyed", G_CALLBACK (fmb_folder_file_destroyed), folder);

  folder->monitor = NULL;
  folder->reload_info = FALSE;
}



static void
fmb_folder_dispose (GObject *object)
{
  FmbFolder *folder = FMB_FOLDER (object);

  if (!folder->in_destruction)
    {
      folder->in_destruction = TRUE;
      g_signal_emit (G_OBJECT (folder), folder_signals[DESTROY], 0);
      folder->in_destruction = FALSE;
    }

  (*G_OBJECT_CLASS (fmb_folder_parent_class)->dispose) (object);
}



static void
fmb_folder_finalize (GObject *object)
{
  FmbFolder *folder = FMB_FOLDER (object);

  if (folder->corresponding_file)
    fmb_file_unwatch (folder->corresponding_file);

  /* disconnect from the FmbFileMonitor instance */
  g_signal_handlers_disconnect_matched (folder->file_monitor, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, folder);
  g_object_unref (folder->file_monitor);

  /* disconnect from the file alteration monitor */
  if (G_LIKELY (folder->monitor != NULL))
    {
      g_signal_handlers_disconnect_matched (folder->monitor, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, folder);
      g_file_monitor_cancel (folder->monitor);
      g_object_unref (folder->monitor);
    }

  /* cancel the pending job (if any) */
  if (G_UNLIKELY (folder->job != NULL))
    {
      g_signal_handlers_disconnect_matched (folder->job, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, folder);
      g_object_unref (folder->job);
      folder->job = NULL;
    }

  /* disconnect from the corresponding file */
  if (G_LIKELY (folder->corresponding_file != NULL))
    {
      /* drop the reference */
      g_object_set_qdata (G_OBJECT (folder->corresponding_file), fmb_folder_quark, NULL);
      g_object_unref (G_OBJECT (folder->corresponding_file));
    }

  /* stop metadata collector */
  if (folder->content_type_idle_id != 0)
    g_source_remove (folder->content_type_idle_id);

  /* release references to the new files */
  fmb_g_file_list_free (folder->new_files);

  /* release references to the current files */
  fmb_g_file_list_free (folder->files);

  (*G_OBJECT_CLASS (fmb_folder_parent_class)->finalize) (object);
}



static void
fmb_folder_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  FmbFolder *folder = FMB_FOLDER (object);

  switch (prop_id)
    {
    case PROP_CORRESPONDING_FILE:
      g_value_set_object (value, folder->corresponding_file);
      break;

    case PROP_LOADING:
      g_value_set_boolean (value, fmb_folder_get_loading (folder));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_folder_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  FmbFolder *folder = FMB_FOLDER (object);

  switch (prop_id)
    {
    case PROP_CORRESPONDING_FILE:
      if (folder->corresponding_file)
        fmb_file_unwatch (folder->corresponding_file);
      folder->corresponding_file = g_value_dup_object (value);
      if (folder->corresponding_file)
        fmb_file_watch (folder->corresponding_file);
      break;

    case PROP_LOADING:
      _fmb_assert_not_reached ();
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_folder_real_destroy (FmbFolder *folder)
{
  g_signal_handlers_destroy (G_OBJECT (folder));
}



static void
fmb_folder_error (BlxoJob       *job,
                     GError       *error,
                     FmbFolder *folder)
{
  _fmb_return_if_fail (FMB_IS_FOLDER (folder));
  _fmb_return_if_fail (FMB_IS_JOB (job));

  /* tell the consumer about the problem */
  g_signal_emit (G_OBJECT (folder), folder_signals[ERROR], 0, error);
}



static gboolean
fmb_folder_files_ready (FmbJob    *job,
                           GList        *files,
                           FmbFolder *folder)
{
  _fmb_return_val_if_fail (FMB_IS_FOLDER (folder), FALSE);
  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (folder->monitor == NULL, FALSE);

  /* merge the list with the existing list of new files */
  folder->new_files = g_list_concat (folder->new_files, files);

  /* indicate that we took over ownership of the file list */
  return TRUE;
}



static gboolean
fmb_folder_content_type_loader_idle (gpointer data)
{
  _fmb_return_val_if_fail (FMB_IS_FOLDER (data), FALSE);

  FmbFolder *folder = FMB_FOLDER (data);
  GList        *lp;

  /* load another files content type */
  for (lp = folder->content_type_ptr; lp != NULL; lp = lp->next)
    if (fmb_file_load_content_type (lp->data))
      {
        /* if this was the last file, abort */
        if (G_UNLIKELY (lp->next == NULL))
          break;

        /* set pointer to next file for the next iteration */
        folder->content_type_ptr = lp->next;

        return TRUE;
      }

  /* all content types loaded */
  return FALSE;
}



static void
fmb_folder_content_type_loader_idle_destroyed (gpointer data)
{
  _fmb_return_if_fail (FMB_IS_FOLDER (data));

  FMB_FOLDER (data)->content_type_idle_id = 0;
}



static void
fmb_folder_content_type_loader (FmbFolder *folder)
{
  _fmb_return_if_fail (FMB_IS_FOLDER (folder));
  _fmb_return_if_fail (folder->content_type_idle_id == 0);

  /* set the pointer to the start of the list */
  folder->content_type_ptr = folder->files;

  /* schedule idle */
  folder->content_type_idle_id = g_idle_add_full (G_PRIORITY_LOW, fmb_folder_content_type_loader_idle,
                                                  folder, fmb_folder_content_type_loader_idle_destroyed);
}



static void
fmb_folder_finished (BlxoJob       *job,
                        FmbFolder *folder)
{
  FmbFile *file;
  GList      *files;
  GList      *lp;

  _fmb_return_if_fail (FMB_IS_FOLDER (folder));
  _fmb_return_if_fail (FMB_IS_JOB (job));
  _fmb_return_if_fail (FMB_IS_FILE (folder->corresponding_file));
  _fmb_return_if_fail (folder->monitor == NULL);
  _fmb_return_if_fail (folder->content_type_idle_id == 0);

  /* check if we need to merge new files with existing files */
  if (G_UNLIKELY (folder->files != NULL))
    {
      /* determine all added files (files on new_files, but not on files) */
      for (files = NULL, lp = folder->new_files; lp != NULL; lp = lp->next)
        if (g_list_find (folder->files, lp->data) == NULL)
          {
            /* put the file on the added list */
            files = g_list_prepend (files, lp->data);

            /* add to the internal files list */
            folder->files = g_list_prepend (folder->files, lp->data);
            g_object_ref (G_OBJECT (lp->data));
          }

      /* check if any files were added */
      if (G_UNLIKELY (files != NULL))
        {
          /* emit a "files-added" signal for the added files */
          g_signal_emit (G_OBJECT (folder), folder_signals[FILES_ADDED], 0, files);

          /* release the added files list */
          g_list_free (files);
        }

      /* determine all removed files (files on files, but not on new_files) */
      for (files = NULL, lp = folder->files; lp != NULL; )
        {
          /* determine the file */
          file = FMB_FILE (lp->data);

          /* determine the next list item */
          lp = lp->next;

          /* check if the file is not on new_files */
          if (g_list_find (folder->new_files, file) == NULL)
            {
              /* put the file on the removed list (owns the reference now) */
              files = g_list_prepend (files, file);

              /* remove from the internal files list */
              folder->files = g_list_remove (folder->files, file);
            }
        }

      /* check if any files were removed */
      if (G_UNLIKELY (files != NULL))
        {
          /* emit a "files-removed" signal for the removed files */
          g_signal_emit (G_OBJECT (folder), folder_signals[FILES_REMOVED], 0, files);

          /* release the removed files list */
          fmb_g_file_list_free (files);
        }

      /* drop the temporary new_files list */
      fmb_g_file_list_free (folder->new_files);
      folder->new_files = NULL;
    }
  else
    {
      /* just use the new files for the files list */
      folder->files = folder->new_files;
      folder->new_files = NULL;

      if (folder->files != NULL)
        {
          /* emit a "files-added" signal for the new files */
          g_signal_emit (G_OBJECT (folder), folder_signals[FILES_ADDED], 0, folder->files);
        }
    }

  /* schedule a reload of the file information of all files if requested */
  if (folder->reload_info)
    {
      for (lp = folder->files; lp != NULL; lp = lp->next)
        fmb_file_reload (lp->data);

      /* reload folder information too */
      fmb_file_reload (folder->corresponding_file);

      folder->reload_info = FALSE;
    }

  /* we did it, the folder is loaded */
  g_signal_handlers_disconnect_matched (folder->job, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, folder);
  g_object_unref (folder->job);
  folder->job = NULL;

  /* restart the content type idle loader */
  fmb_folder_content_type_loader (folder);

  /* add us to the file alteration monitor */
  folder->monitor = g_file_monitor_directory (fmb_file_get_file (folder->corresponding_file),
                                              G_FILE_MONITOR_SEND_MOVED, NULL, NULL);
  if (G_LIKELY (folder->monitor != NULL))
    g_signal_connect (folder->monitor, "changed", G_CALLBACK (fmb_folder_monitor), folder);

  /* tell the consumers that we have loaded the directory */
  g_object_notify (G_OBJECT (folder), "loading");
}



static void
fmb_folder_file_changed (FmbFileMonitor *file_monitor,
                            FmbFile        *file,
                            FmbFolder      *folder)
{
  _fmb_return_if_fail (FMB_IS_FILE (file));
  _fmb_return_if_fail (FMB_IS_FOLDER (folder));
  _fmb_return_if_fail (FMB_IS_FILE_MONITOR (file_monitor));

  /* check if the corresponding file changed... */
  if (G_UNLIKELY (folder->corresponding_file == file))
    {
      /* ...and if so, reload the folder */
      fmb_folder_reload (folder, FALSE);
    }
}



static void
fmb_folder_file_destroyed (FmbFileMonitor *file_monitor,
                              FmbFile        *file,
                              FmbFolder      *folder)
{
  GList     files;
  GList    *lp;
  gboolean  restart = FALSE;

  _fmb_return_if_fail (FMB_IS_FILE (file));
  _fmb_return_if_fail (FMB_IS_FOLDER (folder));
  _fmb_return_if_fail (FMB_IS_FILE_MONITOR (file_monitor));

  /* check if the corresponding file was destroyed */
  if (G_UNLIKELY (folder->corresponding_file == file))
    {
      /* the folder is useless now */
      if (!folder->in_destruction)
        g_object_run_dispose (G_OBJECT (folder));
    }
  else
    {
      /* check if we have that file */
      lp = g_list_find (folder->files, file);
      if (G_LIKELY (lp != NULL))
        {
          if (folder->content_type_idle_id != 0)
            restart = g_source_remove (folder->content_type_idle_id);

          /* remove the file from our list */
          folder->files = g_list_delete_link (folder->files, lp);

          /* tell everybody that the file is gone */
          files.data = file; files.next = files.prev = NULL;
          g_signal_emit (G_OBJECT (folder), folder_signals[FILES_REMOVED], 0, &files);

          /* drop our reference to the file */
          g_object_unref (G_OBJECT (file));

          /* continue collecting the metadata */
          if (restart)
            fmb_folder_content_type_loader (folder);
        }
    }
}



#if DEBUG_FILE_CHANGES
static void
fmb_file_infos_equal (FmbFile *file,
                         GFile      *event_file)
{
  gchar     **attrs;
  GFileInfo  *info1 = G_FILE_INFO (file->info);
  GFileInfo  *info2;
  guint       i;
  gchar      *attr1, *attr2;
  gboolean    printed = FALSE;
  gchar      *bname;

  attrs = g_file_info_list_attributes (info1, NULL);
  info2 = g_file_query_info (event_file, FMBX_FILE_INFO_NAMESPACE,
                             G_FILE_QUERY_INFO_NONE, NULL, NULL);

  if (info1 != NULL && info2 != NULL)
    {
      for (i = 0; attrs[i] != NULL; i++)
        {
          if (g_file_info_has_attribute (info2, attrs[i]))
            {
              attr1 = g_file_info_get_attribute_as_string (info1, attrs[i]);
              attr2 = g_file_info_get_attribute_as_string (info2, attrs[i]);

              if (g_strcmp0 (attr1, attr2) != 0)
                {
                  if (!printed)
                    {
                      bname = g_file_get_basename (event_file);
                      g_print ("%s\n", bname);
                      g_free (bname);

                      printed = TRUE;
                    }

                  g_print ("  %s: %s -> %s\n", attrs[i], attr1, attr2);
                }

              g_free (attr1);
              g_free (attr2);
            }
        }

      g_object_unref (info2);
    }

  if (printed)
    g_print ("\n");

  g_free (attrs);
}
#endif



static void
fmb_folder_monitor (GFileMonitor     *monitor,
                       GFile            *event_file,
                       GFile            *other_file,
                       GFileMonitorEvent event_type,
                       gpointer          user_data)
{
  FmbFolder *folder = FMB_FOLDER (user_data);
  FmbFile   *file;
  FmbFile   *other_parent;
  GList        *lp;
  GList         list;
  gboolean      restart = FALSE;

  _fmb_return_if_fail (G_IS_FILE_MONITOR (monitor));
  _fmb_return_if_fail (FMB_IS_FOLDER (folder));
  _fmb_return_if_fail (folder->monitor == monitor);
  _fmb_return_if_fail (folder->job == NULL);
  _fmb_return_if_fail (FMB_IS_FILE (folder->corresponding_file));
  _fmb_return_if_fail (G_IS_FILE (event_file));

  /* check on which file the event occurred */
  if (!g_file_equal (event_file, fmb_file_get_file (folder->corresponding_file)))
    {
      /* check if we already ship the file */
      for (lp = folder->files; lp != NULL; lp = lp->next)
        if (g_file_equal (event_file, fmb_file_get_file (lp->data)))
          break;

      /* stop the content type collector */
      if (folder->content_type_idle_id != 0)
        restart = g_source_remove (folder->content_type_idle_id);

      /* if we don't have it, add it if the event is not an "deleted" event */
      if (G_UNLIKELY (lp == NULL && event_type != G_FILE_MONITOR_EVENT_DELETED))
        {
          /* allocate a file for the path */
          file = fmb_file_get (event_file, NULL);
          if (G_UNLIKELY (file != NULL))
            {
              /* prepend it to our internal list */
              folder->files = g_list_prepend (folder->files, file);

              /* tell others about the new file */
              list.data = file; list.next = list.prev = NULL;
              g_signal_emit (G_OBJECT (folder), folder_signals[FILES_ADDED], 0, &list);
            }
        }
      else if (lp != NULL)
        {
          if (event_type == G_FILE_MONITOR_EVENT_DELETED)
            {
              FmbFile *destroyed;

              /* destroy the file */
              fmb_file_destroy (lp->data);

              /* if the file has not been destroyed by now, reload it to invalidate it */
              destroyed = fmb_file_cache_lookup (event_file);
              if (destroyed != NULL)
                {
                  fmb_file_reload (destroyed);
                  g_object_unref (destroyed);
                }
            }

          else if (event_type == G_FILE_MONITOR_EVENT_MOVED)
            {
              /* destroy the old file and update the new one */
              fmb_file_destroy (lp->data);
              if (other_file != NULL)
                {
                  file = fmb_file_get(other_file, NULL);
                  if (file != NULL && FMB_IS_FILE (file))
                    {
                      fmb_file_reload (file);

                      /* if source and target folders are different, also tell
                         the target folder to reload for the changes */
                      if (fmb_file_has_parent (file))
                        {
                          other_parent = fmb_file_get_parent (file, NULL);
                          if (other_parent &&
                              !g_file_equal (fmb_file_get_file(folder->corresponding_file),
                                             fmb_file_get_file(other_parent)))
                            {
                              fmb_file_reload (other_parent);
                              g_object_unref (other_parent);
                            }
                        }

                      /* drop reference on the other file */
                      g_object_unref (file);
                    }
                }

              /* reload the folder of the source file */
              fmb_file_reload (folder->corresponding_file);
            }
          else
            {
#if DEBUG_FILE_CHANGES
              fmb_file_infos_equal (lp->data, event_file);
#endif
              fmb_file_reload (lp->data);
            }
        }

      /* check if we need to restart the collector */
      if (restart)
        fmb_folder_content_type_loader (folder);
    }
  else
    {
      /* update/destroy the corresponding file */
      if (event_type == G_FILE_MONITOR_EVENT_DELETED)
        {
          if (!fmb_file_exists (folder->corresponding_file))
            fmb_file_destroy (folder->corresponding_file);
        }
      else
        {
          fmb_file_reload (folder->corresponding_file);
        }
    }
}



/**
 * fmb_folder_get_for_file:
 * @file : a #FmbFile.
 *
 * Opens the specified @file as #FmbFolder and
 * returns a reference to the folder.
 *
 * The caller is responsible to free the returned
 * object using g_object_unref() when no longer
 * needed.
 *
 * Return value: the #FmbFolder which corresponds
 *               to @file.
 **/
FmbFolder*
fmb_folder_get_for_file (FmbFile *file)
{
  FmbFolder *folder;

  _fmb_return_val_if_fail (FMB_IS_FILE (file), NULL);

  /* make sure the file is loaded */
  if (!fmb_file_check_loaded (file))
    return NULL;

  _fmb_return_val_if_fail (fmb_file_is_directory (file), NULL);

  /* load if the file is not a folder */
  if (!fmb_file_is_directory (file))
    return NULL;

  /* determine the "fmb-folder" quark on-demand */
  if (G_UNLIKELY (fmb_folder_quark == 0))
    fmb_folder_quark = g_quark_from_static_string ("fmb-folder");

  /* check if we already know that folder */
  folder = g_object_get_qdata (G_OBJECT (file), fmb_folder_quark);
  if (G_UNLIKELY (folder != NULL))
    {
      g_object_ref (G_OBJECT (folder));
    }
  else
    {
      /* allocate the new instance */
      folder = g_object_new (FMB_TYPE_FOLDER, "corresponding-file", file, NULL);

      /* connect the folder to the file */
      g_object_set_qdata (G_OBJECT (file), fmb_folder_quark, folder);

      /* schedule the loading of the folder */
      fmb_folder_reload (folder, FALSE);
    }

  return folder;
}



/**
 * fmb_folder_get_corresponding_file:
 * @folder : a #FmbFolder instance.
 *
 * Returns the #FmbFile corresponding to this @folder.
 *
 * No reference is taken on the returned #FmbFile for
 * the caller, so if you need a persistent reference to
 * the file, you'll have to call g_object_ref() yourself.
 *
 * Return value: the #FmbFile corresponding to @folder.
 **/
FmbFile*
fmb_folder_get_corresponding_file (const FmbFolder *folder)
{
  _fmb_return_val_if_fail (FMB_IS_FOLDER (folder), NULL);
  return folder->corresponding_file;
}



/**
 * fmb_folder_get_files:
 * @folder : a #FmbFolder instance.
 *
 * Returns the list of files currently known for @folder.
 * The returned list is owned by @folder and may not be freed!
 *
 * Return value: the list of #FmbFiles for @folder.
 **/
GList*
fmb_folder_get_files (const FmbFolder *folder)
{
  _fmb_return_val_if_fail (FMB_IS_FOLDER (folder), NULL);
  return folder->files;
}



/**
 * fmb_folder_get_loading:
 * @folder : a #FmbFolder instance.
 *
 * Tells whether the contents of the @folder are currently
 * being loaded.
 *
 * Return value: %TRUE if @folder is loading, else %FALSE.
 **/
gboolean
fmb_folder_get_loading (const FmbFolder *folder)
{
  _fmb_return_val_if_fail (FMB_IS_FOLDER (folder), FALSE);
  return (folder->job != NULL);
}



/**
 * fmb_folder_reload:
 * @folder : a #FmbFolder instance.
 * @reload_info : reload all information for the files too
 *
 * Tells the @folder object to reread the directory
 * contents from the underlying media.
 **/
void
fmb_folder_reload (FmbFolder *folder,
                      gboolean      reload_info)
{
  _fmb_return_if_fail (FMB_IS_FOLDER (folder));

  /* reload file info too? */
  folder->reload_info = reload_info;

  /* stop metadata collector */
  if (folder->content_type_idle_id != 0)
    g_source_remove (folder->content_type_idle_id);

  /* check if we are currently connect to a job */
  if (G_UNLIKELY (folder->job != NULL))
    {
      /* disconnect from the job */
      g_signal_handlers_disconnect_matched (folder->job, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, folder);
      g_object_unref (folder->job);
      folder->job = NULL;
    }

  /* disconnect from the file alteration monitor */
  if (G_UNLIKELY (folder->monitor != NULL))
    {
      g_signal_handlers_disconnect_matched (folder->monitor, G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, folder);
      g_file_monitor_cancel (folder->monitor);
      g_object_unref (folder->monitor);
      folder->monitor = NULL;
    }

  /* reset the new_files list */
  fmb_g_file_list_free (folder->new_files);
  folder->new_files = NULL;

  /* start a new job */
  folder->job = fmb_io_jobs_list_directory (fmb_file_get_file (folder->corresponding_file));
  g_signal_connect (folder->job, "error", G_CALLBACK (fmb_folder_error), folder);
  g_signal_connect (folder->job, "finished", G_CALLBACK (fmb_folder_finished), folder);
  g_signal_connect (folder->job, "files-ready", G_CALLBACK (fmb_folder_files_ready), folder);

  /* tell all consumers that we're loading */
  g_object_notify (G_OBJECT (folder), "loading");
}
