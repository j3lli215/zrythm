/*
 * Copyright (C) 2018-2021 Alexandros Theodotou <alex at zrythm dot org>
 *
 * This file is part of Zrythm
 *
 * Zrythm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Zrythm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Zrythm.  If not, see <https://www.gnu.org/licenses/>.
 */

/**
 * \file
 *
 * GTK utils.
 */

#ifndef __UTILS_GTK_H__
#define __UTILS_GTK_H__

#include <stdbool.h>

#include <gtk/gtk.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtksourceview/gtksource.h>
#pragma GCC diagnostic pop

/**
 * @addtogroup utils
 *
 * @{
 */

#define DESTROY_LATER(x) \
  g_idle_add ( \
    (GSourceFunc) z_gtk_widget_destroy_idle, \
      GTK_WIDGET (x));

#define DEFAULT_CLIPBOARD \
  gtk_clipboard_get_default ( \
    gdk_display_get_default ())

#define CREATE_MIDI_LEARN_MENU_ITEM \
  z_gtk_create_menu_item ( \
    _("MIDI learn"), "midi-logo", false, NULL)

#define CREATE_CUT_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("Cu_t"), "edit-cut", false, action)

#define CREATE_COPY_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("_Copy"), "edit-copy", false, action)

#define CREATE_PASTE_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("_Paste"), "edit-paste", false, action)

#define CREATE_DELETE_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("_Delete"), "edit-delete", false, action)

#define CREATE_CLEAR_SELECTION_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    /* TRANSLATORS: deselects everything */ \
    _("Cle_ar Selection"), "edit-clear", \
    false, action)

#define CREATE_SELECT_ALL_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("Select A_ll"), "edit-select-all", \
    false, action)

#define CREATE_DUPLICATE_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("Duplicate"), "edit-duplicate", false, action)

#define CREATE_MUTE_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("Mute"), "mute", false, action)

#define CREATE_UNMUTE_MENU_ITEM(action) \
  z_gtk_create_menu_item ( \
    _("Unmute"), NULL, false, action)

#define z_gtk_assistant_set_current_page_complete( \
  assistant, complete) \
  gtk_assistant_set_page_complete ( \
    GTK_ASSISTANT (assistant), \
    gtk_assistant_get_nth_page ( \
      GTK_ASSISTANT (assistant), \
      gtk_assistant_get_current_page ( \
        GTK_ASSISTANT (assistant))), \
    complete);

typedef enum IconType IconType;

enum ZGtkResize
{
  Z_GTK_NO_RESIZE,
  Z_GTK_RESIZE
};

enum ZGtkShrink
{
  Z_GTK_NO_SHRINK,
  Z_GTK_SHRINK
};

static inline GtkWidget *
z_gtk_notebook_get_current_page_widget (
  GtkNotebook * notebook)
{
  return
    gtk_notebook_get_nth_page (
      notebook,
      gtk_notebook_get_current_page (notebook));
}

static inline GtkWidget *
z_gtk_notebook_get_current_tab_label_widget (
  GtkNotebook * notebook)
{
  return
    gtk_notebook_get_tab_label (
      notebook,
      z_gtk_notebook_get_current_page_widget (
        notebook));
}

int
z_gtk_get_primary_monitor_scale_factor (void);

int
z_gtk_get_primary_monitor_refresh_rate (void);

bool
z_gtk_is_wayland (void);

void
z_gtk_tree_view_remove_all_columns (
  GtkTreeView * treeview);

int
z_gtk_widget_destroy_idle (
  GtkWidget * widget);

/**
 * @note Bumps reference, must be decremented after
 * calling.
 */
void
z_gtk_container_remove_all_children (
  GtkContainer * container);

void
z_gtk_container_destroy_all_children (
  GtkContainer * container);

void
z_gtk_container_remove_children_of_type (
  GtkContainer * container,
  GType          type);

void
z_gtk_overlay_add_if_not_exists (
  GtkOverlay * overlay,
  GtkWidget *  widget);

/**
 * Returns the primary or secondary label of the
 * given GtkMessageDialog.
 *
 * @param secondary 0 for primary, 1 for secondary.
 */
GtkLabel *
z_gtk_message_dialog_get_label (
  GtkMessageDialog * self,
  const int          secondary);

/**
 * Configures a simple value-text combo box using
 * the given model.
 */
void
z_gtk_configure_simple_combo_box (
  GtkComboBox * cb,
  GtkTreeModel * model);

/**
 * Sets the icon name and optionally text.
 */
void
z_gtk_button_set_icon_name (
  GtkButton *  btn,
  const char * name);

/**
 * Sets the icon name and optionally text.
 */
void
z_gtk_button_set_icon_name_and_text (
  GtkButton *  btn,
  const char * name,
  const char * text,
  bool         icon_first,
  GtkOrientation orientation,
  int          spacing);

/**
 * Creates a button with the given icon name.
 */
GtkButton *
z_gtk_button_new_with_icon (
  const char * name);

/**
 * Creates a toggle button with the given icon name.
 */
GtkToggleButton *
z_gtk_toggle_button_new_with_icon (
  const char * name);

/**
 * Creates a toggle button with the given icon name.
 */
GtkToggleButton *
z_gtk_toggle_button_new_with_icon_and_text (
  const char * name,
  const char * text,
  bool         icon_first,
  GtkOrientation orientation,
  int          spacing);

/**
 * Creates a button with the given icon name and
 * text.
 */
GtkButton *
z_gtk_button_new_with_icon_and_text (
  const char * name,
  const char * text,
  bool         icon_first,
  GtkOrientation orientation,
  int          spacing);

/**
 * Creates a button with the given resource name as icon.
 */
GtkButton *
z_gtk_button_new_with_resource (
  IconType  icon_type,
  const char * name);

/**
 * Creates a toggle button with the given resource name as
 * icon.
 */
GtkToggleButton *
z_gtk_toggle_button_new_with_resource (
  IconType  icon_type,
  const char * name);

#define z_gtk_create_menu_item( \
  lbl_name,icn_name,is_toggle,action_name) \
  z_gtk_create_menu_item_full ( \
    lbl_name, icn_name, 0, NULL, is_toggle, \
    action_name)

/**
 * Creates a menu item.
 */
GtkMenuItem *
z_gtk_create_menu_item_full (
  const gchar *   label_name,
  const gchar *   icon_name,
  IconType        resource_icon_type,
  const gchar *   resource,
  bool            is_toggle,
  const char *    action_name);

/**
 * Returns a pointer stored at the given selection.
 */
void *
z_gtk_get_single_selection_pointer (
  GtkTreeView * tv,
  int           column);

/**
 * Returns the label from a given GtkMenuItem.
 *
 * The menu item must have a box with an optional
 * icon and a label inside.
 */
GtkLabel *
z_gtk_get_label_from_menu_item (
  GtkMenuItem * mi);

/**
 * Gets the tooltip for the given action on the
 * given widget.
 *
 * If the action is valid, an orange text showing
 * the accelerator will be added to the tooltip.
 *
 * @return A new string that must be free'd with
 *   g_free().
 */
char *
z_gtk_get_tooltip_for_action (
  const char * detailed_action,
  const char * tooltip);

/**
 * Sets the tooltip for the given action on the
 * given widget.
 *
 * If the action is valid, an orange text showing
 * the accelerator will be added to the tooltip.
 */
void
z_gtk_widget_set_tooltip_for_action (
  GtkWidget *  widget,
  const char * detailed_action,
  const char * tooltip);

/**
 * Sets the tooltip and finds the accel keys and
 * appends them to the tooltip in small text.
 */
void
z_gtk_set_tooltip_for_actionable (
  GtkActionable * actionable,
  const char *    tooltip);

/**
 * Changes the size of the icon inside tool buttons.
 */
void
z_gtk_tool_button_set_icon_size (
  GtkToolButton * toolbutton,
  GtkIconSize     icon_size);

/**
 * Adds the given style class to the widget.
 */
void
z_gtk_widget_add_style_class (
  GtkWidget   *widget,
  const gchar *class_name);

/**
 * Removes the given style class from the widget.
 */
void
z_gtk_widget_remove_style_class (
  GtkWidget   *widget,
  const gchar *class_name);

/**
 * Gets the GdkDevice for a GtkWidget.
 */
static inline GdkDevice *
z_gtk_widget_get_device (
  GtkWidget * widget)
{
  return (gdk_seat_get_pointer (
    gdk_display_get_default_seat (
      gtk_widget_get_display (widget))));
}

static inline GdkScreen *
z_gtk_widget_get_screen (
  GtkWidget * widget)
{
  GdkScreen * screen =
    gdk_visual_get_screen (
      gdk_window_get_visual (
        gtk_widget_get_window (widget)));
  return screen;
}

static inline GdkWindow *
z_gtk_widget_get_root_gdk_window (
  GtkWidget * widget)
{
  GdkScreen * screen =
    z_gtk_widget_get_screen (widget);
  return
    gdk_screen_get_root_window (screen);
}

static inline void
z_gtk_widget_get_global_coordinates (
  GtkWidget * widget,
  int *       x,
  int *       y)
{
  GdkDevice * dev =
    z_gtk_widget_get_device (widget);
  GdkWindow * win =
    z_gtk_widget_get_root_gdk_window (widget);
  gdk_window_get_device_position (
    win, dev, x, y, NULL);
}

static inline void
z_gtk_widget_get_global_coordinates_double (
  GtkWidget * widget,
  double *    x,
  double *    y)
{
  GdkDevice * dev =
    z_gtk_widget_get_device (widget);
  GdkWindow * win =
    z_gtk_widget_get_root_gdk_window (widget);
  gdk_window_get_device_position_double (
    win, dev, x, y, NULL);
}

/**
 * Wraps the cursor to the given global coordinates.
 */
static inline void
z_gtk_warp_cursor_to (
  GtkWidget * widget, int x, int y)
{
  GdkDevice * dev =
    z_gtk_widget_get_device (widget);
  GdkScreen * screen =
    z_gtk_widget_get_screen (widget);
  gdk_device_warp (dev, screen, x, y);
}

/**
 * Sets the GdkModifierType given for the widget.
 *
 * Used in eg. drag_motion events to check if
 * Ctrl is held.
 */
static inline void
z_gtk_widget_get_mask (
  GtkWidget * widget,
  GdkModifierType * mask)
{
  gdk_window_get_device_position (
    gtk_widget_get_window (widget),
    z_gtk_widget_get_device (widget),
    NULL, NULL, mask);
}

/**
 * Returns if the keyval is an Alt key.
 */
static inline int
z_gtk_keyval_is_alt (
  const guint keyval)
{
  return
    keyval == GDK_KEY_Alt_L ||
    keyval == GDK_KEY_Alt_R ||
    keyval == GDK_KEY_Meta_L ||
    keyval == GDK_KEY_Meta_R;
}

/**
 * Returns if the keyval is a Control key.
 */
static inline int
z_gtk_keyval_is_ctrl (
  const guint keyval)
{
  return
    keyval == GDK_KEY_Control_L ||
    keyval == GDK_KEY_Control_R;
}

/**
 * Returns if the keyval is an arrow key.
 */
static inline int
z_gtk_keyval_is_arrow (
  const guint keyval)
{
  return
    keyval == GDK_KEY_Left ||
    keyval == GDK_KEY_Right ||
    keyval == GDK_KEY_Down ||
    keyval == GDK_KEY_Up;
}

/**
 * Returns if the keyval is a Shift key.
 */
static inline int
z_gtk_keyval_is_shift (
  const guint keyval)
{
  return
    keyval == GDK_KEY_Shift_L ||
    keyval == GDK_KEY_Shift_R;
}

/**
 * Returns the single child of a container.
 */
static inline GtkWidget *
z_gtk_container_get_single_child (
  GtkContainer * container)
{
  GList *children, *iter;
  GtkWidget * widget;
  children =
    gtk_container_get_children (container);
  for (iter = children;
       iter != NULL;
       iter = g_list_next (iter))
    {
      widget =
        GTK_WIDGET (iter->data);
      g_list_free (children);
      return widget;
    }
  g_return_val_if_reached (NULL);
}

/**
 * Returns the nth child of a container.
 */
GtkWidget *
z_gtk_container_get_nth_child (
  GtkContainer * container,
  int            index);

/**
 * Sets the ellipsize mode of each text cell
 * renderer in the combo box.
 */
void
z_gtk_combo_box_set_ellipsize_mode (
  GtkComboBox * self,
  PangoEllipsizeMode ellipsize);

/**
 * Sets the given emblem to the button, or unsets
 * the emblem if \ref emblem_icon is NULL.
 */
void
z_gtk_button_set_emblem (
  GtkButton *  btn,
  const char * emblem_icon);

/**
 * Makes the given notebook foldable.
 *
 * The pages of the notebook must all be wrapped
 * in GtkBox's.
 */
void
z_gtk_setup_foldable_notebook (
  GtkNotebook * notebook);

/**
 * Sets the margin on all 4 sides on the widget.
 */
void
z_gtk_widget_set_margin (
  GtkWidget * widget,
  int         margin);

GtkFlowBoxChild *
z_gtk_flow_box_get_selected_child (
  GtkFlowBox * self);

/**
 * Callback to use for simple directory links.
 */
bool
z_gtk_activate_dir_link_func (
  GtkLabel * label,
  char *     uri,
  void *     data);

GtkSourceLanguageManager *
z_gtk_source_language_manager_get (void);

/**
 * Makes the given GtkNotebook detachable to
 * a new window.
 */
void
z_gtk_notebook_make_detachable (
  GtkNotebook * notebook,
  GtkWindow *   parent_window);

/**
 * Wraps the message area in a scrolled window.
 */
void
z_gtk_message_dialog_wrap_message_area_in_scroll (
  GtkMessageDialog * dialog,
  int                min_width,
  int                min_height);

/**
 * Returns the full text contained in the text
 * buffer.
 *
 * Must be free'd using g_free().
 */
char *
z_gtk_text_buffer_get_full_text (
  GtkTextBuffer * buffer);

/**
 * Generates a screenshot image for the given
 * widget.
 *
 * See gdk_pixbuf_savev() for the parameters.
 *
 * @param accept_fallback Whether to accept a
 *   fallback "no image" pixbuf.
 * @param[out] ret_dir Placeholder for directory to
 *   be deleted after using the screenshot.
 * @param[out] ret_path Placeholder for absolute
 *   path to the screenshot.
 */
void
z_gtk_generate_screenshot_image (
  GtkWidget *  widget,
  const char * type,
  char **      option_keys,
  char **      option_values,
  char **      ret_dir,
  char **      ret_path,
  bool         accept_fallback);

/**
 * Sets the action target of the given GtkActionable
 * to be binded to the given setting.
 *
 * Mainly used for binding GSettings keys to toggle
 * buttons.
 */
void
z_gtk_actionable_set_action_from_setting (
  GtkActionable * actionable,
  GSettings *     settings,
  const char *    key);

/**
 * Returns column number or -1 if not found or on
 * error.
 */
int
z_gtk_tree_view_column_get_column_id (
  GtkTreeViewColumn * col);

/**
 * @}
 */
#endif
