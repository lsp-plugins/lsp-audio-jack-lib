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

#include <jack/midiport.h>

#include <stdlib.h>
#include <errno.h>

namespace lsp
{
    namespace audio
    {
        namespace jack
        {
            // Definition for header files that do not support this flag
            static constexpr uint32_t JackPortIsMIDI2       = 0x20;

            static constexpr uint32_t PORT_TYPE_FREE        = 0xffffffff;
            static constexpr uint32_t PORT_MASK_ALL         = PORT_DIR_MASK | PORT_TYPE_MASK;

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
                pClient                         = NULL;
                pUserData                       = NULL;
                pCallbacks                      = NULL;

                io_parameters_t * const ip      = &sIOParams;
                ip->sample_rate                 = 0;
                ip->buffer_size                 = 0;
                ip->max_buffer_size             = 0;

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

                nLatency                        = 0;
                vPorts                          = NULL;
                nFirst                          = 0;
                nCapacity                       = 0;
                bActivated                      = false;

                // Export virtual table
                #define AUDIO_PIPEWIRE_BACKEND_EXP(func)   audio::backend_t::func = backend_t::func;

                AUDIO_PIPEWIRE_BACKEND_EXP(connect);
                AUDIO_PIPEWIRE_BACKEND_EXP(set_latency);
                AUDIO_PIPEWIRE_BACKEND_EXP(disconnect);
                AUDIO_PIPEWIRE_BACKEND_EXP(destroy);

                AUDIO_PIPEWIRE_BACKEND_EXP(register_port);
                AUDIO_PIPEWIRE_BACKEND_EXP(unregister_port);
                AUDIO_PIPEWIRE_BACKEND_EXP(port_system_name);

                AUDIO_PIPEWIRE_BACKEND_EXP(connect_ports);
                AUDIO_PIPEWIRE_BACKEND_EXP(disconnect_ports);

                AUDIO_PIPEWIRE_BACKEND_EXP(audio_buffers_count);
                AUDIO_PIPEWIRE_BACKEND_EXP(get_audio_buffer);

                AUDIO_PIPEWIRE_BACKEND_EXP(read_midi_event);
                AUDIO_PIPEWIRE_BACKEND_EXP(write_midi_event);

                #undef AUDIO_PIPEWIRE_BACKEND_EXP
            }

            status_t backend_t::register_port(jack_client_t *client, port_t *port)
            {
                // Determine flags
                unsigned long port_flags= ((port->nType & PORT_DIR_MASK) == PORT_DIR_OUT) ? JackPortIsOutput : JackPortIsInput;
                const char *port_type   = NULL;
                switch (port->nType & PORT_TYPE_MASK)
                {
                    case PORT_TYPE_AUDIO:
                        port_type           = JACK_DEFAULT_AUDIO_TYPE;
                        break;
                    case PORT_TYPE_MIDI:
                        port_type           = JACK_DEFAULT_MIDI_TYPE;
                        break;
                    case PORT_TYPE_MIDI2:
                        port_type           = JACK_DEFAULT_MIDI_TYPE;
                        port_flags         |= JackPortIsMIDI2;
                        break;
                    default:
                        return STATUS_BAD_STATE;
                }

                // Register port
                port->pPort         = jack_port_register(client, port->sID, port_type, port_flags, 0);
                return (port->pPort != NULL) ? STATUS_OK : STATUS_UNKNOWN_ERR;
            }

            status_t backend_t::register_ports(jack_client_t *client)
            {
                for (size_t i=0, n=nCapacity; i<n; ++i)
                {
                    port_t * const port = &vPorts[i];
                    if ((port->nType == PORT_TYPE_FREE) ||
                        (port->pPort != NULL))
                        continue;

                    status_t res        = register_port(client, port);
                    if (res != STATUS_OK)
                        return res;
                }

                return STATUS_OK;
            }

            void backend_t::unregister_ports(jack_client_t *client)
            {
                for (size_t i=0, n=nCapacity; i<n; ++i)
                {
                    port_t * const port = &vPorts[i];
                    if ((port->nType == PORT_TYPE_FREE) ||
                        (port->pPort == NULL))
                        continue;

                    // Unregister port
                    jack_port_unregister(client, port->pPort);
                    port->pPort = NULL;
                }
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
                io_parameters_t * const io  = & back->sIOParams;
                io->sample_rate             = jack_get_sample_rate(client);
                io->buffer_size             = jack_get_buffer_size(client);
                io->max_buffer_size         = io->buffer_size;

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

                // Register previously defined ports
                status_t res = back->register_ports(client);
                if (res != STATUS_OK)
                {
                    back->unregister_ports(client);
                    return res;
                }

                // Commit state
                back->pClient       = client;
                back->pUserData     = user_data;
                back->pCallbacks    = callbacks;

                lsp_finally {
                    if (client != NULL)
                    {
                        back->pClient       = NULL;
                        back->pUserData     = NULL;
                        back->pCallbacks    = NULL;
                    }
                };

                // Issue connected callback
                res = ((callbacks) && (callbacks->on_connected)) ?
                    callbacks->on_connected(user_data, io) :
                    STATUS_OK;
                lsp_finally {
                    if ((client != NULL) && (callbacks) && (callbacks->on_connection_lost))
                        callbacks->on_connection_lost(user_data);
                };
                if (res != STATUS_OK)
                    return res;

                // Activate JACK client
                res = ((callbacks) && (callbacks->on_activated)) ?
                    callbacks->on_activated(user_data) :
                    STATUS_OK;
                if (res != STATUS_OK)
                    return res;

                if (jack_activate(client) != 0)
                {
                    lsp_error("Could not activate JACK client");

                    // Issue deactivation callback
                    if ((callbacks) && (callbacks->on_deactivated))
                        callbacks->on_deactivated(user_data);

                    return STATUS_DISCONNECTED;
                }
                back->bActivated    = true;

                // Do not close client on successful connection
                client              = NULL;

                return STATUS_OK;
            }

            status_t backend_t::set_latency(audio::backend_t *self, uint32_t latency)
            {
                backend_t * const back          = cast(self);
                if (back->nLatency == latency)
                    return STATUS_OK;

                back->nLatency                  = latency;
                if (back->pClient)
                    jack_recompute_total_latencies(back->pClient);
                return STATUS_OK;
            }

            status_t backend_t::disconnect(audio::backend_t *self)
            {
                backend_t * const back          = cast(self);
                jack_client_t * const client    = back->pClient;
                if (client == NULL)
                    return STATUS_BAD_STATE;

                // Deactivate application
                status_t res                    = STATUS_OK;
                const callbacks_t * const cb    = back->pCallbacks;
                void * const user_data          = back->pUserData;

                if (back->bActivated)
                {
                    jack_deactivate(client);

                    back->bActivated                = false;
                    if ((cb) && (cb->on_deactivated))
                        res     = update_status(res, cb->on_deactivated(user_data));
                }

                // Unregister ports
                back->unregister_ports(client);

                // Close client connection
                jack_client_close(client);
                if ((cb) && (cb->on_disconnected))
                    cb->on_disconnected(user_data);

                // Forget the client
                back->pClient                   = NULL;

                // Cleanup I/O parameters
                io_parameters_t * const ip      = &back->sIOParams;
                ip->sample_rate                 = 0;
                ip->buffer_size                 = 0;
                ip->max_buffer_size             = 0;

                // Cleanup I/O position
                io_position_t * const npos      = &back->sIOPosition;
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

                return res;
            }

            void backend_t::destroy(audio::backend_t *self)
            {
                backend_t * const back          = cast(self);

                // Issue disconnect and free allocated memory
                disconnect(back);

                // Free allocated memory for ports
                back->nFirst                = 0;
                back->nCapacity             = 0;
                if (back->vPorts != NULL)
                {
                    free(back->vPorts);
                    back->vPorts                = NULL;
                }

                // Deallocate memory
                free(back);
            }

            backend_t::port_t *backend_t::alloc_port(const char *id, uint32_t flags)
            {
                uint32_t first      = nFirst;
                uint32_t capacity   = nCapacity;

                // Check that memory should be re-allocated
                if (first >= capacity)
                {
                    const size_t new_cap    = lsp_max((capacity << 1), 4u);
                    port_t * const items    = static_cast<port_t *>(realloc(vPorts, sizeof(port_t) * new_cap));
                    if (!items)
                        return NULL;

                    for (size_t i=capacity; i<new_cap; ++i)
                    {
                        port_t * const port     = &items[i];
                        port->nType             = PORT_TYPE_FREE;
                        port->pPort             = NULL;
                        port->pBuffer           = NULL;
                        port->sID[0]            = '\0';
                    }

                    capacity                = new_cap;
                    vPorts                  = items;
                    nCapacity               = capacity;
                }

                // Find unused port in list
                for ( ; first < capacity; ++first)
                {
                    port_t * const port     = &vPorts[first];
                    if (port->nType == PORT_TYPE_FREE)
                    {
                        port->nType             = flags & PORT_MASK_ALL;
                        port->pPort             = NULL;
                        strncpy(port->sID, id, MAX_PORT_ID_BYTES);
                        port->sID[MAX_PORT_ID_BYTES-1]  = '\0';

                        nFirst                  = first + 1;
                        return port;
                    }
                }

                nFirst              = first;

                return NULL;
            }

            void backend_t::free_port(port_t *port)
            {
                if (port == NULL)
                    return;

                port->nType         = PORT_TYPE_FREE;
                nFirst              = lsp_min(nFirst, port_id_t(port - vPorts));
            }

            port_id_t backend_t::register_port(audio::backend_t *self, const char *id, uint32_t flags)
            {
                backend_t * const back  = cast(self);

                // Check arguments
                if (strlen(id) >= MAX_PORT_ID_BYTES)
                    return -STATUS_TOO_BIG;

                // Determine flags
                switch (flags & PORT_TYPE_MASK)
                {
                    case PORT_TYPE_AUDIO:
                    case PORT_TYPE_MIDI:
                    case PORT_TYPE_MIDI2:
                        break;
                    default:
                        return -STATUS_INVALID_VALUE;
                }

                // Add port
                port_t *port = back->alloc_port(id, flags);
                if (port == NULL)
                    return -STATUS_NO_MEM;
                lsp_finally { back->free_port(port); };

                // Register port if connected to client
                jack_client_t * const client = back->pClient;
                if (client != NULL)
                {
                    status_t res = back->register_port(client, port);
                    if (res != STATUS_OK)
                        return -port_id_t(res);
                }

                // Do not free port
                return port_id_t(release_ptr(port) - back->vPorts);
            }

            status_t backend_t::unregister_port(audio::backend_t *self, port_id_t port_id)
            {
                backend_t * const back  = cast(self);

                if ((port_id < 0) || (port_id >= back->nCapacity))
                    return STATUS_INVALID_VALUE;

                port_t * const port = &back->vPorts[port_id];
                if (port->nType == PORT_TYPE_FREE)
                    return STATUS_INVALID_VALUE;

                // Unregister port if connected to client
                jack_client_t * const client = back->pClient;
                if (port->pPort == NULL)
                    return (client == NULL) ? STATUS_OK : STATUS_BAD_STATE;

                if (client != NULL)
                {
                    // Register port
                    if (jack_port_unregister(client, port->pPort) != 0)
                        return STATUS_UNKNOWN_ERR;
                }

                // Free port
                port->pPort         = NULL;
                back->free_port(port);

                return STATUS_OK;
            }

            const char *backend_t::port_system_name(audio::backend_t *self, port_id_t port_id)
            {
                backend_t * const back  = cast(self);

                if ((port_id < 0) || (port_id >= back->nCapacity))
                    return NULL;

                port_t * const port = &back->vPorts[port_id];
                if ((port->nType == PORT_TYPE_FREE) ||
                    (port->pPort == NULL))
                    return NULL;

                return jack_port_name(port->pPort);
            }

            status_t backend_t::connect_ports(audio::backend_t *self, const char *source, const char *destination)
            {
                backend_t * const back  = cast(self);
                if (back->pClient == NULL)
                    return STATUS_BAD_STATE;

                const int result = jack_connect(back->pClient, source, destination);
                switch (result)
                {
                    case 0: return STATUS_OK;
                    case EEXIST: return STATUS_ALREADY_BOUND;
                    default: break;
                }
                return STATUS_UNKNOWN_ERR;
            }

            status_t backend_t::disconnect_ports(audio::backend_t *self, const char *source, const char *destination)
            {
                backend_t * const back  = cast(self);
                if (back->pClient == NULL)
                    return STATUS_BAD_STATE;

                const int result = jack_disconnect(back->pClient, source, destination);
                switch (result)
                {
                    case 0: return STATUS_OK;
                    case EEXIST: return STATUS_ALREADY_BOUND;
                    default: break;
                }
                return STATUS_UNKNOWN_ERR;
            }

            size_t backend_t::audio_buffers_count(audio::backend_t *self, port_id_t port_id)
            {
                backend_t * const back  = cast(self);
                if ((port_id < 0) || (port_id >= back->nCapacity))
                    return 0;

                port_t * const port = &back->vPorts[port_id];
                if ((port->nType == PORT_TYPE_FREE) ||
                    ((port->nType & PORT_TYPE_MASK) != PORT_TYPE_AUDIO) ||
                    (port->pPort == NULL))
                    return 0;
                return (port->pBuffer != NULL) ? 1 : 0;
            }

            float *backend_t::get_audio_buffer(audio::backend_t *self, port_id_t port_id, size_t index)
            {
                backend_t * const back  = cast(self);
                if ((port_id < 0) || (port_id >= back->nCapacity))
                    return NULL;

                port_t * const port = &back->vPorts[port_id];
                if ((port->nType == PORT_TYPE_FREE) ||
                    ((port->nType & PORT_TYPE_MASK) != PORT_TYPE_AUDIO))
                    return NULL;
                return static_cast<float *>(port->pBuffer);
            }

            status_t backend_t::read_midi_event(audio::backend_t *self, port_id_t port_id, midi_event_t *event, uint32_t *index)
            {
                if ((event == NULL) || (index == NULL))
                    return STATUS_BAD_ARGUMENTS;

                backend_t * const back  = cast(self);
                if ((port_id < 0) || (port_id >= back->nCapacity))
                    return STATUS_INVALID_VALUE;

                port_t * const port = &back->vPorts[port_id];
                if (((port->nType != PORT_MIDI_IN) && (port->nType != PORT_MIDI2_IN)) ||
                    (port->pBuffer == NULL))
                    return STATUS_BAD_FORMAT;

                // Obtain MIDI event
                const uint32_t ev_id    = *index;
                const auto num_events   = jack_midi_get_event_count(port->pBuffer);
                if (ev_id >= num_events)
                    return STATUS_NO_DATA;

                // Fetch the event
                jack_midi_event_t ev;
                const int result = jack_midi_event_get(&ev, port->pBuffer, *index);
                if (result == 0)
                {
                    event->timestamp        = uint32_t(ev.time);
                    event->size             = uint32_t(ev.size);
                    event->data             = reinterpret_cast<uint8_t *>(ev.buffer);
                    ++(*index);

                    return STATUS_OK;
                }

                return (result == ENODATA) ? STATUS_NO_DATA : STATUS_UNKNOWN_ERR;
            }

            uint8_t *backend_t::write_midi_event(audio::backend_t *self, port_id_t port_id, uint32_t timestamp, uint32_t size)
            {
                backend_t * const back  = cast(self);
                if ((port_id < 0) || (port_id >= back->nCapacity))
                    return NULL;

                port_t * const port = &back->vPorts[port_id];
                if (((port->nType != PORT_MIDI_OUT) && (port->nType != PORT_MIDI2_OUT)) ||
                    (port->pBuffer == NULL))
                    return NULL;

                // Submit MIDI event
                return reinterpret_cast<uint8_t *>(jack_midi_event_reserve(port->pBuffer, timestamp, size));
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
                if (back->sIOParams.buffer_size == nframes)
                    return 0;

                back->sIOParams.buffer_size         = nframes;
                back->sIOParams.max_buffer_size     = nframes;

                if (back->pClient == NULL)
                    return 0;

                const callbacks_t * const cb = back->pCallbacks;
                const status_t res = ((cb) && (cb->on_io_changed)) ?
                    cb->on_io_changed(back->pUserData, &back->sIOParams) :
                    STATUS_OK;
                return (res == STATUS_OK) ? 0 : -1;
            }

            int backend_t::on_sample_rate_changed(jack_nframes_t nframes, void *self)
            {
                backend_t * const back = cast(self);
                if (back->sIOParams.sample_rate == nframes)
                    return 0;

                back->sIOParams.sample_rate         = nframes;

                if (back->pClient == NULL)
                    return 0;

                const callbacks_t * const cb = back->pCallbacks;
                const status_t res = ((cb) && (cb->on_io_changed)) ?
                    cb->on_io_changed(back->pUserData, &back->sIOParams) :
                    STATUS_OK;
                return (res == STATUS_OK) ? 0 : -1;
            }

            int backend_t::on_process(jack_nframes_t nframes, void *self)
            {
                backend_t * const back = cast(self);

                // Issue on_process() callback
                const callbacks_t * const cb = back->pCallbacks;

                if ((cb) && (cb->on_process))
                {
                    // Obtain all buffers
                    for (size_t i=0, n=back->nCapacity; i<n; ++i)
                    {
                        port_t * const port = &back->vPorts[i];
                        if ((port->nType == PORT_TYPE_FREE) ||
                            (port->pPort == NULL))
                            continue;

                        port->pBuffer = jack_port_get_buffer(port->pPort, nframes);
                        if (((port->nType == PORT_MIDI_OUT) || (port->nType == PORT_MIDI2_OUT)) &&
                            (port->pBuffer != NULL))
                            jack_midi_clear_buffer(port->pBuffer);
                    }

                    // Issue the processing callback
                    status_t res = cb->on_process(back->pUserData, &back->sIOPosition, uint32_t(nframes));

                    // Cleanup pointers to buffers
                    for (size_t i=0, n=back->nCapacity; i<n; ++i)
                    {
                        port_t * const port = &back->vPorts[i];
                        if ((port->nType == PORT_TYPE_FREE) ||
                            (port->pPort == NULL))
                            continue;

                        port->pBuffer = NULL;
                    }

                    return (res == STATUS_OK) ? 0 : -1;
                }
                return 0;
            }

            int backend_t::on_sync(jack_transport_state_t state, jack_position_t *pos, void *self)
            {
                backend_t * const back = cast(self);

                // Update I/O position
                io_position_t *npos = &back->sIOPosition;
                npos->speed         = ((state == JackTransportRolling) || (state == JackTransportLooping)) ? 1.0f : 0.0f;
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
                }
                npos->beats_per_minute_change = 0;

                return 0;
            }

            int backend_t::on_latency_sync(jack_latency_callback_mode_t mode, void *self)
            {
                backend_t * const back = cast(self);
                if (mode != JackCaptureLatency)
                    return 0;

                jack_latency_range_t range;
                const uint32_t latency = back->nLatency;

                for (size_t i=0, n=back->nCapacity; i<n; ++i)
                {
                    port_t * const port = &back->vPorts[i];
                    if ((port->nType != PORT_TYPE_FREE) &&
                        ((port->nType & PORT_DIR_MASK) == PORT_DIR_OUT) &&
                        (port->pPort != NULL))
                    {
                        // Report latency for the input port
                        jack_port_get_latency_range(port->pPort, mode, &range);
                        range.min += latency;
                        range.max += latency;
                        jack_port_set_latency_range(port->pPort, mode, &range);
                    }
                }

                return 0;
            }

        } /* namespace jack */
    } /* namespace audio */
} /* namespace lsp */




