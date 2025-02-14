/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>
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
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#include <fmb/fmb-emblem-chooser.h>
#include <fmb/fmb-gobject-extensions.h>
#include <fmb/fmb-private.h>



/* Property identifiers */
enum
{
  PROP_0,
  PROP_FILES,
};



static void       fmb_emblem_chooser_dispose         (GObject                   *object);
static void       fmb_emblem_chooser_get_property    (GObject                   *object,
                                                         guint                      prop_id,
                                                         GValue                    *value,
                                                         GParamSpec                *pspec);
static void       fmb_emblem_chooser_set_property    (GObject                   *object,
                                                         guint                      prop_id,
                                                         const GValue              *value,
                                                         GParamSpec                *pspec);
static void       fmb_emblem_chooser_realize         (GtkWidget                 *widget);
static void       fmb_emblem_chooser_unrealize       (GtkWidget                 *widget);
static void       fmb_emblem_chooser_button_toggled  (GtkToggleButton           *button,
                                                         FmbEmblemChooser       *chooser);
static void       fmb_emblem_chooser_file_changed    (FmbEmblemChooser       *chooser);
static void       fmb_emblem_chooser_theme_changed   (GtkIconTheme              *icon_theme,
                                                         FmbEmblemChooser       *chooser);
static void       fmb_emblem_chooser_create_buttons  (FmbEmblemChooser       *chooser);
static GtkWidget *fmb_emblem_chooser_create_button   (FmbEmblemChooser       *chooser,
                                                         const gchar               *emblem);
static GList     *fmb_emblem_chooser_get_files       (const FmbEmblemChooser *chooser);
static void       fmb_emblem_chooser_set_files       (FmbEmblemChooser       *chooser,
                                                         GList                     *files);



struct _FmbEmblemChooserClass
{
  GtkScrolledWindowClass __parent__;
};

struct _FmbEmblemChooser
{
  GtkScrolledWindow __parent__;

  GtkIconTheme *icon_theme;
  GList        *files;
  GtkWidget    *table;
};



G_DEFINE_TYPE (FmbEmblemChooser, fmb_emblem_chooser, GTK_TYPE_SCROLLED_WINDOW)



static void
fmb_emblem_chooser_class_init (FmbEmblemChooserClass *klass)
{
  GtkWidgetClass *gtkwidget_class;
  GObjectClass   *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->dispose = fmb_emblem_chooser_dispose;
  gobject_class->get_property = fmb_emblem_chooser_get_property;
  gobject_class->set_property = fmb_emblem_chooser_set_property;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->realize = fmb_emblem_chooser_realize;
  gtkwidget_class->unrealize = fmb_emblem_chooser_unrealize;

  /**
   * FmbEmblemChooser::file:
   *
   * The file for which emblems should be choosen.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_FILES,
                                   g_param_spec_boxed ("files", "files", "files",
                                                       FMBX_TYPE_FILE_INFO_LIST,
                                                       BLXO_PARAM_READWRITE));
}



static void
fmb_emblem_chooser_init (FmbEmblemChooser *chooser)
{
  GtkWidget *viewport;

  /* setup the scrolled window instance */
  gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (chooser), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
  gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (chooser), NULL);
  gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (chooser), NULL);

  /* setup the viewport */
  viewport = gtk_viewport_new (gtk_scrolled_window_get_hadjustment (GTK_SCROLLED_WINDOW (chooser)),
                               gtk_scrolled_window_get_vadjustment (GTK_SCROLLED_WINDOW (chooser)));
  gtk_viewport_set_shadow_type (GTK_VIEWPORT (viewport), GTK_SHADOW_NONE);
  gtk_container_add (GTK_CONTAINER (chooser), viewport);
  gtk_widget_show (viewport);

  /* setup the wrap table */
  chooser->table = g_object_new (BLXO_TYPE_WRAP_TABLE,
                                 "border-width", 6,
                                 "homogeneous", TRUE,
                                 "col-spacing", 12,
                                 "row-spacing", 12,
                                 NULL);
  gtk_container_add (GTK_CONTAINER (viewport), chooser->table);
  gtk_widget_show (chooser->table);
}



static void
fmb_emblem_chooser_dispose (GObject *object)
{
  FmbEmblemChooser *chooser = FMB_EMBLEM_CHOOSER (object);

  /* disconnect from the file */
  fmb_emblem_chooser_set_files (chooser, NULL);

  (*G_OBJECT_CLASS (fmb_emblem_chooser_parent_class)->dispose) (object);
}



static void
fmb_emblem_chooser_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  FmbEmblemChooser *chooser = FMB_EMBLEM_CHOOSER (object);

  switch (prop_id)
    {
    case PROP_FILES:
      g_value_set_boxed (value, fmb_emblem_chooser_get_files (chooser));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_emblem_chooser_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  FmbEmblemChooser *chooser = FMB_EMBLEM_CHOOSER (object);

  switch (prop_id)
    {
    case PROP_FILES:
      fmb_emblem_chooser_set_files (chooser, g_value_get_boxed (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_emblem_chooser_realize (GtkWidget *widget)
{
  FmbEmblemChooser *chooser = FMB_EMBLEM_CHOOSER (widget);

  /* let the GtkWidget class perform the realization */
  (*GTK_WIDGET_CLASS (fmb_emblem_chooser_parent_class)->realize) (widget);

  /* determine the icon theme for the new screen */
  chooser->icon_theme = gtk_icon_theme_get_for_screen (gtk_widget_get_screen (widget));
  g_signal_connect (G_OBJECT (chooser->icon_theme), "changed",
                    G_CALLBACK (fmb_emblem_chooser_theme_changed),
                    chooser);
  g_object_ref (G_OBJECT (chooser->icon_theme));

  /* create the emblem buttons */
  fmb_emblem_chooser_create_buttons (chooser);
}



static void
fmb_emblem_chooser_unrealize (GtkWidget *widget)
{
  FmbEmblemChooser *chooser = FMB_EMBLEM_CHOOSER (widget);

  /* drop all check buttons */
  gtk_container_foreach (GTK_CONTAINER (chooser->table), (GtkCallback) gtk_widget_destroy, NULL);

  /* release our reference on the icon theme */
  g_signal_handlers_disconnect_by_func (G_OBJECT (chooser->icon_theme), fmb_emblem_chooser_theme_changed, chooser);
  g_object_unref (G_OBJECT (chooser->icon_theme));
  chooser->icon_theme = NULL;

  /* let the GtkWidget class perform the unrealization */
  (*GTK_WIDGET_CLASS (fmb_emblem_chooser_parent_class)->unrealize) (widget);
}



static void
fmb_emblem_chooser_button_toggled (GtkToggleButton     *button,
                                      FmbEmblemChooser *chooser)
{
  const gchar *emblem_name;
  GList       *emblem_names;
  GList       *lp;
  GList       *delete_link;
  gboolean     is_modified;

  _fmb_return_if_fail (GTK_IS_TOGGLE_BUTTON (button));
  _fmb_return_if_fail (FMB_IS_EMBLEM_CHOOSER (chooser));

  /* we just ignore toggle events if no file is set */
  if (G_LIKELY (chooser->files == NULL))
    return;

  /* get the name of the toggled button */
  emblem_name = g_object_get_data (G_OBJECT (button), I_("fmb-emblem"));
  if (G_UNLIKELY (emblem_name == NULL))
    return;

  /* once clicked, it is active or inactive */
  gtk_toggle_button_set_inconsistent (button, FALSE);

  for (lp = chooser->files; lp != NULL; lp = lp->next)
    {
      emblem_names = fmb_file_get_emblem_names (FMB_FILE (lp->data));
      is_modified = FALSE;

      if (gtk_toggle_button_get_active (button))
        {
          /* check if we need to add the new emblem */
          if (g_list_find_custom (emblem_names, emblem_name, (GCompareFunc) strcmp) == NULL)
            {
              emblem_names = g_list_append (emblem_names, (gchar *) emblem_name);
              is_modified = TRUE;
            }
        }
      else
        {
          delete_link = g_list_find_custom (emblem_names, emblem_name, (GCompareFunc) strcmp);
          if (delete_link != NULL)
            {
              emblem_names = g_list_delete_link (emblem_names, delete_link);
              is_modified = TRUE;
            }
        }

      if (is_modified)
      {
        g_signal_handlers_block_by_func (lp->data, fmb_emblem_chooser_file_changed, chooser);
        fmb_file_set_emblem_names (FMB_FILE (lp->data), emblem_names);
        g_signal_handlers_unblock_by_func (lp->data, fmb_emblem_chooser_file_changed, chooser);
      }

      g_list_free (emblem_names);
    }
}



static void
fmb_emblem_chooser_file_changed (FmbEmblemChooser *chooser)
{
  const gchar *emblem_name;
  GHashTable  *emblem_names;
  GList       *file_emblems;
  GList       *children;
  GList       *lp, *li;
  guint       *count;
  guint        n_files = 0;

  _fmb_return_if_fail (FMB_IS_EMBLEM_CHOOSER (chooser));

  emblem_names = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, g_free);

  /* determine the emblems set for the files */
  for (lp = chooser->files; lp != NULL; lp = lp->next)
    {
      file_emblems = fmb_file_get_emblem_names (FMB_FILE (lp->data));
      for (li = file_emblems; li != NULL; li = li->next)
        {
          count = g_hash_table_lookup (emblem_names, li->data);
          if (count == NULL)
            {
              count = g_new0 (guint, 1);
              g_hash_table_insert (emblem_names, li->data, count);
            }

          *count = *count + 1;
        }

      n_files++;
    }

  /* toggle the button states appropriately */
  children = gtk_container_get_children (GTK_CONTAINER (chooser->table));
  for (lp = children; lp != NULL; lp = lp->next)
    {
      emblem_name = g_object_get_data (G_OBJECT (lp->data), I_("fmb-emblem"));
      count = g_hash_table_lookup (emblem_names, emblem_name);

      g_signal_handlers_block_by_func (lp->data, fmb_emblem_chooser_button_toggled, chooser);

      if (count == NULL || *count == n_files)
        {
          gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (lp->data), FALSE);
          gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (lp->data), count != NULL);
        }
      else
        {
          gtk_toggle_button_set_inconsistent (GTK_TOGGLE_BUTTON (lp->data), TRUE);
        }

      g_signal_handlers_unblock_by_func (lp->data, fmb_emblem_chooser_button_toggled, chooser);
    }
  g_list_free (children);

  g_hash_table_destroy (emblem_names);
}



static void
fmb_emblem_chooser_theme_changed (GtkIconTheme        *icon_theme,
                                     FmbEmblemChooser *chooser)
{
  _fmb_return_if_fail (GTK_IS_ICON_THEME (icon_theme));
  _fmb_return_if_fail (FMB_IS_EMBLEM_CHOOSER (chooser));
  _fmb_return_if_fail (chooser->icon_theme == icon_theme);

  /* drop the current buttons */
  gtk_container_foreach (GTK_CONTAINER (chooser->table), (GtkCallback) gtk_widget_destroy, NULL);

  /* create buttons for the new theme */
  fmb_emblem_chooser_create_buttons (chooser);
}



static void
fmb_emblem_chooser_create_buttons (FmbEmblemChooser *chooser)
{
  GtkWidget *button;
  GList     *emblems;
  GList     *lp;

  /* determine the emblems for the icon theme */
  emblems = gtk_icon_theme_list_icons (chooser->icon_theme, "Emblems");

  /* sort the emblem list */
  emblems = g_list_sort (emblems, (GCompareFunc) g_ascii_strcasecmp);

  /* create buttons for the emblems */
  for (lp = emblems; lp != NULL; lp = lp->next)
    {
      /* skip special emblems, as they cannot be selected */
      if (strcmp (lp->data, FMB_FILE_EMBLEM_NAME_SYMBOLIC_LINK) != 0
          && strcmp (lp->data, FMB_FILE_EMBLEM_NAME_CANT_READ) != 0
          && strcmp (lp->data, FMB_FILE_EMBLEM_NAME_CANT_WRITE) != 0
          && strcmp (lp->data, FMB_FILE_EMBLEM_NAME_DESKTOP) != 0)
        {
          /* create a button and add it to the table */
          button = fmb_emblem_chooser_create_button (chooser, lp->data);
          if (G_LIKELY (button != NULL))
            {
              gtk_container_add (GTK_CONTAINER (chooser->table), button);
              gtk_widget_show (button);
            }
        }
      g_free (lp->data);
    }
  g_list_free (emblems);

  /* be sure to update the buttons according to the selected file */
  if (G_LIKELY (chooser->files != NULL))
    fmb_emblem_chooser_file_changed (chooser);
}



static GtkWidget*
fmb_emblem_chooser_create_button (FmbEmblemChooser *chooser,
                                     const gchar         *emblem)
{
  GtkIconInfo *info;
  const gchar *name;
  GtkWidget   *button = NULL;
  GtkWidget   *image;
  GdkPixbuf   *icon;

  /* lookup the icon info for the emblem */
  info = gtk_icon_theme_lookup_icon (chooser->icon_theme, emblem, 48, 0);
  if (G_UNLIKELY (info == NULL))
    return NULL;

  /* try to load the icon */
  icon = gtk_icon_info_load_icon (info, NULL);
  if (G_UNLIKELY (icon == NULL))
    goto done;

  /* determine the display name for the emblem */
  name = gtk_icon_info_get_display_name (info);
  if (G_UNLIKELY (name == NULL))
    name = (strncmp (emblem, "emblem-", 7) == 0) ? emblem + 7 : emblem;

  /* allocate the button */
  button = gtk_check_button_new ();
  gtk_widget_set_can_focus (button, FALSE);
  g_object_set_data_full (G_OBJECT (button), I_("fmb-emblem"), g_strdup (emblem), g_free);
  g_signal_connect (G_OBJECT (button), "toggled", G_CALLBACK (fmb_emblem_chooser_button_toggled), chooser);

  /* allocate the image */
  image = gtk_image_new_from_pixbuf (icon);
  gtk_container_add (GTK_CONTAINER (button), image);
  gtk_widget_set_tooltip_text (image, name);
  gtk_widget_show (image);

  g_object_unref (G_OBJECT (icon));
done:
  gtk_icon_info_free (info);
  return button;
}



/**
 * fmb_emblem_chooser_new:
 *
 * Allocates a new #FmbEmblemChooser.
 *
 * Return value: the newly allocated #FmbEmblemChooser.
 **/
GtkWidget*
fmb_emblem_chooser_new (void)
{
  return g_object_new (FMB_TYPE_EMBLEM_CHOOSER, NULL);
}



/**
 * fmb_emblem_chooser_get_files:
 * @chooser : a #FmbEmblemChooser.
 *
 * Returns the #FmbFile's associated with
 * the @chooser or %NULL.
 *
 * Return value: the #FmbFile associated
 *               with @chooser.
 **/
GList*
fmb_emblem_chooser_get_files (const FmbEmblemChooser *chooser)
{
  _fmb_return_val_if_fail (FMB_IS_EMBLEM_CHOOSER (chooser), NULL);
  return chooser->files;
}



/**
 * fmb_emblem_chooser_set_file:
 * @chooser : a #FmbEmblemChooser.
 * @file    : a list of #FmbFile's or %NULL.
 *
 * Associates @chooser with @file.
 **/
void
fmb_emblem_chooser_set_files (FmbEmblemChooser *chooser,
                                 GList               *files)
{
  GList *lp;

  _fmb_return_if_fail (FMB_IS_EMBLEM_CHOOSER (chooser));

  if (G_LIKELY (chooser->files != files))
    {
      /* disconnect from the previous file (if any) */
      for (lp = chooser->files; lp != NULL; lp = lp->next)
        {
          g_signal_handlers_disconnect_by_func (G_OBJECT (lp->data), fmb_emblem_chooser_file_changed, chooser);
          g_object_unref (G_OBJECT (lp->data));
        }
      g_list_free (chooser->files);

      /* activate the new file */
      chooser->files = g_list_copy (files);

      /* connect to the new file (if any) */
      for (lp = files; lp != NULL; lp = lp->next)
        {
          g_object_ref (G_OBJECT (lp->data));
          g_signal_connect_swapped (G_OBJECT (lp->data), "changed", G_CALLBACK (fmb_emblem_chooser_file_changed), chooser);
        }

      if (chooser->files != NULL)
        fmb_emblem_chooser_file_changed (chooser);
    }
}


