/*
 * Copyright (C) 2019 Alexandros Theodotou
 *
 * This file is part of Zrythm
 *
 * Zrythm is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Zrythm is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Zrythm.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "actions/create_midi_arranger_selections_action.h"
#include "actions/create_timeline_selections_action.h"
#include "actions/delete_midi_arranger_selections_action.h"
#include "actions/delete_timeline_selections_action.h"
#include "actions/duplicate_midi_arranger_selections_action.h"
#include "actions/duplicate_timeline_selections_action.h"
#include "actions/edit_channel_action.h"
#include "actions/edit_track_action.h"
#include "actions/move_plugin_action.h"
#include "actions/move_midi_arranger_selections_action.h"
#include "actions/move_timeline_selections_action.h"
#include "actions/undoable_action.h"

#include <glib.h>

/**
 * Performs the action.
 *
 * Note: only to be called by undo manager.
 */
void
undoable_action_do (UndoableAction * self)
{
  /* uppercase, camel case, snake case */
#define DO_ACTION(uc,sc,cc) \
  case UNDOABLE_ACTION_TYPE_##uc: \
    sc##_action_do ((cc##Action *) self); \
    break;

  switch (self->type)
    {
    /*DO_ACTION (CREATE_CHANNEL,*/
               /*create_channel,*/
               /*CreateChannel);*/
    /*DO_ACTION (EDIT_CHANNEL,*/
               /*edit_channel,*/
               /*EditChannel);*/
    /*DO_ACTION (DELETE_CHANNEL,*/
               /*delete_channel,*/
               /*DeleteChannel);*/
    /*DO_ACTION (MOVE_CHANNEL,*/
               /*move_channel,*/
               /*MoveChannel);*/
    /*DO_ACTION (EDIT_TRACK,*/
               /*edit_track,*/
               /*EditTrack);*/
    DO_ACTION (MOVE_PLUGIN,
               move_plugin,
               MovePlugin);
    /*DO_ACTION (COPY_PLUGIN,*/
               /*copy_plugin,*/
               /*CopyPlugin);*/
    /*DO_ACTION (DELETE_PLUGIN,*/
               /*delete_plugin,*/
               /*DeletePlugin);*/
    DO_ACTION (CREATE_TL_SELECTIONS,
               create_timeline_selections,
               CreateTimelineSelections);
    DO_ACTION (DELETE_TL_SELECTIONS,
               delete_timeline_selections,
               DeleteTimelineSelections);
    DO_ACTION (MOVE_TL_SELECTIONS,
               move_timeline_selections,
               MoveTimelineSelections);
    DO_ACTION (DUPLICATE_TL_SELECTIONS,
               duplicate_timeline_selections,
               DuplicateTimelineSelections);
    DO_ACTION (CREATE_MA_SELECTIONS,
               create_midi_arranger_selections,
               CreateMidiArrangerSelections);
    DO_ACTION (DUPLICATE_MA_SELECTIONS,
               duplicate_midi_arranger_selections,
               DuplicateMidiArrangerSelections);
    DO_ACTION (DELETE_MA_SELECTIONS,
               delete_midi_arranger_selections,
               DeleteMidiArrangerSelections);
    DO_ACTION (MOVE_MA_SELECTIONS,
               move_midi_arranger_selections,
               MoveMidiArrangerSelections);
    default:
      g_warn_if_reached ();
      break;
    }

#undef DO_ACTION
}

/**
 * Undoes the action.
 *
 * Note: only to be called by undo manager.
 */
void
undoable_action_undo (UndoableAction * self)
{
/* uppercase, camel case, snake case */
#define UNDO_ACTION(uc,sc,cc) \
  case UNDOABLE_ACTION_TYPE_##uc: \
    sc##_action_undo ((cc##Action *) self); \
    break;

  switch (self->type)
    {
    /*UNDO_ACTION (CREATE_CHANNEL,*/
               /*create_channel,*/
               /*CreateChannel);*/
    /*UNDO_ACTION (EDIT_CHANNEL,*/
               /*edit_channel,*/
               /*EditChannel);*/
    /*UNDO_ACTION (DELETE_CHANNEL,*/
               /*delete_channel,*/
               /*DeleteChannel);*/
    /*UNDO_ACTION (MOVE_CHANNEL,*/
               /*move_channel,*/
               /*MoveChannel);*/
    /*UNDO_ACTION (EDIT_TRACK,*/
               /*edit_track,*/
               /*EditTrack);*/
    UNDO_ACTION (MOVE_PLUGIN,
               move_plugin,
               MovePlugin);
    /*UNDO_ACTION (COPY_PLUGIN,*/
               /*copy_plugin,*/
               /*CopyPlugin);*/
    /*UNDO_ACTION (DELETE_PLUGIN,*/
               /*delete_plugin,*/
               /*DeletePlugin);*/
    UNDO_ACTION (CREATE_TL_SELECTIONS,
               create_timeline_selections,
               CreateTimelineSelections);
    UNDO_ACTION (DELETE_TL_SELECTIONS,
               delete_timeline_selections,
               DeleteTimelineSelections);
    UNDO_ACTION (MOVE_TL_SELECTIONS,
               move_timeline_selections,
               MoveTimelineSelections);
    UNDO_ACTION (DUPLICATE_TL_SELECTIONS,
               duplicate_timeline_selections,
               DuplicateTimelineSelections);
    UNDO_ACTION (CREATE_MA_SELECTIONS,
               create_midi_arranger_selections,
               CreateMidiArrangerSelections);
    UNDO_ACTION (DUPLICATE_MA_SELECTIONS,
               duplicate_midi_arranger_selections,
               DuplicateMidiArrangerSelections);
    UNDO_ACTION (DELETE_MA_SELECTIONS,
               delete_midi_arranger_selections,
               DeleteMidiArrangerSelections);
    UNDO_ACTION (MOVE_MA_SELECTIONS,
               move_midi_arranger_selections,
               MoveMidiArrangerSelections);
    default:
      g_warn_if_reached ();
      break;
    }

#undef UNDO_ACTION
}

void
undoable_action_free (UndoableAction * self)
{
/* uppercase, camel case, snake case */
#define FREE_ACTION(uc,sc,cc) \
  case UNDOABLE_ACTION_TYPE_##uc: \
    sc##_action_free ((cc##Action *) self); \
    break;

  switch (self->type)
    {
    /*FREE_ACTION (CREATE_CHANNEL,*/
               /*create_channel,*/
               /*CreateChannel);*/
    /*FREE_ACTION (EDIT_CHANNEL,*/
               /*edit_channel,*/
               /*EditChannel);*/
    /*FREE_ACTION (DELETE_CHANNEL,*/
               /*delete_channel,*/
               /*DeleteChannel);*/
    /*FREE_ACTION (MOVE_CHANNEL,*/
               /*move_channel,*/
               /*MoveChannel);*/
    /*FREE_ACTION (EDIT_TRACK,*/
               /*edit_track,*/
               /*EditTrack);*/
    FREE_ACTION (MOVE_PLUGIN,
               move_plugin,
               MovePlugin);
    /*FREE_ACTION (COPY_PLUGIN,*/
               /*copy_plugin,*/
               /*CopyPlugin);*/
    /*FREE_ACTION (DELETE_PLUGIN,*/
               /*delete_plugin,*/
               /*DeletePlugin);*/
    FREE_ACTION (CREATE_TL_SELECTIONS,
               create_timeline_selections,
               CreateTimelineSelections);
    FREE_ACTION (DELETE_TL_SELECTIONS,
               delete_timeline_selections,
               DeleteTimelineSelections);
    FREE_ACTION (MOVE_TL_SELECTIONS,
               move_timeline_selections,
               MoveTimelineSelections);
    FREE_ACTION (DUPLICATE_TL_SELECTIONS,
               duplicate_timeline_selections,
               DuplicateTimelineSelections);
    FREE_ACTION (CREATE_MA_SELECTIONS,
               create_midi_arranger_selections,
               CreateMidiArrangerSelections);
    FREE_ACTION (DUPLICATE_MA_SELECTIONS,
               duplicate_midi_arranger_selections,
               DuplicateMidiArrangerSelections);
    FREE_ACTION (DELETE_MA_SELECTIONS,
               delete_midi_arranger_selections,
               DeleteMidiArrangerSelections);
    FREE_ACTION (MOVE_MA_SELECTIONS,
               move_midi_arranger_selections,
               MoveMidiArrangerSelections);
    default:
      g_warn_if_reached ();
      break;
    }

#undef FREE_ACTION
}
