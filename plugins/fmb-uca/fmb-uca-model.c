/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2005-2006 Benedikt Meurer <benny@xfce.org>
 * Copyright (c) 2009-2012 Jannis Pohlmann <jannis@xfce.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#ifdef HAVE_ERRNO_H
#include <errno.h>
#endif
#ifdef HAVE_LOCALE_H
#include <locale.h>
#endif
#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_PATHS_H
#include <paths.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include <glib/gi18n-lib.h>
#include <glib/gstdio.h>

#include <gio/gio.h>

#include <gtk/gtk.h>

#include <blxo/blxo.h>

#include <libbladeutil/libbladeutil.h>

#include <fmb-uca/fmb-uca-model.h>



/* not all systems define _PATH_BSHELL */
#ifndef _PATH_BSHELL
#define _PATH_BSHELL "/bin/sh"
#endif



typedef struct _FmbUcaModelItem FmbUcaModelItem;



typedef enum
{
  PARSER_START,
  PARSER_ACTIONS,
  PARSER_ACTION,
  PARSER_ICON,
  PARSER_NAME,
  PARSER_UNIQUE_ID,
  PARSER_COMMAND,
  PARSER_STARTUP_NOTIFY,
  PARSER_PATTERNS,
  PARSER_DESCRIPTION,
  PARSER_DIRECTORIES,
  PARSER_AUDIO_FILES,
  PARSER_IMAGE_FILES,
  PARSER_OTHER_FILES,
  PARSER_TEXT_FILES,
  PARSER_VIDEO_FILES,
} ParserState;



static void               fmb_uca_model_tree_model_init  (GtkTreeModelIface    *iface);
static void               fmb_uca_model_finalize         (GObject              *object);
static GtkTreeModelFlags  fmb_uca_model_get_flags        (GtkTreeModel         *tree_model);
static gint               fmb_uca_model_get_n_columns    (GtkTreeModel         *tree_model);
static GType              fmb_uca_model_get_column_type  (GtkTreeModel         *tree_model,
                                                             gint                  column);
static gboolean           fmb_uca_model_get_iter         (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter,
                                                             GtkTreePath          *path);
static GtkTreePath       *fmb_uca_model_get_path         (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter);
static void               fmb_uca_model_get_value        (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter,
                                                             gint                  column,
                                                             GValue               *value);
static gboolean           fmb_uca_model_iter_next        (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter);
static gboolean           fmb_uca_model_iter_children    (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter,
                                                             GtkTreeIter          *parent);
static gboolean           fmb_uca_model_iter_has_child   (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter);
static gint               fmb_uca_model_iter_n_children  (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter);
static gboolean           fmb_uca_model_iter_nth_child   (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter,
                                                             GtkTreeIter          *parent,
                                                             gint                  n);
static gboolean           fmb_uca_model_iter_parent      (GtkTreeModel         *tree_model,
                                                             GtkTreeIter          *iter,
                                                             GtkTreeIter          *child);
static gboolean           fmb_uca_model_load_from_file   (FmbUcaModel       *uca_model,
                                                             const gchar          *filename,
                                                             GError              **error);
static void               fmb_uca_model_item_reset       (FmbUcaModelItem   *item);
static void               fmb_uca_model_item_free        (gpointer              data);
static void               start_element_handler             (GMarkupParseContext  *context,
                                                             const gchar          *element_name,
                                                             const gchar         **attribute_names,
                                                             const gchar         **attribute_values,
                                                             gpointer              user_data,
                                                             GError              **error);
static void               end_element_handler               (GMarkupParseContext  *context,
                                                             const gchar          *element_name,
                                                             gpointer              user_data,
                                                             GError              **error);
static void               text_handler                      (GMarkupParseContext  *context,
                                                             const gchar          *text,
                                                             gsize                 text_len,
                                                             gpointer              user_data,
                                                             GError              **error);



struct _FmbUcaModelClass
{
  GObjectClass __parent__;
};

struct _FmbUcaModel
{
  GObject __parent__;

  GtkIconFactory *icon_factory;

  GList          *items;
  gint            stamp;
};

struct _FmbUcaModelItem
{
  gchar         *name;
  gchar         *description;
  gchar         *unique_id;
  gchar         *icon_name;
  GIcon         *gicon;
  gchar         *command;
  guint          startup_notify : 1;
  gchar        **patterns;
  FmbUcaTypes types;

  /* derived attributes */
  guint          multiple_selection : 1;
};

typedef XFCE_GENERIC_STACK(ParserState) ParserStack;

typedef struct
{
  ParserStack    *stack;
  FmbUcaModel *model;
  gchar          *locale;
  GString        *name;
  gboolean        name_use;
  guint           name_match;
  GString        *unique_id;
  GString        *icon_name;
  GString        *command;
  GString        *patterns;
  GString        *description;
  gboolean        startup_notify;
  gboolean        description_use;
  guint           description_match;
  gboolean        unique_id_generated;
  FmbUcaTypes  types;
} Parser;



static const GMarkupParser markup_parser =
{
  start_element_handler,
  end_element_handler,
  text_handler,
  NULL,
  NULL,
};



FMBX_DEFINE_TYPE_WITH_CODE (FmbUcaModel,
                               fmb_uca_model,
                               G_TYPE_OBJECT,
                               FMBX_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL,
                                                            fmb_uca_model_tree_model_init));



static void
fmb_uca_model_class_init (FmbUcaModelClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_uca_model_finalize;
}



static void
fmb_uca_model_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags = fmb_uca_model_get_flags;
  iface->get_n_columns = fmb_uca_model_get_n_columns;
  iface->get_column_type = fmb_uca_model_get_column_type;
  iface->get_iter = fmb_uca_model_get_iter;
  iface->get_path = fmb_uca_model_get_path;
  iface->get_value = fmb_uca_model_get_value;
  iface->iter_next = fmb_uca_model_iter_next;
  iface->iter_children = fmb_uca_model_iter_children;
  iface->iter_has_child = fmb_uca_model_iter_has_child;
  iface->iter_n_children = fmb_uca_model_iter_n_children;
  iface->iter_nth_child = fmb_uca_model_iter_nth_child;
  iface->iter_parent = fmb_uca_model_iter_parent;
}



static void
fmb_uca_model_init (FmbUcaModel *uca_model)
{
  GError *error = NULL;
  gchar  *filename;

  /* allocate a new icon factory for our action icons
   * and add it to the default icon factories
   */
  uca_model->icon_factory = gtk_icon_factory_new ();
  gtk_icon_factory_add_default (uca_model->icon_factory);

  /* generate a unique stamp */
  uca_model->stamp = g_random_int ();

  /* determine the path to the uca.xml config */
  filename = xfce_resource_lookup (XFCE_RESOURCE_CONFIG, "Fmb/uca.xml");
  if (G_LIKELY (filename != NULL))
    {
      /* try to load the file */
      if (!fmb_uca_model_load_from_file (uca_model, filename, &error))
        {
          g_warning ("Failed to load `%s': %s", filename, error->message);
          g_error_free (error);
        }

      /* release the filename */
      g_free (filename);
    }
}



static void
fmb_uca_model_finalize (GObject *object)
{
  FmbUcaModel *uca_model = FMB_UCA_MODEL (object);

  /* drop our icon factory */
  gtk_icon_factory_remove_default (uca_model->icon_factory);
  g_object_unref (G_OBJECT (uca_model->icon_factory));

  /* release all items */
  g_list_free_full (uca_model->items, fmb_uca_model_item_free);

  (*G_OBJECT_CLASS (fmb_uca_model_parent_class)->finalize) (object);
}



static GtkTreeModelFlags
fmb_uca_model_get_flags (GtkTreeModel *tree_model)
{
  return GTK_TREE_MODEL_LIST_ONLY;
}



static gint
fmb_uca_model_get_n_columns (GtkTreeModel *tree_model)
{
  return FMB_UCA_MODEL_N_COLUMNS;
}



static GType
fmb_uca_model_get_column_type (GtkTreeModel *tree_model,
                                  gint          column)
{
  switch (column)
    {
    case FMB_UCA_MODEL_COLUMN_NAME:
      return G_TYPE_STRING;

    case FMB_UCA_MODEL_COLUMN_UNIQUE_ID:
      return G_TYPE_STRING;

    case FMB_UCA_MODEL_COLUMN_DESCRIPTION:
      return G_TYPE_STRING;

    case FMB_UCA_MODEL_COLUMN_GICON:
      return G_TYPE_ICON;

    case FMB_UCA_MODEL_COLUMN_ICON_NAME:
      return G_TYPE_STRING;

    case FMB_UCA_MODEL_COLUMN_COMMAND:
      return G_TYPE_STRING;

    case FMB_UCA_MODEL_COLUMN_STARTUP_NOTIFY:
      return G_TYPE_BOOLEAN;

    case FMB_UCA_MODEL_COLUMN_PATTERNS:
      return G_TYPE_STRING;

    case FMB_UCA_MODEL_COLUMN_TYPES:
      return G_TYPE_UINT;

    case FMB_UCA_MODEL_COLUMN_STOCK_LABEL:
      return G_TYPE_STRING;

    default:
      g_assert_not_reached ();
      return G_TYPE_INVALID;
    }
}



static gboolean
fmb_uca_model_get_iter (GtkTreeModel *tree_model,
                           GtkTreeIter  *iter,
                           GtkTreePath  *path)
{
  FmbUcaModel *uca_model = FMB_UCA_MODEL (tree_model);

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), FALSE);
  g_return_val_if_fail (gtk_tree_path_get_depth (path) > 0, FALSE);

  iter->stamp = uca_model->stamp;
  iter->user_data = g_list_nth (uca_model->items, gtk_tree_path_get_indices (path)[0]);

  return (iter->user_data != NULL);
}



static GtkTreePath*
fmb_uca_model_get_path (GtkTreeModel *tree_model,
                           GtkTreeIter  *iter)
{
  FmbUcaModel *uca_model = FMB_UCA_MODEL (tree_model);
  gint            idx;

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), NULL);
  g_return_val_if_fail (iter->stamp == uca_model->stamp, NULL);

  /* determine the index of the iter */
  idx = g_list_position (uca_model->items, iter->user_data);
  if (G_UNLIKELY (idx < 0))
    return NULL;

  return gtk_tree_path_new_from_indices (idx, -1);
}



static void
fmb_uca_model_get_value (GtkTreeModel *tree_model,
                            GtkTreeIter  *iter,
                            gint          column,
                            GValue       *value)
{
  FmbUcaModelItem *item = ((GList *) iter->user_data)->data;
  FmbUcaModel     *uca_model = FMB_UCA_MODEL (tree_model);
  gchar              *str;

  g_return_if_fail (FMB_UCA_IS_MODEL (uca_model));
  g_return_if_fail (iter->stamp == uca_model->stamp);

  /* initialize the value with the proper type */
  g_value_init (value, gtk_tree_model_get_column_type (tree_model, column));

  switch (column)
    {
    case FMB_UCA_MODEL_COLUMN_NAME:
      g_value_set_static_string (value, item->name ? item->name : "");
      break;

    case FMB_UCA_MODEL_COLUMN_DESCRIPTION:
      g_value_set_static_string (value, item->description);
      break;

    case FMB_UCA_MODEL_COLUMN_GICON:
      if (item->gicon == NULL && item->icon_name != NULL)
        {
          /* cache gicon from the name */
          item->gicon = g_icon_new_for_string (item->icon_name, NULL);
        }

      g_value_set_object (value, item->gicon);
      break;

    case FMB_UCA_MODEL_COLUMN_ICON_NAME:
      g_value_set_static_string (value, item->icon_name);
      break;

    case FMB_UCA_MODEL_COLUMN_UNIQUE_ID:
      g_value_set_static_string (value, item->unique_id);
      break;

    case FMB_UCA_MODEL_COLUMN_COMMAND:
      g_value_set_static_string (value, item->command);
      break;

    case FMB_UCA_MODEL_COLUMN_STARTUP_NOTIFY:
      g_value_set_boolean (value, item->startup_notify);
      break;

    case FMB_UCA_MODEL_COLUMN_PATTERNS:
      str = g_strjoinv (";", item->patterns);
      g_value_take_string (value, str);
      break;

    case FMB_UCA_MODEL_COLUMN_TYPES:
      g_value_set_uint (value, item->types);
      break;

    case FMB_UCA_MODEL_COLUMN_STOCK_LABEL:
      str = g_markup_printf_escaped ("<b>%s</b>\n%s", (item->name != NULL) ? item->name : "", (item->description != NULL) ? item->description : "");
      g_value_take_string (value, str);
      break;

    default:
      g_assert_not_reached ();
    }
}



static gboolean
fmb_uca_model_iter_next (GtkTreeModel *tree_model,
                            GtkTreeIter  *iter)
{
  g_return_val_if_fail (FMB_UCA_IS_MODEL (tree_model), FALSE);
  g_return_val_if_fail (iter->stamp == FMB_UCA_MODEL (tree_model)->stamp, FALSE);

  iter->user_data = g_list_next (iter->user_data);
  return (iter->user_data != NULL);
}



static gboolean
fmb_uca_model_iter_children (GtkTreeModel *tree_model,
                                GtkTreeIter  *iter,
                                GtkTreeIter  *parent)
{
  FmbUcaModel *uca_model = FMB_UCA_MODEL (tree_model);

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), FALSE);

  if (G_LIKELY (parent == NULL && uca_model->items != NULL))
    {
      iter->stamp = uca_model->stamp;
      iter->user_data = uca_model->items;
      return TRUE;
    }

  return FALSE;
}



static gboolean
fmb_uca_model_iter_has_child (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter)
{
  return FALSE;
}



static gint
fmb_uca_model_iter_n_children (GtkTreeModel *tree_model,
                                  GtkTreeIter  *iter)
{
  FmbUcaModel *uca_model = FMB_UCA_MODEL (tree_model);

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), 0);

  return (iter == NULL) ? g_list_length (uca_model->items) : 0;
}



static gboolean
fmb_uca_model_iter_nth_child (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter,
                                 GtkTreeIter  *parent,
                                 gint          n)
{
  FmbUcaModel *uca_model = FMB_UCA_MODEL (tree_model);

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), FALSE);

  if (G_LIKELY (parent != NULL))
    {
      iter->stamp = uca_model->stamp;
      iter->user_data = g_list_nth (uca_model->items, n);
      return (iter->user_data != NULL);
    }

  return FALSE;
}



static gboolean
fmb_uca_model_iter_parent (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter,
                              GtkTreeIter  *child)
{
  return FALSE;
}



static gboolean
fmb_uca_model_load_from_file (FmbUcaModel *uca_model,
                                 const gchar    *filename,
                                 GError        **error)
{
  GMarkupParseContext *context;
  gboolean             succeed;
  Parser               parser;
  gchar               *content;
  gsize                content_len;

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);
  g_return_val_if_fail (g_path_is_absolute (filename), FALSE);

  /* read the file info memory */
  if (!g_file_get_contents (filename, &content, &content_len, error))
    return FALSE;

  /* initialize the parser */
  parser.stack = xfce_stack_new (ParserStack);
  parser.model = uca_model;
  parser.locale = g_strdup (setlocale (LC_MESSAGES, NULL));
  parser.name = g_string_new (NULL);
  parser.unique_id = g_string_new (NULL);
  parser.icon_name = g_string_new (NULL);
  parser.command = g_string_new (NULL);
  parser.patterns = g_string_new (NULL);
  parser.description = g_string_new (NULL);
  parser.startup_notify = FALSE;
  parser.unique_id_generated = FALSE;
  xfce_stack_push (parser.stack, PARSER_START);

  /* parse the file */
  context = g_markup_parse_context_new (&markup_parser, 0, &parser, NULL);
  succeed = g_markup_parse_context_parse (context, content, content_len, error)
         && g_markup_parse_context_end_parse (context, error);

  /* cleanup */
  g_markup_parse_context_free (context);
  g_string_free (parser.description, TRUE);
  g_string_free (parser.patterns, TRUE);
  g_string_free (parser.command, TRUE);
  g_string_free (parser.icon_name, TRUE);
  g_string_free (parser.unique_id, TRUE);
  g_string_free (parser.name, TRUE);
  g_free (parser.locale);
  xfce_stack_free (parser.stack);
  g_free (content);

  /* save model to store new ids */
  if (succeed
      && parser.unique_id_generated)
    succeed = fmb_uca_model_save (uca_model, error);

  return succeed;
}



static void
fmb_uca_model_item_reset (FmbUcaModelItem *item)
{
  /* release the previous values... */
  g_strfreev (item->patterns);
  g_free (item->description);
  g_free (item->command);
  g_free (item->name);
  g_free (item->unique_id);
  g_free (item->icon_name);

  if (item->gicon != NULL)
    g_object_unref (item->gicon);

  /* ...and reset the item memory */
  memset (item, 0, sizeof (*item));
}



static void
fmb_uca_model_item_free (gpointer data)
{
  FmbUcaModelItem *item = data;
  fmb_uca_model_item_reset (item);
  g_free (item);
}



static void
start_element_handler (GMarkupParseContext *context,
                       const gchar         *element_name,
                       const gchar        **attribute_names,
                       const gchar        **attribute_values,
                       gpointer             user_data,
                       GError             **error)
{
  Parser *parser = user_data;
  guint   match, n;

  switch (xfce_stack_top (parser->stack))
    {
    case PARSER_START:
      if (strcmp (element_name, "actions") == 0)
        xfce_stack_push (parser->stack, PARSER_ACTIONS);
      else
        goto unknown_element;
      break;

    case PARSER_ACTIONS:
      if (strcmp (element_name, "action") == 0)
        {
          parser->name_match = XFCE_LOCALE_NO_MATCH;
          parser->description_match = XFCE_LOCALE_NO_MATCH;
          parser->types = 0;
          parser->startup_notify = FALSE;
          g_string_truncate (parser->icon_name, 0);
          g_string_truncate (parser->name, 0);
          g_string_truncate (parser->unique_id, 0);
          g_string_truncate (parser->command, 0);
          g_string_truncate (parser->patterns, 0);
          g_string_truncate (parser->description, 0);
          xfce_stack_push (parser->stack, PARSER_ACTION);
        }
      else
        goto unknown_element;
      break;

    case PARSER_ACTION:
      if (strcmp (element_name, "name") == 0)
        {
          for (n = 0; attribute_names[n] != NULL; ++n)
            if (strcmp (attribute_names[n], "xml:lang") == 0)
              break;

          if (G_LIKELY (attribute_names[n] == NULL))
            {
              parser->name_use = (parser->name_match <= XFCE_LOCALE_NO_MATCH);
            }
          else
            {
              match = xfce_locale_match (parser->locale, attribute_values[n]);
              if (parser->name_match < match)
                {
                  parser->name_match = match;
                  parser->name_use = TRUE;
                }
              else
                {
                  parser->name_use = FALSE;
                }
            }

          if (parser->name_use)
            g_string_truncate (parser->name, 0);

          xfce_stack_push (parser->stack, PARSER_NAME);
        }
      else if (strcmp (element_name, "unique-id") == 0)
        {
          g_string_truncate (parser->unique_id, 0);
          xfce_stack_push (parser->stack, PARSER_UNIQUE_ID);
        }
      else if (strcmp (element_name, "icon") == 0)
        {
          g_string_truncate (parser->icon_name, 0);
          xfce_stack_push (parser->stack, PARSER_ICON);
        }
      else if (strcmp (element_name, "command") == 0)
        {
          g_string_truncate (parser->command, 0);
          xfce_stack_push (parser->stack, PARSER_COMMAND);
        }
      else if (strcmp (element_name, "patterns") == 0)
        {
          g_string_truncate (parser->patterns, 0);
          xfce_stack_push (parser->stack, PARSER_PATTERNS);
        }
      else if (strcmp (element_name, "description") == 0)
        {
          for (n = 0; attribute_names[n] != NULL; ++n)
            if (strcmp (attribute_names[n], "xml:lang") == 0)
              break;

          if (G_LIKELY (attribute_names[n] == NULL))
            {
              parser->description_use = (parser->description_match <= XFCE_LOCALE_NO_MATCH);
            }
          else
            {
              match = xfce_locale_match (parser->locale, attribute_values[n]);
              if (parser->description_match < match)
                {
                  parser->description_match = match;
                  parser->description_use = TRUE;
                }
              else
                {
                  parser->description_use = FALSE;
                }
            }

          if (parser->description_use)
            g_string_truncate (parser->description, 0);

          xfce_stack_push (parser->stack, PARSER_DESCRIPTION);
        }
      else if (strcmp (element_name, "startup-notify") == 0)
        {
          parser->startup_notify = TRUE;
          xfce_stack_push (parser->stack, PARSER_STARTUP_NOTIFY);
        }
      else if (strcmp (element_name, "directories") == 0)
        {
          parser->types |= FMB_UCA_TYPE_DIRECTORIES;
          xfce_stack_push (parser->stack, PARSER_DIRECTORIES);
        }
      else if (strcmp (element_name, "audio-files") == 0)
        {
          parser->types |= FMB_UCA_TYPE_AUDIO_FILES;
          xfce_stack_push (parser->stack, PARSER_AUDIO_FILES);
        }
      else if (strcmp (element_name, "image-files") == 0)
        {
          parser->types |= FMB_UCA_TYPE_IMAGE_FILES;
          xfce_stack_push (parser->stack, PARSER_IMAGE_FILES);
        }
      else if (strcmp (element_name, "other-files") == 0)
        {
          parser->types |= FMB_UCA_TYPE_OTHER_FILES;
          xfce_stack_push (parser->stack, PARSER_OTHER_FILES);
        }
      else if (strcmp (element_name, "text-files") == 0)
        {
          parser->types |= FMB_UCA_TYPE_TEXT_FILES;
          xfce_stack_push (parser->stack, PARSER_TEXT_FILES);
        }
      else if (strcmp (element_name, "video-files") == 0)
        {
          parser->types |= FMB_UCA_TYPE_VIDEO_FILES;
          xfce_stack_push (parser->stack, PARSER_VIDEO_FILES);
        }
      else
        goto unknown_element;
      break;

    default:
      goto unknown_element;
    }

  return;

unknown_element:
  g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
               _("Unknown element <%s>"), element_name);
}



static void
end_element_handler (GMarkupParseContext *context,
                     const gchar         *element_name,
                     gpointer             user_data,
                     GError             **error)
{
  GtkTreeIter iter;
  Parser     *parser = user_data;

  switch (xfce_stack_top (parser->stack))
    {
    case PARSER_START:
      g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_INVALID_CONTENT,
                   _("End element handler called while in root context"));
      return;

    case PARSER_ACTIONS:
      if (strcmp (element_name, "actions") != 0)
        goto unknown_element;
      break;

    case PARSER_ACTION:
      if (strcmp (element_name, "action") == 0)
        {
          fmb_uca_model_append (parser->model, &iter);
          fmb_uca_model_update (parser->model, &iter,
                                   parser->name->str,
                                   parser->unique_id->str,
                                   parser->description->str,
                                   parser->icon_name->str,
                                   parser->command->str,
                                   parser->startup_notify,
                                   parser->patterns->str,
                                   parser->types);

          /* check if a new id should've been generated */
          if (blxo_str_is_empty (parser->unique_id->str))
            parser->unique_id_generated = TRUE;
        }
      else
        goto unknown_element;
      break;

    case PARSER_NAME:
      if (strcmp (element_name, "name") != 0)
        goto unknown_element;
      break;

    case PARSER_UNIQUE_ID:
      if (strcmp (element_name, "unique-id") != 0)
        goto unknown_element;
      break;

    case PARSER_ICON:
      if (strcmp (element_name, "icon") != 0)
        goto unknown_element;
      break;

    case PARSER_COMMAND:
      if (strcmp (element_name, "command") != 0)
        goto unknown_element;
      break;

    case PARSER_PATTERNS:
      if (strcmp (element_name, "patterns") != 0)
        goto unknown_element;
      break;

    case PARSER_DESCRIPTION:
      if (strcmp (element_name, "description") != 0)
        goto unknown_element;
      break;

    case PARSER_STARTUP_NOTIFY:
      if (strcmp (element_name, "startup-notify") != 0)
        goto unknown_element;
      break;

    case PARSER_DIRECTORIES:
      if (strcmp (element_name, "directories") != 0)
        goto unknown_element;
      break;

    case PARSER_AUDIO_FILES:
      if (strcmp (element_name, "audio-files") != 0)
        goto unknown_element;
      break;

    case PARSER_IMAGE_FILES:
      if (strcmp (element_name, "image-files") != 0)
        goto unknown_element;
      break;

    case PARSER_OTHER_FILES:
      if (strcmp (element_name, "other-files") != 0)
        goto unknown_element;
      break;

    case PARSER_TEXT_FILES:
      if (strcmp (element_name, "text-files") != 0)
        goto unknown_element;
      break;

    case PARSER_VIDEO_FILES:
      if (strcmp (element_name, "video-files") != 0)
        goto unknown_element;
      break;

    default:
      goto unknown_element;
    }

  xfce_stack_pop (parser->stack);
  return;

unknown_element:
  g_set_error (error, G_MARKUP_ERROR, G_MARKUP_ERROR_UNKNOWN_ELEMENT,
               _("Unknown closing element <%s>"), element_name);
}



static void
text_handler (GMarkupParseContext *context,
              const gchar         *text,
              gsize                text_len,
              gpointer             user_data,
              GError             **error)
{
  Parser *parser = user_data;

  switch (xfce_stack_top (parser->stack))
    {
    case PARSER_NAME:
      if (parser->name_use)
        g_string_append_len (parser->name, text, text_len);
      break;

    case PARSER_UNIQUE_ID:
      g_string_append_len (parser->unique_id, text, text_len);
      break;

    case PARSER_ICON:
      g_string_append_len (parser->icon_name, text, text_len);
      break;

    case PARSER_COMMAND:
      g_string_append_len (parser->command, text, text_len);
      break;

    case PARSER_PATTERNS:
      g_string_append_len (parser->patterns, text, text_len);
      break;

    case PARSER_DESCRIPTION:
      if (parser->description_use)
        g_string_append_len (parser->description, text, text_len);
      break;

    default:
      break;
    }
}



static gchar *
fmb_uca_model_get_unique_id (void)
{
  static guint counter = 0;

  /* produce a "<timestamp>-<counter>" string */
  return g_strdup_printf ("%" G_GINT64_FORMAT "-%d",
                          g_get_real_time (),
                          ++counter);
}



/**
 * fmb_uca_model_get_default:
 *
 * Returns a reference to the default shared
 * #FmbUcaModel instance. The caller is 
 * responsible to free the returned instance
 * using g_object_unref() when no longer 
 * needed.
 *
 * Return value: a reference to the default
 *               #FmbUcaModel instance.
 **/
FmbUcaModel*
fmb_uca_model_get_default (void)
{
  static FmbUcaModel *model = NULL;

  if (model == NULL)
    {
      model = g_object_new (FMB_UCA_TYPE_MODEL, NULL);
      g_object_add_weak_pointer (G_OBJECT (model), (gpointer) &model);
    }
  else
    {
      g_object_ref (G_OBJECT (model));
    }

  return model;
}



static inline FmbUcaTypes
types_from_mime_type (const gchar *mime_type)
{
  if (mime_type == NULL)
    return 0;
  if (strcmp (mime_type, "inode/directory") == 0)
    return FMB_UCA_TYPE_DIRECTORIES;
  else if (strncmp (mime_type, "audio/", 6) == 0)
    return FMB_UCA_TYPE_AUDIO_FILES;
  else if (strncmp (mime_type, "image/", 6) == 0)
    return FMB_UCA_TYPE_IMAGE_FILES;
  else if (strncmp (mime_type, "text/", 5) == 0)
    return FMB_UCA_TYPE_TEXT_FILES;
  else if (strncmp (mime_type, "video/", 6) == 0)
    return FMB_UCA_TYPE_VIDEO_FILES;
  else if (strncmp (mime_type, "application/", 12) == 0)
    {
      /* quite cumbersome, certain mime types do not
       * belong here, but despite that fact, they are...
       */
      mime_type += 12;
      if (strcmp (mime_type, "javascript") == 0
          || strcmp (mime_type, "x-awk") == 0
          || strcmp (mime_type, "x-csh") == 0
          || strcmp (mime_type, "xhtml+xml") == 0
          || strcmp (mime_type, "xml") == 0)
        return FMB_UCA_TYPE_TEXT_FILES;
      else if (strcmp (mime_type, "ogg") == 0)
        return FMB_UCA_TYPE_AUDIO_FILES;
    }
  return 0;
}



/**
 * fmb_uca_model_match:
 * @uca_model  : a #FmbUcaModel.
 * @file_infos : a #GList of #FmbxFileInfo<!---->s.
 *
 * Returns the #GtkTreePath<!---->s for the items in @uca_model,
 * that are defined for the #FmbxFileInfo<!---->s in the
 * @file_infos.
 *
 * The caller is responsible to free the returned list using
 * <informalexample><programlisting>
 * g_list_free_full (list, (GDestroyNotify) gtk_tree_path_free);
 * </programlisting></informalexample>
 *
 * Return value: the list of #GtkTreePath<!---->s to items
 *               that match @file_infos.
 **/
GList*
fmb_uca_model_match (FmbUcaModel *uca_model,
                        GList          *file_infos)
{
  typedef struct
  {
    gchar          *name;
    FmbUcaTypes  types;
  } FmbUcaFile;

  FmbUcaModelItem *item;
  FmbUcaFile      *files;
  GFile              *location;
  gchar              *mime_type;
  gboolean            matches;
  GList              *paths = NULL;
  GList              *lp;
  gint                n_files;
  gint                i, m, n;
  gchar              *path_test;

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), NULL);
  g_return_val_if_fail (file_infos != NULL, NULL);

  /* special case to avoid overhead */
  if (G_UNLIKELY (uca_model->items == NULL))
    return NULL;

  /* determine the FmbUcaFile's for the given file_infos */
  n_files = g_list_length (file_infos);
  files = g_new (FmbUcaFile, n_files);
  for (lp = file_infos, n = 0; lp != NULL; lp = lp->next, ++n)
    {
      location = fmbx_file_info_get_location (lp->data);

      path_test = g_file_get_path (location);
      if (path_test == NULL)
        {
          /* cannot handle non-local files */
          g_object_unref (location);
          g_free (files);
          return NULL;
        }
      g_free (path_test);

      g_object_unref (location);

      mime_type = fmbx_file_info_get_mime_type (lp->data);

      files[n].name = fmbx_file_info_get_name (lp->data);
      files[n].types = types_from_mime_type (mime_type);

      if (G_UNLIKELY (files[n].types == 0))
        files[n].types = FMB_UCA_TYPE_OTHER_FILES;

      g_free (mime_type);
    }

  /* lookup the matching items */
  for (i = 0, lp = uca_model->items; lp != NULL; ++i, lp = lp->next)
    {
      /* check if we can just ignore this item */
      item = (FmbUcaModelItem *) lp->data;
      if (!item->multiple_selection && n_files > 1)
        continue;

      /* match the specified files */
      for (n = 0; n < n_files; ++n)
        {
          /* verify that we support this type of file */
          if ((files[n].types & item->types) == 0)
            break;

          /* atleast on pattern must match the file name */
          for (m = 0, matches = FALSE; item->patterns[m] != NULL && !matches; ++m)
            matches = g_pattern_match_simple (item->patterns[m], files[n].name);

          /* no need to continue if none of the patterns match */
          if (!matches)
            break;
        }

      /* add the path if all files match one of the patterns */
      if (G_UNLIKELY (n == n_files))
        paths = g_list_append (paths, gtk_tree_path_new_from_indices (i, -1));
    }

  /* cleanup */
  for (n = 0; n < n_files; ++n)
    g_free (files[n].name);
  g_free (files);

  return paths;
}



/**
 * fmb_uca_model_append:
 * @uca_model : a #FmbUcaModel.
 * @iter      : return location for the new #GtkTreeIter.
 *
 * Appends a new item to the @uca_model and returns the
 * @iter of the new item.
 **/
void
fmb_uca_model_append (FmbUcaModel *uca_model,
                         GtkTreeIter    *iter)
{
  FmbUcaModelItem *item;
  GtkTreePath        *path;

  g_return_if_fail (FMB_UCA_IS_MODEL (uca_model));
  g_return_if_fail (iter != NULL);

  /* append the new item */
  item = g_new0 (FmbUcaModelItem, 1);
  uca_model->items = g_list_append (uca_model->items, item);

  /* determine the tree iter of the new item */
  iter->stamp = uca_model->stamp;
  iter->user_data = g_list_last (uca_model->items);

  /* notify listeners about the new item */
  path = gtk_tree_model_get_path (GTK_TREE_MODEL (uca_model), iter);
  gtk_tree_model_row_inserted (GTK_TREE_MODEL (uca_model), path, iter);
  gtk_tree_path_free (path);
}



/**
 * fmb_uca_model_exchange:
 * @uca_model : a #FmbUcaModel.
 * @iter_a    : a #GtkTreeIter.
 * @iter_b    : a #GtkTreeIter.
 *
 * Exchanges the items at @iter_a and @iter_b in
 * @uca_model.
 **/
void
fmb_uca_model_exchange (FmbUcaModel *uca_model,
                           GtkTreeIter    *iter_a,
                           GtkTreeIter    *iter_b)
{
  FmbUcaModelItem *item;
  GtkTreePath        *path;
  GList              *list_a = iter_a->user_data;
  GList              *list_b = iter_b->user_data;
  gint               *new_order;
  gint                n_items;
  gint                n;

  g_return_if_fail (FMB_UCA_IS_MODEL (uca_model));
  g_return_if_fail (iter_a->stamp == uca_model->stamp);
  g_return_if_fail (iter_b->stamp == uca_model->stamp);

  /* allocate and initialize the new order array */
  n_items = g_list_length (uca_model->items);
  new_order = g_newa (gint, n_items);
  for (n = 0; n < n_items; ++n)
    new_order[n] = n;

  /* change new_order appropriately */
  new_order[g_list_position (uca_model->items, list_a)] = g_list_position (uca_model->items, list_b);
  new_order[g_list_position (uca_model->items, list_b)] = g_list_position (uca_model->items, list_a);

  /* perform the exchange */
  item = list_a->data;
  list_a->data = list_b->data;
  list_b->data = item;

  /* notify listeners about the new order */
  path = gtk_tree_path_new ();
  gtk_tree_model_rows_reordered (GTK_TREE_MODEL (uca_model), path, NULL, new_order);
  gtk_tree_path_free (path);
}



/**
 * fmb_uca_model_remove:
 * @uca_model : a #FmbUcaModel.
 * @iter      : a #GtkTreeIter.
 *
 * Removes the item at the given @iter from the
 * @uca_model.
 **/
void
fmb_uca_model_remove (FmbUcaModel *uca_model,
                         GtkTreeIter    *iter)
{
  FmbUcaModelItem *item;
  GtkTreePath        *path;

  g_return_if_fail (FMB_UCA_IS_MODEL (uca_model));
  g_return_if_fail (iter->stamp == uca_model->stamp);

  /* determine the path for the item to remove */
  path = gtk_tree_model_get_path (GTK_TREE_MODEL (uca_model), iter);

  /* remove the node from the list */
  item = ((GList *) iter->user_data)->data;
  uca_model->items = g_list_delete_link (uca_model->items, iter->user_data);
  fmb_uca_model_item_free (item);

  /* notify listeners */
  gtk_tree_model_row_deleted (GTK_TREE_MODEL (uca_model), path);

  /* cleanup */
  gtk_tree_path_free (path);
}



/**
 * fmb_uca_model_update:
 * @uca_model          : a #FmbUcaModel.
 * @iter               : the #GtkTreeIter of the item to update.
 * @name               : the name of the item.
 * @unique_id          : a unique ID for the item.
 * @description        : the description of the item.
 * @icon               : the icon for the item.
 * @command            : the command of the item.
 * @patterns           : the patterns to match files for this item.
 * @types              : where to apply this item.
 *
 * Updates the @uca_model item at @iter with the given values.
 **/
void
fmb_uca_model_update (FmbUcaModel *uca_model,
                         GtkTreeIter    *iter,
                         const gchar    *name,
                         const gchar    *unique_id,
                         const gchar    *description,
                         const gchar    *icon,
                         const gchar    *command,
                         gboolean        startup_notify,
                         const gchar    *patterns,
                         FmbUcaTypes  types)
{
  FmbUcaModelItem *item;
  GtkTreePath        *path;
  guint               n, m;

  g_return_if_fail (FMB_UCA_IS_MODEL (uca_model));
  g_return_if_fail (iter->stamp == uca_model->stamp);

  /* reset the previous item values */
  item = ((GList *) iter->user_data)->data;
  fmb_uca_model_item_reset (item);

  /* setup the new item values */
  if (G_LIKELY (name != NULL && *name != '\0'))
    item->name = g_strdup (name);
  if (G_LIKELY (icon != NULL && *icon != '\0'))
    item->icon_name = g_strdup (icon);
  if (G_LIKELY (command != NULL && *command != '\0'))
    item->command = g_strdup (command);
  if (G_LIKELY (description != NULL && *description != '\0'))
    item->description = g_strdup (description);
  item->types = types;
  item->startup_notify = startup_notify;

  /* set the unique id once */
  if (item->unique_id == NULL)
    {
      if (G_LIKELY (unique_id != NULL && *unique_id != '\0'))
        item->unique_id = g_strdup (unique_id);
      else
        item->unique_id = fmb_uca_model_get_unique_id ();
    }

  /* setup the patterns */
  item->patterns = g_strsplit ((patterns != NULL && *patterns != '\0') ? patterns : "*", ";", -1);
  for (m = n = 0; item->patterns[m] != NULL; ++m)
    {
      if (G_UNLIKELY (item->patterns[m][0] == '\0'))
        g_free (item->patterns[m]);
      else
        item->patterns[n++] = g_strstrip (item->patterns[m]);
    }
  item->patterns[n] = NULL;

  /* check if this item will work for multiple files */
  item->multiple_selection = (command != NULL && (strstr (command, "%F") != NULL
                                               || strstr (command, "%D") != NULL
                                               || strstr (command, "%N") != NULL
                                               || strstr (command, "%U") != NULL));

  /* notify listeners about the changed item */
  path = gtk_tree_model_get_path (GTK_TREE_MODEL (uca_model), iter);
  gtk_tree_model_row_changed (GTK_TREE_MODEL (uca_model), path, iter);
  gtk_tree_path_free (path);
}



/**
 * fmb_uca_model_save:
 * @uca_model : a #FmbUcaModel.
 * @error     : return location for errors or %NULL.
 *
 * Instructs @uca_model to sync its current state to
 * persistent storage.
 *
 * Return value: %TRUE on success, %FALSE if @error is set.
 **/
gboolean
fmb_uca_model_save (FmbUcaModel *uca_model,
                       GError        **error)
{
  FmbUcaModelItem *item;
  gboolean            result = FALSE;
  gchar              *tmp_path;
  gchar              *patterns;
  gchar              *escaped;
  gchar              *path;
  GList              *lp;
  FILE               *fp;
  gint                fd;

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* determine the save location */
  path = xfce_resource_save_location (XFCE_RESOURCE_CONFIG, "Fmb/uca.xml", TRUE);
  if (G_UNLIKELY (path == NULL))
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_IO, _("Failed to determine save location for uca.xml"));
      return FALSE;
    }

  /* try to open a temporary file */
  tmp_path = g_strconcat (path, ".XXXXXX", NULL);
  fd = g_mkstemp (tmp_path);
  if (G_UNLIKELY (fd < 0))
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s", g_strerror (errno));
      goto done;
    }

  /* wrap the file descriptor into a file pointer */
  fp = fdopen (fd, "w");

  /* write the header */
  fprintf (fp, "<?xml encoding=\"UTF-8\" version=\"1.0\"?>\n<actions>\n");

  /* write the model items */
  for (lp = uca_model->items; lp != NULL; lp = lp->next)
    {
      item = (FmbUcaModelItem *) lp->data;
      fprintf (fp, "<action>\n");
      patterns = g_strjoinv (";", item->patterns);
      escaped = g_markup_printf_escaped ("\t<icon>%s</icon>\n"
                                         "\t<name>%s</name>\n"
                                         "\t<unique-id>%s</unique-id>\n"
                                         "\t<command>%s</command>\n"
                                         "\t<description>%s</description>\n"
                                         "\t<patterns>%s</patterns>\n",
                                         (item->icon_name != NULL) ? item->icon_name : "",
                                         (item->name != NULL) ? item->name : "",
                                         (item->unique_id != NULL) ? item->unique_id : "",
                                         (item->command != NULL) ? item->command : "",
                                         (item->description != NULL) ? item->description : "",
                                         patterns);
      fprintf (fp, "%s", escaped);
      g_free (patterns);
      g_free (escaped);
      if (item->startup_notify)
        fprintf (fp, "\t<startup-notify/>\n");
      if ((item->types & FMB_UCA_TYPE_DIRECTORIES) != 0)
        fprintf (fp, "\t<directories/>\n");
      if ((item->types & FMB_UCA_TYPE_AUDIO_FILES) != 0)
        fprintf (fp, "\t<audio-files/>\n");
      if ((item->types & FMB_UCA_TYPE_IMAGE_FILES) != 0)
        fprintf (fp, "\t<image-files/>\n");
      if ((item->types & FMB_UCA_TYPE_OTHER_FILES) != 0)
        fprintf (fp, "\t<other-files/>\n");
      if ((item->types & FMB_UCA_TYPE_TEXT_FILES) != 0)
        fprintf (fp, "\t<text-files/>\n");
      if ((item->types & FMB_UCA_TYPE_VIDEO_FILES) != 0)
        fprintf (fp, "\t<video-files/>\n");
      fprintf (fp, "</action>\n");
    }

  /* write the footer and close the tmp file */
  fprintf (fp, "</actions>\n");
  fclose (fp);

  /* rename the file (atomic) */
  if (g_rename (tmp_path, path) < 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno), "%s", g_strerror (errno));
      g_unlink (tmp_path);
    }
  else
    {
      /* yeppa, successful */
      result = TRUE;
    }

done:
  /* cleanup */
  g_free (tmp_path);
  g_free (path);

  return result;
}



/**
 * fmb_uca_model_parse_argv:
 * @uca_model   : a #FmbUcaModel.
 * @iter        : the #GtkTreeIter of the item.
 * @file_infos  : the #GList of #FmbxFileInfo<!---->s to pass to the item.
 * @argcp       : return location for number of args.
 * @argvp       : return location for array of args.
 * @error       : return location for errors or %NULL.
 *
 * Return value: %TRUE on success, %FALSE if @error is set.
 **/
gboolean
fmb_uca_model_parse_argv (FmbUcaModel *uca_model,
                             GtkTreeIter    *iter,
                             GList          *file_infos,
                             gint           *argcp,
                             gchar        ***argvp,
                             GError        **error)
{
  FmbUcaModelItem *item;
  const gchar        *p;
  GString            *command_line = g_string_new (NULL);
  GList              *lp;
  gchar              *dirname;
  gchar              *quoted;
  gchar              *path;
  gchar              *uri;
  GFile              *location;

  g_return_val_if_fail (FMB_UCA_IS_MODEL (uca_model), FALSE);
  g_return_val_if_fail (iter->stamp == uca_model->stamp, FALSE);
  g_return_val_if_fail (error == NULL || *error == NULL, FALSE);

  /* verify that a command is set for the item */
  item = (FmbUcaModelItem *) ((GList *) iter->user_data)->data;
  if (item->command == NULL || *item->command == '\0')
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_INVAL, _("Command not configured"));
      goto error;
    }

  /* generate the command line */
  for (p = item->command; *p != '\0'; ++p)
    {
      if (p[0] == '%' && p[1] != '\0')
        {
          switch (*++p)
            {
            case 'f':
              if (G_LIKELY (file_infos != NULL))
                {
                  location = fmbx_file_info_get_location (file_infos->data);
                  path = g_file_get_path (location);
                  g_object_unref (location);

                  if (G_UNLIKELY (path == NULL))
                    goto error;

                  quoted = g_shell_quote (path);
                  g_string_append (command_line, quoted);
                  g_free (quoted);
                  g_free (path);
                }
              break;

            case 'F':
              for (lp = file_infos; lp != NULL; lp = lp->next)
                {
                  if (G_LIKELY (lp != file_infos))
                    g_string_append_c (command_line, ' ');

                  location = fmbx_file_info_get_location (lp->data);
                  path = g_file_get_path (location);
                  g_object_unref (location);

                  if (G_UNLIKELY (path == NULL))
                    goto error;

                  quoted = g_shell_quote (path);
                  g_string_append (command_line, quoted);
                  g_free (quoted);
                  g_free (path);
                }
              break;

            case 'u':
              if (G_LIKELY (file_infos != NULL))
                {
                  uri = fmbx_file_info_get_uri (file_infos->data);
                  quoted = g_shell_quote (uri);
                  g_string_append (command_line, quoted);
                  g_free (quoted);
                  g_free (uri);
                }
              break;

            case 'U':
              for (lp = file_infos; lp != NULL; lp = lp->next)
                {
                  if (G_LIKELY (lp != file_infos))
                    g_string_append_c (command_line, ' ');

                  uri = fmbx_file_info_get_uri (lp->data);
                  quoted = g_shell_quote (uri);
                  g_string_append (command_line, quoted);
                  g_free (quoted);
                  g_free (uri);
                }
              break;

            case 'd':
              if (G_LIKELY (file_infos != NULL))
                {
                  location = fmbx_file_info_get_location (file_infos->data);
                  path = g_file_get_path (location);
                  g_object_unref (location);

                  if (G_UNLIKELY (path == NULL))
                    goto error;

                  dirname = g_path_get_dirname (path);
                  quoted = g_shell_quote (dirname);
                  g_string_append (command_line, quoted);
                  g_free (dirname);
                  g_free (quoted);
                  g_free (path);
                }
              break;

            case 'D':
              for (lp = file_infos; lp != NULL; lp = lp->next)
                {
                  if (G_LIKELY (lp != file_infos))
                    g_string_append_c (command_line, ' ');

                  location = fmbx_file_info_get_location (lp->data);
                  path = g_file_get_path (location);
                  g_object_unref (location);

                  if (G_UNLIKELY (path == NULL))
                    goto error;

                  dirname = g_path_get_dirname (path);
                  quoted = g_shell_quote (dirname);
                  g_string_append (command_line, quoted);
                  g_free (dirname);
                  g_free (quoted);
                  g_free (path);
                }
              break;

            case 'n':
              if (G_LIKELY (file_infos != NULL))
                {
                  path = fmbx_file_info_get_name (file_infos->data);
                  quoted = g_shell_quote (path);
                  g_string_append (command_line, quoted);
                  g_free (quoted);
                  g_free (path);
                }
              break;

            case 'N':
              for (lp = file_infos; lp != NULL; lp = lp->next)
                {
                  if (G_LIKELY (lp != file_infos))
                    g_string_append_c (command_line, ' ');

                  path = fmbx_file_info_get_name (lp->data);
                  quoted = g_shell_quote (path);
                  g_string_append (command_line, quoted);
                  g_free (quoted);
                  g_free (path);
                }
              break;

            case '%':
              g_string_append_c (command_line, '%');
              break;
            }
        }
      else
        {
          g_string_append_c (command_line, *p);
        }
    }

  /* we run the command using the bourne shell (or the systems
   * replacement for the bourne shell), so environment variables
   * and backticks can be used for the action commands.
   */
  (*argcp) = 3;
  (*argvp) = g_new (gchar *, 4);
  (*argvp)[0] = g_strdup (_PATH_BSHELL);
  (*argvp)[1] = g_strdup ("-c");
  (*argvp)[2] = g_string_free (command_line, FALSE);
  (*argvp)[3] = NULL;

  return TRUE;

error:
  g_string_free (command_line, TRUE);
  return FALSE;
}


