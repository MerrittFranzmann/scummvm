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

#include "common/config-manager.h"
#include "common/random.h"

#include "engines/nancy/util.h"
#include "engines/nancy/nancy.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/input.h"
#include "engines/nancy/cursor.h"
#include "engines/nancy/state/scene.h"

#include "engines/nancy/action/puzzle/lockpickpuzzle.h"

namespace Nancy {
namespace Action {

void LockPickPuzzle::readData(Common::SeekableReadStream &stream) {
	stream.skip(33);						// 0x00 unused name, always empty
	_hoverCursorType = stream.readUint16LE();	// 0x21
	stream.skip(8);							// 0x23 double, always 0.25
	readFilename(stream, _imageName);		// 0x2B overlay atlas, ends at 0x4C

	const uint16 numTumblers = stream.readUint16LE();
	_tumblers.resize(numTumblers);
	for (uint i = 0; i < numTumblers; ++i) {
		Tumbler &t = _tumblers[i];
		readFilename(stream, t.name);

		const uint16 numFrames = stream.readUint16LE();
		t.frames.resize(numFrames);
		for (uint j = 0; j < numFrames; ++j) {
			stream.skip(2);	// per-frame flag; always 0 in this game
			readRect(stream, t.frames[j]);
		}
	}

	const uint16 numSlots = stream.readUint16LE();
	_slots.resize(numSlots);
	for (uint i = 0; i < numSlots; ++i) {
		Slot &s = _slots[i];
		readFilename(stream, s.label);
		readFilename(stream, s.tumblerName);

		const uint16 numLinks = stream.readUint16LE();
		s.linkLabels.resize(numLinks);
		for (uint j = 0; j < numLinks; ++j) {
			readFilename(stream, s.linkLabels[j]);
		}

		readRect(stream, s.destRect);
		s.initialFrame = stream.readSint32LE();
		stream.skip(2);	// always -1
	}

	_clickSounds.readData(stream);

	// Four bytes that read 00 00 01 00 in every one of the game's twelve
	// records, so nothing here can tell them apart. Most likely a count of
	// alternative solutions (always 1) preceded by a pad, matching the way
	// TurningPuzzle carries up to three winning orders.
	stream.skip(4);

	const uint16 numSolution = stream.readUint16LE();
	for (uint i = 0; i < numSolution; ++i) {
		Common::String label;
		readFilename(stream, label);
		const int32 frame = stream.readSint32LE();

		const int slotID = findSlot(label);
		if (slotID >= 0) {
			_slots[slotID].requiredFrame = frame;
		}
	}

	// The flag raised when the lock opens: a leading byte (1 in all twelve
	// records, presumably "a flag is present"), then the usual label/value
	// pair. Identified by looking the labels up in EVNT: they come out as
	// EV_FrontDoorLocked (cleared), EV_Solved_Lock_Pick and EV_Got_Egg2,
	// which is exactly what the three lockpick scenes should be setting.
	const byte haveSolveFlag = stream.readByte();
	_solveFlag.label = stream.readSint16LE();
	_solveFlag.flag = stream.readByte();
	if (!haveSolveFlag) {
		_solveFlag.label = -1;
	}

	// Tail25
	stream.skip(2);
	readRect(stream, _exitHotspot);
	_exitCursorType = stream.readUint16LE();
	_exitScene._sceneChange.sceneID = stream.readUint16LE();
	_exitScene._sceneChange.frameID = 0;
	_exitScene._sceneChange.verticalOffset = 0;
	_exitScene._sceneChange.continueSceneSound = kContinueSceneSound;
	_exitScene._flag.label = stream.readSint16LE();
	_exitScene._flag.flag = stream.readByte();

	// Resolve the by-name cross references now that everything is in.
	for (uint i = 0; i < _slots.size(); ++i) {
		Slot &s = _slots[i];
		s.tumblerID = findTumbler(s.tumblerName);
		s.currentFrame = s.initialFrame;

		for (uint j = 0; j < s.linkLabels.size(); ++j) {
			const int linked = findSlot(s.linkLabels[j]);
			if (linked >= 0 && (uint)linked != i) {
				s.links.push_back(linked);
			}
		}

		if (s.tumblerID >= 0 && !_tumblers[s.tumblerID].frames.empty()) {
			const int numFrames = _tumblers[s.tumblerID].frames.size();
			if (s.currentFrame < 0 || s.currentFrame >= numFrames) {
				s.currentFrame = 0;
			}
		}
	}

	if (gDebugLevel < 1) {
		return;
	}

	debugC(1, kDebugScene, "LockPickPuzzle: atlas \"%s\", cursor %u, %u tumblers, %u slots, "
			"solve flag %d (%s) = %u, exit scene %u, exit hotspot (%d,%d,%d,%d)",
			_imageName.toString().c_str(), _hoverCursorType, numTumblers, numSlots,
			_solveFlag.label, g_nancy->getEventFlagName(_solveFlag.label).c_str(), _solveFlag.flag,
			_exitScene._sceneChange.sceneID,
			_exitHotspot.left, _exitHotspot.top, _exitHotspot.right, _exitHotspot.bottom);

	for (uint i = 0; i < _tumblers.size(); ++i) {
		const Tumbler &t = _tumblers[i];
		Common::String frames;
		for (uint j = 0; j < t.frames.size(); ++j) {
			frames += Common::String::format(" [%d,%d,%d,%d]",
				t.frames[j].left, t.frames[j].top, t.frames[j].right, t.frames[j].bottom);
		}
		debugC(2, kDebugScene, "  tumbler %u \"%s\":%s", i, t.name.c_str(), frames.c_str());
	}

	for (uint i = 0; i < _slots.size(); ++i) {
		const Slot &s = _slots[i];
		Common::String links;
		for (uint j = 0; j < s.linkLabels.size(); ++j) {
			links += " " + s.linkLabels[j];
		}
		debugC(2, kDebugScene, "  slot %u \"%s\" tumbler \"%s\"(%d) dest (%d,%d,%d,%d) init %d "
				"required %d links:%s", i, s.label.c_str(), s.tumblerName.c_str(), s.tumblerID,
				s.destRect.left, s.destRect.top, s.destRect.right, s.destRect.bottom,
				s.initialFrame, s.requiredFrame, links.c_str());
	}
}

int LockPickPuzzle::findSlot(const Common::String &label) const {
	for (uint i = 0; i < _slots.size(); ++i) {
		if (_slots[i].label.equalsIgnoreCase(label)) {
			return i;
		}
	}

	return -1;
}

int LockPickPuzzle::findTumbler(const Common::String &name) const {
	for (uint i = 0; i < _tumblers.size(); ++i) {
		if (_tumblers[i].name.equalsIgnoreCase(name)) {
			return i;
		}
	}

	return -1;
}

void LockPickPuzzle::init() {
	Common::Rect screenBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(screenBounds.width(), screenBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(screenBounds);

	g_nancy->_resource->loadImage(_imageName, _image);
	registerGraphics();

	if (gDebugLevel >= 1) {
		Common::String frames;
		for (uint i = 0; i < _slots.size(); ++i) {
			frames += Common::String::format(" %s=%d/%d", _slots[i].label.c_str(),
				_slots[i].currentFrame, _slots[i].requiredFrame);
		}
		debugC(1, kDebugScene, "LockPickPuzzle \"%s\" starting, atlas %dx%d, frames:%s",
			_description.c_str(), _image.w, _image.h, frames.c_str());
	}
}

void LockPickPuzzle::drawSlot(uint slotID) {
	const Slot &s = _slots[slotID];
	if (s.tumblerID < 0) {
		return;
	}

	const Tumbler &t = _tumblers[s.tumblerID];
	if (t.frames.empty()) {
		return;
	}

	uint frame = (uint)s.currentFrame < t.frames.size() ? (uint)s.currentFrame : 0;

	_drawSurface.fillRect(s.destRect, _drawSurface.getTransparentColor());
	_drawSurface.blitFrom(_image, t.frames[frame], s.destRect);
	_needsRedraw = true;
}

void LockPickPuzzle::drawAllSlots() {
	for (uint i = 0; i < _slots.size(); ++i) {
		drawSlot(i);
	}
}

bool LockPickPuzzle::isSolved() const {
	bool anyRequirement = false;

	for (uint i = 0; i < _slots.size(); ++i) {
		if (_slots[i].requiredFrame < 0) {
			continue;
		}

		anyRequirement = true;
		if (_slots[i].currentFrame != _slots[i].requiredFrame) {
			return false;
		}
	}

	return anyRequirement;
}

void LockPickPuzzle::advanceSlot(uint slotID) {
	Slot &s = _slots[slotID];
	if (s.tumblerID < 0) {
		return;
	}

	const uint numFrames = _tumblers[s.tumblerID].frames.size();
	if (numFrames == 0) {
		return;
	}

	s.currentFrame = (s.currentFrame + 1) % (int)numFrames;
	drawSlot(slotID);
}

void LockPickPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		drawAllSlots();
		NancySceneState.setNoHeldItem();
		_state = kRun;
		// fall through
	case kRun:
		if (autoPlay() && !_solved) {
			if (!_autoPlanned) {
				planAutoClicks();
			} else if (!_autoClicks.empty()) {
				const uint slot = _autoClicks[_autoClicks.size() - 1];
				_autoClicks.pop_back();
				clickSlot(slot);
			}
		}

		if (!_solved && isSolved()) {
			_solved = true;

			debugC(1, kDebugScene, "LockPickPuzzle solved, setting flag %d (%s) = %u",
				_solveFlag.label, g_nancy->getEventFlagName(_solveFlag.label).c_str(), _solveFlag.flag);
			NancySceneState.setEventFlag(_solveFlag);

			_state = kActionTrigger;
		}

		break;
	case kActionTrigger:
		if (g_nancy->_sound->isSoundPlaying(_clickSound)) {
			return;
		}

		g_nancy->_sound->stopSound(_clickSound);
		_exitScene.execute();
		finishExecution();
		break;
	}
}

void LockPickPuzzle::handleInput(NancyInput &input) {
	if (_solved) {
		return;
	}

	if (NancySceneState.getViewport().convertViewportToScreen(_exitHotspot).contains(input.mousePos)) {
		g_nancy->_cursor->setCursorType(_exitCursorType ?
			(CursorManager::CursorType)_exitCursorType : g_nancy->_cursor->_puzzleExitCursor, true);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			_state = kActionTrigger;
		}

		return;
	}

	for (uint i = 0; i < _slots.size(); ++i) {
		if (!NancySceneState.getViewport().convertViewportToScreen(_slots[i].destRect).contains(input.mousePos)) {
			continue;
		}

		if (_hoverCursorType) {
			g_nancy->_cursor->setCursorType((CursorManager::CursorType)_hoverCursorType, true);
		} else {
			g_nancy->_cursor->setCursorType(CursorManager::kHotspot);
		}

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			clickSlot(i);
		}

		input.eatMouseInput();
		return;
	}
}

// -- debug affordance -------------------------------------------------------------
//
// This lock is randomised. S2970 runs a type-92 RandomizeEventFlags that picks
// one of scratch flags 1031..1034 with equal weight, and the four type-171
// records in that scene are gated one per flag (times two for difficulty), so
// which of the eight variants you get is decided at scene entry. A click list
// copied out of a walkthrough is therefore wrong seven times in eight; the
// solution has to be computed from the record that actually loaded.
//
// It computes cheaply. A click on slot i steps slot i and every slot geared to
// it by exactly one frame, so the slots form an abelian group under clicking:
// the end state depends only on how many times each slot was clicked, not on the
// order, and clicking slot i n_i times (n_i = its tumbler's frame count) is the
// identity. Searching click counts modulo the period of each slot is a finite
// exhaustive search - 6 slots x 6 frames = 46656 candidates in the twelve records
// this game ships - and it either finds the exact solution or proves there is
// none. The plan is then spent one click per frame through clickSlot(), the same
// function a real click calls, so the win is still detected by isSolved().
//
// nancy_lockpick_autoplay defaults off; ordinary play is unchanged.
bool LockPickPuzzle::autoPlay() {
	return ConfMan.hasKey("nancy_lockpick_autoplay") && ConfMan.getBool("nancy_lockpick_autoplay");
}

void LockPickPuzzle::clickSlot(uint slotID) {
	if (slotID >= _slots.size()) {
		return;
	}

	// A click steps the clicked tumbler and everything geared to it.
	advanceSlot(slotID);
	for (uint j = 0; j < _slots[slotID].links.size(); ++j) {
		advanceSlot(_slots[slotID].links[j]);
	}

	if (gDebugLevel >= 1) {
		Common::String frames;
		for (uint j = 0; j < _slots.size(); ++j) {
			frames += Common::String::format(" %s=%d", _slots[j].label.c_str(), _slots[j].currentFrame);
		}
		debugC(1, kDebugScene, "LockPickPuzzle: clicked slot %u \"%s\", frames:%s",
			slotID, _slots[slotID].label.c_str(), frames.c_str());
	}

	if (!_clickSounds.names.empty()) {
		uint idx = _clickSounds.names.size() == 1 ? 0 :
			g_nancy->_randomSource->getRandomNumber(_clickSounds.names.size() - 1);
		const Common::String &name = _clickSounds.names[idx];

		if (!name.empty() && name != "NO SOUND") {
			g_nancy->_sound->stopSound(_clickSound);
			_clickSound.name = name;
			_clickSound.channelID = _clickSounds.channel;
			_clickSound.numLoops = _clickSounds.numLoops > 0 ? _clickSounds.numLoops : 1;
			_clickSound.volume = _clickSounds.volume;
			g_nancy->_sound->loadSound(_clickSound);
			g_nancy->_sound->playSound(_clickSound);
		}
	}
}

void LockPickPuzzle::planAutoClicks() {
	_autoPlanned = true;
	_autoClicks.clear();

	const uint numSlots = _slots.size();
	if (numSlots == 0 || numSlots > 10) {
		warning("LockPickPuzzle autoplay: %u slots is outside the searched range", numSlots);
		return;
	}

	// Per-slot frame count, and the effect matrix: effect[i][j] is 1 when clicking
	// slot i steps slot j.
	Common::Array<uint> numFrames;
	numFrames.resize(numSlots);
	for (uint j = 0; j < numSlots; ++j) {
		const int t = _slots[j].tumblerID;
		numFrames[j] = (t >= 0 && (uint)t < _tumblers.size()) ? _tumblers[t].frames.size() : 0;
		if (numFrames[j] == 0) {
			warning("LockPickPuzzle autoplay: slot %u has no frames", j);
			return;
		}
	}

	Common::Array<Common::Array<bool> > effect;
	effect.resize(numSlots);
	for (uint i = 0; i < numSlots; ++i) {
		effect[i].resize(numSlots);
		effect[i][i] = true;
		for (uint k = 0; k < _slots[i].links.size(); ++k) {
			const uint j = _slots[i].links[k];
			if (j < numSlots) {
				effect[i][j] = true;
			}
		}
	}

	// Clicking slot i is periodic with the lcm of the frame counts it touches.
	Common::Array<uint> period;
	period.resize(numSlots);
	uint32 space = 1;
	for (uint i = 0; i < numSlots; ++i) {
		uint p = 1;
		for (uint j = 0; j < numSlots; ++j) {
			if (effect[i][j]) {
				// lcm(p, numFrames[j])
				uint a = p, b = numFrames[j];
				while (b) { uint t = a % b; a = b; b = t; }
				p = p / a * numFrames[j];
			}
		}

		period[i] = p;
		if (space > 2000000u / p) {
			warning("LockPickPuzzle autoplay: search space too large");
			return;
		}

		space *= p;
	}

	Common::Array<uint> counts;
	counts.resize(numSlots);
	for (uint32 n = 0; n < space; ++n) {
		uint32 rest = n;
		for (uint i = 0; i < numSlots; ++i) {
			counts[i] = rest % period[i];
			rest /= period[i];
		}

		bool ok = true;
		for (uint j = 0; j < numSlots && ok; ++j) {
			if (_slots[j].requiredFrame < 0) {
				continue;
			}

			uint steps = 0;
			for (uint i = 0; i < numSlots; ++i) {
				if (effect[i][j]) {
					steps += counts[i];
				}
			}

			ok = (uint)((_slots[j].currentFrame + steps) % numFrames[j]) == (uint)_slots[j].requiredFrame;
		}

		if (!ok) {
			continue;
		}

		for (uint i = 0; i < numSlots; ++i) {
			for (uint c = 0; c < counts[i]; ++c) {
				_autoClicks.push_back(i);
			}
		}

		debugC(1, kDebugScene, "LockPickPuzzle autoplay: %u clicks found in a %u-state search",
			(uint)_autoClicks.size(), (uint)space);
		return;
	}

	warning("LockPickPuzzle autoplay: no click plan reaches the record's solution");
}

} // End of namespace Action
} // End of namespace Nancy
