/*
 * Copyright (C) 2019-2021 Alexandros Theodotou <alex at zrythm dot org>
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

#include <stdlib.h>

#include "audio/audio_region.h"
#include "audio/track.h"
#include "audio/track_lane.h"
#include "audio/tracklist.h"
#include "gui/backend/event.h"
#include "gui/backend/event_manager.h"
#include "gui/widgets/arranger.h"
#include "midilib/src/midifile.h"
#include "midilib/src/midiinfo.h"
#include "project.h"
#include "utils/arrays.h"
#include "utils/error.h"
#include "utils/flags.h"
#include "utils/mem.h"
#include "utils/objects.h"
#include "zrythm_app.h"

#include <glib/gi18n.h>

/**
 * Inits the TrackLane after a project was loaded.
 */
void
track_lane_init_loaded (
  TrackLane * self,
  Track *     track)
{
  self->track = track;
  self->regions_size =
    (size_t) self->num_regions;
  int i;
  ZRegion * region;
  for (i = 0; i < self->num_regions; i++)
    {
      region = self->regions[i];
      region->magic = REGION_MAGIC;
      ArrangerObject * r_obj =
        (ArrangerObject *) region;
      region_set_lane (region, self);
      arranger_object_init_loaded (r_obj);
    }
}

/**
 * Creates a new TrackLane at the given pos in the
 * given Track.
 *
 * @param track The Track to create the TrackLane for.
 * @param pos The position (index) in the Track that
 *   this lane will be placed in.
 */
TrackLane *
track_lane_new (
  Track * track,
  int     pos)
{
  TrackLane * self = object_new (TrackLane);
  self->schema_version = TRACK_LANE_SCHEMA_VERSION;
  self->pos = pos;
  self->track = track;

  self->name =
    g_strdup_printf (_("Lane %d"), pos + 1);

  self->regions_size = 1;
  self->regions =
    object_new_n (self->regions_size, ZRegion *);

  self->height = TRACK_DEF_HEIGHT;

  return self;
}

/**
 * Rename the lane.
 *
 * @param with_action Whether to make this an
 *   undoable action.
 */
void
track_lane_rename (
  TrackLane *  self,
  const char * new_name,
  bool         with_action)
{
  if (with_action)
    {
      GError * err = NULL;
      bool ret =
        tracklist_selections_action_perform_edit_rename_lane (
          self, new_name, &err);
      if (!ret)
        {
          HANDLE_ERROR (
            err, "%s", _("Failed to rename lane"));
        }
      EVENTS_PUSH (
        ET_TRACK_LANES_VISIBILITY_CHANGED, NULL);
    }
  else
    {
      char * prev_name = self->name;
      self->name = g_strdup (new_name);
      g_free (prev_name);
    }
}

/**
 * Wrapper over track_lane_rename().
 */
void
track_lane_rename_with_action (
  TrackLane *  self,
  const char * new_name)
{
  track_lane_rename (self, new_name, true);
}

const char *
track_lane_get_name (
  TrackLane * self)
{
  return self->name;
}

/**
 * Updates the positions in each child recursively.
 *
 * @param from_ticks Whether to update the
 *   positions based on ticks (true) or frames
 *   (false).
 */
void
track_lane_update_positions (
  TrackLane * self,
  bool        from_ticks)
{
  for (int i = 0; i < self->num_regions; i++)
    {
      ArrangerObject * r_obj =
        (ArrangerObject *) self->regions[i];

      /* project not ready yet */
      if (!PROJECT || !AUDIO_ENGINE->pre_setup)
        continue;

      g_return_if_fail (
        IS_REGION_AND_NONNULL (r_obj));
      arranger_object_update_positions (
        r_obj, from_ticks);
    }
}

/**
 * Adds a ZRegion to the given TrackLane.
 */
void
track_lane_add_region (
  TrackLane * self,
  ZRegion *   region)
{
  track_lane_insert_region (
    self, region, self->num_regions);
}

/**
 * Inserts a ZRegion to the given TrackLane at the
 * given index.
 */
void
track_lane_insert_region (
  TrackLane * self,
  ZRegion *   region,
  int         idx)
{
  g_return_if_fail (
    self && IS_REGION (region) && idx >= 0 &&
    (region->id.type == REGION_TYPE_AUDIO ||
     region->id.type == REGION_TYPE_MIDI));

  region_set_lane (region, self);

  array_double_size_if_full (
    self->regions, self->num_regions,
    self->regions_size, ZRegion *);
  for (int i = self->num_regions; i > idx; i--)
    {
      self->regions[i] = self->regions[i - 1];
      self->regions[i]->id.idx = i;
      region_update_identifier (
        self->regions[i]);
    }
  self->num_regions++;
  self->regions[idx] = region;
  region->id.lane_pos = self->pos;
  region->id.idx = idx;
  region_update_identifier (region);

  if (region->id.type == REGION_TYPE_AUDIO)
    {
      AudioClip * clip =
        audio_region_get_clip (region);
      g_return_if_fail (clip);
    }
}

/**
 * Sets the new track name hash to all the lane's
 * objects recursively.
 */
void
track_lane_update_track_name_hash (
  TrackLane *   self)
{
  Track * track = self->track;
  g_return_if_fail (IS_TRACK_AND_NONNULL (track));

  for (int i = 0; i < self->num_regions; i++)
    {
      ZRegion * region = self->regions[i];
      region->id.track_name_hash =
        track_get_name_hash (track);
      region->id.lane_pos = self->pos;
      region_update_identifier (region);
    }
}

/**
 * Clones the TrackLane.
 *
 * @param track New owner track, if any.
 */
TrackLane *
track_lane_clone (
  const TrackLane * src,
  Track *           track)
{
  TrackLane * self = object_new (TrackLane);
  self->schema_version = TRACK_LANE_SCHEMA_VERSION;
  self->track = track;

  self->name =
    g_strdup (src->name);
  self->regions_size =
    (size_t) src->num_regions;
  self->regions =
    object_new_n (self->regions_size, ZRegion *);
  self->height =
    src->height;
  self->pos = src->pos;
  self->mute = src->mute;
  self->solo = src->solo;
  self->midi_ch = src->midi_ch;

  ZRegion * region, * new_region;
  self->num_regions = src->num_regions;
  self->regions =
    g_realloc (
      self->regions,
      sizeof (ZRegion *) *
        (size_t) src->num_regions);
  for (int i = 0; i < src->num_regions; i++)
    {
      /* clone region */
      region = src->regions[i];
      new_region =
        (ZRegion *)
        arranger_object_clone (
          (ArrangerObject *) region);

      self->regions[i] = new_region;
      region_set_lane (new_region, self);

      region_gen_name (
        new_region, region->name, NULL, NULL);
    }

  return self;
}

/**
 * Unselects all arranger objects.
 *
 * TODO replace with "select_all" and boolean param.
 */
void
track_lane_unselect_all (
  TrackLane * self)
{
  Track * track = track_lane_get_track (self);
  g_return_if_fail (track);
  for (int i = 0; i < self->num_regions; i++)
    {
      ZRegion * region = self->regions[i];
      arranger_object_select (
        (ArrangerObject *) region, false, false,
        F_NO_PUBLISH_EVENTS);
    }
}

/**
 * Removes all objects recursively from the track
 * lane.
 */
void
track_lane_clear (
  TrackLane * self)
{
  Track * track = track_lane_get_track (self);
  g_return_if_fail (IS_TRACK_AND_NONNULL (track));

  g_message (
    "clearing track lane %d (%p) for track '%s' | "
    "num regions %d",
    self->pos, self, track->name, self->num_regions);

  for (int i = self->num_regions - 1; i >= 0; i--)
    {
      ZRegion * region = self->regions[i];
      g_return_if_fail (
        IS_REGION (region)
        &&
        region->id.track_name_hash ==
          track_get_name_hash (track)
        && region->id.lane_pos == self->pos);
      track_remove_region (
        track, region, 0, 1);
    }

  g_return_if_fail (self->num_regions == 0);
}

/**
 * Removes but does not free the region.
 */
void
track_lane_remove_region (
  TrackLane * self,
  ZRegion *   region)
{
  g_return_if_fail (IS_REGION (region));

  if (track_lane_is_in_active_project (self)
      && !track_lane_is_auditioner (self))
    {
      /* if clip editor region index is greater
       * than this index, decrement it */
      ZRegion * clip_editor_r =
        clip_editor_get_region (CLIP_EDITOR);
      if (clip_editor_r
          &&
          clip_editor_r->id.track_name_hash ==
            region->id.track_name_hash
          &&
          clip_editor_r->id.lane_pos ==
            region->id.lane_pos
          &&
          clip_editor_r->id.idx > region->id.idx)
        {
          CLIP_EDITOR->region_id.idx--;
        }
    }

  bool deleted = false;
  array_delete_confirm (
    self->regions, self->num_regions, region,
    deleted);
  g_return_if_fail (deleted);

  for (int i = region->id.idx; i < self->num_regions;
       i++)
    {
      ZRegion * r = self->regions[i];
      r->id.idx = i;
      region_update_identifier (r);
    }
}

Tracklist *
track_lane_get_tracklist (
  TrackLane * self)
{
  if (track_lane_is_auditioner (self))
    return SAMPLE_PROCESSOR->tracklist;
  else
    return TRACKLIST;
}

Track *
track_lane_get_track (
  TrackLane * self)
{
  g_return_val_if_fail (self->track, NULL);
  return self->track;
}

/**
 * Writes the lane to the given MIDI file.
 */
void
track_lane_write_to_midi_file (
  TrackLane * self,
  MIDI_FILE * mf)
{
  /* All data is written out to _tracks_ not
   * channels. We therefore
  ** set the current channel before writing
  data out. Channel assignments
  ** can change any number of times during the
  file, and affect all
  ** tracks messages until it is changed. */
  midiFileSetTracksDefaultChannel (
    mf, self->track->pos, MIDI_CHANNEL_1);

  Track * track = track_lane_get_track (self);
  g_return_if_fail (track);

  /* add track name */
  midiTrackAddText (
    mf, self->track->pos, textTrackName,
    track->name);

  ZRegion * region;
  for (int i = 0; i < self->num_regions; i++)
    {
      region = self->regions[i];
      midi_region_write_to_midi_file (
        region, mf, 1, true, true);
    }
}

/**
 * Frees the TrackLane.
 */
void
track_lane_free (
  TrackLane * self)
{
  g_free_and_null (self->name);

  for (int i = 0; i < self->num_regions; i++)
    {
      arranger_object_free (
        (ArrangerObject *) self->regions[i]);
    }

  object_zero_and_free_if_nonnull (self->regions);

  object_zero_and_free (self);
}
