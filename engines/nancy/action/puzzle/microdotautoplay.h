/* ScummVM - Graphic Adventure Engine
 *
 * ScummVM is the legal property of its developers, whose names
 * are too numerous to list here. Please refer to the COPYRIGHT
 * file distributed with this source distribution.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef NANCY_ACTION_MICRODOTAUTOPLAY_H
#define NANCY_ACTION_MICRODOTAUTOPLAY_H

#include "common/rect.h"

// Debug affordance for the Nancy18 microdot viewers, S3280/S3281. See the block
// comment in microdotautoplay.cpp for the model; nancy_microdot_autoplay
// defaults off and nothing here runs in ordinary play.
//
// Declared in namespace Nancy rather than Nancy::Action deliberately: input.cpp
// is the only caller, and pulling the name Nancy::Action into that translation
// unit hides Common::Action from initKeymaps(). The chase hook's one entry point
// is declared in input.h for exactly the same reason.
namespace Nancy {

// True when this input poll should synthesise a left click at `clickAt`, which
// is the centre of the viewer's own exit hotspot in screen space.
bool microdotAutoPlayNextClick(Common::Point &clickAt);

} // End of namespace Nancy

#endif // NANCY_ACTION_MICRODOTAUTOPLAY_H
