/*
 * Copyright (C) 2021 Alexandros Theodotou <alex at zrythm dot org>
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

#include "audio/control_port.h"
#include "audio/modulator_track.h"
#include "audio/track.h"
#include "audio/tracklist.h"
#include "gui/widgets/dialogs/bind_cc_dialog.h"
#include "gui/widgets/dialogs/port_info.h"
#include "gui/widgets/knob.h"
#include "gui/widgets/knob_with_name.h"
#include "gui/widgets/live_waveform.h"
#include "gui/widgets/modulator_macro.h"
#include "gui/widgets/port_connections_popover.h"
#include "plugins/plugin.h"
#include "project.h"
#include "utils/arrays.h"
#include "utils/cairo.h"
#include "utils/error.h"
#include "utils/flags.h"
#include "utils/gtk.h"
#include "utils/string.h"

#include <gtk/gtk.h>

#include <glib/gi18n.h>

G_DEFINE_TYPE (
  ModulatorMacroWidget, modulator_macro_widget,
  GTK_TYPE_GRID)

static bool
on_inputs_draw (
  GtkWidget *            widget,
  cairo_t *              cr,
  ModulatorMacroWidget * self)
{
  GtkStyleContext *context =
    gtk_widget_get_style_context (widget);

  int width =
    gtk_widget_get_allocated_width (widget);
  int height =
    gtk_widget_get_allocated_height (widget);
  gtk_render_background (
    context, cr, 0, 0, width, height);

  Port * port =
    P_MODULATOR_TRACK->modulator_macros[
      self->modulator_macro_idx]->cv_in;

  if (port->num_srcs == 0)
    {
      const char * str = _("No inputs");
      int w, h;
      z_cairo_get_text_extents_for_widget (
        widget, self->layout, str, &w, &h);
      cairo_set_source_rgba (cr, 1, 1, 1, 1);

#if 0
      cairo_save (cr);
      cairo_translate (cr, width / 2, height / 2);
      cairo_rotate (cr, - 1.570796);
      cairo_move_to (
        cr, - w / 2.0, - h / 2.0);
#endif
      cairo_move_to (
        cr, width / 2.0 - w / 2.0,
        height / 2.0 - h / 2.0);
      z_cairo_draw_text (
        cr, widget, self->layout, str);

#if 0
      cairo_restore (cr);
#endif
    }
  else
    {
      double val_w =
        ((double) width / (double) port->num_srcs);
      for (int i = 0; i < port->num_srcs; i++)
        {
          double val_h =
            (double)
            ((port->srcs[i]->buf[0] - port->minf) /
             (port->maxf - port->minf)) *
            (double) height;
          cairo_set_source_rgba (cr, 1, 1, 0, 1);
          cairo_rectangle (
            cr, val_w * (double) i,
            (double) height - val_h,
            val_w, 1);
          cairo_fill (cr);

          if (i != 0)
            {
              cairo_set_source_rgba (
                cr, 0.4, 0.4, 0.4, 1);
              z_cairo_draw_vertical_line (
                cr, val_w * i, 0, height, 1);
            }
        }
    }

  return false;
}

static bool
on_output_draw (
  GtkWidget *            widget,
  cairo_t *              cr,
  ModulatorMacroWidget * self)
{
  GtkStyleContext *context =
    gtk_widget_get_style_context (widget);

  int width =
    gtk_widget_get_allocated_width (widget);
  int height =
    gtk_widget_get_allocated_height (widget);
  gtk_render_background (
    context, cr, 0, 0, width, height);

  Port * port =
    P_MODULATOR_TRACK->modulator_macros[
      self->modulator_macro_idx]->cv_out;

  cairo_set_source_rgba (cr, 1, 1, 0, 1);
  double val_h =
    (double)
    ((port->buf[0] - port->minf) /
     (port->maxf - port->minf)) *
    (double) height;
  cairo_rectangle (
    cr, 0, (double) height - val_h, width, 1);
  cairo_fill (cr);

  return false;
}

static void
on_view_info_activate (
  GtkMenuItem * menuitem,
  Port *        port)
{
  PortInfoDialogWidget * dialog =
    port_info_dialog_widget_new (port);
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
on_bind_midi_cc (
  GtkMenuItem * menuitem,
  Port *        port)
{
  BindCcDialogWidget * dialog =
    bind_cc_dialog_widget_new (port, true);

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
on_reset_control (
  GtkMenuItem * menuitem,
  Port *        port)
{
  GError * err = NULL;
  bool ret =
    port_action_perform_reset_control (
      &port->id, &err);
  if (!ret)
    {
      HANDLE_ERROR (
        err,
        _("Failed to reset control '%s'"),
        port->id.label);
    }
}

static void
on_knob_right_click (
  GtkGestureMultiPress *gesture,
  gint                  n_press,
  gdouble               x,
  gdouble               y,
  Port *                port)
{
  if (n_press != 1)
    return;

  GtkWidget *menu, *menuitem;
  menu = gtk_menu_new();

  menuitem =
    gtk_menu_item_new_with_label (_("Reset"));
  g_signal_connect (
    menuitem, "activate",
    G_CALLBACK (on_reset_control), port);
  gtk_menu_shell_append (
    GTK_MENU_SHELL (menu), menuitem);

  menuitem =
    GTK_WIDGET (CREATE_MIDI_LEARN_MENU_ITEM);
  g_signal_connect (
    menuitem, "activate",
    G_CALLBACK (on_bind_midi_cc), port);
  gtk_menu_shell_append (
    GTK_MENU_SHELL (menu), menuitem);

  menuitem =
    gtk_menu_item_new_with_label (_("View info"));
  g_signal_connect (
    menuitem, "activate",
    G_CALLBACK (on_view_info_activate), port);
  gtk_menu_shell_append (
    GTK_MENU_SHELL (menu), menuitem);

  gtk_widget_show_all (menu);

  gtk_menu_popup_at_pointer (GTK_MENU(menu), NULL);
}

void
modulator_macro_widget_refresh (
  ModulatorMacroWidget * self)
{
}

static void
on_automate_clicked (
  GtkButton *            btn,
  Port *                 port)
{
  g_return_if_fail (port);

  PortConnectionsPopoverWidget * popover =
    port_connections_popover_widget_new (
      GTK_WIDGET (btn), port);
  gtk_widget_show_all (GTK_WIDGET (popover));

#if 0
  g_signal_connect (
    G_OBJECT (popover), "closed",
    G_CALLBACK (on_popover_closed), self);
#endif
}

static bool
redraw_cb (
  GtkWidget *             widget,
  GdkFrameClock *        frame_clock,
  ModulatorMacroWidget * self)
{
  gtk_widget_queue_draw (widget);

  return G_SOURCE_CONTINUE;
}

ModulatorMacroWidget *
modulator_macro_widget_new (
  int modulator_macro_idx)
{
  ModulatorMacroWidget * self =
    g_object_new (MODULATOR_MACRO_WIDGET_TYPE, NULL);

  self->modulator_macro_idx = modulator_macro_idx;

  ModulatorMacroProcessor * macro =
    P_MODULATOR_TRACK->modulator_macros[
      modulator_macro_idx];
  Port * port = macro->macro;

  KnobWidget * knob =
    knob_widget_new_simple (
      control_port_get_val,
      control_port_get_default_val,
      control_port_set_real_val,
      port, port->minf, port->maxf, 48, port->zerof);
  self->knob_with_name =
    knob_with_name_widget_new (
      macro,
      (GenericStringGetter)
        modulator_macro_processor_get_name,
      (GenericStringSetter)
        modulator_macro_processor_set_name,
      knob,
      GTK_ORIENTATION_VERTICAL, true, 2);
  gtk_grid_attach (
    GTK_GRID (self),
    GTK_WIDGET (self->knob_with_name), 1, 0, 1, 2);

  /* add context menu */
  GtkGestureMultiPress * mp =
    GTK_GESTURE_MULTI_PRESS (
      gtk_gesture_multi_press_new (
        GTK_WIDGET (knob)));
  gtk_gesture_single_set_button (
    GTK_GESTURE_SINGLE (mp),
    GDK_BUTTON_SECONDARY);
  g_signal_connect (
    G_OBJECT (mp), "pressed",
    G_CALLBACK (on_knob_right_click), port);

  g_signal_connect (
    G_OBJECT (self->outputs), "clicked",
    G_CALLBACK (on_automate_clicked),
    P_MODULATOR_TRACK->modulator_macros[
      modulator_macro_idx]->cv_out);
  g_signal_connect (
    G_OBJECT (self->add_input), "clicked",
    G_CALLBACK (on_automate_clicked),
    P_MODULATOR_TRACK->modulator_macros[
      modulator_macro_idx]->cv_in);

  g_signal_connect (
    G_OBJECT (self->inputs), "draw",
    G_CALLBACK (on_inputs_draw), self);
  g_signal_connect (
    G_OBJECT (self->output), "draw",
    G_CALLBACK (on_output_draw), self);

  gtk_widget_add_tick_callback (
    GTK_WIDGET (self->inputs),
    (GtkTickCallback) redraw_cb,
    self, NULL);
  gtk_widget_add_tick_callback (
    GTK_WIDGET (self->output),
    (GtkTickCallback) redraw_cb,
    self, NULL);

  return self;
}

static void
finalize (
  ModulatorMacroWidget * self)
{
  G_OBJECT_CLASS (
    modulator_macro_widget_parent_class)->
      finalize (G_OBJECT (self));
}

static void
modulator_macro_widget_class_init (
  ModulatorMacroWidgetClass * _klass)
{
  GtkWidgetClass * klass =
    GTK_WIDGET_CLASS (_klass);

  resources_set_class_template (
    klass, "modulator_macro.ui");

  gtk_widget_class_set_css_name (
    klass, "modulator-macro");

#define BIND_CHILD(x) \
  gtk_widget_class_bind_template_child ( \
    klass, ModulatorMacroWidget, x)

  BIND_CHILD (inputs);
  BIND_CHILD (output);
  BIND_CHILD (add_input);
  BIND_CHILD (outputs);

  GObjectClass * goklass = G_OBJECT_CLASS (_klass);
  goklass->finalize = (GObjectFinalizeFunc) finalize;
}

static void
modulator_macro_widget_init (
  ModulatorMacroWidget * self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_widget_set_size_request (
    GTK_WIDGET (self->inputs), -1, 12);
  /*gtk_widget_set_size_request (*/
    /*GTK_WIDGET (self->inputs), 24, -1);*/

  self->layout =
    z_cairo_create_pango_layout_from_string (
      GTK_WIDGET (self->inputs), "Sans 7",
      PANGO_ELLIPSIZE_NONE, -1);
}
