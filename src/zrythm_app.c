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
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *
 * Copyright (C) 2005-2019 Paul Davis <paul@linuxaudiosystems.com>
 * Copyright (C) 2005 Taybin Rutkin <taybin@taybin.com>
 * Copyright (C) 2006-2008 Doug McLain <doug@nostar.net>
 * Copyright (C) 2006-2015 David Robillard <d@drobilla.net>
 * Copyright (C) 2006-2017 Tim Mayberry <mojofunk@gmail.com>
 * Copyright (C) 2006 Sampo Savolainen <v2@iki.fi>
 * Copyright (C) 2009-2012 Carl Hetherington <carl@carlh.net>
 * Copyright (C) 2012-2019 Robin Gareus <robin@gareus.org>
 * Copyright (C) 2013-2015 John Emmas <john@creativepost.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "zrythm-config.h"

#ifdef _WOE32
#include <stdio.h>   // for _setmaxstdio
#else
#include <sys/mman.h>
#include <sys/resource.h>
#endif
#include <stdlib.h>

#include "actions/actions.h"
#include "actions/undo_manager.h"
#include "audio/engine.h"
#include "audio/router.h"
#include "audio/quantize_options.h"
#include "audio/track.h"
#include "audio/tracklist.h"
#ifdef HAVE_GUILE
#include "guile/guile.h"
#include "guile/project_generator.h"
#endif
#include "gui/accel.h"
#include "gui/backend/file_manager.h"
#include "gui/backend/piano_roll.h"
#include "gui/widgets/dialogs/bug_report_dialog.h"
#include "gui/widgets/first_run_assistant.h"
#include "gui/widgets/main_window.h"
#include "gui/widgets/project_assistant.h"
#include "gui/widgets/splash.h"
#include "plugins/plugin_manager.h"
#include "project.h"
#include "settings/settings.h"
#include "utils/arrays.h"
#include "utils/backtrace.h"
#include "utils/cairo.h"
#include "utils/env.h"
#include "utils/flags.h"
#include "utils/gtk.h"
#include "utils/io.h"
#include "utils/localization.h"
#include "utils/log.h"
#include "utils/math.h"
#include "utils/object_pool.h"
#include "utils/objects.h"
#include "utils/string.h"
#include "utils/symap.h"
#include "utils/ui.h"
#include "zrythm.h"
#include "zrythm_app.h"

#include "ext/whereami/whereami.h"

#include "Wrapper.h"

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef HAVE_GTK_SOURCE_VIEW_4
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#include <gtksourceview/gtksource.h>
#pragma GCC diagnostic pop
#endif

#include <audec/audec.h>
#include <fftw3.h>
#ifdef HAVE_LSP_DSP
#include <lsp-plug.in/dsp/dsp.h>
#endif
#include <suil/suil.h>
#ifdef HAVE_X11
#include <X11/Xlib.h>
#endif
#ifdef PHONE_HOME
#include <curl/curl.h>
#endif

/** This is declared extern in zrythm_app.h. */
ZrythmApp * zrythm_app = NULL;

G_DEFINE_TYPE (
  ZrythmApp, zrythm_app,
  GTK_TYPE_APPLICATION);

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wanalyzer-unsafe-call-within-signal-handler"
/** SIGSEGV handler. */
static void
segv_handler (int sig)
{
  char prefix[200];
#ifdef _WOE32
  strcpy (
    prefix, _("Error - Backtrace:\n"));
#else
  sprintf (
    prefix,
    _("Error: %s - Backtrace:\n"), strsignal (sig));
#endif
  char * bt =
    backtrace_get_with_lines (prefix, 100, true);

  /* call the callback to write queued messages
   * and get last few lines of the log, before
   * logging the backtrace */
  log_idle_cb (LOG);
  g_message ("%s", bt);
  log_idle_cb (LOG);

  char str[500];
  sprintf (
    str, _("%s has crashed. "), PROGRAM_NAME);
  GtkWidget * dialog =
    bug_report_dialog_new (
      GTK_WINDOW (MAIN_WINDOW), str, bt);

  /* run the dialog */
  gtk_dialog_run (GTK_DIALOG (dialog));
  gtk_widget_destroy (dialog);

  exit (EXIT_FAILURE);
}
#pragma GCC diagnostic pop

/**
 * Initializes the array of recent projects in
 * Zrythm app.
 */
static void
init_recent_projects ()
{
  g_message ("Initializing recent projects...");

  gchar ** recent_projects =
    g_settings_get_strv (
      S_GENERAL, "recent-projects");

  /* get recent projects */
  ZRYTHM->num_recent_projects = 0;
  int count = 0;
  char * prj;
  while (recent_projects[count])
    {
      prj = recent_projects[count];

      /* skip duplicates */
      if (array_contains_cmp (
             ZRYTHM->recent_projects,
             ZRYTHM->num_recent_projects,
             prj, strcmp, 0, 1))
        {
          count++;
          continue;
        }

      ZRYTHM->recent_projects[
        ZRYTHM->num_recent_projects++] =
          g_strdup (prj);
    }
  g_strfreev (recent_projects);

  /* set last element to NULL because the call
   * takes a NULL terminated array */
  ZRYTHM->recent_projects[
    ZRYTHM->num_recent_projects] = NULL;

  /* save the new list */
  g_settings_set_strv (
    S_GENERAL, "recent-projects",
    (const char * const *) ZRYTHM->recent_projects);

  g_message ("done");
}

/**
 * Called after the main window and the project have been
 * initialized. Sets up the window using the backend.
 *
 * This is the final step.
 */
static void
on_setup_main_window (
  GSimpleAction * action,
  GVariant *      parameter,
  gpointer        data)
{
  g_message ("setting up...");

  ZrythmApp * self = ZRYTHM_APP (data);

  zrythm_app_set_progress_status (
    self, _("Setting up main window"), 0.98);

  /*main_window_widget_setup (MAIN_WINDOW);*/

  /*router_recalc_graph (ROUTER);*/
  /*engine_set_run (AUDIO_ENGINE, true);*/

#ifndef TRIAL_VER
  /* add timeout for auto-saving projects */
  unsigned int autosave_interval =
    g_settings_get_uint (
      S_P_PROJECTS_GENERAL, "autosave-interval");
  if (autosave_interval > 0)
    {
      PROJECT->last_autosave_time =
        g_get_monotonic_time ();
      g_timeout_add_seconds (
        3, project_autosave_cb, NULL);
    }
#endif

  if (self->splash)
    {
      splash_window_widget_close (self->splash);
      self->splash = NULL;
    }

  g_message ("done");
}

/**
 * Called after the main window has been
 * initialized.
 *
 * Loads the project backend or creates the default
 * one.
 * FIXME rename
 */
static void on_load_project (
  GSimpleAction  *action,
  GVariant *parameter,
  gpointer  user_data)
{
  zrythm_app_set_progress_status (
    zrythm_app,
    _("Loading project"),
    0.8);

  int ret =
    project_load (
      ZRYTHM->open_filename,
      ZRYTHM->opening_template);

  if (ret != 0)
    {
      char msg[600];
      sprintf (
        msg,
        _("No project has been selected. %s "
        "will now close."),
        PROGRAM_NAME);
      ui_show_error_message (
        NULL, msg);
      exit (0);
    }

  g_action_group_activate_action (
    G_ACTION_GROUP (zrythm_app),
    "setup_main_window",
    NULL);
}

static void *
init_thread (
  gpointer data)
{
  g_message ("init thread starting...");

  ZrythmApp * self = ZRYTHM_APP (data);

  zrythm_app_set_progress_status (
    self, _("Initializing settings"), 0.0);

  ZRYTHM->debug = env_get_int ("ZRYTHM_DEBUG", 0);
  /* init zrythm folders ~/Zrythm */
  char msg[500];
  sprintf (
    msg,
    _("Initializing %s directories"),
    PROGRAM_NAME);
  zrythm_app_set_progress_status (
    self, msg, 0.01);
  zrythm_init_user_dirs_and_files (ZRYTHM);
  init_recent_projects ();
  zrythm_init_templates (ZRYTHM);

  /* init log */
  zrythm_app_set_progress_status (
    self, _("Initializing logging system"), 0.02);
  log_init_with_file (LOG, NULL);

#if defined (_WOE32) || defined (__APPLE__)
  g_warning (
    "Warning, you are running a non-free operating "
    "system.");
#endif

  zrythm_app_set_progress_status (
    self, _("Initializing caches"), 0.05);
  self->ui_caches = ui_caches_new ();

  zrythm_app_set_progress_status (
    self, _("Initializing file manager"), 0.15);
  file_manager_load_files (FILE_MANAGER);

  if (!g_settings_get_boolean (
         S_GENERAL, "first-run"))
    {
      zrythm_app_set_progress_status (
        self, _("Scanning plugins"), 0.4);
      plugin_manager_scan_plugins (
        ZRYTHM->plugin_manager, 0.7,
        &ZRYTHM->progress);
    }

  self->init_finished = true;

  g_message ("done");

  return NULL;
}

/**
 * Unlike the init thread, this will run in the
 * main GTK thread. Do not put expensive logic here.
 *
 * This should be ran after the expensive
 * initialization has finished.
 */
static int
idle_func (
  ZrythmApp * self)
{
  if (self->init_finished)
    {
      log_init_writer_idle (LOG, 3);

      g_action_group_activate_action (
        G_ACTION_GROUP (self),
        "prompt_for_project", NULL);

      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

/**
 * Thread that scans for plugins after the first
 * run.
 */
static void *
scan_plugins_after_first_run_thread (
  gpointer data)
{
  g_message ("scanning...");

  ZrythmApp * self = ZRYTHM_APP (data);

  plugin_manager_scan_plugins (
    ZRYTHM->plugin_manager,
    0.7, &ZRYTHM->progress);

  self->init_finished = true;

  g_message ("done");

  return NULL;
}

static void
on_first_run_assistant_apply (
  GtkAssistant * _assistant,
  ZrythmApp *    self)
{
  g_message ("on apply...");

  g_settings_set_boolean (
    S_GENERAL, "first-run", 0);

  /* start plugin scanning in another thread */
  self->init_thread =
    g_thread_new (
      "scan_plugins_after_first_run_thread",
      (GThreadFunc)
      scan_plugins_after_first_run_thread,
      self);

  /* set a source func in the main GTK thread to
   * check when scanning finished */
  self->init_finished = 0;
  g_idle_add ((GSourceFunc) idle_func, self);

  gtk_widget_set_visible (
    GTK_WIDGET (self->first_run_assistant), 0);

  /* close the first run assistant if it ran
   * before */
  if (self->assistant)
    {
      DESTROY_LATER (_assistant);
      self->first_run_assistant = NULL;
    }

  g_message ("done");
}

static void
on_first_run_assistant_cancel ()
{
  g_message ("%s: cancel", __func__);

  exit (0);
}

/**
 * Called before on_load_project.
 *
 * Checks if a project was given in the command
 * line. If not, it prompts the user for a project
 * (start assistant).
 */
static void on_prompt_for_project (
  GSimpleAction * action,
  GVariant *      parameter,
  gpointer        data)
{
  g_message ("prompting...");

  ZrythmApp * self = zrythm_app;

  if (ZRYTHM->open_filename)
    {
      g_action_group_activate_action (
        G_ACTION_GROUP (self),
        "load_project", NULL);
    }
  else
    {
      if (g_settings_get_boolean (
            S_GENERAL, "first-run"))
        {
          /* warranty disclaimer */
          GtkDialogFlags flags =
            GTK_DIALOG_DESTROY_WITH_PARENT;
          GtkWidget * dialog =
            gtk_message_dialog_new (
              NULL, flags,
              GTK_MESSAGE_INFO,
              GTK_BUTTONS_OK,
"Copyright © 2018-2021 The Zrythm contributors\n"
"\n"
"Zrythm is free software: you can redistribute it and/or modify\n"
"it under the terms of the GNU Affero General Public License as published by\n"
"the Free Software Foundation, either version 3 of the License, or\n"
"(at your option) any later version.\n"
"\n"
"Zrythm is distributed in the hope that it will be useful,\n"
"but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
"MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
"GNU Affero General Public License for more details.\n"
"\n"
"You should have received a copy of the GNU Affero General Public License\n"
"along with Zrythm.  If not, see <https://www.gnu.org/licenses/>."
#if !defined(HAVE_CUSTOM_NAME) || !defined(HAVE_CUSTOM_LOGO_AND_SPLASH)
"\n\nZrythm and the Zrythm logo are trademarks of Alexandros Theodotou"
#endif
);
          gtk_window_set_title (
            GTK_WINDOW (dialog),
            _("License Information"));
          gtk_window_set_icon_name (
            GTK_WINDOW (dialog), "zrythm");
          gtk_dialog_run (GTK_DIALOG (dialog));
          gtk_widget_destroy (dialog);

          self->first_run_assistant =
            first_run_assistant_widget_new (
              GTK_WINDOW (self->splash));
          g_signal_connect (
            G_OBJECT (self->first_run_assistant),
            "apply",
            G_CALLBACK (
              on_first_run_assistant_apply), self);
          g_signal_connect (
            G_OBJECT (self->first_run_assistant),
            "cancel",
            G_CALLBACK (
              on_first_run_assistant_cancel),
            NULL);
          gtk_window_present (
            GTK_WINDOW (
              self->first_run_assistant));

          return;
        }

      zrythm_app_set_progress_status (
        self, _("Waiting for project"), 0.8);

      /* show the assistant */
      self->assistant =
        project_assistant_widget_new (
          GTK_WINDOW (self->splash), 1);
      gtk_widget_set_visible (
        GTK_WIDGET (self->assistant), 1);

#ifdef __APPLE__
  /* possibly not necessary / working, forces app *
   * window on top */
  show_on_top();
#endif
    }

  g_message ("done");
}

/**
 * Sets the current status and progress percentage
 * during loading.
 *
 * The splash screen then reads these values from
 * the Zrythm struct.
 */
void
zrythm_app_set_progress_status (
  ZrythmApp *  self,
  const char * text,
  const double perc)
{
  zix_sem_wait (&self->progress_status_lock);
  g_message ("%s", text);
  strcpy (self->status, text);
  ZRYTHM->progress = perc;
  zix_sem_post (&self->progress_status_lock);
}

void
zrythm_app_set_font_scale (
  ZrythmApp * self,
  double      font_scale)
{
  /* this was taken from the GtkInspector
   * visual.c code */
  g_object_set (
    self->default_settings,
    "gtk-xft-dpi",
    (int) (font_scale * 96 * 1024),
    NULL);
}

/*
 * Called after startup if no filename is passed on
 * command line.
 */
static void
zrythm_app_activate (
  GApplication * _app)
{
  g_message ("Activating...");
  /*g_message ("activate %d", *task_id);*/

  g_message ("done");
}

/**
 * Called when a filename is passed to the command line
 * instead of activate.
 *
 * Always gets called after startup and before the tasks.
 */
static void
zrythm_app_open (
  GApplication * _app,
  GFile **       files,
  gint           n_files,
  const gchar *  hint)
{
  g_message ("Opening...");

  g_warn_if_fail (n_files == 1);

  GFile * file = files[0];
  ZRYTHM->open_filename =
    g_file_get_path (file);
  g_message ("open %s", ZRYTHM->open_filename);

  g_message ("done");
}

static void
print_gdk_pixbuf_format_info (
  gpointer data,
  gpointer user_data)
{
  GdkPixbufFormat * format =
    (GdkPixbufFormat *) data;
  ZrythmApp * self = ZRYTHM_APP (user_data);

  char * name =
    gdk_pixbuf_format_get_name (format);
  char * description =
    gdk_pixbuf_format_get_description (format);
  char * license =
    gdk_pixbuf_format_get_license (format);
  char mime_types[800] = "";
  char ** _mime_types =
    gdk_pixbuf_format_get_mime_types (
      format);
  char * tmp = NULL;
  int i = 0;
  while ((tmp = _mime_types[i++]))
    {
      strcat (mime_types, tmp);
      strcat (mime_types, ", ");
    }
  mime_types[strlen (mime_types) - 2] = '\0';
  g_strfreev (_mime_types);
  char extensions[800] = "";
  char ** _extensions =
    gdk_pixbuf_format_get_extensions (
      format);
  tmp = NULL;
  i = 0;
  while ((tmp = _extensions[i++]))
    {
      strcat (extensions, tmp);
      strcat (extensions, ", ");
      if (g_str_has_prefix (tmp, "svg"))
        {
          self->have_svg_loader = true;
        }
    }
  extensions[strlen (extensions) - 2] = '\0';
  g_strfreev (_extensions);
  g_message (
    "Found GDK Pixbuf Format:\n"
    "name: %s\ndescription: %s\n"
    "mime types: %s\nextensions: %s\n"
    "is scalable: %d\nis disabled: %d\n"
    "license: %s",
    name, description, mime_types,
    extensions,
    gdk_pixbuf_format_is_scalable (format),
    gdk_pixbuf_format_is_disabled (format),
    license);
  g_free (name);
  g_free (description);
  g_free (license);
}

static void
load_icon (
  GtkIconTheme * icon_theme,
  const char *   icon_name)
{
  g_message (
    "Attempting to load an icon from the icon "
    "theme...");
  GError * err = NULL;
  GdkPixbuf * icon =
    gtk_icon_theme_load_icon (
      icon_theme, icon_name, 48, 0, &err);
  g_message ("icon: %p", icon);
  if (err)
    {
      char err_msg[600];
      sprintf (
        err_msg,
        "Failed to load icon from icon theme: %s. "
        "Please install zrythm and breeze-icons.",
        err->message);
      g_critical ("%s", err_msg);
      fprintf (stderr, "%s\n", err_msg);
      ui_show_message_full (
        NULL, GTK_MESSAGE_ERROR, err_msg);
      g_error ("Failed to load icon");
    }
  g_object_unref (icon);
  g_message ("Icon loaded.");
}

static void
lock_memory (void)
{
#ifdef _WOE32
  /* TODO */
#else

  bool have_unlimited_mem = false;

  struct rlimit rl;
  if (getrlimit (RLIMIT_MEMLOCK, &rl) == 0)
    {
      if (rl.rlim_max == RLIM_INFINITY)
        {
          if (rl.rlim_cur == RLIM_INFINITY)
            {
              have_unlimited_mem = true;
            }
          else
            {
              rl.rlim_cur = RLIM_INFINITY;
              if (setrlimit (
                    RLIMIT_MEMLOCK, &rl) == 0)
                {
                  have_unlimited_mem = true;
                }
              else
                {
                  ui_show_error_message (
                    NULL,
                    "Could not set system memory "
                    "lock limit to 'unlimited'");
                }
            }
        }
      else
        {
          ui_show_message_printf (
            NULL, GTK_MESSAGE_WARNING,
            "Your user does not have enough "
            "privileges to allow %s to lock "
            "unlimited memory. This may cause "
            "audio dropouts. Please refer to "
            "the user manual for details.",
            PROGRAM_NAME);
        }
    }
  else
    {
      ui_show_message_printf (
        NULL, GTK_MESSAGE_WARNING,
        "Could not get system memory lock limit "
        "(%s)",
        strerror (errno));
    }

  if (have_unlimited_mem)
    {
      /* lock down memory */
      g_message ("Locking down memory...");
      if (mlockall (MCL_CURRENT | MCL_FUTURE))
        {
          ui_show_message_printf (
            NULL, GTK_MESSAGE_WARNING,
            "Cannot lock down memory: %s",
            strerror (errno));
        }
    }
#endif
}

/**
 * lotsa_files_please() from ardour
 * (libs/ardour/globals.cc).
 */
static void
raise_open_file_limit ()
{
#ifdef _WOE32
  /* this only affects stdio. 2048 is the maxium possible (512 the default).
   *
   * If we want more, we'll have to replaces the POSIX I/O interfaces with
   * Win32 API calls (CreateFile, WriteFile, etc) which allows for 16K.
   *
   * see http://stackoverflow.com/questions/870173/is-there-a-limit-on-number-of-open-files-in-windows
   * and http://bugs.mysql.com/bug.php?id=24509
   */
  int newmax = _setmaxstdio (2048);
  if (newmax > 0)
    {
      g_message (
        "Your system is configured to limit %s to "
        "%d open files",
        PROGRAM_NAME, newmax);
    }
  else
    {
      g_warning (
        "Could not set system open files limit. "
        "Current limit is %d open files",
        _getmaxstdio ());
    }
#else /* else if not _WOE32 */
  struct rlimit rl;

  if (getrlimit (RLIMIT_NOFILE, &rl) == 0)
    {
#ifdef __APPLE__
      /* See the COMPATIBILITY note on the Apple setrlimit() man page */
      rl.rlim_cur =
        MIN ((rlim_t) OPEN_MAX, rl.rlim_max);
#else
      rl.rlim_cur = rl.rlim_max;
#endif

      if (setrlimit (RLIMIT_NOFILE, &rl) != 0)
        {
          if (rl.rlim_cur == RLIM_INFINITY)
            {

              g_warning (
                "Could not set system open files "
                "limit to \"unlimited\"");
            }
          else
            {
              g_warning (
                "Could not set system open files "
                "limit to %ju",
                (uintmax_t) rl.rlim_cur);
            }

        }
      else
        {
          if (rl.rlim_cur != RLIM_INFINITY)
            {
              g_message (
                "Your system is configured to "
                "limit %s to %ju open files",
                PROGRAM_NAME, (uintmax_t) rl.rlim_cur);
            }
        }
    }
  else
    {
      g_warning (
        "Could not get system open files limit (%s)",
        strerror (errno));
    }
#endif
}

/**
 * First function that gets called afted CLI args
 * are parsed and processed.
 *
 * This gets called before open or activate.
 */
static void
zrythm_app_startup (
  GApplication * app)
{
  g_message ("Starting up...");

  ZrythmApp * self = ZRYTHM_APP (app);

  /* init localization, using system locale if
   * first run */
  GSettings * prefs =
    g_settings_new (
      GSETTINGS_ZRYTHM_PREFIX ".general");
  localization_init (
    g_settings_get_boolean (
      prefs, "first-run"), true);
  g_object_unref (G_OBJECT (prefs));

  char * exe_path = NULL;
  int dirname_length, length;
  length =
    wai_getExecutablePath (
      NULL, 0, &dirname_length);
  if (length > 0)
  {
    exe_path =
      (char *) malloc ((size_t) length + 1);
    wai_getExecutablePath (
      exe_path, length, &dirname_length);
    exe_path[length] = '\0';
  }

  ZRYTHM =
    zrythm_new (
      exe_path ? exe_path : self->argv[0], true,
      false, true);

  char * ver = zrythm_get_version (0);
  fprintf (
    stdout,
    _("%s-%s Copyright (C) 2018-2021 The Zrythm contributors\n\n"
    "%s comes with ABSOLUTELY NO WARRANTY!\n\n"
    "This is free software, and you are welcome to redistribute it\n"
    "under certain conditions. See the file `COPYING' for details.\n\n"
    "Write comments and bugs to %s\n"
    "Support this project at https://liberapay.com/Zrythm\n\n"),
    PROGRAM_NAME, ver, PROGRAM_NAME,
    ISSUE_TRACKER_URL);
  g_free (ver);

  char * cur_dir = g_get_current_dir ();
  g_message (
    "Running Zrythm in %s", cur_dir);
  g_free (cur_dir);

#ifdef HAVE_GUILE
  guile_init (self->argc, self->argv);
#endif

  g_message ("GTK_THEME=%s", getenv ("GTK_THEME"));

  /* install segfault handler */
  g_message ("Installing signal handler...");
  signal (SIGSEGV, segv_handler);
  signal (SIGABRT, segv_handler);

#ifdef HAVE_X11
  /* init xlib threads */
  g_message ("Initing X threads...");
  XInitThreads ();
#endif

  /* init suil */
  g_message ("Initing suil...");
  suil_init(
    &self->argc, &self->argv, SUIL_ARG_NONE);

  /* init fftw */
  g_message ("Making fftw planner thread safe...");
  fftw_make_planner_thread_safe ();
  fftwf_make_planner_thread_safe ();

  /* init audio decoder */
  g_message ("Initing audio decoder...");
  audec_init ();

#ifdef HAVE_LSP_DSP
  /* init lsp dsp */
  g_message ("Initing LSP DSP...");
  lsp_dsp_init();

  /* output information about the system */
  lsp_dsp_info_t * info = lsp_dsp_info();
  if (info)
    {
      printf("Architecture:   %s\n", info->arch);
      printf("Processor:      %s\n", info->cpu);
      printf("Model:          %s\n", info->model);
      printf("Features:       %s\n", info->features);
      free(info);
    }
  else
    {
      g_warning ("Failed to get system info");
    }

#endif

  /* init curl */
#ifdef PHONE_HOME
  curl_global_init (CURL_GLOBAL_ALL);
#endif

  /* init random */
  g_message ("Initing random...");
#ifdef _WOE32
  srand ((unsigned int) time (NULL));
#else
  srandom ((unsigned int) time (NULL));
#endif

  /* init gtksourceview */
#ifdef HAVE_GTK_SOURCE_VIEW_4
  gtk_source_init ();
#endif

  G_APPLICATION_CLASS (zrythm_app_parent_class)->
    startup (G_APPLICATION (self));

  g_message (
    "called startup on G_APPLICATION_CLASS");

  bool ret =
    g_application_get_is_registered (
      G_APPLICATION (self));
  bool remote =
    g_application_get_is_remote (
      G_APPLICATION (self));
  GMenuModel * app_menu_model =
    gtk_application_get_app_menu (
      GTK_APPLICATION (app));
  bool prefers_app_menu =
    gtk_application_prefers_app_menu (
      GTK_APPLICATION (app));
  g_message (
    "application registered: %d, is remote %d, "
    "app menu exists: %d, prefers app menu: %d",
    ret, remote, app_menu_model ? true : false,
    prefers_app_menu);

  g_message (
    "application resources base path: %s",
    g_application_get_resource_base_path (
      G_APPLICATION (app)));

  self->default_settings =
    gtk_settings_get_default ();

  /* set theme */
  g_object_set (
    self->default_settings,
    "gtk-theme-name", "Matcha-dark-sea", NULL);
  g_object_set (
    self->default_settings,
    "gtk-application-prefer-dark-theme", 1, NULL);
  int scale_factor =
    z_gtk_get_primary_monitor_scale_factor ();
  g_message (
    "Monitor scale factor: %d", scale_factor);
#if defined(_WOE32)
  g_object_set (
    self->default_settings,
    "gtk-font-name", "Segoe UI Normal 10", NULL);
  g_object_set (
    self->default_settings,
    "gtk-cursor-theme-name", "Adwaita", NULL);
  /*g_object_set (*/
    /*self->default_settings,*/
    /*"gtk-icon-theme-name", "breeze-dark", NULL);*/
#elif defined(__APPLE__)
  g_object_set (
    self->default_settings,
    "gtk-font-name", "Regular 10", NULL);
#else
  g_object_set (
    self->default_settings,
    "gtk-font-name", "Cantarell Regular 10", NULL);
#endif

  /* explicitly set font scaling */
  double font_scale =
    g_settings_get_double (
      S_P_UI_GENERAL, "font-scale");
  zrythm_app_set_font_scale (self, font_scale);

  g_message ("Theme set");

  GtkIconTheme * icon_theme =
    gtk_icon_theme_get_default ();
  g_object_set (
    self->default_settings, "gtk-icon-theme-name",
    "zrythm-dark", NULL);

  /* prepend freedesktop system icons to search
   * path, just in case */
  char * parent_datadir =
    zrythm_get_dir (ZRYTHM_DIR_SYSTEM_PARENT_DATADIR);
  char * freedesktop_icon_theme_dir =
    g_build_filename (
      parent_datadir, "icons", NULL);
  gtk_icon_theme_prepend_search_path (
    icon_theme, freedesktop_icon_theme_dir);
  g_message (
    "prepended icon theme search path: %s",
    freedesktop_icon_theme_dir);
  g_free (parent_datadir);
  g_free (freedesktop_icon_theme_dir);

  /* prepend zrythm system icons to search path */
  char * system_themes_dir =
    zrythm_get_dir (ZRYTHM_DIR_SYSTEM_THEMESDIR);
  char * system_icon_theme_dir =
    g_build_filename (
      system_themes_dir, "icons", NULL);
  gtk_icon_theme_prepend_search_path (
    icon_theme,
    system_icon_theme_dir);
  g_message (
    "prepended icon theme search path: %s",
    system_icon_theme_dir);
  g_free (system_themes_dir);
  g_free (system_icon_theme_dir);

  /* prepend user custom icons to search path */
  char * user_themes_dir =
    zrythm_get_dir (ZRYTHM_DIR_USER_THEMES);
  char * user_icon_theme_dir =
    g_build_filename (
      user_themes_dir, "icons", NULL);
  gtk_icon_theme_prepend_search_path (
    icon_theme,
    user_icon_theme_dir);
  g_message (
    "prepended icon theme search path: %s",
    user_icon_theme_dir);
  g_free (user_themes_dir);
  g_free (user_icon_theme_dir);

  /* look for found loaders */
  g_message ("looking for GDK Pixbuf formats...");
  GSList * formats_list =
    gdk_pixbuf_get_formats ();
  g_slist_foreach (
    formats_list, print_gdk_pixbuf_format_info,
    self);
  g_slist_free (g_steal_pointer (&formats_list));
  if (!self->have_svg_loader)
    {
      fprintf (
        stderr, "SVG loader was not found.\n");
      exit (-1);
    }

  char * modules = NULL;
  g_object_get (
    self->default_settings,
    "gtk-modules", &modules,
    NULL);
  g_message ("GTK modules: %s", modules);

  /* try to load some icons */
  /* zrythm */
  load_icon (icon_theme, "solo");
  /* breeze dark */
  load_icon (icon_theme, "node-type-cusp");

  g_message ("Setting gtk icon theme resource paths...");
  gtk_icon_theme_add_resource_path (
    gtk_icon_theme_get_default (),
    "/org/zrythm/Zrythm/app/icons/zrythm");
  gtk_icon_theme_add_resource_path (
    gtk_icon_theme_get_default (),
    "/org/zrythm/Zrythm/app/icons/fork-awesome");
  gtk_icon_theme_add_resource_path (
    gtk_icon_theme_get_default (),
    "/org/zrythm/Zrythm/app/icons/font-awesome");
  gtk_icon_theme_add_resource_path (
    gtk_icon_theme_get_default (),
    "/org/zrythm/Zrythm/app/icons/ext");
  gtk_icon_theme_add_resource_path (
    gtk_icon_theme_get_default (),
    "/org/zrythm/Zrythm/app/icons/gnome-builder");
  gtk_icon_theme_add_resource_path (
    gtk_icon_theme_get_default (),
    "/org/zrythm/Zrythm/app/icons/breeze-icons");

  /*gtk_icon_theme_set_search_path (*/
    /*gtk_icon_theme_get_default (),*/
    /*path,*/
    /*1);*/
  g_message ("Resource paths set");

  /* set default css provider */
  GtkCssProvider * css_provider =
    gtk_css_provider_new();
  user_themes_dir =
    zrythm_get_dir (ZRYTHM_DIR_USER_THEMES);
  char * css_theme_path =
    g_build_filename (
      user_themes_dir, "theme.css", NULL);
  g_free (user_themes_dir);
  if (!g_file_test (
         css_theme_path, G_FILE_TEST_EXISTS))
    {
      g_free (css_theme_path);
      system_themes_dir =
        zrythm_get_dir (
          ZRYTHM_DIR_SYSTEM_THEMESDIR);
      css_theme_path =
        g_build_filename (
          system_themes_dir,
          "zrythm-theme.css", NULL);
      g_free (system_themes_dir);
    }
  GError * err = NULL;
  gtk_css_provider_load_from_path (
    css_provider, css_theme_path, &err);
  if (err)
    {
      g_warning (
        "Failed to load CSS from path %s: %s",
        css_theme_path, err->message);
    }
  gtk_style_context_add_provider_for_screen (
    gdk_screen_get_default (),
    GTK_STYLE_PROVIDER (css_provider), 800);
  g_object_unref (css_provider);
  g_message (
    "set default css provider from path: %s",
    css_theme_path);

  /* set default window icon */
  gtk_window_set_default_icon_name ("zrythm");

  /* allow maximum number of open files (taken from
   * ardour) */
  raise_open_file_limit ();

  lock_memory ();

  /* show splash screen */
  self->splash =
    splash_window_widget_new (self);
  g_debug ("created splash widget");
  gtk_window_present (
    GTK_WINDOW (self->splash));
  g_debug ("presented splash widget");

  /* start initialization in another thread */
  zix_sem_init (&self->progress_status_lock, 1);
  self->init_thread =
    g_thread_new (
      "init_thread", (GThreadFunc) init_thread,
      self);

  /* set a source func in the main GTK thread to
   * check when initialization finished */
  g_idle_add ((GSourceFunc) idle_func, self);

  /* install accelerators for each action */
#define INSTALL_ACCEL(keybind,action) \
  accel_install_primary_action_accelerator ( \
    keybind, action)

  INSTALL_ACCEL ("F1", "app.manual");
  INSTALL_ACCEL ("<Alt>F4", "app.quit");
  INSTALL_ACCEL ("F11", "app.fullscreen");
  INSTALL_ACCEL (
    "<Control><Shift>p", "app.preferences");
  INSTALL_ACCEL (
    "F2", "win.rename-track-or-region");
  INSTALL_ACCEL ("<Control>n", "win.new");
  INSTALL_ACCEL ("<Control>o", "win.open");
  INSTALL_ACCEL ("<Control>s", "win.save");
  INSTALL_ACCEL (
    "<Control><Shift>s", "win.save-as");
  INSTALL_ACCEL ("<Control>e", "win.export-as");
  INSTALL_ACCEL ("<Control>z", "win.undo");
  INSTALL_ACCEL (
    "<Control><Shift>z", "win.redo");
  INSTALL_ACCEL (
    "<Control>x", "win.cut");
  INSTALL_ACCEL (
    "<Control>c", "win.copy");
  INSTALL_ACCEL (
    "<Control>v", "win.paste");
  INSTALL_ACCEL (
    "<Control>d", "win.duplicate");
  INSTALL_ACCEL (
    "Delete", "win.delete");
  INSTALL_ACCEL (
    "<Control><Shift>a", "win.clear-selection");
  INSTALL_ACCEL (
    "<Control>a", "win.select-all");
  INSTALL_ACCEL (
    "<Control><Shift>4", "win.toggle-left-panel");
  INSTALL_ACCEL (
    "<Control><Shift>6", "win.toggle-right-panel");
  INSTALL_ACCEL (
    "<Control><Shift>2", "win.toggle-bot-panel");
  INSTALL_ACCEL (
    "<Control>equal", "win.zoom-in");
  INSTALL_ACCEL (
    "<Control>minus", "win.zoom-out");
  INSTALL_ACCEL (
    "<Control>plus", "win.original-size");
  INSTALL_ACCEL (
    "<Control>bracketleft", "win.best-fit");
  INSTALL_ACCEL (
    "<Control>l", "win.loop-selection");
  INSTALL_ACCEL (
    "1", "win.select-mode");
  INSTALL_ACCEL (
    "2", "win.edit-mode");
  INSTALL_ACCEL (
    "3", "win.cut-mode");
  INSTALL_ACCEL (
    "4", "win.eraser-mode");
  INSTALL_ACCEL (
    "5", "win.ramp-mode");
  INSTALL_ACCEL (
    "6", "win.audition-mode");
  accel_install_action_accelerator (
    "KP_4", "BackSpace", "win.goto-prev-marker");
  INSTALL_ACCEL (
    "KP_6", "win.goto-next-marker");
  INSTALL_ACCEL (
    "space", "win.play-pause");
  INSTALL_ACCEL (
    "Q", "win.quick-quantize::global");
  INSTALL_ACCEL (
    "<Alt>Q", "win.quantize-options::global");
  INSTALL_ACCEL (
    "<Shift>M", "win.mute-selection::global");
  INSTALL_ACCEL (
    "<Control>J", "win.merge-selection");
  INSTALL_ACCEL (
    gdk_keyval_name (GDK_KEY_Home),
    "win.go-to-start");

#undef INSTALL_ACCEL

  g_message ("done");
}

/**
 * Called immediately after the main GTK loop
 * terminates.
 *
 * This is also called manually on SIGINT.
 */
static void
zrythm_app_on_shutdown (
  GApplication * application,
  ZrythmApp *    self)
{
  g_message ("Shutting down...");

  if (ZRYTHM)
    {
      zrythm_free (ZRYTHM);
    }

#ifdef HAVE_GTK_SOURCE_VIEW_4
  gtk_source_finalize ();
#endif

  g_message ("done");
}

/**
 * Checks that the file exists and exits if it
 * doesn't.
 */
static void
verify_file_exists (
  const char * file)
{
  if (!file ||
      !g_file_test (file, G_FILE_TEST_EXISTS))
    {
      char str[600];
      sprintf (
        str, _("File %s not found."), file);
      strcat (str, "\n");
      fprintf (stderr, "%s", str);
      exit (-1);
    }
}

/**
 * Checks that the output is not NULL and exits if it
 * is.
 */
static void
verify_output_exists (
  ZrythmApp * self)
{
  if (!self->output_file)
    {
      char str[600];
      sprintf (
        str,
        "%s\n",
        _("An output file was not specified. Please "
        "pass one with `--output=FILE`."));
      fprintf (stderr, "%s", str);
      exit (EXIT_FAILURE);
    }
}

static bool
print_settings (
  ZrythmApp * self)
{
  localization_init (false, false);
  settings_print (self->pretty_print);

  exit (EXIT_SUCCESS);
}

static void
convert_project (
  ZrythmApp *  self,
  bool         compress,
  const char * file_to_convert)
{
  verify_file_exists (file_to_convert);

  char * output;
  size_t output_size;
  char * err_msg = NULL;
  if (compress)
    {
      verify_output_exists (self);

      err_msg =
        project_compress (
          &self->output_file, NULL,
          PROJECT_COMPRESS_FILE,
          file_to_convert, 0,
          PROJECT_COMPRESS_FILE);
    }
  else
    {
      if (self->output_file)
        {
          err_msg =
            project_decompress (
              &self->output_file, NULL,
              PROJECT_DECOMPRESS_FILE,
              file_to_convert, 0,
              PROJECT_DECOMPRESS_FILE);
        }
      else
        {
          err_msg =
            project_decompress (
              &output, &output_size,
              PROJECT_DECOMPRESS_DATA,
              file_to_convert, 0,
              PROJECT_DECOMPRESS_FILE);
        }
    }

  if (err_msg)
    {
      fprintf (
        stderr,
        _("Project failed to decompress: %s\n"),
        err_msg);
      g_free (err_msg);
      exit (EXIT_FAILURE);
    }
  else
    {
      if (!compress && !self->output_file)
        {
          output =
            g_realloc (
              output,
              output_size + sizeof (char));
          output[output_size] = '\0';
          fprintf (stdout, "%s\n", output);
        }
      exit (EXIT_SUCCESS);
    }

  fprintf (stdout, "%s\n", _("Unknown operation"));
  exit (EXIT_FAILURE);
}

static bool
gen_project (
  ZrythmApp *  self,
  const char * filepath)
{
  verify_output_exists (self);
#ifdef HAVE_GUILE
  ZRYTHM->generating_project = true;
  ZRYTHM->have_ui = false;
  int script_res =
    guile_project_generator_generate_project_from_file (
      filepath, self->output_file);
  exit (script_res);
#else
  fprintf (
    stderr,
    _("libguile is required for this option\n"));
  exit (EXIT_FAILURE);
#endif
}

static bool
reset_to_factory (void)
{
  settings_reset_to_factory (1, 1);

  exit (EXIT_SUCCESS);
}

/**
 * Called on the local instance to handle options.
 */
static int
on_handle_local_options (
  GApplication * app,
  GVariantDict * opts,
  ZrythmApp *    self)
{
  /*g_debug ("handling options");*/

#if 0
  /* print the contents */
  GVariant * variant = g_variant_dict_end (opts);
  char * str = g_variant_print (variant, true);
  g_warning ("%s", str);
#endif

  if (g_variant_dict_contains (
        opts, "print-settings"))
    {
      print_settings (self);
    }
  else if (g_variant_dict_contains (
             opts, "yaml-to-zpj"))
    {
      char * filepath = NULL;
      g_variant_dict_lookup (
        opts, "yaml-to-zpj", "^ay", &filepath);
      convert_project (self, true, filepath);
    }
  else if (g_variant_dict_contains (
             opts, "zpj-to-yaml"))
    {
      char * filepath = NULL;
      g_variant_dict_lookup (
        opts, "zpj-to-yaml", "^ay", &filepath);
      convert_project (self, false, filepath);
    }
  else if (g_variant_dict_contains (
             opts, "gen-project"))
    {
      char * filepath = NULL;
      g_variant_dict_lookup (
        opts, "gen-project", "^ay", &filepath);
      gen_project (self, filepath);
    }
  else if (g_variant_dict_contains (
             opts, "reset-to-factory"))
    {
      reset_to_factory ();
    }

  if (g_variant_dict_contains (
             opts, "cyaml-log-level"))
    {
      char * log_level = NULL;
      g_variant_dict_lookup (
        opts, "cyaml-log-level", "&s", &log_level);
      if (string_is_equal (log_level, "debug"))
        {
          yaml_set_log_level (CYAML_LOG_DEBUG);
        }
      else if (string_is_equal (log_level, "info"))
        {
          yaml_set_log_level (CYAML_LOG_INFO);
        }
      else if (string_is_equal (
                 log_level, "warning"))
        {
          yaml_set_log_level (CYAML_LOG_WARNING);
        }
      else if (string_is_equal (log_level, "error"))
        {
          yaml_set_log_level (CYAML_LOG_ERROR);
        }
    }

  return -1;
}

static bool
print_version (
  const gchar * option_name,
  const gchar * value,
  gpointer      data,
  GError **     error)
{
  char ver_with_caps[2000];
  zrythm_get_version_with_capabilities (
    ver_with_caps);
  fprintf (
    stdout,
    "%s\n%s\n%s\n%s\n",
    ver_with_caps,
    "Copyright © 2018-2021 The Zrythm contributors",
    "This is free software; see the source for copying conditions.",
    "There is NO "
    "warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.");

  exit (EXIT_SUCCESS);
}

static bool
set_dummy (
  const gchar * option_name,
  const gchar * value,
  gpointer      data,
  GError **     error)
{
  zrythm_app->midi_backend = g_strdup ("none");
  zrythm_app->audio_backend = g_strdup ("none");

  return true;
}

/**
 * Add the option entries.
 *
 * Things that can be processed immediately should
 * be set as callbacks here (like --version).
 *
 * Things that require to know other options before
 * running should be set as NULL and processed
 * in the handle-local-options handler.
 */
static void
add_option_entries (
  ZrythmApp * self)
{
  GOptionEntry entries[] =
    {
      { "version", 'v',  G_OPTION_FLAG_NO_ARG,
        G_OPTION_ARG_CALLBACK, print_version,
        _("Print version information"),
        NULL },
      { "zpj-to-yaml", 0,
        G_OPTION_FLAG_NONE,
        G_OPTION_ARG_FILENAME, NULL,
        _("Convert ZPJ-FILE to YAML"),
        "ZPJ-FILE" },
      { "yaml-to-zpj", 0,
        G_OPTION_FLAG_NONE,
        G_OPTION_ARG_FILENAME, NULL,
        _("Convert YAML-PROJECT-FILE to the .zpj format"),
        "YAML-PROJECT-FILE" },
      { "gen-project", 0,
        G_OPTION_FLAG_NONE,
        G_OPTION_ARG_FILENAME, NULL,
        _("Generate a project from SCRIPT-FILE"),
        "SCRIPT-FILE" },
      { "pretty", 0, G_OPTION_FLAG_NONE,
        G_OPTION_ARG_NONE, &self->pretty_print,
        _("Print output in user-friendly way"),
        NULL },
      { "print-settings", 'p', G_OPTION_FLAG_NONE,
        G_OPTION_ARG_NONE, NULL,
        _("Print current settings"), NULL },
      { "reset-to-factory", 0,
        G_OPTION_FLAG_NONE,
        G_OPTION_ARG_NONE, NULL,
        _("Reset to factory settings"), NULL },
      { "audio-backend", 0, G_OPTION_FLAG_NONE,
        G_OPTION_ARG_STRING, &self->audio_backend,
        _("Override the audio backend to use"),
        "BACKEND" },
      { "midi-backend", 0, G_OPTION_FLAG_NONE,
        G_OPTION_ARG_STRING, &self->midi_backend,
        _("Override the MIDI backend to use"),
        "BACKEND" },
      { "dummy", 0, G_OPTION_FLAG_NO_ARG,
        G_OPTION_ARG_CALLBACK, set_dummy,
        _("Shorthand for --midi-backend=none "
        "--audio-backend=none"),
        NULL },
      { "buf-size", 0, G_OPTION_FLAG_NONE,
        G_OPTION_ARG_INT, &self->buf_size,
        "Override the buffer size to use for the "
        "audio backend, if applicable",
        "BUF_SIZE" },
      { "samplerate", 0, G_OPTION_FLAG_NONE,
        G_OPTION_ARG_INT, &self->samplerate,
        "Override the samplerate to use for the "
        "audio backend, if applicable",
        "SAMPLERATE" },
      { "output", 'o', G_OPTION_FLAG_NONE,
        G_OPTION_ARG_STRING, &self->output_file,
        "File or directory to output to", "FILE" },
      { "cyaml-log-level", 'o', G_OPTION_FLAG_NONE,
        G_OPTION_ARG_STRING, NULL,
        "Cyaml log level",
        "LOG-LEVEL" },
      { NULL },
    };

  g_application_add_main_option_entries (
    G_APPLICATION (self), entries);
  g_application_set_option_context_parameter_string (
    G_APPLICATION (self), _("[PROJECT-FILE]"));

  char examples[8000];
  sprintf (
    examples,
    _("Examples:\n"
    "  --zpj-to-yaml a.zpj > b.yaml        Convert a a.zpj to YAML and save to b.yaml\n"
    "  --gen-project a.scm -o myproject    Generate myproject from a.scm\n"
    "  -p --pretty                         Pretty-print current settings\n\n"
    "Please report issues to %s\n"),
    ISSUE_TRACKER_URL);
  g_application_set_option_context_description (
    G_APPLICATION (self), examples);

  char summary[8000];
  sprintf (
    summary,
    _("Run %s, optionally passing a project "
    "file."),
    PROGRAM_NAME);
  g_application_set_option_context_summary (
    G_APPLICATION (self), summary);
}

/**
 * Creates the Zrythm GApplication.
 *
 * This also initializes the Zrythm struct.
 */
ZrythmApp *
zrythm_app_new (
  int           argc,
  const char ** argv)
{
  ZrythmApp * self =  g_object_new (
    ZRYTHM_APP_TYPE,
    /* if an ID is provided, this application
     * becomes unique (only one instance allowed) */
    /*"application-id", "org.zrythm.Zrythm",*/
    "resource-base-path", "/org/zrythm/Zrythm",
    "flags",
    G_APPLICATION_HANDLES_OPEN,
    NULL);

  zrythm_app = self;

  self->gtk_thread = g_thread_self ();

  /* add option entries */
  add_option_entries (self);

  /* add handlers */
  g_signal_connect (
    G_OBJECT (self), "shutdown",
    G_CALLBACK (zrythm_app_on_shutdown), self);
  g_signal_connect (
    G_OBJECT (self), "handle-local-options",
    G_CALLBACK (on_handle_local_options), self);

  return self;
}

static void
finalize (
  ZrythmApp * self)
{
  g_message (
    "%s (%s): finalizing ZrythmApp...",
    __func__, __FILE__);

  if (self->default_settings)
    {
      g_object_unref (self->default_settings);
    }

  object_free_w_func_and_null (
    ui_caches_free, self->ui_caches);

  /* init curl */
#ifdef PHONE_HOME
  curl_global_cleanup ();
#endif

  g_message ("%s: done", __func__);
}

static void
zrythm_app_class_init (ZrythmAppClass *class)
{
  G_APPLICATION_CLASS (class)->activate =
    zrythm_app_activate;
  G_APPLICATION_CLASS (class)->startup =
    zrythm_app_startup;
  G_APPLICATION_CLASS (class)->open =
    zrythm_app_open;
  /*G_APPLICATION_CLASS (class)->shutdown =*/
    /*zrythm_app_shutdown;*/

  GObjectClass * klass = G_OBJECT_CLASS (class);

  klass->finalize = (GObjectFinalizeFunc) finalize;
}

static void
zrythm_app_init (
  ZrythmApp * self)
{
  gdk_set_allowed_backends (
    "quartz,win32,wayland,x11,*");

  const GActionEntry entries[] = {
    { "prompt_for_project", on_prompt_for_project },
    /*{ "init_main_window", on_init_main_window },*/
    { "setup_main_window", on_setup_main_window },
    { "load_project", on_load_project },
    { "about", activate_about },
    { "fullscreen", activate_fullscreen },
    { "chat", activate_chat },
    { "manual", activate_manual },
    { "news", activate_news },
    { "bugreport", activate_bugreport },
    { "donate", activate_donate },
    { "iconify", activate_iconify },
    { "log", activate_log },
    { "preferences", activate_preferences },
    { "scripting-interface",
      activate_scripting_interface },
    { "quit", activate_quit },
  };

  g_action_map_add_action_entries (
    G_ACTION_MAP (self), entries,
    G_N_ELEMENTS (entries), self);
}
