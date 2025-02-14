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

#ifdef HAVE_MEMORY_H
#include <memory.h>
#endif
#ifdef HAVE_STRING_H
#include <string.h>
#endif

#ifdef HAVE_PCRE
#include <pcre.h>
#endif

#include <blxo/blxo.h>

#include <fmb-sbr/fmb-sbr-replace-renamer.h>



/* Property identifiers */
enum
{
  PROP_0,
  PROP_CASE_SENSITIVE,
  PROP_PATTERN,
  PROP_REPLACEMENT,
  PROP_REGEXP,
};



static void   fmb_sbr_replace_renamer_finalize     (GObject                      *object);
static void   fmb_sbr_replace_renamer_get_property (GObject                      *object,
                                                       guint                         prop_id,
                                                       GValue                       *value,
                                                       GParamSpec                   *pspec);
static void   fmb_sbr_replace_renamer_set_property (GObject                      *object,
                                                       guint                         prop_id,
                                                       const GValue                 *value,
                                                       GParamSpec                   *pspec);
static void   fmb_sbr_replace_renamer_realize      (GtkWidget                    *widget);
static gchar *fmb_sbr_replace_renamer_process      (FmbxRenamer               *renamer,
                                                       FmbxFileInfo              *file,
                                                       const gchar                  *text,
                                                       guint                         idx);
#ifdef HAVE_PCRE
static gchar *fmb_sbr_replace_renamer_pcre_exec    (FmbSbrReplaceRenamer      *replace_renamer,
                                                       const gchar                  *text);
static void   fmb_sbr_replace_renamer_pcre_update  (FmbSbrReplaceRenamer      *replace_renamer);
#endif



struct _FmbSbrReplaceRenamerClass
{
  FmbxRenamerClass __parent__;
};

struct _FmbSbrReplaceRenamer
{
  FmbxRenamer __parent__;
  GtkWidget     *pattern_entry;
  gboolean       case_sensitive;
  gboolean       regexp;
  gchar         *pattern;
  gchar         *replacement;

  /* TRUE if PCRE is available and supports UTF-8 */
  gint           regexp_supported;

  /* PCRE compiled pattern */
#ifdef HAVE_PCRE
  pcre          *pcre_pattern;
  gint           pcre_capture_count;
#endif
};



FMBX_DEFINE_TYPE (FmbSbrReplaceRenamer, fmb_sbr_replace_renamer, FMBX_TYPE_RENAMER);



static void
fmb_sbr_replace_renamer_class_init (FmbSbrReplaceRenamerClass *klass)
{
  FmbxRenamerClass *fmbxrenamer_class;
  GtkWidgetClass      *gtkwidget_class;
  GObjectClass        *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_sbr_replace_renamer_finalize;
  gobject_class->get_property = fmb_sbr_replace_renamer_get_property;
  gobject_class->set_property = fmb_sbr_replace_renamer_set_property;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->realize = fmb_sbr_replace_renamer_realize;

  fmbxrenamer_class = FMBX_RENAMER_CLASS (klass);
  fmbxrenamer_class->process = fmb_sbr_replace_renamer_process;

  /**
   * FmbSbrReplaceRenamer:case-sensitive:
   *
   * Whether to use case sensitive search and replace.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_CASE_SENSITIVE,
                                   g_param_spec_boolean ("case-sensitive",
                                                         "case-sensitive",
                                                         "case-sensitive",
                                                         FALSE,
                                                         G_PARAM_READWRITE));

  /**
   * FmbSbrReplaceRenamer:pattern:
   *
   * The search pattern.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_PATTERN,
                                   g_param_spec_string ("pattern",
                                                        "pattern",
                                                        "pattern",
                                                        NULL,
                                                        G_PARAM_READWRITE));

  /**
   * FmbSbrReplaceRenamer:replacement:
   *
   * The replacement text.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_REPLACEMENT,
                                   g_param_spec_string ("replacement",
                                                        "replacement",
                                                        "replacement",
                                                        NULL,
                                                        G_PARAM_READWRITE));

  /**
   * FmbSbrReplaceRenamer:regexp:
   *
   * Whether to use regular expressions.
   **/
  g_object_class_install_property (gobject_class,
                                   PROP_REGEXP,
                                   g_param_spec_boolean ("regexp",
                                                         "regexp",
                                                         "regexp",
                                                         FALSE,
                                                         G_PARAM_READWRITE));
}



static void
fmb_sbr_replace_renamer_init (FmbSbrReplaceRenamer *replace_renamer)
{
  AtkRelationSet *relations;
  AtkRelation    *relation;
  AtkObject      *object;
  GtkWidget      *table;
  GtkWidget      *label;
  GtkWidget      *entry;
  GtkWidget      *button;

#ifdef HAVE_PCRE
  /* check if PCRE supports UTF-8 */
  if (pcre_config (PCRE_CONFIG_UTF8, &replace_renamer->regexp_supported) != 0)
    replace_renamer->regexp_supported = FALSE;
#endif

  table = gtk_table_new (2, 3, FALSE);
  gtk_table_set_row_spacings (GTK_TABLE (table), 6);
  gtk_table_set_col_spacings (GTK_TABLE (table), 12);
  gtk_box_pack_start (GTK_BOX (replace_renamer), table, FALSE, FALSE, 0);
  gtk_widget_show (table);

  label = gtk_label_new_with_mnemonic (_("_Search For:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 0, 1, GTK_FILL, 0, 0, 0);
  gtk_widget_show (label);

  replace_renamer->pattern_entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (replace_renamer->pattern_entry), TRUE);
  blxo_mutual_binding_new (G_OBJECT (replace_renamer->pattern_entry), "text", G_OBJECT (replace_renamer), "pattern");
  gtk_widget_set_tooltip_text (replace_renamer->pattern_entry, _("Enter the text to search for in the file names."));
  gtk_table_attach (GTK_TABLE (table), replace_renamer->pattern_entry, 1, 2, 0, 1, GTK_EXPAND | GTK_FILL, 0, 0, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), replace_renamer->pattern_entry);
  gtk_widget_show (replace_renamer->pattern_entry);

  /* set Atk label relation for the entry */
  object = gtk_widget_get_accessible (replace_renamer->pattern_entry);
  relations = atk_object_ref_relation_set (gtk_widget_get_accessible (label));
  relation = atk_relation_new (&object, 1, ATK_RELATION_LABEL_FOR);
  atk_relation_set_add (relations, relation);
  g_object_unref (G_OBJECT (relation));

  button = gtk_check_button_new_with_mnemonic (_("Regular _Expression"));
  blxo_mutual_binding_new (G_OBJECT (button), "active", G_OBJECT (replace_renamer), "regexp");
  gtk_widget_set_tooltip_text (button, _("If you enable this option, the pattern will be treated as a regular expression and "
                                         "matched using the Perl-compatible regular expressions (PCRE). Check the documentation "
                                         "for details about the regular expression syntax."));
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 0, 1, GTK_FILL, 0, 0, 0);
  gtk_widget_set_sensitive (button, replace_renamer->regexp_supported);
  gtk_widget_show (button);

  label = gtk_label_new_with_mnemonic (_("Replace _With:"));
  gtk_misc_set_alignment (GTK_MISC (label), 0.0f, 0.5f);
  gtk_table_attach (GTK_TABLE (table), label, 0, 1, 1, 2, GTK_FILL, 0, 0, 0);
  gtk_widget_show (label);

  entry = gtk_entry_new ();
  gtk_entry_set_activates_default (GTK_ENTRY (entry), TRUE);
  blxo_mutual_binding_new (G_OBJECT (entry), "text", G_OBJECT (replace_renamer), "replacement");
  gtk_widget_set_tooltip_text (entry, _("Enter the text that should be used as replacement for the pattern above."));
  gtk_table_attach (GTK_TABLE (table), entry, 1, 2, 1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
  gtk_label_set_mnemonic_widget (GTK_LABEL (label), entry);
  gtk_widget_show (entry);

  /* set Atk label relation for the entry */
  object = gtk_widget_get_accessible (entry);
  relations = atk_object_ref_relation_set (gtk_widget_get_accessible (label));
  relation = atk_relation_new (&object, 1, ATK_RELATION_LABEL_FOR);
  atk_relation_set_add (relations, relation);
  g_object_unref (G_OBJECT (relation));

  button = gtk_check_button_new_with_mnemonic (_("C_ase Sensitive Search"));
  blxo_mutual_binding_new (G_OBJECT (button), "active", G_OBJECT (replace_renamer), "case-sensitive");
  gtk_widget_set_tooltip_text (button, _("If you enable this option, the pattern will be searched in a case-sensitive manner. "
                                         "The default is to use a case-insensitive search."));
  gtk_table_attach (GTK_TABLE (table), button, 2, 3, 1, 2, GTK_FILL, 0, 0, 0);
  gtk_widget_show (button);
}



static void
fmb_sbr_replace_renamer_finalize (GObject *object)
{
  FmbSbrReplaceRenamer *replace_renamer = FMB_SBR_REPLACE_RENAMER (object);

  /* release the PCRE pattern (if any) */
#ifdef HAVE_PCRE
  if (G_UNLIKELY (replace_renamer->pcre_pattern != NULL))
    pcre_free (replace_renamer->pcre_pattern);
#endif

  /* release the strings */
  g_free (replace_renamer->replacement);
  g_free (replace_renamer->pattern);

  (*G_OBJECT_CLASS (fmb_sbr_replace_renamer_parent_class)->finalize) (object);
}



static void
fmb_sbr_replace_renamer_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  FmbSbrReplaceRenamer *replace_renamer = FMB_SBR_REPLACE_RENAMER (object);

  switch (prop_id)
    {
    case PROP_CASE_SENSITIVE:
      g_value_set_boolean (value, fmb_sbr_replace_renamer_get_case_sensitive (replace_renamer));
      break;

    case PROP_PATTERN:
      g_value_set_string (value, fmb_sbr_replace_renamer_get_pattern (replace_renamer));
      break;

    case PROP_REPLACEMENT:
      g_value_set_string (value, fmb_sbr_replace_renamer_get_replacement (replace_renamer));
      break;

    case PROP_REGEXP:
      g_value_set_boolean (value, fmb_sbr_replace_renamer_get_regexp (replace_renamer));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_sbr_replace_renamer_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  FmbSbrReplaceRenamer *replace_renamer = FMB_SBR_REPLACE_RENAMER (object);

  switch (prop_id)
    {
    case PROP_CASE_SENSITIVE:
      fmb_sbr_replace_renamer_set_case_sensitive (replace_renamer, g_value_get_boolean (value));
      break;

    case PROP_PATTERN:
      fmb_sbr_replace_renamer_set_pattern (replace_renamer, g_value_get_string (value));
      break;

    case PROP_REPLACEMENT:
      fmb_sbr_replace_renamer_set_replacement (replace_renamer, g_value_get_string (value));
      break;

    case PROP_REGEXP:
      fmb_sbr_replace_renamer_set_regexp (replace_renamer, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_sbr_replace_renamer_realize (GtkWidget *widget)
{
  /* realize the widget */
  (*GTK_WIDGET_CLASS (fmb_sbr_replace_renamer_parent_class)->realize) (widget);

#ifdef HAVE_PCRE
  /* update the PCRE pattern */
  fmb_sbr_replace_renamer_pcre_update (FMB_SBR_REPLACE_RENAMER (widget));
#endif
}



static gchar*
tsrr_replace (const gchar *text,
              const gchar *pattern,
              const gchar *replacement,
              gboolean     case_sensitive)
{
  const gchar *p;
  const gchar *t;
  gunichar     pc;
  gunichar     tc;
  GString     *result = g_string_sized_new (32);

  while (*text != '\0')
    {
      /* compare the pattern to this part of the text */
      for (p = pattern, t = text; *p != '\0' && *t != '\0'; p = g_utf8_next_char (p), t = g_utf8_next_char (t))
        {
          /* determine the next unichars */
          pc = g_utf8_get_char (p);
          tc = g_utf8_get_char (t);

          /* check if the chars don't match */
          if (pc != tc && (case_sensitive || g_unichar_toupper (pc) != g_unichar_toupper (tc)))
            break;
        }

      /* check if the pattern matches */
      if (G_UNLIKELY (*p == '\0'))
        {
          /* append the replacement to the result... */
          g_string_append (result, replacement);

          /* ...and skip to the text after the pattern */
          text = t;
        }
      else
        {
          /* just append the text char */
          g_string_append_unichar (result, g_utf8_get_char (text));
          text = g_utf8_next_char (text);
        }
    }

  return g_string_free (result, FALSE);
}



static gchar*
fmb_sbr_replace_renamer_process (FmbxRenamer  *renamer,
                                    FmbxFileInfo *file,
                                    const gchar     *text,
                                    guint            idx)
{
  FmbSbrReplaceRenamer *replace_renamer = FMB_SBR_REPLACE_RENAMER (renamer);

  /* nothing to replace if we don't have a pattern */
  if (G_UNLIKELY (replace_renamer->pattern == NULL || *replace_renamer->pattern == '\0'))
    return g_strdup (text);

  /* check if we should use regular expression */
  if (G_UNLIKELY (replace_renamer->regexp))
    {
#ifdef HAVE_PCRE
      /* check if the pattern failed to compile */
      if (G_UNLIKELY (replace_renamer->pcre_pattern == NULL))
        return g_strdup (text);

      /* just execute the pattern */
      return fmb_sbr_replace_renamer_pcre_exec (replace_renamer, text);
#endif
    }

  /* perform the replace operation */
  return tsrr_replace (text, replace_renamer->pattern, replace_renamer->replacement, replace_renamer->case_sensitive);
}



#ifdef HAVE_PCRE
static gchar*
fmb_sbr_replace_renamer_pcre_exec (FmbSbrReplaceRenamer *replace_renamer,
                                      const gchar             *subject)
{
  const gchar *r;
  GString     *result;
  gint         second;
  gint         first;
  gint         idx;
  gint        *ovec;
  gint         olen;
  gint         rc;

  /* guess an initial ovec size */
  olen = (replace_renamer->pcre_capture_count + 10) * 3;
  ovec = g_new0 (gint, olen);

  /* try to match the subject (increasing ovec on-demand) */
  for (rc = 0; rc <= 0; )
    {
      /* try to exec, will return 0 if the ovec is too small */
      rc = pcre_exec (replace_renamer->pcre_pattern, NULL, subject, strlen (subject), 0, PCRE_NOTEMPTY, ovec, olen);
      if (G_UNLIKELY (rc < 0))
        {
          /* no match or error */
          g_free (ovec);
          return g_strdup (subject);
        }
      else if (rc == 0)
        {
          /* ovec too small, try to increase */
          olen += 18;
          ovec = g_realloc (ovec, olen * sizeof (gint));
        }
    }

  /* allocate a string for the result */
  result = g_string_sized_new (32);

  /* append the text before the match */
  g_string_append_len (result, subject, ovec[0]);

  /* apply the replacement */
  for (r = replace_renamer->replacement; *r != '\0'; r = g_utf8_next_char (r))
    {
      if (G_UNLIKELY ((r[0] == '\\' || r[0] == '$') && r[1] != '\0'))
        {
          /* skip the first char ($ or \) */
          r += 1;

          /* default to no subst */
          first = 0;
          second = 0;

          /* check the char after the \ or $ */
          if (r[0] == '+' && rc > 1)
            {
              /* \+ and $+ is replaced with the last subpattern */
              first = ovec[(rc - 1) * 2];
              second = ovec[(rc - 1) * 2 + 1];
            }
          else if (r[0] == '&')
            {
              /* \& and $& is replaced with the first subpattern (the whole match) */
              first = ovec[0];
              second = ovec[1];
            }
          else if (r[0] == '`')
            {
              /* \` and $` is replaced with the text before the whole match */
              first = 0;
              second = ovec[0];
            }
          else if (r[0] == '\'')
            {
              /* \' and $' is replaced with the text after the whole match */
              first = ovec[1];
              second = strlen (subject) - 1;
            }
          else if (g_ascii_isdigit (r[0]))
            {
              /* \<num> and $<num> is replaced with the <num>th subpattern */
              idx = (r[0] - '0');
              if (G_LIKELY (idx >= 0 && idx < rc))
                {
                  first = ovec[2 * idx];
                  second = ovec[2 * idx + 1];
                }
            }
          else if (r[-1] == r[0])
            {
              /* just add the $ or \ char */
              g_string_append_c (result, r[0]);
              continue;
            }
          else
            {
              /* just ignore the $ or \ char */
              continue;
            }

          /* substitute the string */
          g_string_append_len (result, subject + first, second - first);
        }
      else
        {
          /* just append the unichar */
          g_string_append_unichar (result, g_utf8_get_char (r));
        }
    }

  /* append the text after the match */
  g_string_append (result, subject + ovec[1]);

  /* release the output vector */
  g_free (ovec);

  /* return the new name */
  return g_string_free (result, FALSE);
}



static void
fmb_sbr_replace_renamer_pcre_update (FmbSbrReplaceRenamer *replace_renamer)
{
  const gchar *error_message = NULL;
  GdkColor     back;
  GdkColor     text;
  gchar       *tooltip;
  gchar       *message;
  glong        offset;
  gint         error_offset = -1;

  /* pre-compile the pattern if regexp is enabled */
  if (G_UNLIKELY (replace_renamer->regexp))
    {
      /* release the previous pattern (if any) */
      if (G_LIKELY (replace_renamer->pcre_pattern != NULL))
        pcre_free (replace_renamer->pcre_pattern);

      /* try to compile the new pattern */
      replace_renamer->pcre_pattern = pcre_compile (replace_renamer->pattern, (replace_renamer->case_sensitive ? 0 : PCRE_CASELESS) | PCRE_UTF8,
                                                    &error_message, &error_offset, 0);
      if (G_LIKELY (replace_renamer->pcre_pattern != NULL))
        {
          /* determine the subpattern capture count */
          if (pcre_fullinfo (replace_renamer->pcre_pattern, NULL, PCRE_INFO_CAPTURECOUNT, &replace_renamer->pcre_capture_count) != 0)
            {
              /* shouldn't happen, but just to be sure */
              pcre_free (replace_renamer->pcre_pattern);
              replace_renamer->pcre_pattern = NULL;
            }
        }
    }

  /* check if there was an error compiling the pattern */
  if (G_UNLIKELY (error_message != NULL))
    {
      /* convert the message to UTF-8 */
      message = g_locale_to_utf8 (error_message, -1, NULL, NULL, NULL);
      if (G_LIKELY (message != NULL))
        {
          /* determine the UTF-8 char offset */
          offset = g_utf8_pointer_to_offset (replace_renamer->pattern, replace_renamer->pattern + error_offset);

          /* setup a tooltip with the error message */
          tooltip = g_strdup_printf (_("Invalid regular expression, at character position %ld: %s"), offset, message);
          gtk_widget_set_tooltip_text (replace_renamer->pattern_entry, tooltip);
          g_free (tooltip);
        }
      g_free (message);

      /* check if the entry is realized */
      if (gtk_widget_get_realized (replace_renamer->pattern_entry))
        {
          /* if GTK+ wouldn't be that stupid with style properties and 
           * type plugins, this would be themable, but unfortunately
           * GTK+ is totally broken, and so it's hardcoded.
           */
          gdk_color_parse ("#ff6666", &back);
          gdk_color_parse ("White", &text);

          /* setup a red background/text color to indicate the error */
          gtk_widget_modify_base (replace_renamer->pattern_entry, GTK_STATE_NORMAL, &back);
          gtk_widget_modify_text (replace_renamer->pattern_entry, GTK_STATE_NORMAL, &text);
        }
    }
  else
    {
      /* check if the entry is realized */
      if (GTK_WIDGET_REALIZED (replace_renamer->pattern_entry))
        {
          /* reset background/text color */
          gtk_widget_modify_base (replace_renamer->pattern_entry, GTK_STATE_NORMAL, NULL);
          gtk_widget_modify_text (replace_renamer->pattern_entry, GTK_STATE_NORMAL, NULL);
        }

      /* reset to default tooltip */
      gtk_widget_set_tooltip_text (replace_renamer->pattern_entry, _("Enter the text to search for in the file names."));
    }
}
#endif



/**
 * fmb_sbr_replace_renamer_new:
 *
 * Allocates a new #FmbSbrReplaceRenamer instance.
 *
 * Return value: the newly allocated #FmbSbrReplaceRenamer.
 **/
FmbSbrReplaceRenamer*
fmb_sbr_replace_renamer_new (void)
{
  return g_object_new (FMB_SBR_TYPE_REPLACE_RENAMER,
                       "name", _("Search & Replace"),
                       NULL);
}



/**
 * fmb_sbr_replace_renamer_get_case_sensitive:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 *
 * Returns %TRUE if @replace_renamer<!---->s search should
 * be case sensitive.
 *
 * Return value: %TRUE if @replace_renamer is case sensitive.
 **/
gboolean
fmb_sbr_replace_renamer_get_case_sensitive (FmbSbrReplaceRenamer *replace_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer), FALSE);
  return replace_renamer->case_sensitive;
}



/**
 * fmb_sbr_replace_renamer_set_case_sensitive:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 * @case_sensitive  : %TRUE to use case sensitive search.
 *
 * If @case_sensitive is %TRUE the search of @replace_renamer
 * will be case sensitive.
 **/
void
fmb_sbr_replace_renamer_set_case_sensitive (FmbSbrReplaceRenamer *replace_renamer,
                                               gboolean                 case_sensitive)
{
  g_return_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer));

  /* normalize the value */
  case_sensitive = !!case_sensitive;

  /* check if we have a new setting */
  if (G_LIKELY (replace_renamer->case_sensitive != case_sensitive))
    {
      /* apply the new value */
      replace_renamer->case_sensitive = case_sensitive;

#ifdef HAVE_PCRE
      /* pre-compile the pattern */
      fmb_sbr_replace_renamer_pcre_update (replace_renamer);
#endif

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (replace_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (replace_renamer), "case-sensitive");
    }
}



/**
 * fmb_sbr_replace_renamer_get_pattern:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 *
 * Returns the search pattern for @replace_renamer.
 *
 * Return value: the search pattern for @replace_renamer.
 **/
const gchar*
fmb_sbr_replace_renamer_get_pattern (FmbSbrReplaceRenamer *replace_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer), NULL);
  return replace_renamer->pattern;
}



/**
 * fmb_sbr_replace_renamer_set_pattern:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 * @pattern         : the new pattern for @replace_renamer.
 *
 * Sets the search pattern of @replace_renamer to @pattern.
 **/
void
fmb_sbr_replace_renamer_set_pattern (FmbSbrReplaceRenamer *replace_renamer,
                                        const gchar             *pattern)
{
  g_return_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer));
  g_return_if_fail (g_utf8_validate (pattern, -1, NULL));

  /* check if we have a new pattern */
  if (!blxo_str_is_equal (replace_renamer->pattern, pattern))
    {
      /* apply the new value */
      g_free (replace_renamer->pattern);
      replace_renamer->pattern = g_strdup (pattern);

#ifdef HAVE_PCRE
      /* pre-compile the pattern */
      fmb_sbr_replace_renamer_pcre_update (replace_renamer);
#endif

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (replace_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (replace_renamer), "pattern");
    }
}



/**
 * fmb_sbr_replace_renamer_get_regexp:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 *
 * Returns %TRUE if @replace_renamer should use a regular
 * expression.
 *
 * Return value: %TRUE if @replace_renamer uses a regexp.
 **/
gboolean
fmb_sbr_replace_renamer_get_regexp (FmbSbrReplaceRenamer *replace_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer), FALSE);
  return replace_renamer->regexp;
}



/**
 * fmb_sbr_replace_renamer_set_regexp:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 * @regexp          : %TRUE to use regular expressions.
 *
 * If @regexp is %TRUE a regular expression should be used
 * for @replace_renamer.
 **/
void
fmb_sbr_replace_renamer_set_regexp (FmbSbrReplaceRenamer *replace_renamer,
                                       gboolean                 regexp)
{
  g_return_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer));

  /* normalize the value */
  regexp = (!!regexp && replace_renamer->regexp_supported);

  /* check if we have a new value */
  if (G_LIKELY (replace_renamer->regexp != regexp))
    {
      /* apply the new value */
      replace_renamer->regexp = regexp;

#ifdef HAVE_PCRE
      /* pre-compile the pattern */
      fmb_sbr_replace_renamer_pcre_update (replace_renamer);
#endif

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (replace_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (replace_renamer), "regexp");
    }
}



/**
 * fmb_sbr_replace_renamer_get_replacement:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 *
 * Returns the replacement for the @replace_renamer.
 *
 * Return value: the replacement for @replace_renamer.
 **/
const gchar*
fmb_sbr_replace_renamer_get_replacement (FmbSbrReplaceRenamer *replace_renamer)
{
  g_return_val_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer), NULL);
  return replace_renamer->replacement;
}



/**
 * fmb_sbr_replace_renamer_set_replacement:
 * @replace_renamer : a #FmbSbrReplaceRenamer.
 * @replacement     : the new replacement.
 *
 * Sets the replacement of @replace_renamer to @replacement.
 **/
void
fmb_sbr_replace_renamer_set_replacement (FmbSbrReplaceRenamer *replace_renamer,
                                            const gchar             *replacement)
{
  g_return_if_fail (FMB_SBR_IS_REPLACE_RENAMER (replace_renamer));
  g_return_if_fail (g_utf8_validate (replacement, -1, NULL));

  /* check if we have a new replacement */
  if (!blxo_str_is_equal (replace_renamer->replacement, replacement))
    {
      /* apply the setting */
      g_free (replace_renamer->replacement);
      replace_renamer->replacement = g_strdup (replacement);

      /* update the renamer */
      fmbx_renamer_changed (FMBX_RENAMER (replace_renamer));

      /* notify listeners */
      g_object_notify (G_OBJECT (replace_renamer), "replacement");
    }
}



