/*
 * Copyright (C) 2018-2021 Alexandros Theodotou <alex at zrythm dot org>
 * Copyright (C) 2020 Ryan Gonzalez <rymg19 at gmail dot com>
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

#include "zrythm-config.h"

#include <math.h>

#include "audio/engine.h"
#include "audio/engine_rtaudio.h"
#include "audio/engine_sdl.h"
#include "audio/pan.h"
#include "audio/port.h"
#include "gui/widgets/bot_bar.h"
#include "gui/widgets/bot_dock_edge.h"
#include "gui/widgets/center_dock.h"
#include "gui/widgets/clip_editor.h"
#include "gui/widgets/clip_editor_inner.h"
#include "gui/widgets/dialogs/bind_cc_dialog.h"
#include "gui/widgets/main_notebook.h"
#include "gui/widgets/main_window.h"
#include "gui/widgets/editor_ruler.h"
#include "gui/widgets/ruler.h"
#include "gui/widgets/timeline_panel.h"
#include "gui/widgets/timeline_ruler.h"
#include "project.h"
#include "settings/settings.h"
#include "utils/color.h"
#include "utils/gtk.h"
#include "utils/localization.h"
#include "utils/objects.h"
#include "utils/string.h"
#include "utils/ui.h"
#include "zrythm_app.h"

#include <glib/gi18n.h>

/**
 * Sets cursor from icon name.
 */
void
ui_set_cursor_from_icon_name (
  GtkWidget *  widget,
  const char * name,
  int          offset_x,
  int          offset_y)
{
  GdkWindow * win =
    gtk_widget_get_parent_window (widget);
  if (!GDK_IS_WINDOW (win))
    return;

  g_return_if_fail (offset_x >= 0 && offset_y >= 0);

  /* check the cache first */
  for (int i = 0; i < UI_CACHES->num_cursors; i++)
    {
      g_return_if_fail (i < UI_MAX_CURSORS);

      UiCursor * cursor =
        &UI_CACHES->cursors[i];
      if (string_is_equal (name, cursor->name) &&
          cursor->offset_x == offset_x &&
          cursor->offset_y == offset_y)
        {
          gdk_window_set_cursor (
            win, cursor->cursor);
          return;
        }
    }

  GdkPixbuf * pixbuf =
    gtk_icon_theme_load_icon (
      gtk_icon_theme_get_default (), name,
      18, 0, NULL);
  if (!GDK_IS_PIXBUF (pixbuf))
    {
      g_warning (
        "no pixbuf for %s", name);
      return;
    }
  int adjusted_offset_x =
    MIN (
      offset_x, gdk_pixbuf_get_width (pixbuf) - 1);
  int adjusted_offset_y =
    MIN (
      offset_y, gdk_pixbuf_get_height (pixbuf) - 1);
  GdkCursor * gdk_cursor =
    gdk_cursor_new_from_pixbuf (
      gdk_display_get_default (), pixbuf,
      adjusted_offset_x, adjusted_offset_y);

  /* add the cursor to the caches */
  UiCursor * cursor =
    &UI_CACHES->cursors[UI_CACHES->num_cursors++];
  strcpy (cursor->name, name);
  cursor->cursor = gdk_cursor;
  cursor->pixbuf = pixbuf;
  cursor->offset_x = offset_x;
  cursor->offset_y = offset_y;

  gdk_window_set_cursor (win, cursor->cursor);
}

/**
 * Sets cursor from standard cursor name.
 */
void
ui_set_cursor_from_name (
  GtkWidget * widget,
  const char * name)
{
  GdkWindow * win =
    gtk_widget_get_parent_window (widget);
  if (!GDK_IS_WINDOW (win))
    return;
  GdkCursor * cursor =
    gdk_cursor_new_from_name (
      gdk_display_get_default (),
      name);
  gdk_window_set_cursor (win, cursor);
}

void
ui_set_pointer_cursor (
  GtkWidget * widget)
{
  ui_set_cursor_from_icon_name (
    GTK_WIDGET (widget), "edit-select", 3, 1);
}

/**
 * Shows a popup message of the given type with the
 * given message.
 */
void
ui_show_message_full (
  GtkWindow *    parent_window,
  GtkMessageType type,
  const char *   format,
  ...)
{
  va_list args;
  va_start (args, format);

  static char buf[40000];
  vsprintf (buf, format, args);

  if (ZRYTHM_HAVE_UI)
    {
      GtkDialogFlags flags =
        parent_window ?
          GTK_DIALOG_DESTROY_WITH_PARENT : 0;
      GtkWidget * dialog =
        gtk_message_dialog_new (
          parent_window, flags, type,
          GTK_BUTTONS_CLOSE, "%s", buf);
      gtk_window_set_title (
        GTK_WINDOW (dialog), PROGRAM_NAME);
      gtk_window_set_icon_name (
        GTK_WINDOW (dialog), "zrythm");
      if (parent_window)
        {
          gtk_window_set_transient_for (
            GTK_WINDOW (dialog), parent_window);
        }
      gtk_dialog_run (GTK_DIALOG (dialog));
      gtk_widget_destroy (dialog);
    }
  else
    {
      switch (type)
        {
        case GTK_MESSAGE_ERROR:
          g_warning ("%s", buf);
          break;
        case GTK_MESSAGE_INFO:
          g_message ("%s", buf);
          break;
        default:
          g_critical ("should not be reached");
          break;
        }
    }

  va_end (args);
}

/**
 * Returns the matching hit child, or NULL.
 */
GtkWidget *
ui_get_hit_child (
  GtkContainer * parent,
  double         x, ///< x in parent space
  double         y, ///< y in parent space
  GType          type) ///< type to look for
{
  GList *children, *iter;

  /* go through each overlay child */
  children =
    gtk_container_get_children (parent);
  for (iter = children;
       iter != NULL;
       iter = g_list_next (iter))
    {
      GtkWidget * widget = GTK_WIDGET (iter->data);

      if (!gtk_widget_get_visible (widget))
        continue;

      GtkAllocation allocation;
      gtk_widget_get_allocation (
        widget,
        &allocation);

      gint wx, wy;
      gtk_widget_translate_coordinates (
        GTK_WIDGET (parent),
        GTK_WIDGET (widget),
        (int) x, (int) y, &wx, &wy);

      /* if hit */
      if (wx >= 0 &&
          wx <= allocation.width &&
          wy >= 0 &&
          wy <= allocation.height)
        {
          /* if type matches */
          if (G_TYPE_CHECK_INSTANCE_TYPE (
                widget,
                type))
            {
              g_list_free (children);
              return widget;
            }
        }
    }

  g_list_free (children);
  return NULL;
}

NONNULL
static void
px_to_pos (
  double        px,
  Position *    pos,
  bool          use_padding,
  RulerWidget * ruler)
{
  if (use_padding)
    {
      px -= SPACE_BEFORE_START_D;

      /* clamp at 0 */
      if (px < 0.0)
        px = 0.0;
    }

  pos->schema_version = POSITION_SCHEMA_VERSION;
  pos->ticks = px / ruler->px_per_tick;
  position_update_frames_from_ticks (pos);
}

/**
 * Converts from pixels to position.
 *
 * Only works with positive numbers. Negatives will
 * be clamped at 0. If a negative is needed, pass
 * the abs to this function and then change the
 * sign.
 *
 * @param has_padding Whether @ref px contains
 *   padding.
 */
void
ui_px_to_pos_timeline (
  double     px,
  Position * pos,
  bool       has_padding)
{
  if (!MAIN_WINDOW || !MW_RULER)
    return;

  px_to_pos (
    px, pos, has_padding,
    Z_RULER_WIDGET (MW_RULER));
}


/**
 * Converts from pixels to position.
 *
 * Only works with positive numbers. Negatives will
 * be clamped at 0. If a negative is needed, pass
 * the abs to this function and then change the
 * sign.
 *
 * @param has_padding Whether @ref px contains
 *   padding.
 */
void
ui_px_to_pos_editor (
  double     px,
  Position * pos,
  bool       has_padding)
{
  if (!MAIN_WINDOW || !EDITOR_RULER)
    return;

  px_to_pos (
    px, pos, has_padding,
    Z_RULER_WIDGET (EDITOR_RULER));
}

PURE
NONNULL
static inline int
pos_to_px (
  Position *       pos,
  int              use_padding,
  RulerWidget *    ruler)
{
  int px =
    (int)
    (pos->ticks * ruler->px_per_tick);

  if (use_padding)
    px += SPACE_BEFORE_START;

  return px;
}

/**
 * Converts position to px, optionally adding the
 * ruler padding.
 */
int
ui_pos_to_px_timeline (
  Position *       pos,
  int              use_padding)
{
  if (!MAIN_WINDOW || !MW_RULER)
    return 0;

  return pos_to_px (
    pos, use_padding, (RulerWidget *) (MW_RULER));
}

/**
 * Gets pixels from the position, based on the
 * piano_roll ruler.
 */
int
ui_pos_to_px_editor (
  Position *       pos,
  bool             use_padding)
{
  if (!MAIN_WINDOW || !EDITOR_RULER)
    return 0;

  return
    pos_to_px (
      pos, use_padding,
      Z_RULER_WIDGET (EDITOR_RULER));
}

/**
 * @param has_padding Whether the given px contains
 *   padding.
 */
static long
px_to_frames (
  double        px,
  int           has_padding,
  RulerWidget * ruler)
{
  if (has_padding)
    {
      px -= SPACE_BEFORE_START;

      /* clamp at 0 */
      if (px < 0.0)
        px = 0.0;
    }

  return
    (long)
    (((double) AUDIO_ENGINE->frames_per_tick * px) /
    ruler->px_per_tick);
}

/**
 * Converts from pixels to frames.
 *
 * Returns the frames.
 *
 * @param has_padding Whether then given px contains
 *   padding.
 */
long
ui_px_to_frames_timeline (
  double px,
  int    has_padding)
{
  if (!MAIN_WINDOW || !MW_RULER)
    return 0;

  return
    px_to_frames (
      px, has_padding,
      Z_RULER_WIDGET (MW_RULER));
}

/**
 * Converts from pixels to frames.
 *
 * Returns the frames.
 *
 * @param has_padding Whether then given px contains
 *   padding.
 */
long
ui_px_to_frames_editor (
  double px,
  int    has_padding)
{
  if (!MAIN_WINDOW || !EDITOR_RULER)
    return 0;

  return
    px_to_frames (
      px, has_padding,
      Z_RULER_WIDGET (EDITOR_RULER));
}

/**
 * Returns if \ref rect is hit or not by the
 * given coordinate.
 *
 * @param check_x Check x-axis for match.
 * @param check_y Check y-axis for match.
 * @param x x in parent space.
 * @param y y in parent space.
 * @param x_padding Padding to add to the x
 *   of the object when checking if hit.
 *   The bigger the padding the more space the
 *   child will have to get hit.
 * @param y_padding Padding to add to the y
 *   of the object when checking if hit.
 *   The bigger the padding the more space the
 *   child will have to get hit.
 */
bool
ui_is_point_in_rect_hit (
  GdkRectangle * rect,
  const bool     check_x,
  const bool     check_y,
  double         x,
  double         y,
  double         x_padding,
  double         y_padding)
{
  /* make coordinates local to the rect */
  x -= rect->x;
  y -= rect->y;

  /* if hit */
  if ((!check_x ||
        (x >= - x_padding &&
         x <= rect->width + x_padding)) &&
      (!check_y ||
        (y >= - y_padding &&
         y <= rect->height + y_padding)))
    {
      return true;
    }
  return false;
}

/**
 * Returns if the child is hit or not by the
 * coordinates in parent.
 *
 * @param check_x Check x-axis for match.
 * @param check_y Check y-axis for match.
 * @param x x in parent space.
 * @param y y in parent space.
 * @param x_padding Padding to add to the x
 *   of the object when checking if hit.
 *   The bigger the padding the more space the
 *   child will have to get hit.
 * @param y_padding Padding to add to the y
 *   of the object when checking if hit.
 *   The bigger the padding the more space the
 *   child will have to get hit.
 */
int
ui_is_child_hit (
  GtkWidget * parent,
  GtkWidget *    child,
  const int            check_x,
  const int            check_y,
  const double         x,
  const double         y,
  const double         x_padding,
  const double         y_padding)
{
  GtkAllocation allocation;
  gtk_widget_get_allocation (
    child,
    &allocation);

  gint wx, wy;
  gtk_widget_translate_coordinates (
    GTK_WIDGET (parent),
    child,
    (int) x, (int) y, &wx, &wy);

  //g_message ("wx wy %d %d", wx, wy);

  /* if hit */
  if ((!check_x ||
        (wx >= - x_padding &&
         wx <= allocation.width + x_padding)) &&
      (!check_y ||
        (wy >= - y_padding &&
         wy <= allocation.height + y_padding)))
    {
      return 1;
    }
  return 0;
}

/**
 * Hides the notification.
 *
 * Used ui_show_notification to be called after
 * a timeout.
 */
static int
hide_notification_async ()
{
  gtk_revealer_set_reveal_child (
    GTK_REVEALER (MAIN_WINDOW->revealer),
    0);

  return FALSE;
}

/**
 * Shows a notification in the revealer.
 */
void
ui_show_notification (const char * msg)
{
  gtk_label_set_text (MAIN_WINDOW->notification_label,
                      msg);
  gtk_revealer_set_reveal_child (
    GTK_REVEALER (MAIN_WINDOW->revealer),
    1);
  g_timeout_add_seconds (
    3, (GSourceFunc) hide_notification_async, NULL);
}

/**
 * Show notification from non-GTK threads.
 *
 * This should be used internally. Use the
 * ui_show_notification_idle macro instead.
 */
int
ui_show_notification_idle_func (char * msg)
{
  ui_show_notification (msg);
  g_free (msg);

  return G_SOURCE_REMOVE;
}

/**
 * Converts RGB to hex string.
 */
void
ui_rgb_to_hex (
  double red,
  double green,
  double blue,
  char * buf)
{
  sprintf (
    buf, "#%hhx%hhx%hhx",
    (char) (red * 255.0),
    (char) (green * 255.0),
    (char) (blue * 255.0));
}

void
ui_gdk_rgba_to_hex (
  GdkRGBA * color,
  char *    buf)
{
  ui_rgb_to_hex (
    color->red, color->green, color->blue, buf);
}

/**
 * Returns the modifier type (state mask) from the
 * given gesture.
 */
void
ui_get_modifier_type_from_gesture (
  GtkGestureSingle * gesture,
  GdkModifierType *  state_mask) ///< return value
{
  GdkEventSequence *sequence =
    gtk_gesture_single_get_current_sequence (
      gesture);
  const GdkEvent * event =
    gtk_gesture_get_last_event (
      GTK_GESTURE (gesture), sequence);
  gdk_event_get_state (event, state_mask);
}

#define CREATE_SIMPLE_MODEL_BOILERPLATE \
  enum \
  { \
    VALUE_COL, \
    TEXT_COL, \
    ID_COL, \
  }; \
  GtkTreeIter iter; \
  GtkListStore *store; \
  gint i; \
 \
  store = \
  gtk_list_store_new (3, \
         G_TYPE_INT, \
         G_TYPE_STRING, \
  G_TYPE_STRING); \
 \
  int num_elements = G_N_ELEMENTS (values); \
  for (i = 0; i < num_elements; i++) \
    { \
      gtk_list_store_append (store, &iter); \
      char id[40]; \
      sprintf (id, "%d", values[i]); \
      gtk_list_store_set (store, &iter, \
                          VALUE_COL, values[i], \
                          TEXT_COL, labels[i], \
                          ID_COL, id, \
                          -1); \
    } \
 \
  return GTK_TREE_MODEL (store);

/**
 * Creates and returns a language model for combo
 * boxes.
 */
static GtkTreeModel *
ui_create_language_model ()
{
  int values[NUM_LL_LANGUAGES];
  const char * labels[NUM_LL_LANGUAGES];
  for (int i = 0; i < NUM_LL_LANGUAGES; i++)
    {
      values[i] = i;
      labels[i] =
        localization_get_string_w_code (i);
    }

  CREATE_SIMPLE_MODEL_BOILERPLATE;
}

static GtkTreeModel *
ui_create_audio_backends_model (void)
{
  const int values[] = {
    AUDIO_BACKEND_DUMMY,
#ifdef HAVE_LIBSOUNDIO
    AUDIO_BACKEND_DUMMY_LIBSOUNDIO,
#endif
#ifdef HAVE_ALSA
    AUDIO_BACKEND_ALSA,
  #ifdef HAVE_LIBSOUNDIO
    AUDIO_BACKEND_ALSA_LIBSOUNDIO,
  #endif
  #ifdef HAVE_RTAUDIO
    AUDIO_BACKEND_ALSA_RTAUDIO,
  #endif
#endif /* HAVE_ALSA */
#ifdef HAVE_JACK
    AUDIO_BACKEND_JACK,
  #ifdef HAVE_LIBSOUNDIO
    AUDIO_BACKEND_JACK_LIBSOUNDIO,
  #endif
  #ifdef HAVE_RTAUDIO
    AUDIO_BACKEND_JACK_RTAUDIO,
  #endif
#endif /* HAVE_JACK */
#ifdef HAVE_PULSEAUDIO
    AUDIO_BACKEND_PULSEAUDIO,
  #ifdef HAVE_LIBSOUNDIO
    AUDIO_BACKEND_PULSEAUDIO_LIBSOUNDIO,
  #endif
  #ifdef HAVE_RTAUDIO
    AUDIO_BACKEND_PULSEAUDIO_RTAUDIO,
  #endif
#endif /* HAVE_PULSEAUDIO */
#ifdef __APPLE__
  #ifdef HAVE_LIBSOUNDIO
    AUDIO_BACKEND_COREAUDIO_LIBSOUNDIO,
  #endif
  #ifdef HAVE_RTAUDIO
    AUDIO_BACKEND_COREAUDIO_RTAUDIO,
  #endif
#endif /* __APPLE__ */
#ifdef HAVE_SDL
    AUDIO_BACKEND_SDL,
#endif
#ifdef _WOE32
  #ifdef HAVE_LIBSOUNDIO
    AUDIO_BACKEND_WASAPI_LIBSOUNDIO,
  #endif
  #ifdef HAVE_RTAUDIO
    AUDIO_BACKEND_WASAPI_RTAUDIO,
    AUDIO_BACKEND_ASIO_RTAUDIO,
  #endif
#endif /* _WOE32 */
  };
  const gchar *labels[] = {
    _(audio_backend_str[AUDIO_BACKEND_DUMMY]),
#ifdef HAVE_LIBSOUNDIO
    _(audio_backend_str[AUDIO_BACKEND_DUMMY_LIBSOUNDIO]),
#endif
#ifdef HAVE_ALSA
    _(audio_backend_str[AUDIO_BACKEND_ALSA]),
  #ifdef HAVE_LIBSOUNDIO
    _(audio_backend_str[AUDIO_BACKEND_ALSA_LIBSOUNDIO]),
  #endif
  #ifdef HAVE_RTAUDIO
    _(audio_backend_str[AUDIO_BACKEND_ALSA_RTAUDIO]),
  #endif
#endif /* HAVE_ALSA */
#ifdef HAVE_JACK
    _(audio_backend_str[AUDIO_BACKEND_JACK]),
  #ifdef HAVE_LIBSOUNDIO
    _(audio_backend_str[AUDIO_BACKEND_JACK_LIBSOUNDIO]),
  #endif
  #ifdef HAVE_RTAUDIO
    _(audio_backend_str[AUDIO_BACKEND_JACK_RTAUDIO]),
  #endif
#endif /* HAVE_JACK */
#ifdef HAVE_PULSEAUDIO
    _(audio_backend_str[AUDIO_BACKEND_PULSEAUDIO]),
  #ifdef HAVE_LIBSOUNDIO
    _(audio_backend_str[AUDIO_BACKEND_PULSEAUDIO_LIBSOUNDIO]),
  #endif
  #ifdef HAVE_RTAUDIO
    _(audio_backend_str[AUDIO_BACKEND_PULSEAUDIO_RTAUDIO]),
  #endif
#endif /* HAVE_PULSEAUDIO */
#ifdef __APPLE__
  #ifdef HAVE_LIBSOUNDIO
    _(audio_backend_str[AUDIO_BACKEND_COREAUDIO_LIBSOUNDIO]),
  #endif
  #ifdef HAVE_RTAUDIO
    _(audio_backend_str[AUDIO_BACKEND_COREAUDIO_RTAUDIO]),
  #endif
#endif /* __APPLE__ */
#ifdef HAVE_SDL
    _(audio_backend_str[AUDIO_BACKEND_SDL]),
#endif
#ifdef _WOE32
  #ifdef HAVE_LIBSOUNDIO
    _(audio_backend_str[AUDIO_BACKEND_WASAPI_LIBSOUNDIO]),
  #endif
  #ifdef HAVE_RTAUDIO
    _(audio_backend_str[AUDIO_BACKEND_WASAPI_RTAUDIO]),
    _(audio_backend_str[AUDIO_BACKEND_ASIO_RTAUDIO]),
  #endif
#endif /* _WOE32 */
  };

  CREATE_SIMPLE_MODEL_BOILERPLATE;
}
static GtkTreeModel *
ui_create_midi_backends_model (void)
{
  const int values[] = {
    MIDI_BACKEND_DUMMY,
#ifdef HAVE_ALSA
    MIDI_BACKEND_ALSA,
  #ifdef HAVE_RTMIDI
    MIDI_BACKEND_ALSA_RTMIDI,
  #endif
#endif
#ifdef HAVE_JACK
    MIDI_BACKEND_JACK,
  #ifdef HAVE_RTMIDI
    MIDI_BACKEND_JACK_RTMIDI,
  #endif
#endif
#ifdef _WOE32
    MIDI_BACKEND_WINDOWS_MME,
  #ifdef HAVE_RTMIDI
    MIDI_BACKEND_WINDOWS_MME_RTMIDI,
  #endif
#endif
#ifdef __APPLE__
  #ifdef HAVE_RTMIDI
    MIDI_BACKEND_COREMIDI_RTMIDI,
  #endif
#endif
  };
  const gchar * labels[] = {
    _(midi_backend_str[MIDI_BACKEND_DUMMY]),
#ifdef HAVE_ALSA
    _(midi_backend_str[MIDI_BACKEND_ALSA]),
  #ifdef HAVE_RTMIDI
    _(midi_backend_str[MIDI_BACKEND_ALSA_RTMIDI]),
  #endif
#endif
#ifdef HAVE_JACK
    _(midi_backend_str[MIDI_BACKEND_JACK]),
  #ifdef HAVE_RTMIDI
    _(midi_backend_str[MIDI_BACKEND_JACK_RTMIDI]),
  #endif
#endif
#ifdef _WOE32
    _(midi_backend_str[MIDI_BACKEND_WINDOWS_MME]),
  #ifdef HAVE_RTMIDI
    _(midi_backend_str[MIDI_BACKEND_WINDOWS_MME_RTMIDI]),
  #endif
#endif
#ifdef __APPLE__
  #ifdef HAVE_RTMIDI
    _(midi_backend_str[MIDI_BACKEND_COREMIDI_RTMIDI]),
  #endif
#endif
  };

  CREATE_SIMPLE_MODEL_BOILERPLATE;
}

static GtkTreeModel *
ui_create_pan_algo_model (void)
{

  const int values[3] = {
    PAN_ALGORITHM_LINEAR,
    PAN_ALGORITHM_SQUARE_ROOT,
    PAN_ALGORITHM_SINE_LAW,
  };
  const gchar *labels[3] = {
    /* TRANSLATORS: Pan algorithm */
    _("Linear"),
    _("Square Root"),
    _("Sine (Equal Power)"),
  };

  CREATE_SIMPLE_MODEL_BOILERPLATE;
}

static GtkTreeModel *
ui_create_pan_law_model (void)
{

  const int values[3] = {
    PAN_LAW_0DB,
    PAN_LAW_MINUS_3DB,
    PAN_LAW_MINUS_6DB,
  };
  const gchar *labels[3] = {
    /* TRANSLATORS: Pan algorithm */
    "0dB",
    "-3dB",
    "-6dB",
  };

  CREATE_SIMPLE_MODEL_BOILERPLATE;
}

static GtkTreeModel *
ui_create_buffer_size_model (void)
{
  const int values[NUM_AUDIO_ENGINE_BUFFER_SIZES] = {
    AUDIO_ENGINE_BUFFER_SIZE_16,
    AUDIO_ENGINE_BUFFER_SIZE_32,
    AUDIO_ENGINE_BUFFER_SIZE_64,
    AUDIO_ENGINE_BUFFER_SIZE_128,
    AUDIO_ENGINE_BUFFER_SIZE_256,
    AUDIO_ENGINE_BUFFER_SIZE_512,
    AUDIO_ENGINE_BUFFER_SIZE_1024,
    AUDIO_ENGINE_BUFFER_SIZE_2048,
    AUDIO_ENGINE_BUFFER_SIZE_4096,
  };
  const gchar *labels[NUM_AUDIO_ENGINE_BUFFER_SIZES] = {
    "16", "32", "64", "128", "256", "512",
    "1024", "2048", "4096",
  };

  CREATE_SIMPLE_MODEL_BOILERPLATE;
}

static GtkTreeModel *
ui_create_samplerate_model (void)
{
  const int values[NUM_AUDIO_ENGINE_SAMPLERATES] = {
    AUDIO_ENGINE_SAMPLERATE_22050,
    AUDIO_ENGINE_SAMPLERATE_32000,
    AUDIO_ENGINE_SAMPLERATE_44100,
    AUDIO_ENGINE_SAMPLERATE_48000,
    AUDIO_ENGINE_SAMPLERATE_88200,
    AUDIO_ENGINE_SAMPLERATE_96000,
    AUDIO_ENGINE_SAMPLERATE_192000,
  };
  const gchar *labels[NUM_AUDIO_ENGINE_BUFFER_SIZES] = {
    "22050", "32000", "44100", "48000",
    "88200", "96000", "192000",
  };

  CREATE_SIMPLE_MODEL_BOILERPLATE;
}

/**
 * Sets up a combo box to have a selection of
 * languages.
 */
void
ui_setup_language_combo_box (
  GtkComboBox * language)
{
  z_gtk_configure_simple_combo_box (
    language, ui_create_language_model ());

  gtk_combo_box_set_active (
    GTK_COMBO_BOX (language),
    g_settings_get_enum (
      S_P_UI_GENERAL,
      "language"));
}

/**
 * Sets up an audio backends combo box.
 */
void
ui_setup_audio_backends_combo_box (
  GtkComboBox * cb)
{
  z_gtk_configure_simple_combo_box (
    cb, ui_create_audio_backends_model ());

  char id[40];
  sprintf (id, "%d",
    g_settings_get_enum (
      S_P_GENERAL_ENGINE,
      "audio-backend"));
  gtk_combo_box_set_active_id (
    GTK_COMBO_BOX (cb), id);
}

/**
 * Sets up a MIDI backends combo box.
 */
void
ui_setup_midi_backends_combo_box (
  GtkComboBox * cb)
{
  z_gtk_configure_simple_combo_box (
    cb, ui_create_midi_backends_model ());

  char id[40];
  sprintf (id, "%d",
    g_settings_get_enum (
      S_P_GENERAL_ENGINE,
      "midi-backend"));
  gtk_combo_box_set_active_id (
    GTK_COMBO_BOX (cb), id);
}

/**
 * Sets up a pan algorithm combo box.
 */
void
ui_setup_pan_algo_combo_box (
  GtkComboBox * cb)
{
  z_gtk_configure_simple_combo_box (
    cb, ui_create_pan_algo_model ());

  gtk_combo_box_set_active (
    GTK_COMBO_BOX (cb),
    g_settings_get_enum (
      S_P_DSP_PAN,
      "pan-algorithm"));
}

/**
 * Sets up a pan law combo box.
 */
void
ui_setup_pan_law_combo_box (
  GtkComboBox * cb)
{
  z_gtk_configure_simple_combo_box (
    cb, ui_create_pan_law_model ());

  gtk_combo_box_set_active (
    GTK_COMBO_BOX (cb),
    g_settings_get_enum (
      S_P_DSP_PAN,
      "pan-law"));
}

/**
 * Returns the "a locale for the language you have
 * selected..." text based on the given language.
 *
 * Must be free'd by caller.
 */
char *
ui_get_locale_not_available_string (
  LocalizationLanguage lang)
{
  /* show warning */
#ifdef _WOE32
  char template =
    _("A locale for the language you have \
selected (%s) is not available. Please install one first \
and restart %s");
#else
  char * template =
    _("A locale for the language you have selected is \
not available. Please enable one first using \
the steps below and try again.\n\
1. Uncomment any locale starting with the \
language code <b>%s</b> in <b>/etc/locale.gen</b> (needs \
root privileges)\n\
2. Run <b>locale-gen</b> as root\n\
3. Restart %s");
#endif

  const char * code =
    localization_get_string_code (lang);
  char * str =
    g_strdup_printf (template, code, PROGRAM_NAME);

  return str;
}

/**
 * Sets up a pan law combo box.
 */
void
ui_setup_buffer_size_combo_box (
  GtkComboBox * cb)
{
  z_gtk_configure_simple_combo_box (
    cb, ui_create_buffer_size_model ());

  char id[40];
  sprintf (id, "%d",
    g_settings_get_enum (
      S_P_GENERAL_ENGINE,
      "buffer-size"));
  gtk_combo_box_set_active_id (
    GTK_COMBO_BOX (cb), id);
}

/**
 * Sets up a pan law combo box.
 */
void
ui_setup_samplerate_combo_box (
  GtkComboBox * cb)
{
  z_gtk_configure_simple_combo_box (
    cb, ui_create_samplerate_model ());

  char id[40];
  sprintf (id, "%d",
    g_settings_get_enum (
      S_P_GENERAL_ENGINE,
      "sample-rate"));
  gtk_combo_box_set_active_id (
    GTK_COMBO_BOX (cb), id);
}

/**
 * Sets up a pan law combo box.
 */
void
ui_setup_device_name_combo_box (
  GtkComboBoxText * cb)
{
  AudioBackend backend =
    (AudioBackend)
    g_settings_get_enum (
      S_P_GENERAL_ENGINE, "audio-backend");

  gtk_combo_box_text_remove_all (cb);

#define SETUP_DEVICES(type) \
  { \
    char * names[1024]; \
    int num_names; \
    engine_##type##_get_device_names ( \
      AUDIO_ENGINE, 0, names, &num_names); \
    for (int i = 0; i < num_names; i++) \
      { \
        gtk_combo_box_text_append ( \
          cb, NULL, names[i]); \
      } \
    char * current_device = \
      g_settings_get_string ( \
        S_P_GENERAL_ENGINE, \
        #type "-audio-device-name"); \
    for (int i = 0; i < num_names; i++) \
      { \
        if (string_is_equal ( \
              names[i], current_device)) \
          { \
            gtk_combo_box_set_active ( \
              GTK_COMBO_BOX (cb), i); \
          } \
        g_free (names[i]); \
      } \
    g_free (current_device); \
  }

  switch (backend)
    {
#ifdef HAVE_SDL
    case AUDIO_BACKEND_SDL:
      SETUP_DEVICES (sdl);
      break;
#endif
#ifdef HAVE_RTAUDIO
    case AUDIO_BACKEND_ALSA_RTAUDIO:
    case AUDIO_BACKEND_JACK_RTAUDIO:
    case AUDIO_BACKEND_PULSEAUDIO_RTAUDIO:
    case AUDIO_BACKEND_COREAUDIO_RTAUDIO:
    case AUDIO_BACKEND_WASAPI_RTAUDIO:
    case AUDIO_BACKEND_ASIO_RTAUDIO:
      SETUP_DEVICES (rtaudio);
      break;
#endif
    default:
      break;
    }
}

/**
 * Sets up the VST paths entry.
 */
void
ui_setup_vst_paths_entry (
  GtkEntry * entry)
{
  char ** paths =
    g_settings_get_strv (
      S_P_PLUGINS_PATHS,
      "vst-search-paths-windows");
  g_return_if_fail (paths);

  int path_idx = 0;
  char * path;
  char delimited_paths[6000];
  delimited_paths[0] = '\0';
  while ((path = paths[path_idx++]) != NULL)
    {
      strcat (delimited_paths, path);
      strcat (delimited_paths, ";");
    }
  delimited_paths[strlen (delimited_paths) - 1] =
    '\0';

  gtk_entry_set_text (
    entry, delimited_paths);
}

/**
 * Updates the the VST paths in the gsettings from
 * the text in the entry.
 */
void
ui_update_vst_paths_from_entry (
  GtkEntry * entry)
{
  const char * txt =
    gtk_entry_get_text (entry);
  g_return_if_fail (txt);
  char ** paths =
    g_strsplit (txt, ";", 0);
  g_settings_set_strv (
    S_P_PLUGINS_PATHS, "vst-search-paths-windows",
    (const char * const *) paths);
  g_free (paths);
}

/**
 * Returns the contrasting color (variation of
 * black or white) based on if the given color is
 * dark enough or not.
 *
 * @param src The source color.
 * @param dest The desination color to write to.
 */
void
ui_get_contrast_color (
  GdkRGBA * src,
  GdkRGBA * dest)
{
  /* if color is too bright use dark text,
   * otherwise use bright text */
  if (color_is_bright (src))
    *dest = UI_COLORS->dark_text;
  else
    *dest = UI_COLORS->bright_text;
}

/**
 * Returns the color in-between two colors.
 *
 * @param transition How far to transition (0.5 for
 *   half).
 */
void
ui_get_mid_color (
  GdkRGBA * dest,
  const GdkRGBA * c1,
  const GdkRGBA * c2,
  const double    transition)
{
  dest->red =
    c1->red * transition +
    c2->red * (1.0 - transition);
  dest->green =
    c1->green * transition +
    c2->green * (1.0 - transition);
  dest->blue =
    c1->blue * transition +
    c2->blue * (1.0 - transition);
  dest->alpha =
    c1->alpha * transition +
    c2->alpha * (1.0 - transition);
}

/**
 * Used in handlers to get the state mask.
 */
GdkModifierType
ui_get_state_mask (
  GtkGesture * gesture)
{
  GdkEventSequence * _sequence =
    gtk_gesture_single_get_current_sequence (
      GTK_GESTURE_SINGLE (gesture));
  const GdkEvent * _event =
    gtk_gesture_get_last_event (
      GTK_GESTURE (gesture), _sequence);
  GdkModifierType state_mask;
  gdk_event_get_state (_event, &state_mask);
  return state_mask;
}

/**
 * Gets the color the widget should be.
 *
 * @param color The original color.
 * @param is_selected Whether the widget is supposed
 *   to be selected or not.
 */
void
ui_get_arranger_object_color (
  GdkRGBA *    color,
  const bool   is_hovered,
  const bool   is_selected,
  const bool   is_transient,
  const bool   is_muted)
{
  if (DEBUGGING)
    color->alpha = 0.4;
  else
    color->alpha = is_transient ? 0.7 : 1.0;
  if (is_muted)
    {
      color->red = 0.6;
      color->green = 0.6;
      color->blue = 0.6;
    }
  if (is_selected)
    {
      color->red += is_muted ? 0.2 : 0.4;
      color->green += 0.2;
      color->blue += 0.2;
      color->alpha = DEBUGGING ? 0.5 : 1.0;
    }
  else if (is_hovered)
    {
      if (color_is_very_bright (color))
        {
          color->red -= 0.1;
          color->green -= 0.1;
          color->blue -= 0.1;
        }
      else
        {
          color->red += 0.1;
          color->green += 0.1;
          color->blue += 0.1;
        }
    }
}

/**
 * Gets a draggable value as a normalized value
 * between 0 and 1.
 *
 * @param size Widget size (either width or height).
 * @param start_px Px at start of drag.
 * @param cur_px Current px.
 * @param last_px Px during last call.
 */
double
ui_get_normalized_draggable_value (
  double       size,
  double       cur_val,
  double       start_px,
  double       cur_px,
  double       last_px,
  double       multiplier,
  UiDragMode   mode)
{
  switch (mode)
    {
    case UI_DRAG_MODE_CURSOR:
      return CLAMP (cur_px / size, 0.0, 1.0);
    case UI_DRAG_MODE_RELATIVE:
      return
        CLAMP (
          cur_val + (cur_px - last_px) / size,
          0.0, 1.0);
    case UI_DRAG_MODE_RELATIVE_WITH_MULTIPLIER:
      return
        CLAMP (
          cur_val +
          (multiplier * (cur_px - last_px)) / size,
          0.0, 1.0);
    }

  g_return_val_if_reached (0.0);
}

UiDetail
ui_get_detail_level (void)
{
  return
    (UiDetail)
    g_settings_get_enum (
      S_P_UI_GENERAL, "graphic-detail");
}

/**
 * All purpose menuitem callback for binding MIDI
 * CC to a port.
 *
 * An action will be performed if bound.
 */
void
ui_bind_midi_cc_item_activate_cb (
  GtkMenuItem * menuitem,
  Port *        port)
{
  BindCcDialogWidget * dialog =
    bind_cc_dialog_widget_new (
      port, true);

  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (GTK_WIDGET (dialog));
}

UiCaches *
ui_caches_new ()
{
  UiCaches * self = object_new (UiCaches);

  GtkWidget * widget =
    gtk_drawing_area_new ();
  GtkStyleContext * context =
    gtk_widget_get_style_context (widget);

  UiColors * colors = &self->colors;
  bool ret;

#define GET_COLOR_FROM_THEME(cname) \
  ret = \
    gtk_style_context_lookup_color ( \
      context, #cname, \
      &colors->cname); \
  g_return_val_if_fail (ret, NULL)

  GET_COLOR_FROM_THEME (bright_green);
  GET_COLOR_FROM_THEME (darkish_green);
  GET_COLOR_FROM_THEME (dark_orange);
  GET_COLOR_FROM_THEME (bright_orange);
  GET_COLOR_FROM_THEME (matcha);
  GET_COLOR_FROM_THEME (prefader_send);
  GET_COLOR_FROM_THEME (postfader_send);
  GET_COLOR_FROM_THEME (solo_active);
  GET_COLOR_FROM_THEME (solo_checked);
  GET_COLOR_FROM_THEME (fader_fill_start);
  GET_COLOR_FROM_THEME (fader_fill_end);
  GET_COLOR_FROM_THEME (highlight_scale_bg);
  GET_COLOR_FROM_THEME (highlight_chord_bg);
  GET_COLOR_FROM_THEME (highlight_bass_bg);
  GET_COLOR_FROM_THEME (highlight_both_bg);
  GET_COLOR_FROM_THEME (highlight_scale_fg);
  GET_COLOR_FROM_THEME (highlight_chord_fg);
  GET_COLOR_FROM_THEME (highlight_bass_fg);
  GET_COLOR_FROM_THEME (highlight_both_fg);

#undef GET_COLOR_FROM_THEME

  gtk_widget_destroy (widget);

  gdk_rgba_parse (
    &colors->dark_text, UI_COLOR_DARK_TEXT);
  gdk_rgba_parse (
    &colors->bright_text, UI_COLOR_BRIGHT_TEXT);
  gdk_rgba_parse (
    &colors->record_active, UI_COLOR_RECORD_ACTIVE);
  gdk_rgba_parse (
    &colors->record_checked,
    UI_COLOR_RECORD_CHECKED);

  return self;
}

void
ui_caches_free (
  UiCaches * self)
{
  for (int i = 0; i < self->num_cursors; i++)
    {
      UiCursor * cursor = &self->cursors[i];

      g_object_unref (cursor->cursor);
      g_object_unref (cursor->pixbuf);
    }

  object_zero_and_free (self);
}
