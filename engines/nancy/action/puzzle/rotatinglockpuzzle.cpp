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

#include "engines/nancy/nancy.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/input.h"
#include "engines/nancy/util.h"

#include "engines/nancy/action/puzzle/rotatinglockpuzzle.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

// The Nancy16 counted sound group: uint16 count, count x char[33], uint16
// channel, uint32, uint16 volume. Only the first name is used.
static void readSoundGroup(Common::SeekableReadStream &stream, SoundDescription &desc) {
	RandomSoundBlock block;
	block.readData(stream);

	if (block.names.empty()) {
		return;
	}

	desc.name = block.names[0];
	desc.channelID = block.channel;
	desc.numLoops = block.numLoops > 0 ? block.numLoops : 1;
	desc.volume = block.volume;
}

void RotatingLockPuzzle::init() {
	_drawSurface.create(_screenPosition.width(), _screenPosition.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	setTransparent(true);

	g_nancy->_resource->loadImage(_imageName, _image);
}

// Nancy16 rewrote the record: the icon count now comes before the dial count,
// the scene changes use the short 7-byte "scene, frame, flag, value" block,
// the sounds are counted sound groups, and the exit hotspot is in the 23-byte
// action-zone tail.
void RotatingLockPuzzle::readNancy16Data(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);

	_iconsPerDial = stream.readUint16LE();
	readRectArray(stream, _srcRects, _iconsPerDial);

	const uint numDials = stream.readUint16LE();
	readRectArray(stream, _destRects, numDials);
	for (uint i = 0; i < numDials; ++i) {
		if (i == 0) {
			_screenPosition = _destRects[i];
		} else {
			_screenPosition.extend(_destRects[i]);
		}
	}

	// Eight unused bytes, then the two hotspot lists. Both hold numDials + 1
	// entries with an empty one in slot 0, so dial i uses entry i + 1.
	stream.skip(8);

	Common::Array<Common::Rect> allUp, allDown;
	readRectArray(stream, allUp, numDials + 1);
	readRectArray(stream, allDown, numDials + 1);
	for (uint i = 0; i < numDials; ++i) {
		_upHotspots.push_back(allUp[i + 1]);
		_downHotspots.push_back(allDown[i + 1]);
	}

	_correctSequence.resize(numDials);
	for (uint i = 0; i < numDials; ++i) {
		_correctSequence[i] = stream.readByte();
	}

	stream.skip(8 - numDials);

	_upCursorType = (CursorManager::CursorType)stream.readUint16LE();
	_downCursorType = (CursorManager::CursorType)stream.readUint16LE();

	readSoundGroup(stream, _clickSound);

	// Unknown. Reads as a flag label and value, but not this puzzle's flag, so
	// it is not applied.
	stream.skip(3);

	_solveScene._sceneChange.sceneID = stream.readUint16LE();
	if (_solveScene._sceneChange.sceneID == kNancy16NoScene) {
		_solveScene._sceneChange.sceneID = kNoScene;
	}

	_solveScene._sceneChange.frameID = stream.readUint16LE();
	_solveScene._sceneChange.continueSceneSound = kContinueSceneSound;
	_solveScene._flag.label = stream.readSint16LE();
	_solveScene._flag.flag = stream.readByte();

	readSoundGroup(stream, _solveSound);
	_solveSoundDelay = 0;

	// The action-zone tail. The single zone is the strip along the bottom of the
	// viewport that gives up.
	const uint16 numZones = stream.readUint16LE();
	for (uint i = 0; i < numZones; ++i) {
		Common::Rect hotspot;
		readRect(stream, hotspot);
		const uint16 cursorType = stream.readUint16LE();
		uint16 sceneID = stream.readUint16LE();
		if (sceneID == kNancy16NoScene) {
			sceneID = kNoScene;
		}

		const int16 flagLabel = stream.readSint16LE();
		const byte flagValue = stream.readByte();

		if (i == 0) {
			_exitHotspot = hotspot;
			_exitCursorType = cursorType;
			_exitScene._sceneChange.sceneID = sceneID;
			_exitScene._sceneChange.continueSceneSound = kContinueSceneSound;
			_exitScene._flag.label = flagLabel;
			_exitScene._flag.flag = flagValue;
		}
	}

	debugC(1, kDebugScene, "RotatingLockPuzzle: \"%s\", %u dials of %u icons, solution %u/%u/%u/%u, "
			"solve scene %u flag %d (%s), exit scene %u",
			_imageName.toString().c_str(), numDials, _iconsPerDial,
			numDials > 0 ? _correctSequence[0] : 0, numDials > 1 ? _correctSequence[1] : 0,
			numDials > 2 ? _correctSequence[2] : 0, numDials > 3 ? _correctSequence[3] : 0,
			_solveScene._sceneChange.sceneID, _solveScene._flag.label,
			g_nancy->getEventFlagName(_solveScene._flag.label).c_str(),
			_exitScene._sceneChange.sceneID);
}

void RotatingLockPuzzle::readData(Common::SeekableReadStream &stream) {
	if (g_nancy->getGameType() >= kGameTypeNancy16) {
		readNancy16Data(stream);
		return;
	}

	const bool isNancy10 = g_nancy->getGameType() >= kGameTypeNancy10;

	readFilename(stream, _imageName);

	uint numDials = stream.readUint16LE();

	_iconsPerDial = isNancy10 ? stream.readUint16LE() : 10;

	const uint numSrcRects = isNancy10 ? 12 : 10;
	_srcRects.reserve(numSrcRects);
	for (uint i = 0; i < numSrcRects; ++i) {
		_srcRects.push_back(Common::Rect());
		readRect(stream, _srcRects.back());
	}

	_destRects.reserve(numDials);
	for (uint i = 0; i < numDials; ++i) {
		_destRects.push_back(Common::Rect());
		readRect(stream, _destRects.back());

		if (i == 0) {
			_screenPosition = _destRects.back();
		} else {
			_screenPosition.extend(_destRects.back());
		}
	}

	stream.skip((8 - numDials) * 16);

	_upHotspots.reserve(numDials);
	for (uint i = 0; i < numDials; ++i) {
		_upHotspots.push_back(Common::Rect());
		readRect(stream, _upHotspots.back());
	}

	_downHotspots.reserve(numDials);
	stream.skip((8 - numDials) * 16);

	for (uint i = 0; i < numDials; ++i) {
		_downHotspots.push_back(Common::Rect());
		readRect(stream, _downHotspots.back());
	}

	stream.skip((8 - numDials) * 16);

	if (g_nancy->getGameType() >= kGameTypeNancy14) {
		// Nancy 14 added per-dial starting positions, stored in a fixed
		// 8-byte slot before the solution
		_startSequence.reserve(numDials);
		for (uint i = 0; i < numDials; ++i) {
			_startSequence.push_back(stream.readByte());
		}

		stream.skip(8 - numDials);
	}

	_correctSequence.reserve(numDials);
	for (uint i = 0; i < numDials; ++i) {
		_correctSequence.push_back(stream.readByte());
	}

	// The correct sequence occupies a fixed 8-byte slot regardless of dial count.
	stream.skip(8 - numDials);

	if (isNancy10) {
		// Nancy 10 added per-puzzle cursor types for the up/down hotspots,
		// stored right after the sequence slot. A value of 0 means "use the
		// default movement cursor"; Nancy 13+ defaults to the blue puzzle
		// up/down cursors instead.
		if (g_nancy->getGameType() >= kGameTypeNancy13) {
			_upCursorType = CursorManager::kNancy13PuzzleMoveUp;
			_downCursorType = CursorManager::kNancy13PuzzleMoveDown;
		}

		int16 upType = stream.readSint16LE();
		int16 downType = stream.readSint16LE();
		if (upType != 0)
			_upCursorType = (CursorManager::CursorType)upType;
		if (downType != 0)
			_downCursorType = (CursorManager::CursorType)downType;
	}

	_clickSound.readNormal(stream);

	if (isNancy10) {
		// Nancy 10 splits the old SceneChangeWithFlag (25 bytes with embedded
		// flag) into a 20-byte SceneChangeDescription + 2-byte pause tail,
		// with the event flag stored as a separate (label, value) pair.
		_solveScene._sceneChange.readData(stream);
		stream.skip(2);
		_solveScene._flag.label = stream.readSint16LE();
		_solveScene._flag.flag  = stream.readByte();

		_solveSoundDelay = stream.readUint16LE();
		_solveSound.readNormal(stream);

		_exitScene._sceneChange.readData(stream);
		stream.skip(2);
		_exitScene._flag.label = stream.readSint16LE();
		_exitScene._flag.flag  = stream.readByte();

		readRect(stream, _exitHotspot);
	} else {
		_solveScene.readData(stream);
		_solveSoundDelay = stream.readUint16LE();
		_solveSound.readNormal(stream);

		_exitScene.readData(stream);
		readRect(stream, _exitHotspot);
	}
}

bool RotatingLockPuzzle::isSolved() const {
	if (_currentSequence.size() < _correctSequence.size()) {
		return false;
	}

	for (uint i = 0; i < _correctSequence.size(); ++i) {
		if (_currentSequence[i] != _correctSequence[i]) {
			return false;
		}
	}

	return true;
}

void RotatingLockPuzzle::execute() {
	const bool isNancy10 = g_nancy->getGameType() >= kGameTypeNancy10;

	switch (_state) {
	case kBegin:
		init();
		registerGraphics();

		NancySceneState.setNoHeldItem();

		for (uint i = 0; i < _correctSequence.size(); ++i) {
			if (!_startSequence.empty() && _startSequence[i] != kRandomStart) {
				_currentSequence.push_back(_startSequence[i]);
				drawDial(i);
				continue;
			}

			byte v = g_nancy->_randomSource->getRandomNumber(_iconsPerDial - 1);
			// Nancy 10 rerolls until the starting value differs from the
			// solution so the puzzle never appears already-solved.
			while (isNancy10 && v == _correctSequence[i])
				v = g_nancy->_randomSource->getRandomNumber(_iconsPerDial - 1);
			_currentSequence.push_back(v);
			drawDial(i);
		}

		g_nancy->_sound->loadSound(_clickSound);
		g_nancy->_sound->loadSound(_solveSound);
		_state = kRun;
		// fall through
	case kRun:
		switch (_solveState) {
		case kNotSolved:

			if (!isSolved()) {
				return;
			}

			_solveSoundPlayTime = g_nancy->getTotalPlayTime() + _solveSoundDelay * 1000;
			_solveState = kPlaySound;
			// fall through
		case kPlaySound:
			if (g_nancy->getTotalPlayTime() <= _solveSoundPlayTime) {
				break;
			}

			g_nancy->_sound->playSound(_solveSound);
			_solveState = kWaitForSound;
			break;
		case kWaitForSound:
			if (!isSolveSoundPlaying()) {
				_state = kActionTrigger;
			}

			break;
		}
		break;
	case kActionTrigger:
		g_nancy->_sound->stopSound(_clickSound);
		g_nancy->_sound->stopSound(_solveSound);

		// The solve event flag is set here, together with the scene change, and
		// not when the solution is first detected: a record whose own dependency
		// tests that flag would stop being evaluated before it could trigger.
		if (_solveState == kNotSolved)
			_exitScene.execute();
		else
			_solveScene.execute();

		finishExecution();
	}
}

void RotatingLockPuzzle::handleInput(NancyInput &input) {
	if (_solveState != kNotSolved) {
		return;
	}

	if (hoverExitHotspot(input)) {
		if (input.input & NancyInput::kLeftMouseButtonUp) {
			_state = kActionTrigger;
		}

		return;
	}

	for (uint i = 0; i < _upHotspots.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_upHotspots[i]).contains(input.mousePos)) {
			// The dial cursors use the idle (non-highlighted) sprite variant
			g_nancy->_cursor->setCursorType(_upCursorType, true, false);

			// The dials accept clicks as fast as they come in; the click sound
			// restarts with every one
			if (input.input & NancyInput::kLeftMouseButtonUp) {
				g_nancy->_sound->loadSound(_clickSound, nullptr, true);
				g_nancy->_sound->playSound(_clickSound);

				int n = _currentSequence[i] + 1;
				if (n >= (int)_iconsPerDial)
					n = 0;
				_currentSequence[i] = (byte)n;
				drawDial(i);
			}

			return;
		}
	}

	for (uint i = 0; i < _downHotspots.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_downHotspots[i]).contains(input.mousePos)) {
			g_nancy->_cursor->setCursorType(_downCursorType, true, false);

			if (input.input & NancyInput::kLeftMouseButtonUp) {
				g_nancy->_sound->loadSound(_clickSound, nullptr, true);
				g_nancy->_sound->playSound(_clickSound);

				int n = (int)_currentSequence[i] - 1;
				if (n < 0)
					n = (int)_iconsPerDial - 1;
				_currentSequence[i] = (byte)n;
				drawDial(i);
			}

			return;
		}
	}
}
void RotatingLockPuzzle::drawDial(uint id) {
	Common::Point destPoint(_destRects[id].left - _screenPosition.left, _destRects[id].top - _screenPosition.top);
	_drawSurface.blitFrom(_image, _srcRects[_currentSequence[id]], destPoint);

	_needsRedraw = true;
}

} // End of namespace Action
} // End of namespace Nancy
