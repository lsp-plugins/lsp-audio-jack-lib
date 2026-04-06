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
                explicit            backend_t();
                void                construct();

                static status_t     connect(audio::backend_t *self, const char *params);
                static status_t     disconnect(audio::backend_t *self);
                static void         destroy(audio::backend_t *self);

            } backend_t;
        } /* namespace jack */
    } /* namespace audio */
} /* namespace lsp */

#endif /* LSP_PLUG_IN_AUDIO_JACK_BACKEND_H_ */
