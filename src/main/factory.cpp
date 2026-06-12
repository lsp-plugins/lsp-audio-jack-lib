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

#include <lsp-plug.in/common/types.h>
#include <lsp-plug.in/audio/jack/backend.h>
#include <lsp-plug.in/audio/jack/factory.h>

#include <stdlib.h>

namespace lsp
{
    namespace audio
    {
        namespace jack
        {
            const audio::backend_metadata_t factory_t::sMetadata[] =
            {
                {
                    "jack",
                    "Jack Audio Backend",
                    "jack",
                    100
                }
            };

            const audio::backend_metadata_t *factory_t::metadata(audio::factory_t *self, size_t id)
            {
                const size_t count = sizeof(sMetadata) / sizeof(audio::backend_metadata_t);
                return (id < count) ? &sMetadata[id] : NULL;
            }

            audio::backend_t *factory_t::create(audio::factory_t *self, size_t id)
            {
                if (id == 0)
                {
                    jack::backend_t *res = static_cast<jack::backend_t *>(::malloc(sizeof(jack::backend_t)));
                    if (res != NULL)
                        res->construct();
                    return res;
                }
                return NULL;
            }

            factory_t::factory_t()
            {
                #define AUDIO_JACK_FACTORY_EXP(func)   audio::factory_t::func   = jack::factory_t::func;
                AUDIO_JACK_FACTORY_EXP(create);
                AUDIO_JACK_FACTORY_EXP(metadata);
                #undef AUDIO_JACK_FACTORY_EXP
            }

            factory_t::~factory_t()
            {
            }
        } /* namespace jack */
    } /* namespace audio */
} /* namespace lsp */



