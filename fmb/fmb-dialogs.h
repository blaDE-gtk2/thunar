/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2005-2007 Benedikt Meurer <benny@xfce.org>
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

#ifndef __FMB_DIALOGS_H__
#define __FMB_DIALOGS_H__

#include <fmb/fmb-enum-types.h>
#include <fmb/fmb-file.h>
#include <fmb/fmb-job.h>

G_BEGIN_DECLS;

FmbJob         *fmb_dialogs_show_rename_file      (gpointer              parent,
                                                         FmbFile           *file);
void               fmb_dialogs_show_about            (GtkWindow            *parent,
                                                         const gchar          *title,
                                                         const gchar          *format,
                                                         ...) G_GNUC_PRINTF (3, 4);
void               fmb_dialogs_show_error            (gpointer              parent,
                                                         const GError         *error,
                                                         const gchar          *format,
                                                         ...) G_GNUC_PRINTF (3, 4);
FmbJobResponse  fmb_dialogs_show_job_ask          (GtkWindow            *parent,
                                                         const gchar          *question,
                                                         FmbJobResponse     choices);
FmbJobResponse  fmb_dialogs_show_job_ask_replace  (GtkWindow            *parent,
                                                         FmbFile           *src_file,
                                                         FmbFile           *dst_file);
void               fmb_dialogs_show_job_error        (GtkWindow            *parent,
                                                         GError               *error);
gboolean           fmb_dialogs_show_insecure_program (gpointer              parent,
                                                         const gchar          *title,
                                                         FmbFile           *file,
                                                         const gchar          *command);

G_END_DECLS;

#endif /* !__FMB_DIALOGS_H__ */
