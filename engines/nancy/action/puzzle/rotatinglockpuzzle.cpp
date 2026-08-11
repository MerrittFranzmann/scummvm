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
// channel, uint32 (always 1), uint16 volume. Only the first name is ever used
// by the two groups this record carries, both of which hold exactly one.
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

// Nancy16 kept the puzzle - the propane cabinet lock in S4408, CAS_PrpnLck_OVL,
// four dials of ten icons each - but rewrote the record around it. The counts
// changed places (the icon count now comes first and governs the length of the
// source list, and the dial count sits after it), the two SceneChangeWithFlags
// became the game's short 7-byte "scene, frame, flag, value" block, the sound
// descriptions became the counted sound group used everywhere else in Nancy16,
// and the exit hotspot moved into the standard 23-byte action-zone tail.
//
// 562/562 bytes exact on the single record.
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

	// Eight zero bytes, then the two hotspot lists. Both hold numDials + 1
	// entries with a null in slot 0, so the wheel beside dial i is at i + 1;
	// the four real rects line up with the striped wheels the scene background
	// draws to the right of each dial window, which is what settles the
	// off-by-one. (The same bytes can be read as numDials-long lists with 24
	// and 16 bytes of padding around them - it consumes identically, and the
	// four rects that come out are the same four either way.)
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

	// An (int16 label, byte value) pair reading EV_Solved_Stakeout = 1, which is
	// not this puzzle's flag: the propane lock's own flag is the one in the
	// scene-change block below, and the stakeout is a different puzzle whose
	// flag is raised by an AR 90 in S5620. With a single record of this type
	// there is nothing to tell whether these three bytes are a second flag the
	// designer left over from a copy-paste or an unrelated scalar - 2530 is
	// equally readable as a delay in milliseconds before the solve sound, which
	// is the field the pre-Nancy16 record has in this position. Left unapplied
	// rather than guessed: raising EV_Solved_Stakeout from here would silently
	// skip stakeout content.
	stream.skip(3);

	_solveExitScene._sceneChange.sceneID = stream.readUint16LE();
	if (_solveExitScene._sceneChange.sceneID == kNancy16NoScene) {
		_solveExitScene._sceneChange.sceneID = kNoScene;
	}

	_solveExitScene._sceneChange.frameID = stream.readUint16LE();
	_solveExitScene._sceneChange.continueSceneSound = kContinueSceneSound;
	_solveExitScene._flag.label = stream.readSint16LE();
	_solveExitScene._flag.flag = stream.readByte();

	readSoundGroup(stream, _solveSound);
	_solveSoundDelay = 0;

	// The action-zone tail, the same 23-byte shape AR 166 and AR 207 use. The
	// single zone is the strip along the bottom of the viewport that gives up.
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
			_solveExitScene._sceneChange.sceneID, _solveExitScene._flag.label,
			g_nancy->getEventFlagName(_solveExitScene._flag.label).c_str(),
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

	_correctSequence.reserve(numDials);
	for (uint i = 0; i < numDials; ++i) {
		_correctSequence.push_back(stream.readByte());
	}

	// The correct sequence occupies a fixed 8-byte slot regardless of dial count.
	stream.skip(8 - numDials);

	if (isNancy10) {
		// Nancy 10 added per-puzzle cursor types for the up/down hotspots,
		// stored right after the sequence slot. A value of 0 means "use the
		// default movement cursor".
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
		_solveExitScene._sceneChange.readData(stream);
		stream.skip(2);
		_solveExitScene._flag.label = stream.readSint16LE();
		_solveExitScene._flag.flag  = stream.readByte();

		_solveSoundDelay = stream.readUint16LE();
		_solveSound.readNormal(stream);

		_exitScene._sceneChange.readData(stream);
		stream.skip(2);
		_exitScene._flag.label = stream.readSint16LE();
		_exitScene._flag.flag  = stream.readByte();

		readRect(stream, _exitHotspot);
		// 16 trailing bytes (cursor type + unused) at offset 0x317 are ignored.
	} else {
		_solveExitScene.readData(stream);
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

void RotatingLockPuzzle::stepDial(uint id, bool up) {
	g_nancy->_sound->playSound(_clickSound);

	int n = (int)_currentSequence[id] + (up ? 1 : -1);
	if (n >= (int)_iconsPerDial) {
		n = 0;
	} else if (n < 0) {
		n = (int)_iconsPerDial - 1;
	}

	_currentSequence[id] = (byte)n;
	drawDial(id);
}

// -- debug affordance ---------------------------------------------------------
//
// The propane cabinet lock in S4408. This is the cheapest of the Venice puzzles
// to automate, and everything the automation needs is BYTE-EXACT from the single
// record (562/562 bytes consumed, see readNancy16Data above): four dials, ten
// icons each, solution 3/4/4/7, and the solve branch to S4407 raising flag 2527
// EV_Solved_Propane_Lock. Nothing here is guessed from the artwork.
//
// The only randomised part of the puzzle is where the dials START - execute()'s
// kBegin rolls each one on _randomSource and, from Nancy 10 on, rerolls while
// the roll equals that dial's solution, so no dial is ever already correct and
// the board is always exactly four dials away from open. So the whole "solver"
// is: walk each dial to its target, taking whichever way round the ten-icon ring
// is shorter.
//
// Two things this deliberately does NOT do:
//
//   * It does not synthesise clicks or consult _upHotspots/_downHotspots. Those
//     two lists are the one part of the record whose reading is INFERRED - they
//     hold numDials + 1 entries and the off-by-one is settled by matching the
//     rects against the wheels the background art draws, not by anything in the
//     data. Driving the dials through stepDial() instead means the hook stays
//     correct even if that reading is ever revised, and means a failure here can
//     only ever be a failure of the solve logic, never of the hotspot map.
//   * It does not bypass the click sound. handleInput() will not accept a second
//     click while _clickSound is still playing, so autoStep() takes that same
//     gate: the hook moves the dials at exactly the rate a human at the mouse
//     could, and the audio comes out of the run sounding like ordinary play.
//
// One dial, one notch per call. Worst case is four dials x five notches = 20
// accepted clicks.
//
// nancy_propane_autoplay defaults off; ordinary play is unchanged.
bool RotatingLockPuzzle::autoPlay() {
	return ConfMan.hasKey("nancy_propane_autoplay") && ConfMan.getBool("nancy_propane_autoplay");
}

void RotatingLockPuzzle::autoStep() {
	// The same gate a real click passes through in handleInput().
	if (g_nancy->_sound->isSoundPlaying(_clickSound)) {
		return;
	}

	const int ring = (int)_iconsPerDial;
	if (ring <= 0) {
		return;
	}

	for (uint i = 0; i < _correctSequence.size(); ++i) {
		if (_currentSequence[i] == _correctSequence[i]) {
			continue;
		}

		// Clicks needed going up vs going down; take the shorter way round.
		const int stepsUp = (((int)_correctSequence[i] - (int)_currentSequence[i]) % ring + ring) % ring;
		stepDial(i, stepsUp * 2 <= ring);
		++_autoClicks;
		return;
	}
}

void RotatingLockPuzzle::execute() {
	const bool isNancy10 = g_nancy->getGameType() >= kGameTypeNancy10;

	switch (_state) {
	case kBegin:
		init();
		registerGraphics();

		NancySceneState.setNoHeldItem();

		for (uint i = 0; i < _correctSequence.size(); ++i) {
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
			if (autoPlay()) {
				autoStep();
			}

			if (!isSolved()) {
				return;
			}

			if (autoPlay()) {
				debugC(1, kDebugScene, "RotatingLockPuzzle autoplay: solved in %u notches, "
						"raising flag %d (%s), scene %u",
						_autoClicks, _solveExitScene._flag.label,
						g_nancy->getEventFlagName(_solveExitScene._flag.label).c_str(),
						_solveExitScene._sceneChange.sceneID);
			}

			NancySceneState.setEventFlag(_solveExitScene._flag);
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
			if (!g_nancy->_sound->isSoundPlaying(_solveSound)) {
				_state = kActionTrigger;
			}

			break;
		}
		break;
	case kActionTrigger:
		g_nancy->_sound->stopSound(_clickSound);
		g_nancy->_sound->stopSound(_solveSound);

		if (_solveState == kNotSolved)
			_exitScene.execute();
		else
			NancySceneState.changeScene(_solveExitScene._sceneChange);

		finishExecution();
	}
}

void RotatingLockPuzzle::handleInput(NancyInput &input) {
	if (_solveState != kNotSolved) {
		return;
	}

	if (NancySceneState.getViewport().convertViewportToScreen(_exitHotspot).contains(input.mousePos)) {
		g_nancy->_cursor->setCursorType(_exitCursorType ?
			(CursorManager::CursorType)_exitCursorType : g_nancy->_cursor->_puzzleExitCursor);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			_state = kActionTrigger;
		}

		return;
	}

	for (uint i = 0; i < _upHotspots.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_upHotspots[i]).contains(input.mousePos)) {
			// The dial cursors use the idle (non-highlighted) sprite variant
			g_nancy->_cursor->setCursorType(_upCursorType, true, false);

			if (!g_nancy->_sound->isSoundPlaying(_clickSound) && input.input & NancyInput::kLeftMouseButtonUp) {
				stepDial(i, true);
			}

			return;
		}
	}

	for (uint i = 0; i < _downHotspots.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_downHotspots[i]).contains(input.mousePos)) {
			g_nancy->_cursor->setCursorType(_downCursorType, true, false);

			if (!g_nancy->_sound->isSoundPlaying(_clickSound) && input.input & NancyInput::kLeftMouseButtonUp) {
				stepDial(i, false);
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
