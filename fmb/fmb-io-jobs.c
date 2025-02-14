/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2009-2011 Jannis Pohlmann <jannis@xfce.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gio/gio.h>

#include <fmb/fmb-application.h>
#include <fmb/fmb-enum-types.h>
#include <fmb/fmb-gio-extensions.h>
#include <fmb/fmb-io-scan-directory.h>
#include <fmb/fmb-io-jobs.h>
#include <fmb/fmb-io-jobs-util.h>
#include <fmb/fmb-job.h>
#include <fmb/fmb-private.h>
#include <fmb/fmb-simple-job.h>
#include <fmb/fmb-thumbnail-cache.h>
#include <fmb/fmb-transfer-job.h>



static GList *
_tij_collect_nofollow (FmbJob *job,
                       GList     *base_file_list,
                       gboolean   unlinking,
                       GError   **error)
{
  GError *err = NULL;
  GList  *child_file_list = NULL;
  GList  *file_list = NULL;
  GList  *lp;

  /* recursively collect the files */
  for (lp = base_file_list; 
       err == NULL && lp != NULL && !blxo_job_is_cancelled (BLXO_JOB (job)); 
       lp = lp->next)
    {
      /* try to scan the directory */
      child_file_list = fmb_io_scan_directory (job, lp->data, 
                                                  G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS, 
                                                  TRUE, unlinking, FALSE, &err);

      /* prepend the new files to the existing list */
      file_list = fmb_g_file_list_prepend (file_list, lp->data);
      file_list = g_list_concat (child_file_list, file_list);
    }

  /* check if we failed */
  if (err != NULL || blxo_job_is_cancelled (BLXO_JOB (job)))
    {
      if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), error))
        g_error_free (err);
      else
        g_propagate_error (error, err);

      /* release the collected files */
      fmb_g_file_list_free (file_list);

      return NULL;
    }

  return file_list;
}



static gboolean
_fmb_io_jobs_create (FmbJob  *job,
                        GArray     *param_values,
                        GError    **error)
{
  GFileOutputStream *stream;
  FmbJobResponse  response = FMB_JOB_RESPONSE_CANCEL;
  GFileInfo         *info;
  GError            *err = NULL;
  GList             *file_list;
  GList             *lp;
  gchar             *base_name;
  gchar             *display_name;
  GFile             *template_file;
  GFileInputStream  *template_stream = NULL;
  
  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 2, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* get the file list */
  file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 0));
  template_file = g_value_get_object (&g_array_index (param_values, GValue, 1));

  /* we know the total amount of files to be processed */
  fmb_job_set_total_files (FMB_JOB (job), file_list);

  /* check if we need to open the template */
  if (template_file != NULL)
    {
      /* open read stream to feed in the new files */
      template_stream = g_file_read (template_file, blxo_job_get_cancellable (BLXO_JOB (job)), &err);
      if (G_UNLIKELY (template_stream == NULL))
        {
          g_propagate_error (error, err);
          return FALSE;
        }
    }

  /* iterate over all files in the list */
  for (lp = file_list; 
       err == NULL && lp != NULL && !blxo_job_is_cancelled (BLXO_JOB (job)); 
       lp = lp->next)
    {
      g_assert (G_IS_FILE (lp->data));

      /* update progress information */
      fmb_job_processing_file (FMB_JOB (job), lp);

again:
      /* try to create the file */
      stream = g_file_create (lp->data, 
                              G_FILE_CREATE_NONE, 
                              blxo_job_get_cancellable (BLXO_JOB (job)),
                              &err);

      /* abort if the job was cancelled */
      if (blxo_job_is_cancelled (BLXO_JOB (job)))
        break;

      /* check if creating failed */
      if (stream == NULL)
        {
          if (err->code == G_IO_ERROR_EXISTS)
            {
              g_clear_error (&err);

              /* the file already exists, query its display name */
              info = g_file_query_info (lp->data,
                                        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                        G_FILE_QUERY_INFO_NONE,
                                        blxo_job_get_cancellable (BLXO_JOB (job)),
                                        NULL);

              /* abort if the job was cancelled */
              if (blxo_job_is_cancelled (BLXO_JOB (job)))
                break;

              /* determine the display name, using the basename as a fallback */
              if (info != NULL)
                {
                  display_name = g_strdup (g_file_info_get_display_name (info));
                  g_object_unref (info);
                }
              else
                {
                  base_name = g_file_get_basename (lp->data);
                  display_name = g_filename_display_name (base_name);
                  g_free (base_name);
                }

              /* ask the user whether he wants to overwrite the existing file */
              response = fmb_job_ask_overwrite (FMB_JOB (job), 
                                                   _("The file \"%s\" already exists"), 
                                                   display_name);

              /* check if we should overwrite */
              if (response == FMB_JOB_RESPONSE_YES)
                {
                  /* try to remove the file. fail if not possible */
                  if (g_file_delete (lp->data, blxo_job_get_cancellable (BLXO_JOB (job)), &err))
                    goto again;
                }
              
              /* clean up */
              g_free (display_name);
            }
          else 
            {
              /* determine display name of the file */
              base_name = g_file_get_basename (lp->data);
              display_name = g_filename_display_basename (base_name);
              g_free (base_name);

              /* ask the user whether to skip/retry this path (cancels the job if not) */
              response = fmb_job_ask_skip (FMB_JOB (job), 
                                              _("Failed to create empty file \"%s\": %s"),
                                              display_name, err->message);
              g_free (display_name);

              g_clear_error (&err);

              /* go back to the beginning if the user wants to retry */
              if (response == FMB_JOB_RESPONSE_RETRY)
                goto again;
            }
        }
      else
        {
          if (template_stream != NULL)
            {
              /* write the template into the new file */
              g_output_stream_splice (G_OUTPUT_STREAM (stream),
                                      G_INPUT_STREAM (template_stream),
                                      G_OUTPUT_STREAM_SPLICE_CLOSE_TARGET,
                                      blxo_job_get_cancellable (BLXO_JOB (job)),
                                      NULL);
            }

          g_object_unref (stream);
        }
    }

  if (template_stream != NULL)
    g_object_unref (template_stream);

  /* check if we have failed */
  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  /* check if the job was cancelled */
  if (blxo_job_is_cancelled (BLXO_JOB (job)))
    return FALSE;

  /* emit the "new-files" signal with the given file list */
  fmb_job_new_files (FMB_JOB (job), file_list);

  return TRUE;
}



FmbJob *
fmb_io_jobs_create_files (GList *file_list,
                             GFile *template_file)
{
  return fmb_simple_job_launch (_fmb_io_jobs_create, 2,
                                   FMB_TYPE_G_FILE_LIST, file_list,
                                   G_TYPE_FILE, template_file);
}



static gboolean
_fmb_io_jobs_mkdir (FmbJob  *job,
                       GArray     *param_values,
                       GError    **error)
{
  FmbJobResponse response;
  GFileInfo        *info;
  GError           *err = NULL;
  GList            *file_list;
  GList            *lp;
  gchar            *base_name;
  gchar            *display_name;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 1, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 0));

  /* we know the total list of files to process */
  fmb_job_set_total_files (FMB_JOB (job), file_list);

  for (lp = file_list; 
       err == NULL && lp != NULL && !blxo_job_is_cancelled (BLXO_JOB (job));
       lp = lp->next)
    {
      g_assert (G_IS_FILE (lp->data));

      /* update progress information */
      fmb_job_processing_file (FMB_JOB (job), lp);

again:
      /* try to create the directory */
      if (!g_file_make_directory (lp->data, blxo_job_get_cancellable (BLXO_JOB (job)), &err))
        {
          if (err->code == G_IO_ERROR_EXISTS)
            {
              g_error_free (err);
              err = NULL;

              /* abort if the job was cancelled */
              if (blxo_job_is_cancelled (BLXO_JOB (job)))
                break;

              /* the file already exists, query its display name */
              info = g_file_query_info (lp->data,
                                        G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                        G_FILE_QUERY_INFO_NONE,
                                        blxo_job_get_cancellable (BLXO_JOB (job)),
                                        NULL);

              /* abort if the job was cancelled */
              if (blxo_job_is_cancelled (BLXO_JOB (job)))
                break;

              /* determine the display name, using the basename as a fallback */
              if (info != NULL)
                {
                  display_name = g_strdup (g_file_info_get_display_name (info));
                  g_object_unref (info);
                }
              else
                {
                  base_name = g_file_get_basename (lp->data);
                  display_name = g_filename_display_name (base_name);
                  g_free (base_name);
                }

              /* ask the user whether he wants to overwrite the existing file */
              response = fmb_job_ask_overwrite (FMB_JOB (job), 
                                                   _("The file \"%s\" already exists"),
                                                   display_name);

              /* check if we should overwrite it */
              if (response == FMB_JOB_RESPONSE_YES)
                {
                  /* try to remove the file, fail if not possible */
                  if (g_file_delete (lp->data, blxo_job_get_cancellable (BLXO_JOB (job)), &err))
                    goto again;
                }

              /* clean up */
              g_free (display_name);
            }
          else
            {
              /* determine the display name of the file */
              base_name = g_file_get_basename (lp->data);
              display_name = g_filename_display_basename (base_name);
              g_free (base_name);

              /* ask the user whether to skip/retry this path (cancels the job if not) */
              response = fmb_job_ask_skip (FMB_JOB (job), 
                                              _("Failed to create directory \"%s\": %s"),
                                              display_name, err->message);
              g_free (display_name);

              g_error_free (err);
              err = NULL;

              /* go back to the beginning if the user wants to retry */
              if (response == FMB_JOB_RESPONSE_RETRY)
                goto again;
            }
        }
    }

  /* check if we have failed */
  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  /* check if the job was cancelled */
  if (blxo_job_is_cancelled (BLXO_JOB (job)))
    return FALSE;

  /* emit the "new-files" signal with the given file list */
  fmb_job_new_files (FMB_JOB (job), file_list);
  
  return TRUE;
}



FmbJob *
fmb_io_jobs_make_directories (GList *file_list)
{
  return fmb_simple_job_launch (_fmb_io_jobs_mkdir, 1,
                                   FMB_TYPE_G_FILE_LIST, file_list);
}



static gboolean
_fmb_io_jobs_unlink (FmbJob  *job,
                        GArray     *param_values,
                        GError    **error)
{
  FmbThumbnailCache *thumbnail_cache;
  FmbApplication    *application;
  FmbJobResponse     response;
  GFileInfo            *info;
  GError               *err = NULL;
  GList                *file_list;
  GList                *lp;
  gchar                *base_name;
  gchar                *display_name;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 1, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* get the file list */
  file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 0));

  /* tell the user that we're preparing to unlink the files */
  blxo_job_info_message (BLXO_JOB (job), _("Preparing..."));

  /* recursively collect files for removal, not following any symlinks */
  file_list = _tij_collect_nofollow (job, file_list, TRUE, &err);

  /* free the file list and fail if there was an error or the job was cancelled */
  if (err != NULL || blxo_job_is_cancelled (BLXO_JOB (job)))
    {
      if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), error))
        g_error_free (err);
      else
        g_propagate_error (error, err);

      fmb_g_file_list_free (file_list);
      return FALSE;
    }

  /* we know the total list of files to process */
  fmb_job_set_total_files (FMB_JOB (job), file_list);

  /* take a reference on the thumbnail cache */
  application = fmb_application_get ();
  thumbnail_cache = fmb_application_get_thumbnail_cache (application);
  g_object_unref (application);

  /* remove all the files */
  for (lp = file_list; lp != NULL && !blxo_job_is_cancelled (BLXO_JOB (job)); lp = lp->next)
    {
      g_assert (G_IS_FILE (lp->data));

      /* skip root folders which cannot be deleted anyway */
      if (fmb_g_file_is_root (lp->data))
        continue;

again:
      /* try to delete the file */
      if (g_file_delete (lp->data, blxo_job_get_cancellable (BLXO_JOB (job)), &err))
        {
          /* notify the thumbnail cache that the corresponding thumbnail can also
           * be deleted now */
          fmb_thumbnail_cache_delete_file (thumbnail_cache, lp->data);
        }
      else
        {
          /* query the file info for the display name */
          info = g_file_query_info (lp->data, 
                                    G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                    G_FILE_QUERY_INFO_NONE, 
                                    blxo_job_get_cancellable (BLXO_JOB (job)), 
                                    NULL);

          /* abort if the job was cancelled */
          if (blxo_job_is_cancelled (BLXO_JOB (job)))
            {
              g_clear_error (&err);
              break;
            }

          /* determine the display name, using the basename as a fallback */
          if (info != NULL)
            {
              display_name = g_strdup (g_file_info_get_display_name (info));
              g_object_unref (info);
            }
          else
            {
              base_name = g_file_get_basename (lp->data);
              display_name = g_filename_display_name (base_name);
              g_free (base_name);
            }

          /* ask the user whether he wants to skip this file */
          response = fmb_job_ask_skip (FMB_JOB (job), 
                                          _("Could not delete file \"%s\": %s"), 
                                          display_name, err->message);
          g_free (display_name);

          /* clear the error */
          g_clear_error (&err);

          /* check whether to retry */
          if (response == FMB_JOB_RESPONSE_RETRY)
            goto again;
        }
    }

  /* release the thumbnail cache */
  g_object_unref (thumbnail_cache);

  /* release the file list */
  fmb_g_file_list_free (file_list);

  if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), error))
    return FALSE;
  else
    return TRUE;
}



FmbJob *
fmb_io_jobs_unlink_files (GList *file_list)
{
  return fmb_simple_job_launch (_fmb_io_jobs_unlink, 1,
                                   FMB_TYPE_G_FILE_LIST, file_list);
}



FmbJob *
fmb_io_jobs_move_files (GList *source_file_list,
                           GList *target_file_list)
{
  FmbJob *job;

  _fmb_return_val_if_fail (source_file_list != NULL, NULL);
  _fmb_return_val_if_fail (target_file_list != NULL, NULL);
  _fmb_return_val_if_fail (g_list_length (source_file_list) == g_list_length (target_file_list), NULL);

  job = fmb_transfer_job_new (source_file_list, target_file_list, 
                                 FMB_TRANSFER_JOB_MOVE);
  
  return FMB_JOB (blxo_job_launch (BLXO_JOB (job)));
}



FmbJob *
fmb_io_jobs_copy_files (GList *source_file_list,
                           GList *target_file_list)
{
  FmbJob *job;

  _fmb_return_val_if_fail (source_file_list != NULL, NULL);
  _fmb_return_val_if_fail (target_file_list != NULL, NULL);
  _fmb_return_val_if_fail (g_list_length (source_file_list) == g_list_length (target_file_list), NULL);

  job = fmb_transfer_job_new (source_file_list, target_file_list,
                                 FMB_TRANSFER_JOB_COPY);

  return FMB_JOB (blxo_job_launch (BLXO_JOB (job)));
}



static GFile *
_fmb_io_jobs_link_file (FmbJob *job,
                           GFile     *source_file,
                           GFile     *target_file,
                           GError   **error)
{
  FmbJobResponse response;
  GError           *err = NULL;
  gchar            *base_name;
  gchar            *display_name;
  gchar            *source_path;
  gint              n;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), NULL);
  _fmb_return_val_if_fail (G_IS_FILE (source_file), NULL);
  _fmb_return_val_if_fail (G_IS_FILE (target_file), NULL);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, NULL);

  /* abort on cancellation */
  if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), error))
    return NULL;

  /* try to determine the source path */
  source_path = g_file_get_path (source_file);
  if (source_path == NULL)
    {
      base_name = g_file_get_basename (source_file);
      display_name = g_filename_display_name (base_name);
      g_set_error (&err, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                   _("Could not create symbolic link to \"%s\" "
                     "because it is not a local file"), display_name);
      g_free (display_name);
      g_free (base_name);
    }

  /* various attempts to create the symbolic link */
  while (err == NULL)
    {
      if (!g_file_equal (source_file, target_file))
        {
          /* try to create the symlink */
          if (g_file_make_symbolic_link (target_file, source_path, 
                                         blxo_job_get_cancellable (BLXO_JOB (job)),
                                         &err))
            {
              /* release the source path */
              g_free (source_path);

              /* return the real target file */
              return g_object_ref (target_file);
            }
        }
      else
        {
          for (n = 1; err == NULL; ++n)
            {
              GFile *duplicate_file = fmb_io_jobs_util_next_duplicate_file (job,
                                                                               source_file,
                                                                               FALSE, n,
                                                                               &err);

              if (err == NULL)
                {
                  /* try to create the symlink */
                  if (g_file_make_symbolic_link (duplicate_file, source_path,
                                                 blxo_job_get_cancellable (BLXO_JOB (job)),
                                                 &err))
                    {
                      /* release the source path */
                      g_free (source_path);

                      /* return the real target file */
                      return duplicate_file;
                    }
                  
                  /* release the duplicate file, we no longer need it */
                  g_object_unref (duplicate_file);
                }

              if (err != NULL && err->domain == G_IO_ERROR && err->code == G_IO_ERROR_EXISTS)
                {
                  /* this duplicate already exists => clear the error and try the next alternative */
                  g_clear_error (&err);
                }
            }
        }

      /* check if we can recover from this error */
      if (err->domain == G_IO_ERROR && err->code == G_IO_ERROR_EXISTS)
        {
          /* ask the user whether to replace the target file */
          response = fmb_job_ask_overwrite (job, "%s", err->message);

          /* reset the error */
          g_clear_error (&err);

          /* propagate the cancelled error if the job was aborted */
          if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), &err))
            break;

          /* try to delete the file */
          if (response == FMB_JOB_RESPONSE_YES)
            {
              /* try to remove the target file. if not possible, err will be set and 
               * the while loop will be aborted */
              g_file_delete (target_file, blxo_job_get_cancellable (BLXO_JOB (job)), &err);
            }

          /* tell the caller that we skipped this file if the user doesn't want to
           * overwrite it */
          if (response == FMB_JOB_RESPONSE_NO)
            return g_object_ref (source_file);
        }
    }

  _fmb_assert (err != NULL);

  /* free the source path */
  g_free (source_path);

  g_propagate_error (error, err);
  return NULL;
}



static gboolean
_fmb_io_jobs_link (FmbJob  *job,
                      GArray     *param_values,
                      GError    **error)
{
  FmbThumbnailCache *thumbnail_cache;
  FmbApplication    *application;
  GError               *err = NULL;
  GFile                *real_target_file;
  GList                *new_files_list = NULL;
  GList                *source_file_list;
  GList                *sp;
  GList                *target_file_list;
  GList                *tp;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 2, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  source_file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 0));
  target_file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 1));

  /* we know the total list of paths to process */
  fmb_job_set_total_files (FMB_JOB (job), source_file_list);

  /* take a reference on the thumbnail cache */
  application = fmb_application_get ();
  thumbnail_cache = fmb_application_get_thumbnail_cache (application);
  g_object_unref (application);

  /* process all files */
  for (sp = source_file_list, tp = target_file_list;
       err == NULL && sp != NULL && tp != NULL;
       sp = sp->next, tp = tp->next)
    {
      _fmb_assert (G_IS_FILE (sp->data));
      _fmb_assert (G_IS_FILE (tp->data));

      /* update progress information */
      fmb_job_processing_file (FMB_JOB (job), sp);

      /* try to create the symbolic link */
      real_target_file = _fmb_io_jobs_link_file (job, sp->data, tp->data, &err);
      if (real_target_file != NULL)
        {
          /* queue the file for the folder update unless it was skipped */
          if (sp->data != real_target_file)
            {
              new_files_list = fmb_g_file_list_prepend (new_files_list, 
                                                           real_target_file);

              /* notify the thumbnail cache that we need to copy the original
               * thumbnail for the symlink to have one too */
              fmb_thumbnail_cache_copy_file (thumbnail_cache, sp->data, 
                                                real_target_file);

            }
  
          /* release the real target file */
          g_object_unref (real_target_file);
        }
    }

  /* release the thumbnail cache */
  g_object_unref (thumbnail_cache);

  if (err != NULL)
    {
      fmb_g_file_list_free (new_files_list);
      g_propagate_error (error, err);
      return FALSE;
    }
  else
    {
      fmb_job_new_files (FMB_JOB (job), new_files_list);
      fmb_g_file_list_free (new_files_list);
      return TRUE;
    }
}



FmbJob *
fmb_io_jobs_link_files (GList *source_file_list,
                           GList *target_file_list)
{
  _fmb_return_val_if_fail (source_file_list != NULL, NULL);
  _fmb_return_val_if_fail (target_file_list != NULL, NULL);
  _fmb_return_val_if_fail (g_list_length (source_file_list) == g_list_length (target_file_list), NULL);

  return fmb_simple_job_launch (_fmb_io_jobs_link, 2,
                                   FMB_TYPE_G_FILE_LIST, source_file_list,
                                   FMB_TYPE_G_FILE_LIST, target_file_list);
}



static gboolean
_fmb_io_jobs_trash (FmbJob  *job,
                       GArray     *param_values,
                       GError    **error)
{
  FmbThumbnailCache *thumbnail_cache;
  FmbApplication    *application;
  GError               *err = NULL;
  GList                *file_list;
  GList                *lp;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 1, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 0));

  if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), error))
    return FALSE;

  /* take a reference on the thumbnail cache */
  application = fmb_application_get ();
  thumbnail_cache = fmb_application_get_thumbnail_cache (application);
  g_object_unref (application);

  for (lp = file_list; err == NULL && lp != NULL; lp = lp->next)
    {
      _fmb_assert (G_IS_FILE (lp->data));

      /* trash the file or folder */
      g_file_trash (lp->data, blxo_job_get_cancellable (BLXO_JOB (job)), &err);

      /* update the thumbnail cache */
      fmb_thumbnail_cache_cleanup_file (thumbnail_cache, lp->data);
    }

  /* release the thumbnail cache */
  g_object_unref (thumbnail_cache);

  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}



FmbJob *
fmb_io_jobs_trash_files (GList *file_list)
{
  _fmb_return_val_if_fail (file_list != NULL, NULL);

  return fmb_simple_job_launch (_fmb_io_jobs_trash, 1,
                                   FMB_TYPE_G_FILE_LIST, file_list);
}



FmbJob *
fmb_io_jobs_restore_files (GList *source_file_list,
                              GList *target_file_list)
{
  FmbJob *job;

  _fmb_return_val_if_fail (source_file_list != NULL, NULL);
  _fmb_return_val_if_fail (target_file_list != NULL, NULL);
  _fmb_return_val_if_fail (g_list_length (source_file_list) == g_list_length (target_file_list), NULL);

  job = fmb_transfer_job_new (source_file_list, target_file_list, 
                                 FMB_TRANSFER_JOB_MOVE);

  return FMB_JOB (blxo_job_launch (BLXO_JOB (job)));
}



static gboolean
_fmb_io_jobs_chown (FmbJob  *job,
                       GArray     *param_values,
                       GError    **error)
{
  FmbJobResponse response;
  const gchar      *message;
  GFileInfo        *info;
  gboolean          recursive;
  GError           *err = NULL;
  GList            *file_list;
  GList            *lp;
  gint              uid;
  gint              gid;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 4, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 0));
  uid = g_value_get_int (&g_array_index (param_values, GValue, 1));
  gid = g_value_get_int (&g_array_index (param_values, GValue, 2));
  recursive = g_value_get_boolean (&g_array_index (param_values, GValue, 3));

  _fmb_assert ((uid >= 0 || gid >= 0) && !(uid >= 0 && gid >= 0));

  /* collect the files for the chown operation */
  if (recursive)
    file_list = _tij_collect_nofollow (job, file_list, FALSE, &err);
  else
    file_list = fmb_g_file_list_copy (file_list);

  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  /* we know the total list of files to process */
  fmb_job_set_total_files (FMB_JOB (job), file_list);

  /* change the ownership of all files */
  for (lp = file_list; lp != NULL && err == NULL; lp = lp->next)
    {
      /* update progress information */
      fmb_job_processing_file (FMB_JOB (job), lp);

      /* try to query information about the file */
      info = g_file_query_info (lp->data, 
                                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                blxo_job_get_cancellable (BLXO_JOB (job)),
                                &err);

      if (err != NULL)
        break;

retry_chown:
      if (uid >= 0)
        {
          /* try to change the owner UID */
          g_file_set_attribute_uint32 (lp->data,
                                       G_FILE_ATTRIBUTE_UNIX_UID, uid,
                                       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                       blxo_job_get_cancellable (BLXO_JOB (job)),
                                       &err);
        }
      else if (gid >= 0)
        {
          /* try to change the owner GID */
          g_file_set_attribute_uint32 (lp->data,
                                       G_FILE_ATTRIBUTE_UNIX_GID, gid,
                                       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                       blxo_job_get_cancellable (BLXO_JOB (job)),
                                       &err);
        }

      /* check if there was a recoverable error */
      if (err != NULL && !blxo_job_is_cancelled (BLXO_JOB (job)))
        {
          /* generate a useful error message */
          message = G_LIKELY (uid >= 0) ? _("Failed to change the owner of \"%s\": %s") 
                                        : _("Failed to change the group of \"%s\": %s");

          /* ask the user whether to skip/retry this file */
          response = fmb_job_ask_skip (FMB_JOB (job), message, 
                                          g_file_info_get_display_name (info),
                                          err->message);

          /* clear the error */
          g_clear_error (&err);

          /* check whether to retry */
          if (response == FMB_JOB_RESPONSE_RETRY)
            goto retry_chown;
        }

      /* release file information */
      g_object_unref (info);
    }

  /* release the file list */
  fmb_g_file_list_free (file_list);

  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
}



FmbJob *
fmb_io_jobs_change_group (GList    *files,
                             guint32   gid,
                             gboolean  recursive)
{
  _fmb_return_val_if_fail (files != NULL, NULL);

  /* files are released when the list if destroyed */
  g_list_foreach (files, (GFunc) g_object_ref, NULL);

  return fmb_simple_job_launch (_fmb_io_jobs_chown, 4,
                                   FMB_TYPE_G_FILE_LIST, files,
                                   G_TYPE_INT, -1,
                                   G_TYPE_INT, (gint) gid,
                                   G_TYPE_BOOLEAN, recursive);
}



static gboolean
_fmb_io_jobs_chmod (FmbJob  *job,
                       GArray     *param_values,
                       GError    **error)
{
  FmbJobResponse response;
  GFileInfo        *info;
  gboolean          recursive;
  GError           *err = NULL;
  GList            *file_list;
  GList            *lp;
  FmbFileMode    dir_mask;
  FmbFileMode    dir_mode;
  FmbFileMode    file_mask;
  FmbFileMode    file_mode;
  FmbFileMode    mask;
  FmbFileMode    mode;
  FmbFileMode    old_mode;
  FmbFileMode    new_mode;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 6, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  file_list = g_value_get_boxed (&g_array_index (param_values, GValue, 0));
  dir_mask = g_value_get_flags (&g_array_index (param_values, GValue, 1));
  dir_mode = g_value_get_flags (&g_array_index (param_values, GValue, 2));
  file_mask = g_value_get_flags (&g_array_index (param_values, GValue, 3));
  file_mode = g_value_get_flags (&g_array_index (param_values, GValue, 4));
  recursive = g_value_get_boolean (&g_array_index (param_values, GValue, 5));

  /* collect the files for the chown operation */
  if (recursive)
    file_list = _tij_collect_nofollow (job, file_list, FALSE, &err);
  else
    file_list = fmb_g_file_list_copy (file_list);

  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  /* we know the total list of files to process */
  fmb_job_set_total_files (FMB_JOB (job), file_list);

  /* change the ownership of all files */
  for (lp = file_list; lp != NULL && err == NULL; lp = lp->next)
    {
      /* update progress information */
      fmb_job_processing_file (FMB_JOB (job), lp);

      /* try to query information about the file */
      info = g_file_query_info (lp->data, 
                                G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME ","
                                G_FILE_ATTRIBUTE_STANDARD_TYPE ","
                                G_FILE_ATTRIBUTE_UNIX_MODE,
                                G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                blxo_job_get_cancellable (BLXO_JOB (job)),
                                &err);

      if (err != NULL)
        break;

retry_chown:
      /* different actions depending on the type of the file */
      if (g_file_info_get_file_type (info) == G_FILE_TYPE_DIRECTORY)
        {
          mask = dir_mask;
          mode = dir_mode;
        }
      else
        {
          mask = file_mask;
          mode = file_mode;
        }

      /* determine the current mode */
      old_mode = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_UNIX_MODE);

      /* generate the new mode, taking the old mode (which contains file type 
       * information) into account */
      new_mode = ((old_mode & ~mask) | mode) & 07777;

      if (old_mode != new_mode)
        {
          /* try to change the file mode */
          g_file_set_attribute_uint32 (lp->data,
                                       G_FILE_ATTRIBUTE_UNIX_MODE, new_mode,
                                       G_FILE_QUERY_INFO_NOFOLLOW_SYMLINKS,
                                       blxo_job_get_cancellable (BLXO_JOB (job)),
                                       &err);
        }

      /* check if there was a recoverable error */
      if (err != NULL && !blxo_job_is_cancelled (BLXO_JOB (job)))
        {
          /* ask the user whether to skip/retry this file */
          response = fmb_job_ask_skip (job,
                                          _("Failed to change the permissions of \"%s\": %s"), 
                                          g_file_info_get_display_name (info),
                                          err->message);

          /* clear the error */
          g_clear_error (&err);

          /* check whether to retry */
          if (response == FMB_JOB_RESPONSE_RETRY)
            goto retry_chown;
        }

      /* release file information */
      g_object_unref (info);
    }

  /* release the file list */
  fmb_g_file_list_free (file_list);

  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }
  else
    {
      return TRUE;
    }
  return TRUE;
}



FmbJob *
fmb_io_jobs_change_mode (GList         *files,
                            FmbFileMode dir_mask,
                            FmbFileMode dir_mode,
                            FmbFileMode file_mask,
                            FmbFileMode file_mode,
                            gboolean       recursive)
{
  _fmb_return_val_if_fail (files != NULL, NULL);

  /* files are released when the list if destroyed */
  g_list_foreach (files, (GFunc) g_object_ref, NULL);

  return fmb_simple_job_launch (_fmb_io_jobs_chmod, 6,
                                   FMB_TYPE_G_FILE_LIST, files,
                                   FMB_TYPE_FILE_MODE, dir_mask,
                                   FMB_TYPE_FILE_MODE, dir_mode,
                                   FMB_TYPE_FILE_MODE, file_mask,
                                   FMB_TYPE_FILE_MODE, file_mode,
                                   G_TYPE_BOOLEAN, recursive);
}



static gboolean
_fmb_io_jobs_ls (FmbJob  *job,
                    GArray     *param_values,
                    GError    **error)
{
  GError *err = NULL;
  GFile  *directory;
  GList  *file_list = NULL;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 1, FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), error))
    return FALSE;

  /* determine the directory to list */
  directory = g_value_get_object (&g_array_index (param_values, GValue, 0));

  /* make sure the object is valid */
  _fmb_assert (G_IS_FILE (directory));

  /* collect directory contents (non-recursively) */
  file_list = fmb_io_scan_directory (job, directory,
                                        G_FILE_QUERY_INFO_NONE, 
                                        FALSE, FALSE, TRUE, &err);

  /* abort on errors or cancellation */
  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }
  else if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), &err))
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  /* check if we have any files to report */
  if (G_LIKELY (file_list != NULL))
    {
      /* emit the "files-ready" signal */
      if (!fmb_job_files_ready (FMB_JOB (job), file_list))
        {
          /* none of the handlers took over the file list, so it's up to us
           * to destroy it */
          fmb_g_file_list_free (file_list);
        }
    }
  
  /* there should be no errors here */
  _fmb_assert (err == NULL);

  /* propagate cancellation error */
  if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), &err))
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  return TRUE;
}



FmbJob *
fmb_io_jobs_list_directory (GFile *directory)
{
  _fmb_return_val_if_fail (G_IS_FILE (directory), NULL);
  
  return fmb_simple_job_launch (_fmb_io_jobs_ls, 1, G_TYPE_FILE, directory);
}



static gboolean
_fmb_io_jobs_rename_notify (FmbFile *file)
{
  _fmb_return_val_if_fail (FMB_IS_FILE (file), FALSE);

  /* tell the associated folder that the file was renamed */
  fmbx_file_info_renamed (FMBX_FILE_INFO (file));

  /* emit the file changed signal */
  fmb_file_changed (file);

  return FALSE;
}



static gboolean
_fmb_io_jobs_rename (FmbJob  *job,
                        GArray     *param_values,
                        GError    **error)
{
  const gchar *display_name;
  FmbFile  *file;
  GError      *err = NULL;

  _fmb_return_val_if_fail (FMB_IS_JOB (job), FALSE);
  _fmb_return_val_if_fail (param_values != NULL, FALSE);
  _fmb_return_val_if_fail (param_values->len == 2, FALSE);
  _fmb_return_val_if_fail (G_VALUE_HOLDS (&g_array_index (param_values, GValue, 0), FMB_TYPE_FILE), FALSE);
  _fmb_return_val_if_fail (G_VALUE_HOLDS_STRING (&g_array_index (param_values, GValue, 1)), FALSE);
  _fmb_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  if (blxo_job_set_error_if_cancelled (BLXO_JOB (job), error))
    return FALSE;

  /* determine the file and display name */
  file = g_value_get_object (&g_array_index (param_values, GValue, 0));
  display_name = g_value_get_string (&g_array_index (param_values, GValue, 1));

  /* try to rename the file */
  if (fmb_file_rename (file, display_name, blxo_job_get_cancellable (BLXO_JOB (job)), TRUE, &err))
    {
      blxo_job_send_to_mainloop (BLXO_JOB (job), 
                                (GSourceFunc) _fmb_io_jobs_rename_notify, 
                                g_object_ref (file), g_object_unref);
    }

  /* abort on errors or cancellation */
  if (err != NULL)
    {
      g_propagate_error (error, err);
      return FALSE;
    }

  return TRUE;
}



FmbJob *
fmb_io_jobs_rename_file (FmbFile  *file,
                            const gchar *display_name)
{
  _fmb_return_val_if_fail (FMB_IS_FILE (file), NULL);
  _fmb_return_val_if_fail (g_utf8_validate (display_name, -1, NULL), NULL);

  return fmb_simple_job_launch (_fmb_io_jobs_rename, 2, 
                                   FMB_TYPE_FILE, file, 
                                   G_TYPE_STRING, display_name);
}
