/*
 * Copyright (C) 2018 Alexandros Theodotou
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

/** \file
 * The audio engine of zyrthm. */

#include <math.h>
#include <stdlib.h>
#include <signal.h>

#include "zrythm_app.h"
#include "audio/channel.h"
#include "audio/engine.h"
#include "audio/midi.h"
#include "audio/mixer.h"
#include "audio/transport.h"
#include "plugins/plugin.h"
#include "plugins/plugin_manager.h"
#include "plugins/lv2_plugin.h"

#include <gtk/gtk.h>

#include <jack/jack.h>
#include <jack/midiport.h>

#define JACK_PORT_T(exp) ((jack_port_t *) exp)
#define MIDI_IN_EVENT(i) (AUDIO_ENGINE->midi_in->midi_events.jack_midi_events[i])
#define MIDI_IN_NUM_EVENTS AUDIO_ENGINE->midi_in->midi_events.num_events

typedef jack_default_audio_sample_t   sample_t;
typedef jack_nframes_t                nframes_t;

 void
engine_update_frames_per_tick (int beats_per_bar,
                               int bpm,
                               int sample_rate)
{
  AUDIO_ENGINE->frames_per_tick =
    (sample_rate * 60.f * beats_per_bar) /
    (bpm * TICKS_PER_BAR);
}

/** Jack sample rate callback. */
static int
jack_sample_rate_cb(nframes_t nframes, void* data)
{
  AUDIO_ENGINE->sample_rate = nframes;

  if (PROJECT)
    engine_update_frames_per_tick (TRANSPORT->beats_per_bar,
                                   TRANSPORT->bpm,
                                   AUDIO_ENGINE->sample_rate);
  else
    engine_update_frames_per_tick (4, 120, 44000);

  g_message ("JACK: Sample rate changed to %d", nframes);
  return 0;
}

/** Jack buffer size callback. */
static int
jack_buffer_size_cb(nframes_t nframes, void* data)
{
  AUDIO_ENGINE->block_length = nframes;
  AUDIO_ENGINE->buf_size_set = true;
#ifdef HAVE_JACK_PORT_TYPE_GET_BUFFER_SIZE
  AUDIO_ENGINE->midi_buf_size = jack_port_type_get_buffer_size(
          AUDIO_ENGINE->client, JACK_DEFAULT_MIDI_TYPE);
#endif
  g_message ("JACK: Block length changed to %d, midi buf size to %lu",
             AUDIO_ENGINE->block_length,
             AUDIO_ENGINE->midi_buf_size);

  /** reallocate port buffers to new size */
  g_message ("Reallocating port buffers to %d", nframes);
  for (int i = 0; i < AUDIO_ENGINE->num_ports; i++)
    {
      Port * port = AUDIO_ENGINE->ports[i];
      port->nframes = nframes;
      port->buf = realloc (port->buf, nframes * sizeof (sample_t));
      /* TODO memset */
    }
  for (int i = 0; i < MIXER->num_channels; i++)
    {
      Channel * channel = MIXER->channels[i];
      for (int j = 0; j < MAX_PLUGINS; j++)
        {
          if (channel->strip[j])
            {
              Plugin * plugin = channel->strip[j];
              if (plugin->descr->protocol = PROT_LV2)
                {
                  lv2_allocate_port_buffers (
                                (LV2_Plugin *)plugin->original_plugin);
                }
            }
        }
    }
  /* FIXME this is the same as block_length */
  AUDIO_ENGINE->nframes = nframes;
  return 0;
}

/**
 * The process callback for this JACK application is called in a
 * special realtime thread once for each audio cycle.
 *
 * This client follows a simple rule: when the JACK transport is
 * running, copy the input port to the output.  When it stops, exit.
 *
 */
static int
jack_process_cb (nframes_t    nframes,     ///< the number of frames to fill
         void              * data)       ///< user data
{
  int * run = (int *) data;
  if (!(* run))
    return 0;

  sample_t * stereo_out_l, * stereo_out_r;
  int i = 0;

  if (TRANSPORT->play_state == PLAYSTATE_PAUSE_REQUESTED)
    {
      TRANSPORT->play_state = PLAYSTATE_PAUSED;
      zix_sem_post (&TRANSPORT->paused);
    }
  else if (TRANSPORT->play_state == PLAYSTATE_ROLL_REQUESTED)
    {
      TRANSPORT->play_state = PLAYSTATE_ROLLING;
    }

  zix_sem_wait (&AUDIO_ENGINE->port_operation_lock);

  /* reset all buffers */
  for (i = 0; i < AUDIO_ENGINE->num_ports; i++)
    {
      if (AUDIO_ENGINE->ports[i])
        port_clear_buffer (AUDIO_ENGINE->ports[i]);
    }

  /*g_message ("jack start");*/

  /* get MIDI events from JACK and store to engine MIDI in port
   * FIXME referencing  AUDIO ENGINE all the time is expensive,
   * use local vars where possible */
  void* port_buf = jack_port_get_buffer (
        JACK_PORT_T (AUDIO_ENGINE->midi_in->data), nframes);
  MIDI_IN_NUM_EVENTS = jack_midi_get_event_count(port_buf);
  if(MIDI_IN_NUM_EVENTS > 0)
    {
      g_message ("JACK: have %d events", MIDI_IN_NUM_EVENTS);
      for(int i=0; i < MIDI_IN_NUM_EVENTS; i++)
        {
          jack_midi_event_t * event = &MIDI_IN_EVENT(i);
          jack_midi_event_get(event, port_buf, i);
          uint8_t type = event->buffer[0] & 0xf0;
          uint8_t channel = event->buffer[0] & 0xf;
          switch (type)
            {
              case 0x90:
                assert (event->size == 3);
                g_message (" note on  (channel %2d): pitch %3d, velocity %3d", channel, event->buffer[1], event->buffer[2]);
                break;
              case 0x80:
                assert (event->size == 3);
                g_message (" note off (channel %2d): pitch %3d, velocity %3d", channel, event->buffer[1], event->buffer[2]);
                break;
              case 0xb0:
                assert (event->size == 3);
                g_message (" control change (channel %2d): controller %3d, value %3d", channel, event->buffer[1], event->buffer[2]);
                break;
              default:
                      break;
            }
          /*g_message ("    event %d time is %d. 1st byte is 0x%x", i,*/
                     /*MIDI_IN_EVENT(i).time, *(MIDI_IN_EVENT(i).buffer));*/
        }
    }

  /* set all to unprocessed for this cycle */
  for (int i = 0; i < MIXER->num_channels; i++)
    {
      Channel * channel = MIXER->channels[i];
      channel->processed = 0;
      for (int j = 0; j < MAX_PLUGINS; j++)
        {
          if (channel->strip[j])
            {
              channel->strip[j]->processed = 0;
            }
        }
    }

  /*
   * process
   */
  mixer_process (nframes);

  /**
   * get jack's buffers with nframes frames for left & right
   */
  stereo_out_l = (sample_t *)
    jack_port_get_buffer (JACK_PORT_T (AUDIO_ENGINE->stereo_out->l->data),
                          nframes);
  stereo_out_r = (sample_t *)
    jack_port_get_buffer (JACK_PORT_T (AUDIO_ENGINE->stereo_out->r->data),
                           nframes);

  /* by this time, the Master channel should have its Stereo Out ports filled.
   * pass their buffers to jack's buffers */
  for (i = 0; i < nframes; i++)
    {
      stereo_out_l[i] = MIXER->master->stereo_out->l->buf[i];
      stereo_out_r[i] = MIXER->master->stereo_out->r->buf[i];
    }

  zix_sem_post (&AUDIO_ENGINE->port_operation_lock);

  /* move playhead as many samples as processed */
  transport_update_playhead (nframes);

  /*g_message ("jack end");*/
  /*
   * processing finished, return 0
   */
  return 0;
}

/**
 * JACK calls this shutdown_callback if the server ever shuts down or
 * decides to disconnect the client.
 */
void
jack_shutdown_cb (void *arg)
{
  // TODO
  g_error ("Jack shutting down...");
}

/**
 * Init audio engine
 */
void
init_audio_engine()
{
    g_message ("Initializing audio engine...");
    AUDIO_ENGINE = calloc (1, sizeof (Audio_Engine));
    Audio_Engine * engine = AUDIO_ENGINE;

    const char **ports;
    const char *client_name = "Zrythm";
    const char *server_name = NULL;
    jack_options_t options = JackNullOption;
    jack_status_t status;
    int i;

    engine->buf_size_set = false;

    // open a client connection to the JACK server
    engine->client = jack_client_open (client_name, options, &status, server_name);
    if (engine->client == NULL) {
            g_error ("jack_client_open() failed, "
                     "status = 0x%2.0x", status);
            if (status & JackServerFailed) {
                    g_error ("Unable to connect to JACK server");
            }
            exit (1);
    }
    if (status & JackServerStarted) {
    // FIXME g_info
            g_message ("JACK server started\n");
    }
    if (status & JackNameNotUnique) {
            client_name = jack_get_client_name(engine->client);
            g_error ("unique name `%s' assigned\n", client_name);
    }

    /* Set audio engine properties */
    engine->sample_rate   = jack_get_sample_rate (engine->client);
    engine->block_length  = jack_get_buffer_size (engine->client);
    engine->midi_buf_size = 4096;
#ifdef HAVE_JACK_PORT_TYPE_GET_BUFFER_SIZE
    engine->midi_buf_size = jack_port_type_get_buffer_size (
          engine->client, JACK_DEFAULT_MIDI_TYPE);
#endif

    /* set jack callbacks */
    jack_set_process_callback (engine->client, &jack_process_cb, &engine->run);
    jack_set_buffer_size_callback(engine->client, &jack_buffer_size_cb, NULL);
    jack_set_sample_rate_callback(engine->client, &jack_sample_rate_cb, NULL);
    jack_on_shutdown(engine->client, &jack_shutdown_cb, NULL);
    /*jack_set_latency_callback(client, &jack_latency_cb, arg);*/
#ifdef JALV_JACK_SESSION
    /*jack_set_session_callback(client, &jack_session_cb, arg);*/
#endif

    /* create ports */
    Port * stereo_out_l = port_new_with_data (
          engine->block_length,
          INTERNAL_JACK_PORT,
          TYPE_AUDIO,
          FLOW_OUTPUT,
          "JACK Stereo Out / L",
          (void *) jack_port_register (engine->client, "Stereo_out_L",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsOutput, 0));
    Port * stereo_out_r = port_new_with_data (
          engine->block_length,
          INTERNAL_JACK_PORT,
          TYPE_AUDIO,
          FLOW_OUTPUT,
          "JACK Stereo Out / R",
          (void *) jack_port_register (engine->client, "Stereo_out_R",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsOutput, 0));
    Port * stereo_in_l = port_new_with_data (
          engine->block_length,
          INTERNAL_JACK_PORT,
          TYPE_AUDIO,
          FLOW_INPUT,
          "JACK Stereo In / L",
          (void *) jack_port_register (engine->client, "Stereo_in_L",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsInput, 0));
    Port * stereo_in_r = port_new_with_data (
          engine->block_length,
          INTERNAL_JACK_PORT,
          TYPE_AUDIO,
          FLOW_INPUT,
          "JACK Stereo In / R",
          (void *) jack_port_register (engine->client, "Stereo_in_R",
                                      JACK_DEFAULT_AUDIO_TYPE,
                                      JackPortIsInput, 0));
    Port * midi_in = port_new_with_data (
          engine->block_length,
          INTERNAL_JACK_PORT,
          TYPE_EVENT,
          FLOW_INPUT,
          "JACK MIDI In",
          (void *) jack_port_register (engine->client, "MIDI_in",
                                       JACK_DEFAULT_MIDI_TYPE,
                                       JackPortIsInput, 0));

    stereo_in_l->owner_jack = 1;
    stereo_in_r->owner_jack = 1;
    stereo_out_l->owner_jack = 1;
    stereo_out_r->owner_jack = 1;
    midi_in->owner_jack = 1;

    engine->stereo_in  = stereo_ports_new (stereo_in_l, stereo_in_r);
    engine->stereo_out = stereo_ports_new (stereo_out_l, stereo_out_r);
    engine->midi_in    = midi_in;

    if (!engine->stereo_in->l->data || !engine->stereo_in->r->data ||
        !engine->stereo_out->l->data || !engine->stereo_out->r->data ||
        !engine->midi_in->data)
      {
        g_error ("no more JACK ports available");
      }

    /* initialize mixer, which handles the processing */
    mixer_init ();

    /* init semaphore */
    zix_sem_init (&AUDIO_ENGINE->port_operation_lock, 1);

    /* Tell the JACK server that we are ready to roll.  Our
     * process() callback will start running now. */
    if (jack_activate (engine->client))
      {
        g_error ("cannot activate client");
        return;
      }
    g_message ("Jack activated");

    /* Connect the ports.  You can't do this before the client is
     * activated, because we can't make connections to clients
     * that aren't running.  Note the confusing (but necessary)
     * orientation of the driver backend ports: playback ports are
     * "input" to the backend, and capture ports are "output" from
     * it.
     */

    ports = jack_get_ports (engine->client, NULL, NULL,
                            JackPortIsPhysical|JackPortIsInput);
    if (ports == NULL) {
            g_error ("no physical playback ports\n");
            exit (1);
    }

    if (jack_connect (engine->client, jack_port_name (
                JACK_PORT_T(engine->stereo_out->l->data)), ports[0])) {
            g_error ("cannot connect output ports\n");
    }

    if (jack_connect (engine->client, jack_port_name (
                JACK_PORT_T (engine->stereo_out->r->data)), ports[1])) {
            g_error ("cannot connect output ports\n");
    }

    jack_free (ports);

}

void
close_audio_engine ()
{
    g_message ("closing audio engine...");

    jack_client_close (AUDIO_ENGINE->client);
}

void
engine_delete_port (Port * port)
{
  for (int i = port->id; i < AUDIO_ENGINE->num_ports - 1; i++)
    {
      AUDIO_ENGINE->ports[i] = AUDIO_ENGINE->ports[i + 1];
    }
  AUDIO_ENGINE->num_ports--;
}
