/*
 * Copyright (C) 2026 Linux Studio Plugins Project <https://lsp-plug.in/>
 *           (C) 2026 Vladimir Sadovnikov <sadko4u@gmail.com>
 *
 * This file is part of lsp-audio-jack-lib
 * Created on: 6 апр. 2026 г.
 *
 * lsp-audio-jack-lib is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * lsp-audio-jack-lib is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with lsp-audio-jack-lib. If not, see <https://www.gnu.org/licenses/>.
 */

#include <lsp-plug.in/common/debug.h>
#include <lsp-plug.in/common/finally.h>
#include <lsp-plug.in/common/status.h>
#include <lsp-plug.in/audio/jack/backend.h>
#include <lsp-plug.in/stdlib/string.h>

#include <stdlib.h>

namespace lsp
{
    namespace audio
    {
        namespace jack
        {
            static inline jack::backend_t *cast(audio::backend_t *self)
            {
                return static_cast<jack::backend_t *>(self);
            }

            static inline jack::backend_t *cast(void *self)
            {
                return static_cast<jack::backend_t *>(self);
            }

            backend_t::backend_t()
            {
                construct();
            }

            void backend_t::construct()
            {
                pClient                     = NULL;
                pUserData                   = NULL;
                pCallbacks                  = NULL;

                sIOParams.sample_rate       = 0;
                sIOParams.buffer_size       = 0;
                sIOParams.max_buffer_size   = 0;

                io_position_t * const npos      = &sIOPosition;
                npos->frame                     = 0;
                npos->bar                       = 0;
                npos->beat                      = 0;
                npos->tick                      = 0;
                npos->speed                     = 1.0f;
                npos->numerator                 = 4.0f;
                npos->denominator               = 4.0f;
                npos->beats_per_minute          = 120.0f;
                npos->beats_per_minute_change   = 0.0f;
                npos->ticks_per_beat            = 4096.0f;

                // Export virtual table
                #define AUDIO_JACK_BACKEND_EXP(func)   audio::backend_t::func = backend_t::func;
                AUDIO_JACK_BACKEND_EXP(connect);
                AUDIO_JACK_BACKEND_EXP(disconnect);
                AUDIO_JACK_BACKEND_EXP(destroy);
                #undef AUDIO_JACK_BACKEND_EXP
            }

            status_t backend_t::connect(
                audio::backend_t *self,
                const connection_params_t *params,
                const callbacks_t *callbacks,
                void *user_data)
            {
                backend_t * const back = cast(self);

                // Check that backend is disconnected
                if (back->pClient != NULL)
                    return STATUS_BAD_STATE;

                // Init client identifier and ensure that it is not longer than jack_client_name_size()
                char *client_name       = NULL;
                lsp_finally {
                    if (client_name != NULL)
                        free(client_name);
                };

                if (params->client_name != NULL)
                {
                    const size_t max_client_size    = jack_client_name_size();
                    client_name         = static_cast<char *>(malloc(max_client_size));
                    if (client_name == NULL)
                        return STATUS_NO_MEM;

                    strncpy(client_name, params->client_name, max_client_size);
                    client_name[max_client_size-1] = '\0';
                }

                // Get JACK client
                jack_status_t jack_status;
                jack_client_t *client = (params->url != NULL) ?
                    jack_client_open(client_name, jack_options_t(JackNoStartServer | JackServerName), &jack_status, params->url) :
                    jack_client_open(client_name, JackNoStartServer, &jack_status);
                if (client == NULL)
                {
                    lsp_warn("Could not connect to JACK (status=0x%08x)", int(jack_status));
                    return STATUS_DISCONNECTED;
                }
                lsp_finally {
                    if (client != NULL)
                        jack_client_close(client);
                };

                // Obtain I/O parameters
                io_parameters_t io_params;
                io_params.sample_rate       = jack_get_sample_rate(client);
                io_params.buffer_size       = jack_get_buffer_size(client);
                io_params.max_buffer_size   = io_params.buffer_size;

                // Set-up shutdown handler
                jack_on_shutdown(client, on_shutdown, back);

                // Set-up buffer size callback
                if (jack_set_buffer_size_callback(client, on_buffer_size_changed, back))
                {
                    lsp_error("Could not setup buffer size callback");
                    return STATUS_DISCONNECTED;
                }

                // Set plugin sample rate and call for settings update
                if (jack_set_sample_rate_callback(client, on_sample_rate_changed, back))
                {
                    lsp_error("Could not setup sample rate callback");
                    return STATUS_DISCONNECTED;
                }

                // Add processing callback
                if (jack_set_process_callback(client, on_process, back))
                {
                    lsp_error("Could not setup processing callback");
                    return STATUS_DISCONNECTED;
                }

                // Setup position synchronization callback
                if (jack_set_sync_callback(client, on_sync, back))
                {
                    lsp_error("Could not setup position sync callback");
                    return STATUS_DISCONNECTED;
                }

                // Set sync timeout for handler
                if (jack_set_sync_timeout(client, 100000)) // 100 msec timeout
                {
                    lsp_error("Could not setup sync timeout");
                    return STATUS_DISCONNECTED;
                }

                // Issue connected callback
                status_t res = ((callbacks) && (callbacks->on_connected)) ?
                    callbacks->on_connected(user_data, &io_params) :
                    STATUS_OK;
                lsp_finally {
                    if ((client != NULL) && (callbacks) && (callbacks->on_connection_lost))
                        callbacks->on_connection_lost(user_data);
                };
                if (res != STATUS_OK)
                    return res;

                // Activate JACK client
                if (jack_activate(client))
                {
                    lsp_error("Could not activate JACK client");
                    return STATUS_DISCONNECTED;
                }

                // Issue activated callback
                res = ((callbacks) && (callbacks->on_activated)) ?
                    callbacks->on_activated(user_data) :
                    STATUS_OK;
                if (res != STATUS_OK)
                    return res;

                // Commit state
                back->pClient       = release_ptr(client);
                back->pUserData     = user_data;
                back->pCallbacks    = callbacks;
                back->sIOParams     = io_params;

                return STATUS_OK;
            }

            status_t backend_t::disconnect(audio::backend_t *self)
            {
                backend_t * const back  = cast(self);
                jack_client_t *client   = back->pClient;
                if (client == NULL)
                    return STATUS_BAD_STATE;

                // Deactivate application
                jack_deactivate(client);
                const callbacks_t * const cb = back->pCallbacks;
                status_t res = ((cb) && (cb->on_deactivated)) ?
                    cb->on_deactivated(back->pUserData) : STATUS_OK;

                // Close client connection
                jack_client_close(client);
                if ((cb) && (cb->on_disconnected))
                    cb->on_disconnected(back->pUserData);

                // Cleanup state
                back->construct();

                return res;
            }

            void backend_t::destroy(audio::backend_t *self)
            {
                // Issue disconnect and free allocated memory
                disconnect(self);
                free(self);
            }

            void backend_t::on_shutdown(void *self)
            {
                backend_t * const back = cast(self);
                const callbacks_t * const cb = back->pCallbacks;
                if ((cb) && (cb->on_connection_lost))
                    cb->on_connection_lost(back->pUserData);
            }

            int backend_t::on_buffer_size_changed(jack_nframes_t nframes, void *self)
            {
                backend_t * const back = cast(self);
                back->sIOParams.buffer_size         = nframes;
                back->sIOParams.max_buffer_size     = nframes;

                const callbacks_t * const cb = back->pCallbacks;
                const status_t res = ((cb) && (cb->on_io_changed)) ?
                    cb->on_io_changed(back->pUserData, &back->sIOParams) :
                    STATUS_OK;
                return (res == STATUS_OK) ? 0 : -1;
            }

            int backend_t::on_sample_rate_changed(jack_nframes_t nframes, void *self)
            {
                backend_t * const back = cast(self);
                back->sIOParams.sample_rate         = nframes;

                const callbacks_t * const cb = back->pCallbacks;
                const status_t res = ((cb) && (cb->on_io_changed)) ?
                    cb->on_io_changed(back->pUserData, &back->sIOParams) :
                    STATUS_OK;
                return (res == STATUS_OK) ? 0 : -1;
            }

            int backend_t::on_process(jack_nframes_t nframes, void *self)
            {
                backend_t * const back = cast(self);

                const callbacks_t * const cb = back->pCallbacks;
                const status_t res = ((cb) && (cb->on_process)) ?
                    cb->on_process(back->pUserData, &back->sIOPosition) :
                    STATUS_OK;
                return (res == STATUS_OK) ? 0 : -1;
            }

            int backend_t::on_sync(jack_transport_state_t state, jack_position_t *pos, void *self)
            {
                backend_t * const back = cast(self);

                // Update I/O position
                io_position_t *npos = &back->sIOPosition;
                npos->speed         = (state == JackTransportRolling) ? 1.0f : 0.0f;
                npos->frame         = pos->frame;

                if (pos->valid & JackPositionBBT)
                {
                    npos->bar               = pos->bar;
                    npos->beat              = pos->beat;
                    npos->tick              = pos->tick;
                    npos->numerator         = pos->beats_per_bar;
                    npos->denominator       = pos->beat_type;
                    npos->beats_per_minute  = pos->beats_per_minute;
                    npos->ticks_per_beat    = pos->ticks_per_beat;
                    npos->tick              = pos->tick;
                }
                else
                {
                    npos->bar               = 0;
                    npos->beat              = 0;
                    npos->tick              = 0;
                    npos->numerator         = 4.0f;
                    npos->denominator       = 4.0f;
                    npos->beats_per_minute  = 120.0f;
                    npos->ticks_per_beat    = 4096.0f;
                    npos->tick              = 0.0f;
                }

                return 0;
            }

        } /* namespace jack */
    } /* namespace audio */
} /* namespace lsp */




