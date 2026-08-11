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

#ifndef NANCY_INPUT_H
#define NANCY_INPUT_H

#include "engines/nancy/commontypes.h"

#include "common/rect.h"
#include "common/keyboard.h"

namespace Common {
class Keymap;
typedef class Array<Keymap *> KeymapArray;
}

namespace Nancy {

namespace State {
class State;
}

// Defined in action/puzzle/chasemappuzzle.cpp. The Venice chase autoplay's one
// call into the input layer: true when this poll should synthesise a left click
// at `clickAt`, which is the map hotspot Nancy should take next. Declared here
// rather than pulled in from that record's own header, because including it
// would bring the namespace name Nancy::Action into scope and hide
// Common::Action from initKeymaps().
bool chaseAutoPlayNextClick(Common::Point &clickAt);

struct NancyInput {
	enum InputType : uint16 {
		kLeftMouseButtonDown	= 1 << 0,
		kLeftMouseButtonHeld	= 1 << 1,
		kLeftMouseButtonUp		= 1 << 2,
		kRightMouseButtonDown	= 1 << 3,
		kRightMouseButtonHeld	= 1 << 4,
		kRightMouseButtonUp		= 1 << 5,
		kMoveUp					= 1 << 6,
		kMoveDown				= 1 << 7,
		kMoveLeft				= 1 << 8,
		kMoveRight				= 1 << 9,
		kMoveFastModifier		= 1 << 10,
		kOpenMainMenu			= 1 << 11,
		kRaycastMap				= 1 << 12,

		kLeftMouseButton		= kLeftMouseButtonDown | kLeftMouseButtonHeld | kLeftMouseButtonUp,
		kRightMouseButton		= kRightMouseButtonDown | kRightMouseButtonHeld | kRightMouseButtonUp
	};

	Common::Point mousePos;
	uint16 input;
	Common::Array<Common::KeyState> otherKbdInput;

	void eatMouseInput() { mousePos.x = -1; input &= ~(kLeftMouseButton | kRightMouseButton); }
};

// This class handles collecting events and translating them to a NancyInput object,
// which can then be pulled by interested classes through getInput()
class InputManager {

public:
	enum NancyAction {
		kNancyActionMoveUp,
		kNancyActionMoveDown,
		kNancyActionMoveLeft,
		kNancyActionMoveRight,
		kNancyActionMoveFast,
		kNancyActionLeftClick,
		kNancyActionRightClick,
		kNancyActionOpenMainMenu,
		kNancyActionShowRaycastMap
	};

	InputManager() :
		_inputs(0),
		_mouseEnabled(true),
		_inputBeginState(NancyState::kNone) {}

	void processEvents();

	NancyInput getInput() const;
	void forceCleanInput();
	void setMouseInputEnabled(bool enabled) { _mouseEnabled = enabled; }
	void setKeymapEnabled(Common::String keymapName, bool enabled);
	void setVKEnabled(bool enabled);

	static void initKeymaps(Common::KeymapArray &keymaps, const char *target);

	static const char *_mazeKeymapID;

private:
	uint16 _inputs;

	// Debug-driven virtual cursor. The autoclick/scene-script/autodrag hooks used
	// to call g_system->warpMouse(), which moves the REAL system pointer - so a
	// headless test run fought the user for their mouse, and several concurrent
	// runs made the machine unusable. These hooks now set a virtual position that
	// getInput() substitutes instead. Once any hook has fired the override is
	// sticky, which also stops a stray human mouse movement from corrupting a
	// scripted run. The engine's own cursor warp (Cursor::_warpedMousePos) is a
	// real game behaviour and is deliberately left alone.
	Common::Point _debugMousePos;
	bool _debugMousePosSet = false;

public:
	void setDebugMousePos(const Common::Point &pos) {
		_debugMousePos = pos;
		_debugMousePosSet = true;
	}

	// True once a debug hook has taken over the cursor. The engine's own
	// warpCursor() must then move the VIRTUAL cursor, not the OS pointer.
	bool isDebugMouseActive() const { return _debugMousePosSet; }

private:
	Common::Array<Common::KeyState> _otherKbdInput;
	bool _mouseEnabled;
	NancyState::NancyState _inputBeginState;
};

} // End of namespace Nancy

#endif // NANCY_INPUT_H
