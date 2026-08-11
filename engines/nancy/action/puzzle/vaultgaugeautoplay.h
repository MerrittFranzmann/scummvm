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

#ifndef NANCY_ACTION_VAULTGAUGEAUTOPLAY_H
#define NANCY_ACTION_VAULTGAUGEAUTOPLAY_H

#include "common/rect.h"

// Debug affordance for the Nancy18 flooded-vault pressure gauges, S6700. See the
// block comment in vaultgaugeautoplay.cpp for the model; nancy_vault_autoplay
// defaults off and nothing here runs in ordinary play.
//
// Declared in namespace Nancy rather than Nancy::Action for the same reason the
// chase hook's entry point is: input.cpp is the only caller, and pulling in the
// name Nancy::Action there hides Common::Action from initKeymaps().
namespace Nancy {

// True when this input poll should synthesise a left click at `clickAt`, which
// is the centre of the wheel-arrow record the planner has chosen.
bool vaultGaugeAutoPlayNextClick(Common::Point &clickAt);

} // End of namespace Nancy

#endif // NANCY_ACTION_VAULTGAUGEAUTOPLAY_H
