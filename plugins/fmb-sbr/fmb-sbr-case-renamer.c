/* vi:set et ai sw=2 sts=2 ts=2: */
/*-
 * Copyright (c) 2006 Benedikt Meurer <benny@xfce.org>
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

#include <blxo/blxo.h>

#include <fmb-sbr/fmb-sbr-case-renamer.h>



/* Property identifiers */
enum
{
  PROP_0,
  PROP_MODE,
};



static void   fmb_sbr_case_renamer_get_property  (GObject                    *object,
                                                     guint                       prop_id,
                                                     GValue                     *value,
                                                     GParamSpec                 *pspec);
static void   fmb_sbr_case_renamer_set_property  (GObject                    *object,
                                                     guint                       prop_id,
                                                     const GValue               *value,
                                                     GParamSpec                 *pspec);
static gchar *fmb_sbr_case_renamer_process       (FmbxRenamer             *renamer,
                                                     FmbxFileInfo            *file,
                                                     const gchar                *text,
                                                     guint                       idx);



struct _FmbSbrCaseRenamerClass
{
  FmbxRenamerClass __parent__;
};

struct _FmbSbrCaseRenamer
{
  FmbxRenamer           __parent__;
  FmbSbrCaseRenamerMode mode;
};



FMBX_DEFINE_TYPE (FmbSbrCaseRenamer, fmb_sbr_case_renamer, FMBX_TYPE_RENAMER);



static void
fmb_sbr_case_renamer_class_init (FmbSbrCaseRenamerClass *klass)
{
  FmbxRenamerClass *fmbxrenamer_class;
  GObjectClass        *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->get_property = fmb_sbr_case_renamer_get_property;
  gobject_class->set_property = fmb_sbr_case_renamer_set_property;

  fmbxrenamer_class = FMBX_RENAMER_CLASS (klass);
  fmbxrenamer_class->process = fmb_sbr_case_renamer_process;

  /**
   * FmbSbrCaseRenamer:mode:
   *
   * The #FmbSbrCaseRenamerMode for this
   * #FmbSbrCaseRenamer instance.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_MODE,
                                   g_param_spec_enum ("mode", "mode", "mode",
                                                      FMB_SBR_TYPE_CASE_RENAMER_MODE,
                                                      FMB_SBR_CASE_RENAMER_MODE_LOWER,
                                                      G_PARAM_READWRITE));
}



static void
fmb_sbr_case_renamer_init (FmbSbrCaseRenamer *case_renamer)
{
  AtkRelationSet *relations;
  AtkRelation    *relation;
  GEnumClass     *klass;
  AtkObject      *object;
  GtkWidget      *combo;
  GtkWidget      *label;
  GtkWidget      *hbox;
  guint           n;

  hbox = gtk_hbox_new (FALSE, 12);
  gtk_box_pack_start (GTK_BOX (case_renamer), hbox, FALSE, FALSE, 0);
  gtk_widget_show (hbox);

  label = gtk_label_new_with_mnemonic (_("Con_vert to:"));
  gtk_box_pack_start (GTK_BOX (hbox), label, FALSE, FALSE, 0);
  gtk_widget_show (label);

  combo = gtk_combo_box_new_text ();
  klass = g_type_class_ref (FMB_SBR_TYPE_CASE_RENAMER_MODE);
  for (n = 0; n < klass->n_values; ++n)
    gtk_combo_box_append_text (GTK_COMBO_BOX (combo), _(klass->values[n].value_nick));
  blxo_mutual_binding_new (G_OBJECT (case_renamer), "mode", G_OBJECT (combo), "active");
  gtk_box_pack_start (GTK_BOX (hbox), combo, FALSE, FALSE, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), combo);
  g_type_class_unref (klass);
  gtk_widget_show (combo);

  /* set Atk label relation for the combo */
  object = gtk_widget_get_accessible (combo);
  relations = atk_object_ref_relation_set (gtk_widget_get_accessible (label));
  relation = atk_relation_new (&object, 1, ATK_RELATION_LABEL_FOR);
  atk_relation_set_add (relations, relation);
  g_object_unref (G_OBJECT (relation));
}



static void
fmb_sbr_case_renamer_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  FmbSbrCaseRenamer *case_renamer = FMB_SBR_CASE_RENAMER (object);

  switch (prop_id)
    {
    case PROP_MODE:
      g_value_set_enum (value, fmb_sbr_case_renamer_get_mode (case_renamer));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_sbr_case_renamer_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  FmbSbrCaseRenamer *case_renamer = FMB_SBR_CASE_RENAMER (object);

  switch (prop_id)
    {
    case PROP_MODE:
      fmb_sbr_case_renamer_set_mode (case_renamer, g_value_get_enum (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static gchar*
tscr_utf8_strcase (const gchar *text,
                   gboolean     camelcase)
{
  const gchar *t;
  gboolean     upper = TRUE;
  gunichar     c;
  GString     *result;

  /* allocate a new string for the result */
  result = g_string_sized_new (32);

  /* convert the input text */
  for (t = text; *t != '\0'; t = g_utf8_next_char (t))
    {
      /* check the next char */
      c = g_utf8_get_char (t);
      if (camelcase
          && g_unichar_isspace (c))
        {
          upper = TRUE;
        }
      else if (upper && g_unichar_isalpha (c))
        {
          c = g_unichar_toupper (c);
          upper = FALSE;
        }
      else
        {
          c = g_unichar_tolower (c);
        }

      /* append the char to the result */
      g_string_append_unichar (result, c);
    }

  return g_string_free (result, FALSE);
}



static gchar*
fmb_sbr_case_renamer_process (FmbxRenamer  *renamer,
                                 FmbxFileInfo *file,
                                 const gchar     *text,
                                 guint            idx)
{
  FmbSbrCaseRenamer *case_renamer = FMB_SBR_CASE_RENAMER (renamer);

  switch (case_renamer->mode)
    {
    case FMB_SBR_CASE_RENAMER_MODE_LOWER:
      return g_utf8_strdown (text, -1);

    case FMB_SBR_CASE_RENAMER_MODE_UPPER:
      return g_utf8_strup (text, -1);

    case FMB_SBR_CASE_RENAMER_MODE_CAMEL:
      return tscr_utf8_strcase (text, TRUE);

   case FMB_SBR_CASE_RENAMER_MODE_SENTENCE:
      return tscr_utf8_strcase (text, FALSE);

    default:
      g_assert_not_reached ();
      return NULL;
    }
}



/**
 * fmb_sbr_case_renamer_new:
 *
 * Allocates a new #FmbSbrCaseRenamer instance.
 *
 * Return value: the newly allocated #FmbSbrCaseRenamer.
 **/
FmbSbrCaseRenamer*
fmb_sbr_case_renamer_new (void)
{
  return g_object_new (FMB_SBR_TYPE_CASE_RENAMER,
                       "name", _("Uppercase / Lowercase"),
                       NULL);
}



/**
 * fmb_sbr_case_renamer_get_mode:
 * @case_renamer : a #FmbSbrCaseRenamer.
 *
 * Returns the current #FmbSbrCaseRenamerMode
 * for the @case_renamer.
 *
 * Return value: the current mode of @case_renamer.
 **/
FmbSbrCaseRenamerMode
fmb_sbr_case_renamer_get_mode (FmbSbrCaseRenamer *case_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_CASE_RENAMER (case_renamer), FMB_SBR_CASE_RENAMER_MODE_LOWER);
  return case_renamer->mode;
}



/**
 * fmb_sbr_case_renamer_set_mode:
 * @case_renamer : a #FmbSbrCaseRenamer.
 * @mode         : the new #FmbSbrCaseRenamerMode.
 *
 * Sets the #FmbSbrCaseRenamerMode of the @case_renamer
 * to the specified @mode.
 **/
void
fmb_sbr_case_renamer_set_mode (FmbSbrCaseRenamer    *case_renamer,
                                  FmbSbrCaseRenamerMode mode)
{
  g_return_if_fail (FMB_SBR_IS_CASE_RENAMER (case_renamer));

  /* check if we already use that mode */
  if (G_UNLIKELY (case_renamer->mode == mode))
    return;

  /* apply the new mode */
  case_renamer->mode = mode;

  /* notify listeners */
  g_object_notify (G_OBJECT (case_renamer), "mode");

  /* emit the "changed" signal */
  fmbx_renamer_changed (FMBX_RENAMER (case_renamer));
}


