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

#include "engines/nancy/cursor.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/input.h"
#include "engines/nancy/nancy.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/util.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/action/puzzle/electrosensorspuzzle.h"

namespace Nancy {
namespace Action {

void ElectroSensorsPuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);

	_leverCursorType = stream.readUint16LE();

	uint16 numLevers = stream.readUint16LE();
	readRectArray(stream, _leverDests, numLevers);

	uint16 numHotspots = stream.readUint16LE();
	readRectArray(stream, _leverHotspots, numHotspots);

	_unknownA = stream.readUint32LE();
	_unknownB = stream.readUint32LE();

	uint16 numFrames = stream.readUint16LE();
	readRectArray(stream, _frameSrcs, numFrames);

	_ratchetSound.readData(stream);
	_windDownSound.readData(stream);

	_solveScene.sceneID = stream.readUint16LE();
	_solveScene.frameID = stream.readUint16LE();
	if (_solveScene.sceneID == kNancy16NoScene) {
		_solveScene.sceneID = kNoScene;
	}
	_solveFlag.label = stream.readSint16LE();
	_solveFlag.flag = stream.readByte();

	_solveSound.readData(stream);

	stream.skip(2);
	readRect(stream, _exitHotspot);
	_exitCursorType = stream.readUint16LE();
	_exitScene.sceneID = stream.readUint16LE();
	if (_exitScene.sceneID == kNancy16NoScene) {
		_exitScene.sceneID = kNoScene;
	}
	_exitFlag.label = stream.readSint16LE();
	_exitFlag.flag = stream.readByte();
}

void ElectroSensorsPuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	g_nancy->_resource->loadImage(_imageName, _image);
	_image.setTransparentColor(_drawSurface.getTransparentColor());

	// Every lever starts up with its meter full, which is frame 0 and also
	// exactly what the static background already shows.
	_leverFrame.resize(_leverDests.size());
	_leverMoving.resize(_leverDests.size());
	_leverNextTick.resize(_leverDests.size());
	for (uint i = 0; i < _leverDests.size(); ++i) {
		_leverFrame[i] = 0;
		_leverMoving[i] = false;
		_leverNextTick[i] = 0;
	}

	redraw();
}

int ElectroSensorsPuzzle::leverAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _leverHotspots.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_leverHotspots[i]).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

// -- debug affordance -------------------------------------------------------------
//
// This panel is the cheapest "puzzle" in the game to automate: the win condition
// isSolved() implements is "every column pulled all the way down", the eight
// levers are independent, there is no order and no way to raise one again, so
// autoplay just pulls each one in turn. It exists mainly so a planner does not
// have to carry eight hard-coded hotspots that would silently rot if the record
// were ever re-read differently - the rects come from the record here.
//
// One lever per call so the ratchet sound is not restarted eight times in one
// frame, and only ever a lever that a click would be allowed to start.
//
// nancy_electro_autoplay defaults off; ordinary play is unchanged.
bool ElectroSensorsPuzzle::autoPlay() {
	return ConfMan.hasKey("nancy_electro_autoplay") && ConfMan.getBool("nancy_electro_autoplay");
}

void ElectroSensorsPuzzle::autoPull(uint32 now) {
	for (uint i = 0; i < _leverFrame.size(); ++i) {
		if (_leverMoving[i] || _leverFrame[i] != 0) {
			continue;
		}

		_leverMoving[i] = true;
		_leverNextTick[i] = now + _unknownB;
		playSoundBlock(_ratchetSound);
		return;
	}
}

bool ElectroSensorsPuzzle::isSolved() const {
	if (_leverFrame.empty() || _frameSrcs.empty()) {
		return false;
	}

	// GUESS: the record carries no solution array, no ordering and no per-column
	// target, and all four instances are identical, so the only win condition the
	// data can express is "every lever pulled and every meter drained" -- i.e.
	// every column sitting on the last animation frame.
	for (uint i = 0; i < _leverFrame.size(); ++i) {
		if (_leverFrame[i] != _frameSrcs.size() - 1) {
			return false;
		}
	}

	return true;
}

void ElectroSensorsPuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	for (uint i = 0; i < _leverDests.size(); ++i) {
		uint frame = _leverFrame[i];
		if (frame >= _frameSrcs.size()) {
			continue;
		}

		_drawSurface.blitFrom(_image, _frameSrcs[frame], Common::Point(_leverDests[i].left, _leverDests[i].top));
	}

	setNeedsRedraw(true);
}

SoundDescription ElectroSensorsPuzzle::playSoundBlock(const RandomSoundBlock &block) {
	SoundDescription desc;
	if (block.names.empty()) {
		return desc;
	}

	uint idx = block.names.size() == 1 ? 0 : g_nancy->_randomSource->getRandomNumber(block.names.size() - 1);
	const Common::String &name = block.names[idx];
	if (name.empty() || name == "NO SOUND") {
		return desc;
	}

	desc.name = name;
	desc.channelID = block.channel;
	desc.numLoops = block.numLoops > 0 ? block.numLoops : 1;
	desc.volume = block.volume;

	g_nancy->_sound->loadSound(desc);
	g_nancy->_sound->playSound(desc);
	return desc;
}

void ElectroSensorsPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun: {
		if (_exitRequested) {
			_state = kActionTrigger;
			break;
		}

		// Advance any lever that is mid-pull.
		const uint32 now = g_nancy->getTotalPlayTime();
		bool dirty = false;

		if (autoPlay() && _solveState == kNotSolved) {
			autoPull(now);
		}

		for (uint i = 0; i < _leverFrame.size(); ++i) {
			if (!_leverMoving[i] || now < _leverNextTick[i]) {
				continue;
			}

			++_leverFrame[i];
			dirty = true;

			// The handle has bottomed out; the power starts draining out of the
			// meter above it.
			if (_leverFrame[i] == kDrainStartFrame) {
				playSoundBlock(_windDownSound);
			}

			if (_leverFrame[i] >= _frameSrcs.size() - 1) {
				_leverFrame[i] = _frameSrcs.size() - 1;
				_leverMoving[i] = false;
			} else {
				_leverNextTick[i] = now + _unknownB;
			}
		}

		if (dirty) {
			redraw();
		}

		switch (_solveState) {
		case kNotSolved:
			if (!isSolved()) {
				break;
			}

			// The solve flag goes up when the puzzle is solved, not when the scene
			// leaves. All four instances name generic flag 1011 and scene 3871/3873/
			// 3877/3879 (byte-exact), and the two sibling records that consume 1011
			// - the SetValue that counts broken locks into player-table index 25,
			// and the EventFlags that raises EV_Solved_Electro1..4 - sit BEFORE this
			// record in every one of the four scenes' arrays. Raising the flag in
			// kActionTrigger alongside the scene change meant they never saw it:
			// records earlier in the array had already run that frame, and 1010-1059
			// are cleared on every scene change. So none of EV_Solved_Electro1..4
			// could be set anywhere in the game, and the count that opens the
			// sapphire alcove in S3875 never moved off zero.
			NancySceneState.setEventFlag(_solveFlag);

			_playingSolveSound = playSoundBlock(_solveSound);
			_solveState = kWaitForSound;
			break;
		case kWaitForSound:
			if (_playingSolveSound.name.empty() || !g_nancy->_sound->isSoundPlaying(_playingSolveSound)) {
				_state = kActionTrigger;
			}

			break;
		}

		break;
	}
	case kActionTrigger:
		if (_solveState == kWaitForSound) {
			// The solve flag went up when isSolved() first held, a frame or more
			// ago, so the siblings that consume it have had their pass. See above.
			NancySceneState.changeScene(_solveScene);
		} else {
			NancySceneState.setEventFlag(_exitFlag);
			NancySceneState.changeScene(_exitScene);
		}

		finishExecution();
		break;
	}
}

void ElectroSensorsPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _exitRequested || _solveState != kNotSolved) {
		return;
	}

	const bool click = (input.input & NancyInput::kLeftMouseButtonUp) != 0;

	// -- Pull a lever. A lever that is already moving, or already all the way
	//    down, is inert; nothing in the record suggests a way to raise one again. --
	int lever = leverAtCursor(input.mousePos);
	if (lever != -1) {
		// The id in the data is a raw Nancy13-style cursor type, which is what the
		// "set from script" path expects.
		g_nancy->_cursor->setCursorType((CursorManager::CursorType)_leverCursorType, true);

		if (click && !_leverMoving[lever] && _leverFrame[lever] == 0) {
			_leverMoving[lever] = true;
			_leverNextTick[lever] = g_nancy->getTotalPlayTime() + _unknownB;
			playSoundBlock(_ratchetSound);
			input.eatMouseInput();
		}

		return;
	}

	// -- Give up and leave. --
	if (!_exitHotspot.isEmpty() &&
			NancySceneState.getViewport().convertViewportToScreen(_exitHotspot).contains(input.mousePos)) {
		g_nancy->_cursor->setCursorType((CursorManager::CursorType)_exitCursorType, true);

		if (click) {
			_exitRequested = true;
			input.eatMouseInput();
		}
	}
}

} // End of namespace Action
} // End of namespace Nancy
