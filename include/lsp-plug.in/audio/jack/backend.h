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

#ifndef LSP_PLUG_IN_AUDIO_JACK_BACKEND_H_
#define LSP_PLUG_IN_AUDIO_JACK_BACKEND_H_

#include <lsp-plug.in/audio/jack/version.h>

#include <lsp-plug.in/audio/iface/backend.h>

#include <jack/jack.h>

namespace lsp
{
    namespace audio
    {
        namespace jack
        {

            typedef struct backend_t: public audio::backend_t
            {
                protected:
                    static constexpr size_t MAX_PORT_ID_BYTES   = 16;

                    typedef struct port_t
                    {
                        uint32_t        nType;
                        uint32_t        nLatency;
                        jack_port_t    *pPort;

                        char            sID[MAX_PORT_ID_BYTES];
                    } port_t;

                public:
                    jack_client_t      *pClient;
                    void               *pUserData;
                    const callbacks_t  *pCallbacks;
                    io_parameters_t     sIOParams;
                    io_position_t       sIOPosition;

                    port_t             *vPorts;
                    port_id_t           nFirst;
                    port_id_t           nCapacity;

                protected:
                    port_t             *alloc_port(const char *id, uint32_t flags);
                    void                free_port(port_t *port);
                    status_t            register_ports(jack_client_t *client);
                    void                unregister_ports(jack_client_t *client);

                protected: // Jack-related callbacks
                    static void         on_shutdown(void *self);
                    static int          on_buffer_size_changed(jack_nframes_t nframes, void *self);
                    static int          on_sample_rate_changed(jack_nframes_t nframes, void *self);
                    static int          on_process(jack_nframes_t nframes, void *self);
                    static int          on_sync(jack_transport_state_t state, jack_position_t *pos, void *self);
                    static int          on_latency_sync(jack_latency_callback_mode_t mode, void *self);

                public:
                    explicit            backend_t();
                    void                construct();

                public:
                    static status_t     connect(
                        audio::backend_t *self,
                        const connection_params_t *params,
                        const callbacks_t *callbacks,
                        void *user_data);
                    static status_t     disconnect(audio::backend_t *self);
                    static void         destroy(audio::backend_t *self);

                    static port_id_t    register_port(audio::backend_t *self, const char *id, uint32_t flags);
                    static status_t     unregister_port(audio::backend_t *self, port_id_t port_id);
                    static status_t     set_port_latency(audio::backend_t *self, port_id_t port_id, uint32_t latency);

                    static size_t       audio_buffer_count(audio::backend_t *self, port_id_t port_id);
                    static float       *get_audio_buffer(audio::backend_t *self, port_id_t port_id, size_t index);

            } backend_t;
        } /* namespace jack */
    } /* namespace audio */
} /* namespace lsp */

#endif /* LSP_PLUG_IN_AUDIO_JACK_BACKEND_H_ */
