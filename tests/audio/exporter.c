/*
 * Copyright (C) 2020-2021 Alexandros Theodotou <alex at zrythm dot org>
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

#include "zrythm-test-config.h"

#include "helpers/plugin_manager.h"
#include "helpers/zrythm.h"

#include "actions/tracklist_selections.h"
#include "audio/encoder.h"
#include "audio/exporter.h"
#include "audio/supported_file.h"
#include "project.h"
#include "utils/chromaprint.h"
#include "utils/math.h"
#include "utils/objects.h"
#include "zrythm.h"

#include <glib.h>

#include <sndfile.h>

static void
print_progress_and_sleep (
  const GenericProgressInfo * info)
{
  while (info->progress < 1.0)
    {
      g_message (
        "progress: %f.1", info->progress * 100.0);
      g_usleep (1000);
    }
}

static void
test_export_wav ()
{
  test_helper_zrythm_init ();

  int ret;

  char * filepath =
    g_build_filename (
      TESTS_SRCDIR, "test.wav", NULL);
  SupportedFile * file =
    supported_file_new_from_path (filepath);
  track_create_with_action (
    TRACK_TYPE_AUDIO, NULL, file, PLAYHEAD,
    TRACKLIST->num_tracks, 1, NULL);

  char * tmp_dir =
    g_dir_make_tmp ("test_wav_prj_XXXXXX", NULL);
  ret =
    project_save (
      PROJECT, tmp_dir, 0, 0, F_NO_ASYNC);
  g_free (tmp_dir);
  g_assert_cmpint (ret, ==, 0);


  for (int i = 0; i < 2; i++)
    {
      for (int j = 0; j < 2; j++)
        {
          g_assert_false (TRANSPORT_IS_ROLLING);
          g_assert_cmpint (
            TRANSPORT->playhead_pos.frames, ==, 0);

          char * filename =
            g_strdup_printf ("test_wav%d.wav", i);

          ExportSettings settings;
          settings.progress_info.has_error = false;
          settings.progress_info.cancelled = false;
          settings.format = AUDIO_FORMAT_WAV;
          settings.artist = g_strdup ("Test Artist");
          settings.title = g_strdup ("Test Title");
          settings.genre = g_strdup ("Test Genre");
          settings.depth = BIT_DEPTH_16;
          settings.time_range = TIME_RANGE_LOOP;
          if (j == 0)
            {
              settings.mode = EXPORT_MODE_FULL;
              tracklist_mark_all_tracks_for_bounce (
                TRACKLIST, F_NO_BOUNCE);
              settings.bounce_with_parents = false;
            }
          else
            {
              settings.mode = EXPORT_MODE_TRACKS;
              tracklist_mark_all_tracks_for_bounce (
                TRACKLIST, F_BOUNCE);
              settings.bounce_with_parents = true;
            }
          char * exports_dir =
            project_get_path (
              PROJECT, PROJECT_PATH_EXPORTS, false);
          settings.file_uri =
            g_build_filename (
              exports_dir, filename, NULL);
          ret = exporter_export (&settings);
          g_assert_false (AUDIO_ENGINE->exporting);
          g_assert_cmpint (ret, ==, 0);

          z_chromaprint_check_fingerprint_similarity (
            filepath, settings.file_uri, 83, 6);

          io_remove (settings.file_uri);
          g_free (filename);
          export_settings_free_members (&settings);

          g_assert_false (TRANSPORT_IS_ROLLING);
          g_assert_cmpint (
            TRANSPORT->playhead_pos.frames, ==, 0);
        }
    }

  g_free (filepath);

  test_helper_zrythm_cleanup ();
}

static void
bounce_region (
  bool with_bpm_automation)
{
#ifdef HAVE_HELM
  test_helper_zrythm_init ();

  Position pos, end_pos;
  position_set_to_bar (&pos, 2);
  position_set_to_bar (&end_pos, 4);

  if (with_bpm_automation)
    {
      /* create bpm automation */
      AutomationTrack * at =
        automation_track_find_from_port (
          P_TEMPO_TRACK->bpm_port, P_TEMPO_TRACK, false);
      ZRegion * r =
        automation_region_new (
          &pos, &end_pos,
          track_get_name_hash (P_TEMPO_TRACK),
          at->index, 0);
      track_add_region (
        P_TEMPO_TRACK, r, at, 0, 1, 0);
      position_set_to_bar (&pos, 1);
      AutomationPoint * ap =
        automation_point_new_float (
          168.434006f, 0.361445993f, &pos);
      automation_region_add_ap (
        r, ap, F_NO_PUBLISH_EVENTS);
      position_set_to_bar (&pos, 2);
      ap =
        automation_point_new_float (
          297.348999f, 0.791164994f, &pos);
      automation_region_add_ap (
        r, ap, F_NO_PUBLISH_EVENTS);
    }

  /* create the plugin track */
  test_plugin_manager_create_tracks_from_plugin (
    HELM_BUNDLE, HELM_URI, true, false, 1);
  Track * track =
    TRACKLIST->tracks[TRACKLIST->num_tracks - 1];
  track_select (
    track, F_SELECT, F_EXCLUSIVE,
    F_NO_PUBLISH_EVENTS);

  /* create midi region */
  char * midi_file =
    g_build_filename (
      MIDILIB_TEST_MIDI_FILES_PATH,
      "M71.MID", NULL);
  int lane_pos = 0;
  int idx_in_lane = 0;
  ZRegion * region =
    midi_region_new_from_midi_file (
      &pos, midi_file, track_get_name_hash (track),
      lane_pos, idx_in_lane, 0);
  track_add_region (
    track, region, NULL, lane_pos,
    F_GEN_NAME, F_NO_PUBLISH_EVENTS);
  arranger_object_select (
    (ArrangerObject *) region,
    F_SELECT,
    F_NO_APPEND, F_NO_PUBLISH_EVENTS);
  arranger_selections_action_perform_create (
    TL_SELECTIONS, NULL);

  /* bounce it */
  ExportSettings settings;
  memset (&settings, 0, sizeof (ExportSettings));
  settings.mode = EXPORT_MODE_REGIONS;
  export_settings_set_bounce_defaults (
    &settings, NULL, region->name);
  timeline_selections_mark_for_bounce (
    TL_SELECTIONS, settings.bounce_with_parents);
  position_add_ms (
    &settings.custom_end, 4000);

  /* start exporting in a new thread */
  GThread * thread =
    g_thread_new (
      "bounce_thread",
      (GThreadFunc) exporter_generic_export_thread,
      &settings);

  print_progress_and_sleep (
    &settings.progress_info);

  g_thread_join (thread);

  if (!with_bpm_automation)
    {
      char * filepath =
        g_build_filename (
          TESTS_SRCDIR,
          "test_mixdown_midi_routed_to_instrument_track.ogg",
          NULL);
      z_chromaprint_check_fingerprint_similarity (
        filepath, settings.file_uri, 97, 34);
      g_free (filepath);
    }

  test_helper_zrythm_cleanup ();
#endif
}

static void
test_bounce_region ()
{
  bounce_region (false);
}

static void
test_bounce_with_bpm_automation ()
{
  bounce_region (true);
}

/**
 * Export the audio mixdown when a MIDI track with
 * data is routed to an instrument track.
 */
static void
test_mixdown_midi_routed_to_instrument_track ()
{
#ifdef HAVE_HELM
  test_helper_zrythm_init ();

  /* create the instrument track */
  test_plugin_manager_create_tracks_from_plugin (
    HELM_BUNDLE, HELM_URI, true, false, 1);
  Track * ins_track =
    TRACKLIST->tracks[TRACKLIST->num_tracks - 1];
  track_select (
    ins_track, F_SELECT, F_EXCLUSIVE,
    F_NO_PUBLISH_EVENTS);

  char * midi_file =
    g_build_filename (
      MIDILIB_TEST_MIDI_FILES_PATH,
      "M71.MID", NULL);

  /* create the MIDI track from a MIDI file */
  SupportedFile * file =
    supported_file_new_from_path (midi_file);
  Track * midi_track =
    track_create_with_action (
      TRACK_TYPE_MIDI, NULL, file, PLAYHEAD,
      TRACKLIST->num_tracks, 1, NULL);
  track_select (
    midi_track, F_SELECT, F_EXCLUSIVE,
    F_NO_PUBLISH_EVENTS);

  /* route the MIDI track to the instrument track */
  tracklist_selections_action_perform_set_direct_out (
    TRACKLIST_SELECTIONS,
    PORT_CONNECTIONS_MGR, ins_track, NULL);

  /* bounce it */
  ExportSettings settings;
  memset (&settings, 0, sizeof (ExportSettings));
  settings.mode = EXPORT_MODE_FULL;
  export_settings_set_bounce_defaults (
    &settings, NULL, __func__);
  settings.time_range = TIME_RANGE_LOOP;

  /* start exporting in a new thread */
  GThread * thread =
    g_thread_new (
      "bounce_thread",
      (GThreadFunc) exporter_generic_export_thread,
      &settings);

  print_progress_and_sleep (
    &settings.progress_info);

  g_thread_join (thread);

  char * filepath =
    g_build_filename (
      TESTS_SRCDIR,
      "test_mixdown_midi_routed_to_instrument_track.ogg",
      NULL);
  z_chromaprint_check_fingerprint_similarity (
    filepath, settings.file_uri, 97, 34);
  g_free (filepath);

  test_helper_zrythm_cleanup ();
#endif
}

static void
test_bounce_region_with_first_note (void)
{
#ifdef HAVE_HELM
  test_helper_zrythm_init ();

  Position pos, end_pos;
  position_set_to_bar (&pos, 2);
  position_set_to_bar (&end_pos, 4);

  /* create the plugin track */
  test_plugin_manager_create_tracks_from_plugin (
    HELM_BUNDLE, HELM_URI, true, false, 1);
  Track * track =
    TRACKLIST->tracks[TRACKLIST->num_tracks - 1];
  track_select (
    track, F_SELECT, F_EXCLUSIVE,
    F_NO_PUBLISH_EVENTS);

  /* create midi region */
  char * midi_file =
    g_build_filename (
      MIDILIB_TEST_MIDI_FILES_PATH,
      "M1.MID", NULL);
  int lane_pos = 0;
  int idx_in_lane = 0;
  ZRegion * region =
    midi_region_new_from_midi_file (
      &pos, midi_file, track_get_name_hash (track),
      lane_pos, idx_in_lane, 0);
  ArrangerObject * r_obj = (ArrangerObject *) region;
  track_add_region (
    track, region, NULL, lane_pos,
    F_GEN_NAME, F_NO_PUBLISH_EVENTS);
  arranger_object_select (
    r_obj, F_SELECT,
    F_NO_APPEND, F_NO_PUBLISH_EVENTS);
  arranger_selections_action_perform_create (
    TL_SELECTIONS, NULL);

  position_init (&pos);
  position_add_beats (&pos, 3);
  arranger_object_loop_start_pos_setter (
    r_obj, &pos);
  arranger_object_clip_start_pos_setter (
    r_obj, &pos);

  for (int i = region->num_midi_notes - 1; i >= 1;
       i--)
    {
      MidiNote * mn = region->midi_notes[i];
      midi_region_remove_midi_note (
        region, mn, F_FREE, F_NO_PUBLISH_EVENTS);
    }
  g_assert_cmpint (
    region->midi_notes[0]->base.pos.frames, ==,
    region->base.loop_start_pos.frames);

  /* bounce it */
  ExportSettings settings;
  memset (&settings, 0, sizeof (ExportSettings));
  settings.mode = EXPORT_MODE_REGIONS;
  export_settings_set_bounce_defaults (
    &settings, NULL, region->name);
  timeline_selections_mark_for_bounce (
    TL_SELECTIONS, settings.bounce_with_parents);
  position_add_ms (
    &settings.custom_end, 4000);

  /* start exporting in a new thread */
  GThread * thread =
    g_thread_new (
      "bounce_thread",
      (GThreadFunc) exporter_generic_export_thread,
      &settings);

  print_progress_and_sleep (
    &settings.progress_info);

  g_thread_join (thread);

  /* assert non silent */
  AudioClip * clip =
    audio_clip_new_from_file (settings.file_uri);
  bool has_audio = false;
  for (long i = 0; i < clip->num_frames; i++)
    {
      for (channels_t j = 0; j < clip->channels; j++)
        {
          if (fabsf (clip->ch_frames[j][i]) > 1e-10f)
            {
              has_audio = true;
              break;
            }
        }
    }
  g_assert_true (has_audio);
  audio_clip_free (clip);

  test_helper_zrythm_cleanup ();
#endif
}

static void
_test_bounce_midi_track_routed_to_instrument_track (
  BounceStep bounce_step,
  bool       with_parents)
{
#ifdef HAVE_HELM
  test_helper_zrythm_init ();

  /* create the instrument track */
  test_plugin_manager_create_tracks_from_plugin (
    HELM_BUNDLE, HELM_URI, true, false, 1);
  Track * ins_track =
    TRACKLIST->tracks[TRACKLIST->num_tracks - 1];
  track_select (
    ins_track, F_SELECT, F_EXCLUSIVE,
    F_NO_PUBLISH_EVENTS);

  char * midi_file =
    g_build_filename (
      MIDILIB_TEST_MIDI_FILES_PATH,
      "M71.MID", NULL);

  /* create the MIDI track from a MIDI file */
  SupportedFile * file =
    supported_file_new_from_path (midi_file);
  Track * midi_track =
    track_create_with_action (
      TRACK_TYPE_MIDI, NULL, file, PLAYHEAD,
      TRACKLIST->num_tracks, 1, NULL);
  track_select (
    midi_track, F_SELECT, F_EXCLUSIVE,
    F_NO_PUBLISH_EVENTS);

  /* route the MIDI track to the instrument track */
  tracklist_selections_action_perform_set_direct_out (
    TRACKLIST_SELECTIONS,
    PORT_CONNECTIONS_MGR, ins_track, NULL);

  /* bounce it */
  ExportSettings settings;
  memset (&settings, 0, sizeof (ExportSettings));
  settings.mode = EXPORT_MODE_TRACKS;
  export_settings_set_bounce_defaults (
    &settings, NULL, __func__);
  settings.time_range = TIME_RANGE_LOOP;
  settings.bounce_with_parents = with_parents;
  settings.bounce_step = bounce_step;

  /* mark the track for bounce */
  tracklist_selections_mark_for_bounce (
    TRACKLIST_SELECTIONS,
    settings.bounce_with_parents, F_NO_MARK_MASTER);

  /* start exporting in a new thread */
  GThread * thread =
    g_thread_new (
      "bounce_thread",
      (GThreadFunc) exporter_generic_export_thread,
      &settings);

  print_progress_and_sleep (
    &settings.progress_info);

  g_thread_join (thread);

  if (with_parents)
    {
      char * filepath =
        g_build_filename (
          TESTS_SRCDIR,
          "test_mixdown_midi_routed_to_instrument_track.ogg",
          NULL);
      z_chromaprint_check_fingerprint_similarity (
        filepath, settings.file_uri, 97, 34);
      g_free (filepath);
    }
  else
    {
      /* assume silence */
      g_assert_true (
        audio_file_is_silent (settings.file_uri));
    }

  test_helper_zrythm_cleanup ();
#endif
}

static void
test_bounce_midi_track_routed_to_instrument_track (void)
{
  _test_bounce_midi_track_routed_to_instrument_track (
    BOUNCE_STEP_POST_FADER, true);
  _test_bounce_midi_track_routed_to_instrument_track (
    BOUNCE_STEP_POST_FADER, false);
}

static void
_test_bounce_instrument_track (
  BounceStep bounce_step,
  bool       with_parents)
{
#if defined (HAVE_HELM) && defined (HAVE_MVERB)
  test_helper_zrythm_init ();

  /* create the instrument track */
  test_plugin_manager_create_tracks_from_plugin (
    HELM_BUNDLE, HELM_URI, true, false, 1);
  Track * ins_track =
    TRACKLIST->tracks[TRACKLIST->num_tracks - 1];
  track_select (
    ins_track, F_SELECT, F_EXCLUSIVE,
    F_NO_PUBLISH_EVENTS);

  /* create a MIDI region on the instrument track */
  char * midi_file =
    g_build_filename (
      MIDILIB_TEST_MIDI_FILES_PATH,
      "M71.MID", NULL);
  ZRegion * r =
    midi_region_new_from_midi_file (
      PLAYHEAD, midi_file,
      track_get_name_hash (ins_track),
      0, 0, 0);
  g_free (midi_file);
  track_add_region (
    ins_track, r, NULL, 0, F_GEN_NAME,
    F_NO_PUBLISH_EVENTS);
  arranger_object_select (
    (ArrangerObject *) r, F_SELECT, F_NO_APPEND,
    F_NO_PUBLISH_EVENTS);
  arranger_selections_action_perform_create (
    TL_SELECTIONS, NULL);

  /* add MVerb insert */
  PluginSetting * setting =
    test_plugin_manager_get_plugin_setting (
      MVERB_BUNDLE, MVERB_URI, false);
  mixer_selections_action_perform_create (
    PLUGIN_SLOT_INSERT,
    track_get_name_hash (ins_track), 0,
    setting, 1, NULL);

  /* adjust fader */
  Fader * fader = track_get_fader (ins_track, true);
  Port * port = fader->amp;
  port_action_perform (
    PORT_ACTION_SET_CONTROL_VAL, &port->id, 0.5f,
    false, NULL);
  g_assert_cmpfloat_with_epsilon (
    port->control, 0.5f, 0.00001f);

  /* bounce it */
  ExportSettings settings;
  memset (&settings, 0, sizeof (ExportSettings));
  settings.mode = EXPORT_MODE_TRACKS;
  export_settings_set_bounce_defaults (
    &settings, NULL, __func__);
  settings.time_range = TIME_RANGE_LOOP;
  settings.bounce_with_parents = with_parents;
  settings.bounce_step = bounce_step;

  /* mark the track for bounce */
  tracklist_selections_mark_for_bounce (
    TRACKLIST_SELECTIONS,
    settings.bounce_with_parents, F_NO_MARK_MASTER);

  /* start exporting in a new thread */
  GThread * thread =
    g_thread_new (
      "bounce_thread",
      exporter_generic_export_thread,
      &settings);

  print_progress_and_sleep (
    &settings.progress_info);

  g_thread_join (thread);

#define CHECK_SAME_AS_FILE(dirname,x,match_rate) \
  char * filepath = \
    g_build_filename (dirname, x, NULL); \
  z_chromaprint_check_fingerprint_similarity ( \
    filepath, settings.file_uri, match_rate, 34); \
  g_free (filepath)

  if (with_parents ||
        bounce_step == BOUNCE_STEP_POST_FADER)
    {
      CHECK_SAME_AS_FILE (
        TESTS_BUILDDIR,
        "test_mixdown_midi_routed_to_instrument_track_w_reverb_half_gain.ogg", 94);
    }
  else if (bounce_step ==
             BOUNCE_STEP_BEFORE_INSERTS)
    {
      CHECK_SAME_AS_FILE (
        TESTS_SRCDIR,
        "test_mixdown_midi_routed_to_instrument_track.ogg", 97);
    }
  else if (bounce_step ==
             BOUNCE_STEP_PRE_FADER)
    {
      CHECK_SAME_AS_FILE (
        TESTS_BUILDDIR,
        "test_mixdown_midi_routed_to_instrument_track_w_reverb.ogg", 88);
    }

#undef CHECK_SAME_AS_FILE

  /* --- check bounce song with offset --- */

  /* move playhead to bar 3 */
  transport_set_playhead_to_bar (TRANSPORT, 3);

  /* move start marker and region to bar 2 */
  Marker * start_marker =
    marker_track_get_start_marker (P_MARKER_TRACK);
  arranger_object_select (
    (ArrangerObject *) start_marker, F_SELECT,
    F_NO_APPEND, F_NO_PUBLISH_EVENTS);
  arranger_object_select (
    (ArrangerObject *) r, F_SELECT, F_APPEND,
    F_NO_PUBLISH_EVENTS);
  arranger_selections_action_perform_move_timeline (
    TL_SELECTIONS,
    TRANSPORT->ticks_per_bar, 0, 0,
    F_NOT_ALREADY_MOVED, NULL);

  /* move end marker to bar 6 */
  Marker * end_marker =
    marker_track_get_end_marker (P_MARKER_TRACK);
  ArrangerObject * end_marker_obj =
    (ArrangerObject *) end_marker;
  arranger_object_select (
    end_marker_obj, F_SELECT,
    F_NO_APPEND, F_NO_PUBLISH_EVENTS);
  arranger_selections_action_perform_move_timeline (
    TL_SELECTIONS,
    TRANSPORT->ticks_per_bar * 6 -
      end_marker_obj->pos.ticks,
    0, 0, F_NOT_ALREADY_MOVED, NULL);

  /* export again */
  memset (&settings, 0, sizeof (ExportSettings));
  settings.mode = EXPORT_MODE_TRACKS;
  export_settings_set_bounce_defaults (
    &settings, NULL, __func__);
  settings.time_range = TIME_RANGE_SONG;
  settings.bounce_with_parents = with_parents;
  settings.bounce_step = bounce_step;

  /* mark the track for bounce */
  tracklist_selections_mark_for_bounce (
    TRACKLIST_SELECTIONS,
    settings.bounce_with_parents, F_NO_MARK_MASTER);

  /* start exporting in a new thread */
  thread =
    g_thread_new (
      "bounce_thread",
      (GThreadFunc) exporter_generic_export_thread,
      &settings);

  print_progress_and_sleep (
    &settings.progress_info);

  g_thread_join (thread);

  /* create audio track with bounced material */
  ArrangerObject * start_marker_obj =
    (ArrangerObject *) start_marker;
  exporter_create_audio_track_after_bounce (
    &settings, &start_marker_obj->pos);

  /* assert exported material starts at start
   * marker and ends at end marker */
  Track * audio_track =
    TRACKLIST->tracks[TRACKLIST->num_tracks - 1];
  ZRegion * bounced_r =
    audio_track->lanes[0]->regions[0];
  ArrangerObject * bounce_r_obj =
    (ArrangerObject *) bounced_r;
  g_assert_cmppos (
    &start_marker_obj->pos, &bounce_r_obj->pos);
  g_assert_cmppos (
    &end_marker_obj->pos, &bounce_r_obj->end_pos);

  test_helper_zrythm_cleanup ();
#endif
}

static void
test_bounce_instrument_track (void)
{
  _test_bounce_instrument_track (
    BOUNCE_STEP_POST_FADER, true);
  _test_bounce_instrument_track (
    BOUNCE_STEP_BEFORE_INSERTS, false);
  _test_bounce_instrument_track (
    BOUNCE_STEP_PRE_FADER, false);
  _test_bounce_instrument_track (
    BOUNCE_STEP_POST_FADER, false);
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

#define TEST_PREFIX "/audio/exporter/"

  g_test_add_func (
    TEST_PREFIX "test export wav",
    (GTestFunc) test_export_wav);
  g_test_add_func (
    TEST_PREFIX "test bounce instrument track",
    (GTestFunc) test_bounce_instrument_track);
  g_test_add_func (
    TEST_PREFIX "test bounce midi track routed to instrument track",
    (GTestFunc) test_bounce_midi_track_routed_to_instrument_track);
  g_test_add_func (
    TEST_PREFIX "test bounce region with first note",
    (GTestFunc) test_bounce_region_with_first_note);
  g_test_add_func (
    TEST_PREFIX "test bounce region",
    (GTestFunc) test_bounce_region);
  g_test_add_func (
    TEST_PREFIX "test bounce with bpm automation",
    (GTestFunc) test_bounce_with_bpm_automation);
  g_test_add_func (
    TEST_PREFIX "test mixdown midi routed to instrument track",
    (GTestFunc) test_mixdown_midi_routed_to_instrument_track);

  return g_test_run ();
}
