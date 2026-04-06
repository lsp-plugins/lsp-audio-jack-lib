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

#include <lsp-plug.in/audio/jack/backend.h>

#include <stdlib.h>

namespace lsp
{
    namespace audio
    {
        namespace jack
        {
            static inline jack::backend_t *cast(audio::backend_t *self)     { return static_cast<jack::backend_t *>(self); }

            backend_t::backend_t()
            {
                construct();
            }

            void backend_t::construct()
            {
                // Export virtual table
                #define AUDIO_JACK_BACKEND_EXP(func)   audio::backend_t::func = backend_t::func;
                AUDIO_JACK_BACKEND_EXP(connect);
                AUDIO_JACK_BACKEND_EXP(disconnect);
                AUDIO_JACK_BACKEND_EXP(destroy);
                #undef AUDIO_JACK_BACKEND_EXP
            }

            status_t backend_t::connect(audio::backend_t *self, const char *params)
            {
//                backend_t * const back = cast(self);

                return STATUS_NOT_IMPLEMENTED;
            }

            status_t backend_t::disconnect(audio::backend_t *self)
            {
//                backend_t * const back = cast(self);

                return STATUS_NOT_IMPLEMENTED;
            }

            void backend_t::destroy(audio::backend_t *self)
            {
//                backend_t * const back = cast(self);

                // Free allocated memory
                free(self);
            }

        } /* namespace jack */
    } /* namespace audio */
} /* namespace lsp */




