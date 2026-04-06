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
                public:
                    jack_client_t      *pClient;
                    void               *pUserData;
                    const callbacks_t  *pCallbacks;
                    io_parameters_t     sIOParams;
                    io_position_t       sIOPosition;

                protected: // Jack-related callbacks
                    static void         on_shutdown(void *self);
                    static int          on_buffer_size_changed(jack_nframes_t nframes, void *self);
                    static int          on_sample_rate_changed(jack_nframes_t nframes, void *self);
                    static int          on_process(jack_nframes_t nframes, void *self);
                    static int          on_sync(jack_transport_state_t state, jack_position_t *pos, void *self);

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

            } backend_t;
        } /* namespace jack */
    } /* namespace audio */
} /* namespace lsp */

#endif /* LSP_PLUG_IN_AUDIO_JACK_BACKEND_H_ */
