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

#ifdef HAVE_STDLIB_H
#include <stdlib.h>
#endif

#include <fmb/fmb-column-model.h>
#include <fmb/fmb-preferences.h>
#include <fmb/fmb-private.h>



/* Signal identifiers */
enum
{
  COLUMNS_CHANGED,
  LAST_SIGNAL,
};



static void               fmb_column_model_tree_model_init         (GtkTreeModelIface      *iface);
static void               fmb_column_model_finalize                (GObject                *object);
static GtkTreeModelFlags  fmb_column_model_get_flags               (GtkTreeModel           *tree_model);
static gint               fmb_column_model_get_n_columns           (GtkTreeModel           *tree_model);
static GType              fmb_column_model_get_column_type         (GtkTreeModel           *tree_model,  
                                                                       gint                    idx);
static gboolean           fmb_column_model_get_iter                (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter,
                                                                       GtkTreePath            *path);
static GtkTreePath       *fmb_column_model_get_path                (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter);
static void               fmb_column_model_get_value               (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter,
                                                                       gint                    idx,
                                                                       GValue                 *value);
static gboolean           fmb_column_model_iter_next               (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter);
static gboolean           fmb_column_model_iter_children           (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter,
                                                                       GtkTreeIter            *parent);
static gboolean           fmb_column_model_iter_has_child          (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter);
static gint               fmb_column_model_iter_n_children         (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter);
static gboolean           fmb_column_model_iter_nth_child          (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter,
                                                                       GtkTreeIter            *parent,
                                                                       gint                    n);
static gboolean           fmb_column_model_iter_parent             (GtkTreeModel           *tree_model,
                                                                       GtkTreeIter            *iter,
                                                                       GtkTreeIter            *child);
static void               fmb_column_model_load_column_order       (FmbColumnModel      *column_model);
static void               fmb_column_model_save_column_order       (FmbColumnModel      *column_model);
static void               fmb_column_model_notify_column_order     (FmbPreferences      *preferences,
                                                                       GParamSpec             *pspec,
                                                                       FmbColumnModel      *column_model);
static void               fmb_column_model_load_column_widths      (FmbColumnModel      *column_model);
static void               fmb_column_model_save_column_widths      (FmbColumnModel      *column_model);
static void               fmb_column_model_notify_column_widths    (FmbPreferences      *preferences,
                                                                       GParamSpec             *pspec,
                                                                       FmbColumnModel      *column_model);
static void               fmb_column_model_load_visible_columns    (FmbColumnModel      *column_model);
static void               fmb_column_model_save_visible_columns    (FmbColumnModel      *column_model);
static void               fmb_column_model_notify_visible_columns  (FmbPreferences      *preferences,
                                                                       GParamSpec             *pspec,
                                                                       FmbColumnModel      *column_model);



struct _FmbColumnModelClass
{
  GObjectClass __parent__;

  /* signals */
  void (*columns_changed) (FmbColumnModel *column_model);
};

struct _FmbColumnModel
{
  GObject __parent__;

  /* the model stamp is only used when debugging is
   * enabled, to make sure we don't accept iterators
   * generated by another model.
   */
#ifndef NDEBUG
  gint               stamp;
#endif

  FmbPreferences *preferences;
  FmbColumn       order[FMB_N_VISIBLE_COLUMNS];
  gboolean           visible[FMB_N_VISIBLE_COLUMNS];
  gint               width[FMB_N_VISIBLE_COLUMNS];
};



static guint column_model_signals[LAST_SIGNAL];



G_DEFINE_TYPE_WITH_CODE (FmbColumnModel, fmb_column_model, G_TYPE_OBJECT,
    G_IMPLEMENT_INTERFACE (GTK_TYPE_TREE_MODEL, fmb_column_model_tree_model_init))



static void
fmb_column_model_class_init (FmbColumnModelClass *klass)
{
  GObjectClass *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_column_model_finalize;

  /**
   * FmbColumnModel::columns-changed:
   * @column_model : a #FmbColumnModel.
   *
   * Emitted by @column_model whenever the
   * order of the columns or the visibility
   * of a column in @column_model is changed.
   **/
  column_model_signals[COLUMNS_CHANGED] =
    g_signal_new (I_("columns-changed"),
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (FmbColumnModelClass, columns_changed),
                  NULL, NULL,
                  g_cclosure_marshal_VOID__VOID,
                  G_TYPE_NONE, 0);
}



static void
fmb_column_model_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags = fmb_column_model_get_flags;
  iface->get_n_columns = fmb_column_model_get_n_columns;
  iface->get_column_type = fmb_column_model_get_column_type;
  iface->get_iter = fmb_column_model_get_iter;
  iface->get_path = fmb_column_model_get_path;
  iface->get_value = fmb_column_model_get_value;
  iface->iter_next = fmb_column_model_iter_next;
  iface->iter_children = fmb_column_model_iter_children;
  iface->iter_has_child = fmb_column_model_iter_has_child;
  iface->iter_n_children = fmb_column_model_iter_n_children;
  iface->iter_nth_child = fmb_column_model_iter_nth_child;
  iface->iter_parent = fmb_column_model_iter_parent;
}



static void
fmb_column_model_init (FmbColumnModel *column_model)
{
  /* grab a reference on the preferences */
  column_model->preferences = fmb_preferences_get ();
  g_signal_connect (G_OBJECT (column_model->preferences), "notify::last-details-view-column-order",
                    G_CALLBACK (fmb_column_model_notify_column_order), column_model);
  g_signal_connect (G_OBJECT (column_model->preferences), "notify::last-details-view-column-widths",
                    G_CALLBACK (fmb_column_model_notify_column_widths), column_model);
  g_signal_connect (G_OBJECT (column_model->preferences), "notify::last-details-view-visible-columns",
                    G_CALLBACK (fmb_column_model_notify_visible_columns), column_model);

  /* generate a random stamp if we're in debug mode */
#ifndef NDEBUG
  column_model->stamp = g_random_int ();
#endif

  /* load the column order */
  fmb_column_model_load_column_order (column_model);

  /* load the column widths */
  fmb_column_model_load_column_widths (column_model);

  /* load the list of visible columns */
  fmb_column_model_load_visible_columns (column_model);
}



static void
fmb_column_model_finalize (GObject *object)
{
  FmbColumnModel *column_model = FMB_COLUMN_MODEL (object);

  /* disconnect from the global preferences */
  g_signal_handlers_disconnect_matched (G_OBJECT (column_model->preferences), G_SIGNAL_MATCH_DATA, 0, 0, NULL, NULL, column_model);
  g_object_unref (G_OBJECT (column_model->preferences));

  (*G_OBJECT_CLASS (fmb_column_model_parent_class)->finalize) (object);
}



static GtkTreeModelFlags
fmb_column_model_get_flags (GtkTreeModel *tree_model)
{
  return GTK_TREE_MODEL_LIST_ONLY;
}



static gint
fmb_column_model_get_n_columns (GtkTreeModel *tree_model)
{
  return FMB_COLUMN_MODEL_N_COLUMNS;
}



static GType
fmb_column_model_get_column_type (GtkTreeModel *tree_model,  
                                     gint          idx)
{
  switch (idx)
    {
    case FMB_COLUMN_MODEL_COLUMN_NAME:
      return G_TYPE_STRING;

    case FMB_COLUMN_MODEL_COLUMN_MUTABLE:
      return G_TYPE_BOOLEAN;

    case FMB_COLUMN_MODEL_COLUMN_VISIBLE:
      return G_TYPE_BOOLEAN;
    }

  _fmb_assert_not_reached ();
  return G_TYPE_INVALID;
}



static gboolean
fmb_column_model_get_iter (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter,
                              GtkTreePath  *path)
{
  FmbColumnModel *column_model = FMB_COLUMN_MODEL (tree_model);
  FmbColumn       column;

  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), FALSE);
  _fmb_return_val_if_fail (gtk_tree_path_get_depth (path) > 0, FALSE);

  /* check if the path is valid */
  column = gtk_tree_path_get_indices (path)[0];
  if (G_UNLIKELY (column >= FMB_N_VISIBLE_COLUMNS))
    return FALSE;

  /* generate an iterator */
  GTK_TREE_ITER_INIT (*iter, column_model->stamp, GINT_TO_POINTER (column));
  return TRUE;
}



static GtkTreePath*
fmb_column_model_get_path (GtkTreeModel *tree_model,
                              GtkTreeIter  *iter)
{
  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (tree_model), NULL);
  _fmb_return_val_if_fail (iter->stamp == FMB_COLUMN_MODEL (tree_model)->stamp, NULL);

  /* generate the path for the iterator */
  return gtk_tree_path_new_from_indices (GPOINTER_TO_INT (iter->user_data), -1);
}



static void
fmb_column_model_get_value (GtkTreeModel *tree_model,
                               GtkTreeIter  *iter,
                               gint          idx,
                               GValue       *value)
{
  FmbColumnModel *column_model = FMB_COLUMN_MODEL (tree_model);
  FmbColumn       column;

  _fmb_return_if_fail (FMB_IS_COLUMN_MODEL (column_model));
  _fmb_return_if_fail (idx < FMB_COLUMN_MODEL_N_COLUMNS);
  _fmb_return_if_fail (iter->stamp == column_model->stamp);

  /* determine the column from the iterator */
  column = GPOINTER_TO_INT (iter->user_data);

  /* resolve the column according to the order */
  column = column_model->order[column];

  switch (idx)
    {
    case FMB_COLUMN_MODEL_COLUMN_NAME:
      g_value_init (value, G_TYPE_STRING);
      g_value_set_static_string (value, fmb_column_model_get_column_name (column_model, column));
      break;

    case FMB_COLUMN_MODEL_COLUMN_MUTABLE:
      g_value_init (value, G_TYPE_BOOLEAN);
      g_value_set_boolean (value, (column != FMB_COLUMN_NAME));
      break;

    case FMB_COLUMN_MODEL_COLUMN_VISIBLE:
      g_value_init (value, G_TYPE_BOOLEAN);
      g_value_set_boolean (value, fmb_column_model_get_column_visible (column_model, column));
      break;

    default:
      _fmb_assert_not_reached ();
      break;
    }
}



static gboolean
fmb_column_model_iter_next (GtkTreeModel *tree_model,
                               GtkTreeIter  *iter)
{
  FmbColumn column;

  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (tree_model), FALSE);
  _fmb_return_val_if_fail (iter->stamp == FMB_COLUMN_MODEL (tree_model)->stamp, FALSE);

  /* move the iterator to the next column */
  column = GPOINTER_TO_INT (iter->user_data) + 1;
  iter->user_data = GINT_TO_POINTER (column);

  /* check if the iterator is still valid */
  return (column < FMB_N_VISIBLE_COLUMNS);
}



static gboolean
fmb_column_model_iter_children (GtkTreeModel *tree_model,
                                   GtkTreeIter  *iter,
                                   GtkTreeIter  *parent)
{
  FmbColumnModel *column_model = FMB_COLUMN_MODEL (tree_model);

  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), FALSE);

  if (G_LIKELY (parent == NULL))
    {
      GTK_TREE_ITER_INIT (*iter, column_model->stamp, GINT_TO_POINTER (0));
      return TRUE;
    }

  return FALSE;
}



static gboolean
fmb_column_model_iter_has_child (GtkTreeModel *tree_model,
                                    GtkTreeIter  *iter)
{
  return FALSE;
}



static gint
fmb_column_model_iter_n_children (GtkTreeModel *tree_model,
                                     GtkTreeIter  *iter)
{
  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (tree_model), 0);

  return (iter == NULL) ? FMB_N_VISIBLE_COLUMNS : 0;
}



static gboolean
fmb_column_model_iter_nth_child (GtkTreeModel *tree_model,
                                    GtkTreeIter  *iter,
                                    GtkTreeIter  *parent,
                                    gint          n)
{
  FmbColumnModel *column_model = FMB_COLUMN_MODEL (tree_model);

  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), FALSE);

  if (G_LIKELY (parent == NULL && n < FMB_N_VISIBLE_COLUMNS))
    {
      GTK_TREE_ITER_INIT (*iter, column_model->stamp, GINT_TO_POINTER (n));
      return TRUE;
    }

  return FALSE;
}



static gboolean
fmb_column_model_iter_parent (GtkTreeModel *tree_model,
                                 GtkTreeIter  *iter,
                                 GtkTreeIter  *child)
{
  return FALSE;
}



static void
fmb_column_model_load_column_order (FmbColumnModel *column_model)
{
  FmbColumn column;
  GEnumClass  *klass;
  GEnumValue  *value;
  gchar      **column_order;
  gchar       *tmp;
  gint         i, j;

  /* determine the column order from the preferences */
  g_object_get (G_OBJECT (column_model->preferences), "last-details-view-column-order", &tmp, NULL);
  column_order = g_strsplit (tmp, ",", -1);
  g_free (tmp);

  /* reset the column order to its default */
  for (j = 0; j < FMB_N_VISIBLE_COLUMNS; ++j)
    column_model->order[j] = j;

  /* now rearrange the columns according to the preferences */
  klass = g_type_class_ref (FMB_TYPE_COLUMN);
  for (i = 0; column_order[i] != NULL; ++i)
    {
      /* determine the enum value for the name */
      value = g_enum_get_value_by_name (klass, column_order[i]);
      if (G_UNLIKELY (value == NULL || value->value == i))
        continue;

      /* find the current position of the value */
      for (j = 0; j < FMB_N_VISIBLE_COLUMNS; ++j)
        if (column_model->order[j] == (guint) value->value)
          break;

      /* check if valid */
      if (G_LIKELY (j < FMB_N_VISIBLE_COLUMNS))
        {
          /* exchange the positions of i and j */
          column = column_model->order[i];
          column_model->order[i] = column_model->order[j];
          column_model->order[j] = column;
        }
    }
  g_type_class_unref (klass);

  /* release the column order */
  g_strfreev (column_order);
}



static void
fmb_column_model_save_column_order (FmbColumnModel *column_model)
{
  GEnumClass *klass;
  GEnumValue *value;
  GString    *column_order;
  gint        n;

  /* allocate a string for the column order */
  column_order = g_string_sized_new (256);

  /* transform the internal visible column list */
  klass = g_type_class_ref (FMB_TYPE_COLUMN);
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    {
      /* append a comma if not empty */
      if (*column_order->str != '\0')
        g_string_append_c (column_order, ',');

      /* append the enum value name */
      value = g_enum_get_value (klass, column_model->order[n]);
      g_string_append (column_order, value->value_name);
    }
  g_type_class_unref (klass);

  /* save the list of visible columns */
  g_signal_handlers_block_by_func (G_OBJECT (column_model->preferences), fmb_column_model_notify_column_order, column_model);
  g_object_set (G_OBJECT (column_model->preferences), "last-details-view-column-order", column_order->str, NULL);
  g_signal_handlers_unblock_by_func (G_OBJECT (column_model->preferences), fmb_column_model_notify_column_order, column_model);

  /* release the string */
  g_string_free (column_order, TRUE);
}



static void
fmb_column_model_notify_column_order (FmbPreferences *preferences,
                                         GParamSpec        *pspec,
                                         FmbColumnModel *column_model)
{
  GtkTreePath *path;
  GtkTreeIter  iter;
  gint         n;

  _fmb_return_if_fail (FMB_IS_COLUMN_MODEL (column_model));
  _fmb_return_if_fail (FMB_IS_PREFERENCES (preferences));

  /* load the new column order */
  fmb_column_model_load_column_order (column_model);

  /* emit "row-changed" for all rows */
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    {
      path = gtk_tree_path_new_from_indices (n, -1);
      if (gtk_tree_model_get_iter (GTK_TREE_MODEL (column_model), &iter, path))
        gtk_tree_model_row_changed (GTK_TREE_MODEL (column_model), path, &iter);
      gtk_tree_path_free (path);
    }

  /* emit "columns-changed" */
  g_signal_emit (G_OBJECT (column_model), column_model_signals[COLUMNS_CHANGED], 0);
}



static void
fmb_column_model_load_column_widths (FmbColumnModel *column_model)
{
  gchar **column_widths;
  gchar  *tmp;
  gint    width;
  gint    n;

  /* determine the column widths from the preferences */
  g_object_get (G_OBJECT (column_model->preferences), "last-details-view-column-widths", &tmp, NULL);
  column_widths = g_strsplit (tmp, ",", -1);
  g_free (tmp);

  /* reset the column widths for the model */
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    column_model->width[n] = 50;
  column_model->width[FMB_COLUMN_NAME] = 200;

  /* parse the widths from the preferences */
  for (n = 0; column_widths[n] != NULL; ++n)
    {
      width = strtol (column_widths[n], NULL, 10);
      if (G_LIKELY (width >= 0))
        column_model->width[n] = width;
    }

  /* release the column width array */
  g_strfreev (column_widths);
}



static void
fmb_column_model_save_column_widths (FmbColumnModel *column_model)
{
  GString *column_widths;
  gint     n;

  /* allocate a string for the column widths */
  column_widths = g_string_sized_new (96);

  /* transform the column widths to a string */
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    {
      /* append a comma if not empty */
      if (*column_widths->str != '\0')
        g_string_append_c (column_widths, ',');

      /* append the width */
      g_string_append_printf (column_widths, "%d", column_model->width[n]);
    }

  /* save the column widths */
  g_signal_handlers_block_by_func (G_OBJECT (column_model->preferences), fmb_column_model_notify_column_widths, column_model);
  g_object_set (G_OBJECT (column_model->preferences), "last-details-view-column-widths", column_widths->str, NULL);
  g_signal_handlers_unblock_by_func (G_OBJECT (column_model->preferences), fmb_column_model_notify_column_widths, column_model);

  /* release the string */
  g_string_free (column_widths, TRUE);
}



static void
fmb_column_model_notify_column_widths (FmbPreferences *preferences,
                                          GParamSpec        *pspec,
                                          FmbColumnModel *column_model)
{
  _fmb_return_if_fail (FMB_IS_COLUMN_MODEL (column_model));
  _fmb_return_if_fail (FMB_IS_PREFERENCES (preferences));

  /* load the new column widths */
  fmb_column_model_load_column_widths (column_model);
}



static void
fmb_column_model_load_visible_columns (FmbColumnModel *column_model)
{
  GEnumClass *klass;
  GEnumValue *value;
  gchar     **visible_columns;
  gchar      *tmp;
  gint        n;

  /* determine the list of visible columns from the preferences */
  g_object_get (G_OBJECT (column_model->preferences), "last-details-view-visible-columns", &tmp, NULL);
  visible_columns = g_strsplit (tmp, ",", -1);
  g_free (tmp);

  /* reset the visible columns for the model */
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    column_model->visible[n] = FALSE;

  /* mark all columns included in the list as visible */
  klass = g_type_class_ref (FMB_TYPE_COLUMN);
  for (n = 0; visible_columns[n] != NULL; ++n)
    {
      /* determine the enum value from the string */
      value = g_enum_get_value_by_name (klass, visible_columns[n]);
      if (G_LIKELY (value != NULL && value->value < FMB_N_VISIBLE_COLUMNS))
        column_model->visible[value->value] = TRUE;
    }
  g_type_class_unref (klass);

  /* the name column is always visible */
  column_model->visible[FMB_COLUMN_NAME] = TRUE;

  /* release the list of visible columns */
  g_strfreev (visible_columns);
}



static void
fmb_column_model_save_visible_columns (FmbColumnModel *column_model)
{
  GEnumClass *klass;
  GString    *visible_columns;
  gint        n;

  /* allocate a string for the visible columns list */
  visible_columns = g_string_sized_new (128);

  /* transform the internal visible column list */
  klass = g_type_class_ref (FMB_TYPE_COLUMN);
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    if (column_model->visible[klass->values[n].value])
      {
        /* append a comma if not empty */
        if (*visible_columns->str != '\0')
          g_string_append_c (visible_columns, ',');

        /* append the enum value name */
        g_string_append (visible_columns, klass->values[n].value_name);
      }
  g_type_class_unref (klass);

  /* save the list of visible columns */
  g_signal_handlers_block_by_func (G_OBJECT (column_model->preferences), fmb_column_model_notify_visible_columns, column_model);
  g_object_set (G_OBJECT (column_model->preferences), "last-details-view-visible-columns", visible_columns->str, NULL);
  g_signal_handlers_unblock_by_func (G_OBJECT (column_model->preferences), fmb_column_model_notify_visible_columns, column_model);

  /* release the string */
  g_string_free (visible_columns, TRUE);
}



static void
fmb_column_model_notify_visible_columns (FmbPreferences *preferences,
                                            GParamSpec        *pspec,
                                            FmbColumnModel *column_model)
{
  GtkTreePath *path;
  GtkTreeIter  iter;
  gint         n;

  _fmb_return_if_fail (FMB_IS_COLUMN_MODEL (column_model));
  _fmb_return_if_fail (FMB_IS_PREFERENCES (preferences));

  /* load the new list of visible columns */
  fmb_column_model_load_visible_columns (column_model);

  /* emit "row-changed" for all rows */
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    {
      path = gtk_tree_path_new_from_indices (n, -1);
      if (gtk_tree_model_get_iter (GTK_TREE_MODEL (column_model), &iter, path))
        gtk_tree_model_row_changed (GTK_TREE_MODEL (column_model), path, &iter);
      gtk_tree_path_free (path);
    }

  /* emit "columns-changed" */
  g_signal_emit (G_OBJECT (column_model), column_model_signals[COLUMNS_CHANGED], 0);
}



/**
 * fmb_column_model_get_default:
 *
 * Returns the default, shared #FmbColumnModel
 * instance.
 *
 * The caller is responsible to free the returned
 * object using g_object_unref() when no longer
 * needed.
 *
 * Return value: the default #FmbColumnModel.
 **/
FmbColumnModel*
fmb_column_model_get_default (void)
{
  static FmbColumnModel *column_model = NULL;

  if (G_UNLIKELY (column_model == NULL))
    {
      column_model = g_object_new (FMB_TYPE_COLUMN_MODEL, NULL);
      g_object_add_weak_pointer (G_OBJECT (column_model), (gpointer) &column_model);
    }
  else
    {
      g_object_ref (G_OBJECT (column_model));
    }

  return column_model;
}



/**
 * fmb_column_model_exchange:
 * @column_model : a #FmbColumnModel.
 * @iter1        : first #GtkTreeIter.
 * @iter2        : second #GtkTreeIter.
 *
 * Exchanges the columns at @iter1 and @iter2
 * in @column_model.
 **/
void
fmb_column_model_exchange (FmbColumnModel *column_model,
                              GtkTreeIter       *iter1,
                              GtkTreeIter       *iter2)
{
  FmbColumn column;
  GtkTreePath *path;
  gint         new_order[FMB_N_VISIBLE_COLUMNS];
  gint         n;

  _fmb_return_if_fail (FMB_IS_COLUMN_MODEL (column_model));
  _fmb_return_if_fail (iter1->stamp == column_model->stamp);
  _fmb_return_if_fail (iter2->stamp == column_model->stamp);

  /* swap the columns */
  column = column_model->order[GPOINTER_TO_INT (iter1->user_data)];
  column_model->order[GPOINTER_TO_INT (iter1->user_data)] = column_model->order[GPOINTER_TO_INT (iter2->user_data)];
  column_model->order[GPOINTER_TO_INT (iter2->user_data)] = column;

  /* initialize the new order array */
  for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
    new_order[n] = n;

  /* perform the swapping on the new order array */
  new_order[GPOINTER_TO_INT (iter1->user_data)] = GPOINTER_TO_INT (iter2->user_data);
  new_order[GPOINTER_TO_INT (iter2->user_data)] = GPOINTER_TO_INT (iter1->user_data);

  /* emit "rows-reordered" */
  path = gtk_tree_path_new ();
  gtk_tree_model_rows_reordered (GTK_TREE_MODEL (column_model), path, NULL, new_order);
  gtk_tree_path_free (path);

  /* emit "columns-changed" */
  g_signal_emit (G_OBJECT (column_model), column_model_signals[COLUMNS_CHANGED], 0);

  /* save the new column order */
  fmb_column_model_save_column_order (column_model);
}



/**
 * fmb_column_model_get_column_for_iter:
 * @column_model : a #FmbColumnModel.
 * @iter         : a valid #GtkTreeIter for @column_model.
 *
 * Returns the #FmbColumn for the given @iter in
 * @column_model.
 *
 * Return value: the #FmbColumn for @iter.
 **/
FmbColumn
fmb_column_model_get_column_for_iter (FmbColumnModel *column_model,
                                         GtkTreeIter       *iter)
{
  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), -1);
  _fmb_return_val_if_fail (iter->stamp == column_model->stamp, -1);
  return column_model->order[GPOINTER_TO_INT (iter->user_data)];
}



/**
 * fmb_column_model_get_column_order:
 * @column_model : a #FmbColumnModel.
 *
 * Returns the current #FmbColumn order for @column_model.
 *
 * Return value: the current #FmbColumn order for the given
 *               @column_model instance.
 **/
const FmbColumn*
fmb_column_model_get_column_order (FmbColumnModel *column_model)
{
  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), NULL);
  return column_model->order;
}



/**
 * fmb_column_model_get_column_name:
 * @column_model : a #FmbColumnModel.
 * @column       : a #FmbColumn.
 *
 * Returns the user visible name for the @column in
 * the @column_model.
 *
 * Return value: the user visible name for @column.
 **/
const gchar*
fmb_column_model_get_column_name (FmbColumnModel *column_model,
                                     FmbColumn       column)
{
  const gchar *column_name = _("Unknown");
  GEnumClass  *klass;
  guint        n;

  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), NULL);
  _fmb_return_val_if_fail (column < FMB_N_VISIBLE_COLUMNS, NULL);

  /* determine the column name from the FmbColumn enum type */
  klass = g_type_class_ref (FMB_TYPE_COLUMN);
  for (n = 0; n < klass->n_values; ++n)
    if (klass->values[n].value == (gint) column)
      column_name = _(klass->values[n].value_nick);
  g_type_class_unref (klass);

  return column_name;
}



/**
 * fmb_column_model_get_column_visible:
 * @column_model : a #FmbColumnModel.
 * @column       : a #FmbColumn.
 *
 * Returns %TRUE if the @column should be displayed
 * in the details view.
 *
 * Return value: %TRUE if @column is visible.
 **/
gboolean
fmb_column_model_get_column_visible (FmbColumnModel *column_model,
                                        FmbColumn       column)
{
  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), FALSE);
  _fmb_return_val_if_fail (column < FMB_N_VISIBLE_COLUMNS, FALSE);
  return column_model->visible[column];
}



/**
 * fmb_column_model_set_column_visible:
 * @column_model : a #FmbColumnModel.
 * @column       : a #FmbColumn.
 * @visible      : %TRUE to make @column visible.
 *
 * Changes the visibility of the @column to @visible in
 * @column_model.
 **/
void
fmb_column_model_set_column_visible (FmbColumnModel *column_model,
                                        FmbColumn       column,
                                        gboolean           visible)
{
  GtkTreePath *path;
  GtkTreeIter  iter;
  gint         n;

  _fmb_return_if_fail (FMB_IS_COLUMN_MODEL (column_model));
  _fmb_return_if_fail (column < FMB_N_VISIBLE_COLUMNS);

  /* cannot change the visibility of the name column */
  if (G_UNLIKELY (column == FMB_COLUMN_NAME))
    return;

  /* normalize the value */
  visible = !!visible;

  /* check if we have a new value */
  if (G_LIKELY (column_model->visible[column] != visible))
    {
      /* apply the new value */
      column_model->visible[column] = visible;

      /* reverse lookup the real column */
      for (n = 0; n < FMB_N_VISIBLE_COLUMNS; ++n)
        if (column_model->order[n] == column)
          {
            column = n;
            break;
          }

      /* emit "row-changed" */
      path = gtk_tree_path_new_from_indices (column, -1);
      if (gtk_tree_model_get_iter (GTK_TREE_MODEL (column_model), &iter, path))
        gtk_tree_model_row_changed (GTK_TREE_MODEL (column_model), path, &iter);
      gtk_tree_path_free (path);

      /* emit "columns-changed" */
      g_signal_emit (G_OBJECT (column_model), column_model_signals[COLUMNS_CHANGED], 0);

      /* save the list of visible columns */
      fmb_column_model_save_visible_columns (column_model);
    }
}



/**
 * fmb_column_model_get_column_width:
 * @column_model : a #FmbColumnModel.
 * @column       : a #FmbColumn.
 *
 * Returns the fixed column width for @column in
 * @column_model.
 *
 * Return value: the fixed width for @column.
 **/
gint
fmb_column_model_get_column_width (FmbColumnModel *column_model,
                                      FmbColumn       column)
{
  _fmb_return_val_if_fail (FMB_IS_COLUMN_MODEL (column_model), -1);
  _fmb_return_val_if_fail (column < FMB_N_VISIBLE_COLUMNS, -1);
  return column_model->width[column];
}



/**
 * fmb_column_model_set_column_width:
 * @column_model : a #FmbColumnModel.
 * @column       : a #FmbColumn.
 * @width        : the new fixed width for @column.
 *
 * Sets the fixed width for @column in @column_model
 * to @width.
 **/
void
fmb_column_model_set_column_width (FmbColumnModel *column_model,
                                      FmbColumn       column,
                                      gint               width)
{
  _fmb_return_if_fail (FMB_IS_COLUMN_MODEL (column_model));
  _fmb_return_if_fail (column < FMB_N_VISIBLE_COLUMNS);
  _fmb_return_if_fail (width >= 0);

  /* check if we have a new width */
  if (G_LIKELY (column_model->width[column] != width))
    {
      /* apply the new value */
      column_model->width[column] = width;

      /* store the settings */
      fmb_column_model_save_column_widths (column_model);
    }
}


