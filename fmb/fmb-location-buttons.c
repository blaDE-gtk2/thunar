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
 *
 * Based on code written by Jonathan Blandford <jrb@gnome.org> for
 * Red Hat, Inc.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <fmb/fmb-application.h>
#include <fmb/fmb-clipboard-manager.h>
#include <fmb/fmb-create-dialog.h>
#include <fmb/fmb-gio-extensions.h>
#include <fmb/fmb-gobject-extensions.h>
#include <fmb/fmb-gtk-extensions.h>
#include <fmb/fmb-location-button.h>
#include <fmb/fmb-location-buttons.h>
#include <fmb/fmb-location-buttons-ui.h>
#include <fmb/fmb-private.h>
#include <fmb/fmb-properties-dialog.h>



#define FMB_LOCATION_BUTTONS_SCROLL_TIMEOUT  (200)



/* Property identifiers */
enum
{
  PROP_0,
  PROP_CURRENT_DIRECTORY,
  PROP_SELECTED_FILES,
  PROP_UI_MANAGER,
};



static void           fmb_location_buttons_component_init            (FmbComponentIface       *iface);
static void           fmb_location_buttons_navigator_init            (FmbNavigatorIface       *iface);
static void           fmb_location_buttons_location_bar_init         (FmbLocationBarIface     *iface);
static void           fmb_location_buttons_finalize                  (GObject                    *object);
static void           fmb_location_buttons_get_property              (GObject                    *object,
                                                                         guint                       prop_id,
                                                                         GValue                     *value,
                                                                         GParamSpec                 *pspec);
static void           fmb_location_buttons_set_property              (GObject                    *object,
                                                                         guint                       prop_id,
                                                                         const GValue               *value,
                                                                         GParamSpec                 *pspec);
static GtkUIManager  *fmb_location_buttons_get_ui_manager            (FmbComponent            *component);
static void           fmb_location_buttons_set_ui_manager            (FmbComponent            *component,
                                                                         GtkUIManager               *ui_manager);
static FmbFile    *fmb_location_buttons_get_current_directory     (FmbNavigator            *navigator);
static void           fmb_location_buttons_set_current_directory     (FmbNavigator            *navigator,
                                                                         FmbFile                 *current_directory);
static void           fmb_location_buttons_unmap                     (GtkWidget                  *widget);
static void           fmb_location_buttons_size_request              (GtkWidget                  *widget,
                                                                         GtkRequisition             *requisition);
static void           fmb_location_buttons_size_allocate             (GtkWidget                  *widget,
                                                                         GtkAllocation              *allocation);
static void           fmb_location_buttons_state_changed             (GtkWidget                  *widget,
                                                                         GtkStateType                previous_state);
static void           fmb_location_buttons_grab_notify               (GtkWidget                  *widget,
                                                                         gboolean                    was_grabbed);
static void           fmb_location_buttons_add                       (GtkContainer               *container,
                                                                         GtkWidget                  *widget);
static void           fmb_location_buttons_remove                    (GtkContainer               *container,
                                                                         GtkWidget                  *widget);
static void           fmb_location_buttons_forall                    (GtkContainer               *container,
                                                                         gboolean                    include_internals,
                                                                         GtkCallback                 callback,
                                                                         gpointer                    callback_data);
static GtkWidget     *fmb_location_buttons_make_button               (FmbLocationButtons      *buttons,
                                                                         FmbFile                 *file);
static void           fmb_location_buttons_remove_1                  (GtkContainer               *container,
                                                                         GtkWidget                  *widget);
static gboolean       fmb_location_buttons_scroll_timeout            (gpointer                    user_data);
static void           fmb_location_buttons_scroll_timeout_destroy    (gpointer                    user_data);
static void           fmb_location_buttons_stop_scrolling            (FmbLocationButtons      *buttons);
static void           fmb_location_buttons_update_sliders            (FmbLocationButtons      *buttons);
static gboolean       fmb_location_buttons_slider_button_press       (GtkWidget                  *button,
                                                                         GdkEventButton             *event,
                                                                         FmbLocationButtons      *buttons);
static gboolean       fmb_location_buttons_slider_button_release     (GtkWidget                  *button,
                                                                         GdkEventButton             *event,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_scroll_left               (GtkWidget                  *button,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_scroll_right              (GtkWidget                  *button,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_clicked                   (FmbLocationButton       *button,
                                                                         gboolean                    open_in_tab,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_context_menu              (FmbLocationButton       *button,
                                                                         GdkEventButton             *event,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_gone                      (FmbLocationButton       *button,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_create_folder      (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_down_folder        (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_empty_trash        (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_open               (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_open_in_new_tab    (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_open_in_new_window (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_paste_into_folder  (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);
static void           fmb_location_buttons_action_properties         (GtkAction                  *action,
                                                                         FmbLocationButtons      *buttons);



struct _FmbLocationButtonsClass
{
  GtkContainerClass __parent__;
};

struct _FmbLocationButtons
{
  GtkContainer __parent__;

  GtkActionGroup    *action_group;
  GtkUIManager      *ui_manager;
  guint              ui_merge_id;

  GtkWidget         *left_slider;
  GtkWidget         *right_slider;

  FmbFile        *current_directory;

  gint               slider_width;
  gboolean           ignore_click : 1;

  GList             *list;
  GList             *fake_root_button;
  GList             *first_scrolled_button;

  guint              scroll_timeout_id;
};



static GQuark fmb_file_quark = 0;



static const GtkActionEntry action_entries[] =
{
  { "location-buttons-down-folder", NULL, "Down Folder", "<alt>Down", NULL, G_CALLBACK (fmb_location_buttons_action_down_folder), },
  { "location-buttons-context-menu", NULL, "Context Menu", NULL, "", NULL, },
  { "location-buttons-open", GTK_STOCK_OPEN, N_("_Open"), "", NULL, G_CALLBACK (fmb_location_buttons_action_open), },
  { "location-buttons-open-in-new-tab", NULL, N_("Open in New Tab"), "", NULL, G_CALLBACK (fmb_location_buttons_action_open_in_new_tab), },
  { "location-buttons-open-in-new-window", NULL, N_("Open in New Window"), "", NULL, G_CALLBACK (fmb_location_buttons_action_open_in_new_window), },
  { "location-buttons-create-folder", "folder-new", N_("Create _Folder..."), "", NULL, G_CALLBACK (fmb_location_buttons_action_create_folder), },
  { "location-buttons-empty-trash", NULL, N_("_Empty Trash"), "", N_("Delete all files and folders in the Trash"), G_CALLBACK (fmb_location_buttons_action_empty_trash), },
  { "location-buttons-paste-into-folder", GTK_STOCK_PASTE, N_("Paste Into Folder"), "", NULL, G_CALLBACK (fmb_location_buttons_action_paste_into_folder), },
  { "location-buttons-properties", GTK_STOCK_PROPERTIES, N_("_Properties..."), "", NULL, G_CALLBACK (fmb_location_buttons_action_properties), },
};



G_DEFINE_TYPE_WITH_CODE (FmbLocationButtons, fmb_location_buttons, GTK_TYPE_CONTAINER,
    G_IMPLEMENT_INTERFACE (FMB_TYPE_NAVIGATOR, fmb_location_buttons_navigator_init)
    G_IMPLEMENT_INTERFACE (FMB_TYPE_COMPONENT, fmb_location_buttons_component_init)
    G_IMPLEMENT_INTERFACE (FMB_TYPE_LOCATION_BAR, fmb_location_buttons_location_bar_init))



static void
fmb_location_buttons_class_init (FmbLocationButtonsClass *klass)
{
  GtkContainerClass *gtkcontainer_class;
  GtkWidgetClass    *gtkwidget_class;
  GObjectClass      *gobject_class;

  gobject_class = G_OBJECT_CLASS (klass);
  gobject_class->finalize = fmb_location_buttons_finalize;
  gobject_class->get_property = fmb_location_buttons_get_property;
  gobject_class->set_property = fmb_location_buttons_set_property;

  gtkwidget_class = GTK_WIDGET_CLASS (klass);
  gtkwidget_class->unmap = fmb_location_buttons_unmap;
  gtkwidget_class->size_request = fmb_location_buttons_size_request;
  gtkwidget_class->size_allocate = fmb_location_buttons_size_allocate;
  gtkwidget_class->state_changed = fmb_location_buttons_state_changed;
  gtkwidget_class->grab_notify = fmb_location_buttons_grab_notify;

  gtkcontainer_class = GTK_CONTAINER_CLASS (klass);
  gtkcontainer_class->add = fmb_location_buttons_add;
  gtkcontainer_class->remove = fmb_location_buttons_remove;
  gtkcontainer_class->forall = fmb_location_buttons_forall;

  /* Override FmbNavigator's properties */
  g_object_class_override_property (gobject_class, PROP_CURRENT_DIRECTORY, "current-directory");

  /* Override FmbComponent's properties */
  g_object_class_override_property (gobject_class, PROP_SELECTED_FILES, "selected-files");
  g_object_class_override_property (gobject_class, PROP_UI_MANAGER, "ui-manager");

  /**
   * FmbLocationButtons:spacing:
   *
   * The amount of space between the path buttons.
   **/
  gtk_widget_class_install_style_property (gtkwidget_class,
                                           g_param_spec_int ("spacing",
                                                             _("Spacing"),
                                                             _("The amount of space between the path buttons"),
                                                             0, G_MAXINT, 3,
                                                             G_PARAM_READABLE));

  fmb_file_quark = g_quark_from_static_string ("button-fmb-file");
}



static void
fmb_location_buttons_component_init (FmbComponentIface *iface)
{
  iface->get_selected_files = (gpointer) blxo_noop_null;
  iface->set_selected_files = (gpointer) blxo_noop;
  iface->get_ui_manager = fmb_location_buttons_get_ui_manager;
  iface->set_ui_manager = fmb_location_buttons_set_ui_manager;
}



static void
fmb_location_buttons_navigator_init (FmbNavigatorIface *iface)
{
  iface->get_current_directory = fmb_location_buttons_get_current_directory;
  iface->set_current_directory = fmb_location_buttons_set_current_directory;
}



static void
fmb_location_buttons_location_bar_init (FmbLocationBarIface *iface)
{
  iface->accept_focus = (gpointer) blxo_noop_false;
  iface->is_standalone = (gpointer) blxo_noop_true;
}



static void
fmb_location_buttons_init (FmbLocationButtons *buttons)
{
  GtkWidget *arrow;

  /* setup the action group for the location buttons */
  buttons->action_group = gtk_action_group_new ("FmbLocationButtons");
  gtk_action_group_set_translation_domain (buttons->action_group, GETTEXT_PACKAGE);
  gtk_action_group_add_actions (buttons->action_group, action_entries, G_N_ELEMENTS (action_entries), buttons);

  gtk_widget_set_has_window (GTK_WIDGET (buttons), FALSE);
  gtk_widget_set_redraw_on_allocate (GTK_WIDGET (buttons), FALSE);

  buttons->left_slider = gtk_button_new ();
  g_signal_connect (G_OBJECT (buttons->left_slider), "button-press-event", G_CALLBACK (fmb_location_buttons_slider_button_press), buttons);
  g_signal_connect (G_OBJECT (buttons->left_slider), "button-release-event", G_CALLBACK (fmb_location_buttons_slider_button_release), buttons);
  g_signal_connect (G_OBJECT (buttons->left_slider), "clicked", G_CALLBACK (fmb_location_buttons_scroll_left), buttons);
  gtk_container_add (GTK_CONTAINER (buttons), buttons->left_slider);
  gtk_widget_show (buttons->left_slider);

  arrow = gtk_arrow_new (GTK_ARROW_LEFT, GTK_SHADOW_OUT);
  gtk_container_add (GTK_CONTAINER (buttons->left_slider), arrow);
  gtk_widget_show (arrow);

  buttons->right_slider = gtk_button_new ();
  g_signal_connect (G_OBJECT (buttons->right_slider), "button-press-event", G_CALLBACK (fmb_location_buttons_slider_button_press), buttons);
  g_signal_connect (G_OBJECT (buttons->right_slider), "button-release-event", G_CALLBACK (fmb_location_buttons_slider_button_release), buttons);
  g_signal_connect (G_OBJECT (buttons->right_slider), "clicked", G_CALLBACK (fmb_location_buttons_scroll_right), buttons);
  gtk_container_add (GTK_CONTAINER (buttons), buttons->right_slider);
  gtk_widget_show (buttons->right_slider);

  arrow = gtk_arrow_new (GTK_ARROW_RIGHT, GTK_SHADOW_OUT);
  gtk_container_add (GTK_CONTAINER (buttons->right_slider), arrow);
  gtk_widget_show (arrow);
}



static void
fmb_location_buttons_finalize (GObject *object)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (object);

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));

  /* disconnect from the selected files and the UI manager */
  fmb_component_set_selected_files (FMB_COMPONENT (buttons), NULL);
  fmb_component_set_ui_manager (FMB_COMPONENT (buttons), NULL);

  /* be sure to cancel the scrolling */
  fmb_location_buttons_stop_scrolling (buttons);

  /* release from the current_directory */
  fmb_navigator_set_current_directory (FMB_NAVIGATOR (buttons), NULL);

  /* release our action group */
  g_object_unref (G_OBJECT (buttons->action_group));

  (*G_OBJECT_CLASS (fmb_location_buttons_parent_class)->finalize) (object);
}



static void
fmb_location_buttons_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  switch (prop_id)
    {
    case PROP_CURRENT_DIRECTORY:
      g_value_set_object (value, fmb_navigator_get_current_directory (FMB_NAVIGATOR (object)));
      break;

    case PROP_SELECTED_FILES:
      g_value_set_boxed (value, fmb_component_get_selected_files (FMB_COMPONENT (object)));
      break;

    case PROP_UI_MANAGER:
      g_value_set_object (value, fmb_component_get_ui_manager (FMB_COMPONENT (object)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static void
fmb_location_buttons_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  switch (prop_id)
    {
    case PROP_CURRENT_DIRECTORY:
      fmb_navigator_set_current_directory (FMB_NAVIGATOR (object), g_value_get_object (value));
      break;

    case PROP_SELECTED_FILES:
      fmb_component_set_selected_files (FMB_COMPONENT (object), g_value_get_boxed (value));
      break;

    case PROP_UI_MANAGER:
      fmb_component_set_ui_manager (FMB_COMPONENT (object), g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}



static GtkUIManager*
fmb_location_buttons_get_ui_manager (FmbComponent *component)
{
  return FMB_LOCATION_BUTTONS (component)->ui_manager;
}



static void
fmb_location_buttons_set_ui_manager (FmbComponent *component,
                                        GtkUIManager    *ui_manager)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (component);
  GError                *error = NULL;

  /* disconnect from the previous ui manager */
  if (G_UNLIKELY (buttons->ui_manager != NULL))
    {
      /* drop our action group from the previous UI manager */
      gtk_ui_manager_remove_action_group (buttons->ui_manager, buttons->action_group);

      /* unmerge our ui controls from the previous UI manager */
      gtk_ui_manager_remove_ui (buttons->ui_manager, buttons->ui_merge_id);

      /* drop our reference on the previous UI manager */
      g_object_unref (G_OBJECT (buttons->ui_manager));
    }

  /* activate the new UI manager */
  buttons->ui_manager = ui_manager;

  /* connect to the new UI manager */
  if (G_LIKELY (ui_manager != NULL))
    {
      /* we keep a reference on the new manager */
      g_object_ref (G_OBJECT (ui_manager));

      /* add our action group to the new manager */
      gtk_ui_manager_insert_action_group (ui_manager, buttons->action_group, -1);

      /* merge our UI control items with the new manager */
      buttons->ui_merge_id = gtk_ui_manager_add_ui_from_string (ui_manager, fmb_location_buttons_ui, fmb_location_buttons_ui_length, &error);
      if (G_UNLIKELY (buttons->ui_merge_id == 0))
        {
          g_error ("Failed to merge FmbLocationButtons menus: %s", error->message);
          g_error_free (error);
        }
    }

  /* notify listeners */
  g_object_notify (G_OBJECT (buttons), "ui-manager");
}



static FmbFile*
fmb_location_buttons_get_current_directory (FmbNavigator *navigator)
{
  return FMB_LOCATION_BUTTONS (navigator)->current_directory;
}



static void
fmb_location_buttons_set_current_directory (FmbNavigator *navigator,
                                               FmbFile      *current_directory)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (navigator);
  FmbFile            *file_parent;
  FmbFile            *file;
  GtkWidget             *button;
  GList                 *lp;

  _fmb_return_if_fail (current_directory == NULL || FMB_IS_FILE (current_directory));

  /* verify that we're not already using the new directory */
  if (G_UNLIKELY (buttons->current_directory == current_directory))
    return;

  /* check if we already have a button for that directory */
  for (lp = buttons->list; lp != NULL; lp = lp->next)
    if (fmb_location_button_get_file (lp->data) == current_directory)
      break;

  /* if we already have a button for that directory, just activate it */
  if (G_UNLIKELY (lp != NULL))
    {
      /* fake a "clicked" event for that button */
      fmb_location_button_clicked (FMB_LOCATION_BUTTON (lp->data));
    }
  else
    {
      /* regenerate the button list */
      if (G_LIKELY (buttons->current_directory != NULL))
        {
          g_object_unref (G_OBJECT (buttons->current_directory));

          /* remove all buttons */
          while (buttons->list != NULL)
            gtk_container_remove (GTK_CONTAINER (buttons), buttons->list->data);

          /* clear the first scrolled and fake root buttons */
          buttons->first_scrolled_button = NULL;
          buttons->fake_root_button = NULL;
        }

      buttons->current_directory = current_directory;

      if (G_LIKELY (current_directory != NULL))
        {
          g_object_ref (G_OBJECT (current_directory));

          gtk_widget_push_composite_child ();

          /* add the new buttons */
          for (file = current_directory; file != NULL; file = file_parent)
            {
              button = fmb_location_buttons_make_button (buttons, file);
              buttons->list = g_list_append (buttons->list, button);
              gtk_container_add (GTK_CONTAINER (buttons), button);
              gtk_widget_show (button);

              /* use 'Home' as fake root button */
              if (fmb_file_is_home (file))
                buttons->fake_root_button = g_list_last (buttons->list);

              /* continue with the parent (if any) */
              file_parent = fmb_file_get_parent (file, NULL);

              if (G_LIKELY (file != current_directory))
                g_object_unref (G_OBJECT (file));
            }

          gtk_widget_pop_composite_child ();
        }
    }

  g_object_notify (G_OBJECT (buttons), "current-directory");
}



static void
fmb_location_buttons_unmap (GtkWidget *widget)
{
  /* be sure to stop the scroll timer before the buttons are unmapped */
  fmb_location_buttons_stop_scrolling (FMB_LOCATION_BUTTONS (widget));

  /* do the real unmap */
  (*GTK_WIDGET_CLASS (fmb_location_buttons_parent_class)->unmap) (widget);
}



static void
fmb_location_buttons_size_request (GtkWidget      *widget,
                                      GtkRequisition *requisition)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (widget);
  GtkRequisition         child_requisition;
  GList                 *lp;
  gint                   spacing;

  gtk_widget_style_get (GTK_WIDGET (buttons),
                        "spacing", &spacing,
                        NULL);

  requisition->width = 0;
  requisition->height = 0;

  /* calculate the size of the biggest button */
  for (lp = buttons->list; lp != NULL; lp = lp->next)
    {
      gtk_widget_size_request (GTK_WIDGET (lp->data), &child_requisition);
      requisition->width = MAX (child_requisition.width, requisition->width);
      requisition->height = MAX (child_requisition.height, requisition->height);
    }

  /* add space for the sliders if we have more than one path */
  buttons->slider_width = MIN (requisition->height * 2 / 3 + 5, requisition->height);
  if (buttons->list != NULL && buttons->list->next != NULL)
    requisition->width += (spacing + buttons->slider_width) * 2;

  gtk_widget_size_request (buttons->left_slider, &child_requisition);
  gtk_widget_size_request (buttons->right_slider, &child_requisition);

  requisition->width += GTK_CONTAINER (widget)->border_width * 2;
  requisition->height += GTK_CONTAINER (widget)->border_width * 2;

  widget->requisition = *requisition;
}



static void
fmb_location_buttons_size_allocate (GtkWidget     *widget,
                                       GtkAllocation *allocation)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (widget);
  GtkTextDirection       direction;
  GtkAllocation          child_allocation;
  GtkWidget             *child;
  gboolean               need_sliders = FALSE;
  gboolean               reached_end;
  GList                 *first_button;
  GList                 *lp;
  gint                   left_slider_offset = 0;
  gint                   right_slider_offset = 0;
  gint                   allocation_width;
  gint                   border_width;
  gint                   slider_space;
  gint                   spacing;
  gint                   width;

  widget->allocation = *allocation;

  /* if no path is set, we don't have to allocate anything */
  if (G_UNLIKELY (buttons->list == NULL))
    return;

  gtk_widget_style_get (GTK_WIDGET (buttons), "spacing", &spacing, NULL);

  direction = gtk_widget_get_direction (widget);
  border_width = GTK_CONTAINER (buttons)->border_width;
  allocation_width = allocation->width - 2 * border_width;

  /* check if we need to display the sliders */
  if (G_LIKELY (buttons->fake_root_button != NULL))
    width = buttons->slider_width + spacing;
  else
    width = 0;

  for (lp = buttons->list; lp != NULL; lp = lp->next)
    {
      width += GTK_WIDGET (lp->data)->requisition.width + spacing;
      if (lp == buttons->fake_root_button)
        break;
    }

  if (width <= allocation_width)
    {
      if (G_LIKELY (buttons->fake_root_button != NULL))
        first_button = buttons->fake_root_button;
      else
        first_button = g_list_last (buttons->list);
    }
  else
    {
      slider_space = 2 * (spacing + buttons->slider_width);

      if (buttons->first_scrolled_button != NULL)
        first_button = buttons->first_scrolled_button;
      else
        first_button = buttons->list;
      need_sliders = TRUE;

      /* To see how much space we have, and how many buttons we can display.
       * We start at the first button, count forward until hit the new
       * button, then count backwards.
       */
      width = GTK_WIDGET (first_button->data)->requisition.width;
      for (lp = first_button->prev, reached_end = FALSE; lp != NULL && !reached_end; lp = lp->prev)
        {
          child = lp->data;
          if (width + child->requisition.width + spacing + slider_space > allocation_width)
            reached_end = TRUE;
          else if (lp == buttons->fake_root_button)
            break;
          else
            width += child->requisition.width + spacing;
        }

      while (first_button->next != NULL && !reached_end)
        {
          child = first_button->data;
          if (width + child->requisition.width + spacing + slider_space > allocation_width)
            {
              reached_end = TRUE;
            }
          else
            {
              width += child->requisition.width + spacing;
              if (first_button == buttons->fake_root_button)
                break;
              first_button = first_button->next;
            }
        }
    }

  /* Now we allocate space to the buttons */
  child_allocation.y = allocation->y + border_width;
  child_allocation.height = MAX (1, allocation->height - border_width * 2);

  if (G_UNLIKELY (direction == GTK_TEXT_DIR_RTL))
    {
      child_allocation.x = allocation->x + allocation->width - border_width;
      if (need_sliders || buttons->fake_root_button != NULL)
        {
          child_allocation.x -= spacing + buttons->slider_width;
          left_slider_offset = allocation->width - border_width - buttons->slider_width;
        }
    }
  else
    {
      child_allocation.x = allocation->x + border_width;
      if (need_sliders || buttons->fake_root_button != NULL)
        {
          left_slider_offset = border_width;
          child_allocation.x += spacing + buttons->slider_width;
        }
    }

  for (lp = first_button; lp != NULL; lp = lp->prev)
    {
      child = lp->data;

      child_allocation.width = child->requisition.width;
      if (G_UNLIKELY (direction == GTK_TEXT_DIR_RTL))
        child_allocation.x -= child_allocation.width;

      /* check to see if we don't have any more space to allocate buttons */
      if (need_sliders && direction == GTK_TEXT_DIR_RTL)
        {
          if (child_allocation.x - spacing - buttons->slider_width < widget->allocation.x + border_width)
            break;
        }
      else if (need_sliders && direction == GTK_TEXT_DIR_LTR)
        {
          if (child_allocation.x + child_allocation.width + spacing + buttons->slider_width > widget->allocation.x + border_width + allocation_width)
            break;
        }

      gtk_widget_set_child_visible (GTK_WIDGET (lp->data), TRUE);
      gtk_widget_size_allocate (child, &child_allocation);

      if (direction == GTK_TEXT_DIR_RTL)
        {
          child_allocation.x -= spacing;
          right_slider_offset = border_width;
        }
      else
        {
          child_allocation.x += child_allocation.width + spacing;
          right_slider_offset = allocation->width - border_width - buttons->slider_width;
        }
    }

  /* now we go hide all the buttons that don't fit */
  for (; lp != NULL; lp = lp->prev)
    gtk_widget_set_child_visible (GTK_WIDGET (lp->data), FALSE);
  for (lp = first_button->next; lp != NULL; lp = lp->next)
    gtk_widget_set_child_visible (GTK_WIDGET (lp->data), FALSE);

  if (need_sliders || buttons->fake_root_button != NULL)
    {
      child_allocation.width = buttons->slider_width;
      child_allocation.x = left_slider_offset + allocation->x;
      gtk_widget_size_allocate (buttons->left_slider, &child_allocation);
      gtk_widget_set_child_visible (buttons->left_slider, TRUE);
      gtk_widget_show_all (buttons->left_slider);

      fmb_location_buttons_update_sliders (buttons);
    }
  else
    {
      gtk_widget_set_child_visible (buttons->left_slider, FALSE);
    }

  if (need_sliders)
    {
      child_allocation.width = buttons->slider_width;
      child_allocation.x = right_slider_offset + allocation->x;
      gtk_widget_size_allocate (buttons->right_slider, &child_allocation);
      gtk_widget_set_child_visible (buttons->right_slider, TRUE);
      gtk_widget_show_all (buttons->right_slider);

      fmb_location_buttons_update_sliders (buttons);
    }
  else
    {
      gtk_widget_set_child_visible (buttons->right_slider, FALSE);
    }
}



static void
fmb_location_buttons_state_changed (GtkWidget   *widget,
                                       GtkStateType previous_state)
{
  if (!gtk_widget_is_sensitive (widget))
    fmb_location_buttons_stop_scrolling (FMB_LOCATION_BUTTONS (widget));
}



static void
fmb_location_buttons_grab_notify (GtkWidget *widget,
                                     gboolean   was_grabbed)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (widget);

  if (!was_grabbed)
    fmb_location_buttons_stop_scrolling (buttons);
}



static void
fmb_location_buttons_add (GtkContainer *container,
                             GtkWidget    *widget)
{
  gtk_widget_set_parent (widget, GTK_WIDGET (container));
}



static void
fmb_location_buttons_remove (GtkContainer *container,
                                GtkWidget    *widget)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (container);
  GList                 *lp;

  if (widget == buttons->left_slider)
    {
      fmb_location_buttons_remove_1 (container, widget);
      buttons->left_slider = NULL;
      return;
    }
  else if (widget == buttons->right_slider)
    {
      fmb_location_buttons_remove_1 (container, widget);
      buttons->right_slider = NULL;
      return;
    }

  for (lp = buttons->list; lp != NULL; lp = lp->next)
    if (widget == GTK_WIDGET (lp->data))
      {
        fmb_location_buttons_remove_1 (container, widget);
        buttons->list = g_list_remove_link (buttons->list, lp);
        g_list_free (lp);
        return;
      }
}



static void
fmb_location_buttons_forall (GtkContainer *container,
                                gboolean      include_internals,
                                GtkCallback   callback,
                                gpointer      callback_data)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (container);
  GtkWidget             *child;
  GList                 *lp;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (callback != NULL);

  for (lp = buttons->list; lp != NULL; )
    {
      child = GTK_WIDGET (lp->data);
      lp = lp->next;

      (*callback) (child, callback_data);
    }

  if (buttons->left_slider != NULL && include_internals)
    (*callback) (buttons->left_slider, callback_data);

  if (buttons->right_slider != NULL && include_internals)
    (*callback) (buttons->right_slider, callback_data);
}



static GtkWidget*
fmb_location_buttons_make_button (FmbLocationButtons *buttons,
                                     FmbFile            *file)
{
  GtkWidget *button;

  /* allocate the button */
  button = fmb_location_button_new ();

  /* connect to the file */
  fmb_location_button_set_file (FMB_LOCATION_BUTTON (button), file);

  /* the current directory is active */
  fmb_location_button_set_active (FMB_LOCATION_BUTTON (button), (file == buttons->current_directory));

  /* connect signal handlers */
  g_signal_connect (G_OBJECT (button), "clicked", G_CALLBACK (fmb_location_buttons_clicked), buttons);
  g_signal_connect (G_OBJECT (button), "gone", G_CALLBACK (fmb_location_buttons_gone), buttons);
  g_signal_connect (G_OBJECT (button), "context-menu", G_CALLBACK (fmb_location_buttons_context_menu), buttons);

  return button;
}



static void
fmb_location_buttons_remove_1 (GtkContainer *container,
                                  GtkWidget    *widget)
{
  gboolean need_resize = gtk_widget_get_visible (widget);
  gtk_widget_unparent (widget);
  if (G_LIKELY (need_resize))
    gtk_widget_queue_resize (GTK_WIDGET (container));
}



static gboolean
fmb_location_buttons_scroll_timeout (gpointer user_data)
{
  FmbLocationButtons *buttons = FMB_LOCATION_BUTTONS (user_data);

  GDK_THREADS_ENTER ();

  if (gtk_widget_has_focus (buttons->left_slider))
    fmb_location_buttons_scroll_left (buttons->left_slider, buttons);
  else if (gtk_widget_has_focus (buttons->right_slider))
    fmb_location_buttons_scroll_right (buttons->right_slider, buttons);

  GDK_THREADS_LEAVE ();

  return TRUE;
}



static void
fmb_location_buttons_scroll_timeout_destroy (gpointer user_data)
{
  FMB_LOCATION_BUTTONS (user_data)->scroll_timeout_id = 0;
}



static void
fmb_location_buttons_stop_scrolling (FmbLocationButtons *buttons)
{
  if (buttons->scroll_timeout_id != 0)
    g_source_remove (buttons->scroll_timeout_id);
}



static void
fmb_location_buttons_update_sliders (FmbLocationButtons *buttons)
{
  GtkWidget *button;

  if (G_LIKELY (buttons->list != NULL))
    {
      button = GTK_WIDGET (buttons->list->data);
      if (gtk_widget_get_child_visible (button))
        {
          gtk_widget_set_sensitive (buttons->right_slider, FALSE);
          fmb_location_buttons_stop_scrolling (buttons);
        }
      else
        {
          gtk_widget_set_sensitive (buttons->right_slider, TRUE);
        }

      button = GTK_WIDGET (g_list_last (buttons->list)->data);
      if (gtk_widget_get_child_visible (button) && buttons->fake_root_button == NULL)
        {
          gtk_widget_set_sensitive (buttons->left_slider, FALSE);
          fmb_location_buttons_stop_scrolling (buttons);
        }
      else
        {
          gtk_widget_set_sensitive (buttons->left_slider, TRUE);
        }
    }
}



static gboolean
fmb_location_buttons_slider_button_press (GtkWidget             *button,
                                             GdkEventButton        *event,
                                             FmbLocationButtons *buttons)
{
  if (!gtk_widget_has_focus (button))
    gtk_widget_grab_focus (button);

  if (event->type != GDK_BUTTON_PRESS || event->button != 1)
    return FALSE;

  buttons->ignore_click = FALSE;

  if (button == buttons->left_slider)
    fmb_location_buttons_scroll_left (button, buttons);
  else if (button == buttons->right_slider)
    fmb_location_buttons_scroll_right (button, buttons);

  if (G_LIKELY (buttons->scroll_timeout_id == 0))
    {
      buttons->scroll_timeout_id = g_timeout_add_full (G_PRIORITY_LOW, FMB_LOCATION_BUTTONS_SCROLL_TIMEOUT,
                                                       fmb_location_buttons_scroll_timeout, buttons,
                                                       fmb_location_buttons_scroll_timeout_destroy);
    }

  return FALSE;
}



static gboolean
fmb_location_buttons_slider_button_release (GtkWidget             *button,
                                               GdkEventButton        *event,
                                               FmbLocationButtons *buttons)
{
  if (event->type == GDK_BUTTON_RELEASE)
    {
      fmb_location_buttons_stop_scrolling (buttons);
      buttons->ignore_click = TRUE;
    }

  return FALSE;
}



static void
fmb_location_buttons_scroll_left (GtkWidget             *button,
                                     FmbLocationButtons *buttons)
{
  GList *lp;

  if (G_UNLIKELY (buttons->ignore_click))
    {
      buttons->ignore_click = FALSE;
      return;
    }

  gtk_widget_queue_resize (GTK_WIDGET (buttons));

  for (lp = g_list_last (buttons->list); lp != NULL; lp = lp->prev)
    if (lp->prev != NULL && gtk_widget_get_child_visible (GTK_WIDGET (lp->prev->data)))
      {
        if (lp->prev == buttons->fake_root_button)
          buttons->fake_root_button = NULL;
        buttons->first_scrolled_button = lp;
        break;
      }
}



static void
fmb_location_buttons_scroll_right (GtkWidget             *button,
                                      FmbLocationButtons *buttons)
{
  GtkTextDirection direction;
  GList           *right_button = NULL;
  GList           *left_button = NULL;
  GList           *lp;
  gint             space_available;
  gint             space_needed;
  gint             border_width;
  gint             spacing;

  if (G_UNLIKELY (buttons->ignore_click))
    {
      buttons->ignore_click = FALSE;
      return;
    }

  gtk_widget_style_get (GTK_WIDGET (buttons),
                        "spacing", &spacing,
                        NULL);

  gtk_widget_queue_resize (GTK_WIDGET (buttons));

  border_width = GTK_CONTAINER (buttons)->border_width;
  direction = gtk_widget_get_direction (GTK_WIDGET (buttons));

  /* find the button at the 'right' end that we have to make visible */
  for (lp = buttons->list; lp != NULL; lp = lp->next)
    if (lp->next != NULL && gtk_widget_get_child_visible (GTK_WIDGET (lp->next->data)))
      {
        right_button = lp;
        break;
      }

  if (G_UNLIKELY (right_button == NULL))
    return;

  /* find the last visible button on the 'left' end */
  for (lp = g_list_last (buttons->list); lp != NULL; lp = lp->prev)
    if (gtk_widget_get_child_visible (GTK_WIDGET (lp->data)))
      {
        left_button = lp;
        break;
      }

  space_needed = GTK_WIDGET (right_button->data)->allocation.width + spacing;
  if (direction == GTK_TEXT_DIR_RTL)
    {
      space_available = buttons->right_slider->allocation.x - GTK_WIDGET (buttons)->allocation.x;
    }
  else
    {
      space_available = (GTK_WIDGET (buttons)->allocation.x + GTK_WIDGET (buttons)->allocation.width - border_width)
                      - (buttons->right_slider->allocation.x + buttons->right_slider->allocation.width);
    }

  if (G_UNLIKELY (left_button == NULL))
    return;

  /* We have space_available extra space that's not being used.  We
   * need space_needed space to make the button fit.  So we walk down
   * from the end, removing buttons until we get all the space we
   * need.
   */
  while (space_available < space_needed)
    {
      space_available += GTK_WIDGET (left_button->data)->allocation.width + spacing;
      left_button = left_button->prev;
      buttons->first_scrolled_button = left_button;
    }
}



static void
fmb_location_buttons_clicked (FmbLocationButton  *button,
                                 gboolean               open_in_tab,
                                 FmbLocationButtons *buttons)
{
  FmbFile *directory;
  GList      *lp;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTON (button));
  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));

  /* determine the directory associated with the clicked button */
  directory = fmb_location_button_get_file (button);

  /* open in tab */
  if (open_in_tab)
    {
      fmb_navigator_open_new_tab (FMB_NAVIGATOR (buttons), directory);
      return;
    }

  /* disconnect from previous current directory (if any) */
  if (G_LIKELY (buttons->current_directory != NULL))
    g_object_unref (G_OBJECT (buttons->current_directory));

  /* setup the new current directory */
  buttons->current_directory = directory;

  /* take a reference on the new directory (if any) */
  if (G_LIKELY (directory != NULL))
    g_object_ref (G_OBJECT (directory));

  /* check if the button is visible on the button bar */
  if (!gtk_widget_get_child_visible (GTK_WIDGET (button)))
    {
      /* scroll to the button */
      buttons->first_scrolled_button = g_list_find (buttons->list, button);
      gtk_widget_queue_resize (GTK_WIDGET (buttons));

      /* we may need to reset the fake_root_button */
      if (G_LIKELY (buttons->fake_root_button != NULL))
        {
          /* check if the fake_root_button is before the first_scrolled_button (from right to left) */
          for (lp = buttons->list; lp != NULL && lp != buttons->first_scrolled_button; lp = lp->next)
            if (lp == buttons->fake_root_button)
              {
                /* reset the fake_root_button */
                buttons->fake_root_button = NULL;
                break;
              }
        }
    }

  /* update all buttons */
  for (lp = buttons->list; lp != NULL; lp = lp->next)
    {
      /* determine the directory for this button */
      button = FMB_LOCATION_BUTTON (lp->data);
      directory = fmb_location_button_get_file (button);

      /* update the location button state (making sure to not recurse with the "clicked" handler) */
      g_signal_handlers_block_by_func (G_OBJECT (button), fmb_location_buttons_clicked, buttons);
      fmb_location_button_set_active (button, directory == buttons->current_directory);
      g_signal_handlers_unblock_by_func (G_OBJECT (button), fmb_location_buttons_clicked, buttons);
    }

  /* notify the surrounding module that we want to change to a different directory.  */
  fmb_navigator_change_directory (FMB_NAVIGATOR (buttons), buttons->current_directory);
}



static void
fmb_location_buttons_gone (FmbLocationButton  *button,
                              FmbLocationButtons *buttons)
{
  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTON (button));
  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (g_list_find (buttons->list, button) != NULL);

  /* drop all buttons up to the button that emitted the "gone" signal */
  while (buttons->list->data != button)
    gtk_widget_destroy (buttons->list->data);

  /* drop the button itself */
  gtk_widget_destroy (GTK_WIDGET (button));
}



static void
fmb_location_buttons_context_menu (FmbLocationButton  *button,
                                      GdkEventButton        *event,
                                      FmbLocationButtons *buttons)
{
  FmbClipboardManager *clipboard;
  const gchar            *display_name;
  FmbFile             *file;
  GtkAction              *action;
  GtkWidget              *menu;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTON (button));

  /* determine the file for the button */
  file = fmb_location_button_get_file (button);
  if (G_LIKELY (file != NULL))
    {
      /* we cannot popup the context menu without an ui manager */
      if (G_LIKELY (buttons->ui_manager != NULL))
        {
          /* determine the display name of the file */
          display_name = fmb_file_get_display_name (file);

          /* be sure to keep a reference on the navigation bar */
          g_object_ref (G_OBJECT (buttons));

          /* grab a reference on the clipboard manager for this display */
          clipboard = fmb_clipboard_manager_get_for_display (gtk_widget_get_display (GTK_WIDGET (buttons)));

          /* setup the "Open" action */
          action = gtk_action_group_get_action (buttons->action_group, "location-buttons-open");
          fmb_gtk_action_set_tooltip (action, _("Open \"%s\" in this window"), display_name);
          g_object_set_qdata_full (G_OBJECT (action), fmb_file_quark, g_object_ref (G_OBJECT (file)), (GDestroyNotify) g_object_unref);
          gtk_action_set_sensitive (action, (file != buttons->current_directory));

          /* setup the "Open in New Window" action */
          action = gtk_action_group_get_action (buttons->action_group, "location-buttons-open-in-new-window");
          fmb_gtk_action_set_tooltip (action, _("Open \"%s\" in a new window"), display_name);
          g_object_set_qdata_full (G_OBJECT (action), fmb_file_quark, g_object_ref (G_OBJECT (file)), (GDestroyNotify) g_object_unref);

          /* setup the "Open in New Tab" action */
          action = gtk_action_group_get_action (buttons->action_group, "location-buttons-open-in-new-tab");
          fmb_gtk_action_set_tooltip (action, _("Open \"%s\" in a new tab"), display_name);
          g_object_set_qdata_full (G_OBJECT (action), fmb_file_quark, g_object_ref (G_OBJECT (file)), (GDestroyNotify) g_object_unref);

          /* setup the "Create Folder..." action */
          action = gtk_action_group_get_action (buttons->action_group, "location-buttons-create-folder");
          fmb_gtk_action_set_tooltip (action, _("Create a new folder in \"%s\""), display_name);
          g_object_set_qdata_full (G_OBJECT (action), fmb_file_quark, g_object_ref (G_OBJECT (file)), (GDestroyNotify) g_object_unref);
          gtk_action_set_sensitive (action, fmb_file_is_writable (file));
          gtk_action_set_visible (action, !fmb_file_is_trashed (file));

          /* setup the "Empty Trash" action */
          action = gtk_action_group_get_action (buttons->action_group, "location-buttons-empty-trash");
          gtk_action_set_visible (action, (fmb_file_is_root (file) && fmb_file_is_trashed (file)));
          gtk_action_set_sensitive (action, (fmb_file_get_size (file) > 0));

          /* setup the "Paste Into Folder" action */
          action = gtk_action_group_get_action (buttons->action_group, "location-buttons-paste-into-folder");
          fmb_gtk_action_set_tooltip (action, _("Move or copy files previously selected by a Cut or Copy command into \"%s\""), display_name);
          g_object_set_qdata_full (G_OBJECT (action), fmb_file_quark, g_object_ref (G_OBJECT (file)), (GDestroyNotify) g_object_unref);
          gtk_action_set_sensitive (action, fmb_clipboard_manager_get_can_paste (clipboard));

          /* setup the "Properties..." action */
          action = gtk_action_group_get_action (buttons->action_group, "location-buttons-properties");
          fmb_gtk_action_set_tooltip (action, _("View the properties of the folder \"%s\""), display_name);
          g_object_set_qdata_full (G_OBJECT (action), fmb_file_quark, g_object_ref (G_OBJECT (file)), (GDestroyNotify) g_object_unref);

          /* run the menu on the screen on the buttons' screen */
          menu = gtk_ui_manager_get_widget (buttons->ui_manager, "/location-buttons-context-menu");
          fmb_gtk_menu_run (GTK_MENU (menu), GTK_WIDGET (buttons), NULL, NULL, event->button, event->time);

          /* cleanup */
          g_object_unref (G_OBJECT (buttons));
          g_object_unref (G_OBJECT (clipboard));
        }
    }
}



static void
fmb_location_buttons_action_create_folder (GtkAction             *action,
                                              FmbLocationButtons *buttons)
{
  FmbApplication *application;
  FmbFile        *directory;
  GList              path_list;
  gchar             *name;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* determine the directory for the action */
  directory = g_object_get_qdata (G_OBJECT (action), fmb_file_quark);
  if (G_UNLIKELY (directory == NULL))
    return;

  /* ask the user to enter a name for the new folder */
  name = fmb_show_create_dialog (GTK_WIDGET (buttons), 
                                    "inode/directory", 
                                    _("New Folder"), 
                                    _("Create New Folder"));
  if (G_LIKELY (name != NULL))
    {
      /* fake the path list */
      path_list.data = g_file_resolve_relative_path (fmb_file_get_file (directory), name);
      path_list.next = path_list.prev = NULL;

      /* launch the operation */
      application = fmb_application_get ();
      fmb_application_mkdir (application, GTK_WIDGET (buttons), &path_list, NULL);
      g_object_unref (G_OBJECT (application));

      /* release the path */
      g_object_unref (path_list.data);

      /* release the file name */
      g_free (name);
    }
}



static void
fmb_location_buttons_action_down_folder (GtkAction             *action,
                                            FmbLocationButtons *buttons)
{
  GList *lp;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* lookup the active button */
  for (lp = buttons->list; lp != NULL; lp = lp->next)
    if (fmb_location_button_get_active (lp->data))
      {
        /* check if we have a folder below that one */
        if (G_LIKELY (lp->prev != NULL))
          {
            /* enter that folder */
            fmb_location_button_clicked (lp->prev->data);
          }

        break;
      }
}



static void
fmb_location_buttons_action_empty_trash (GtkAction             *action,
                                            FmbLocationButtons *buttons)
{
  FmbApplication *application;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* launch the operation */
  application = fmb_application_get ();
  fmb_application_empty_trash (application, GTK_WIDGET (buttons), NULL);
  g_object_unref (G_OBJECT (application));
}



static void
fmb_location_buttons_action_open (GtkAction             *action,
                                     FmbLocationButtons *buttons)
{
  FmbFile *directory;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* determine the directory for the action */
  directory = g_object_get_qdata (G_OBJECT (action), fmb_file_quark);
  if (G_LIKELY (directory != NULL && fmb_file_is_directory (directory)))
    {
      /* open the folder in this window */
      fmb_navigator_change_directory (FMB_NAVIGATOR (buttons), directory);
    }
}



static void
fmb_location_buttons_action_open_in_new_tab (GtkAction             *action,
                                                FmbLocationButtons *buttons)
{
  FmbFile *directory;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* determine the directory for the action */
  directory = g_object_get_qdata (G_OBJECT (action), fmb_file_quark);
  if (G_LIKELY (directory != NULL))
    {
      /* open tab in thsi window */
      fmb_navigator_open_new_tab (FMB_NAVIGATOR (buttons), directory);
    }
}



static void
fmb_location_buttons_action_open_in_new_window (GtkAction             *action,
                                                   FmbLocationButtons *buttons)
{
  FmbApplication *application;
  FmbFile        *directory;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* determine the directory for the action */
  directory = g_object_get_qdata (G_OBJECT (action), fmb_file_quark);
  if (G_LIKELY (directory != NULL))
    {
      /* open a new window for the directory */
      application = fmb_application_get ();
      fmb_application_open_window (application, directory, gtk_widget_get_screen (GTK_WIDGET (buttons)), NULL);
      g_object_unref (G_OBJECT (application));
    }
}



static void
fmb_location_buttons_action_paste_into_folder (GtkAction             *action,
                                                  FmbLocationButtons *buttons)
{
  FmbClipboardManager *clipboard;
  FmbFile             *directory;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* determine the directory for the action */
  directory = g_object_get_qdata (G_OBJECT (action), fmb_file_quark);
  if (G_LIKELY (directory != NULL))
    {
      /* paste files from the clipboard to the folder represented by this button */
      clipboard = fmb_clipboard_manager_get_for_display (gtk_widget_get_display (GTK_WIDGET (buttons)));
      fmb_clipboard_manager_paste_files (clipboard, fmb_file_get_file (directory), GTK_WIDGET (buttons), NULL);
      g_object_unref (G_OBJECT (clipboard));
    }
}



static void
fmb_location_buttons_action_properties (GtkAction             *action,
                                           FmbLocationButtons *buttons)
{
  FmbFile *directory;
  GtkWidget  *toplevel;
  GtkWidget  *dialog;

  _fmb_return_if_fail (FMB_IS_LOCATION_BUTTONS (buttons));
  _fmb_return_if_fail (GTK_IS_ACTION (action));

  /* determine the directory for the action */
  directory = g_object_get_qdata (G_OBJECT (action), fmb_file_quark);
  if (G_LIKELY (directory != NULL))
    {
      /* determine the toplevel window */
      toplevel = gtk_widget_get_toplevel (GTK_WIDGET (buttons));
      if (G_LIKELY (toplevel != NULL && gtk_widget_get_toplevel (toplevel)))
        {
          /* popup the properties dialog */
          dialog = fmb_properties_dialog_new (GTK_WINDOW (toplevel));
          fmb_properties_dialog_set_file (FMB_PROPERTIES_DIALOG (dialog), directory);
          gtk_widget_show (dialog);
        }
    }
}

