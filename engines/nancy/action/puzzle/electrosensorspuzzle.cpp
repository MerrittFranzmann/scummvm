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

	// Every lever starts up with its meter full, which is frame 0
	_leverFrame.resize(_leverDests.size());
	_leverMoving.resize(_leverDests.size());
	_leverNextTick.resize(_leverDests.size());
	for (uint i = 0; i < _leverDests.size(); ++i) {
		_leverFrame[i] = 0;
		_leverMoving[i] = false;
		_leverNextTick[i] = 0;
	}

	// The levers must be pulled in a random order. A wrong pull raises the
	// previously lowered handles but keeps the order.
	_leverOrder.resize(_leverDests.size());
	for (uint i = 0; i < _leverOrder.size(); ++i) _leverOrder[i] = i;
	for (uint i = _leverOrder.size(); i > 1; --i) {
		const uint other = g_nancy->_randomSource->getRandomNumber(i - 1);
		SWAP(_leverOrder[i - 1], _leverOrder[other]);
	}
	_nextLever = 0;
	_resetting = false;
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

void ElectroSensorsPuzzle::pullLever(uint lever, uint32 now) {
	if (_resetting || _nextLever >= _leverOrder.size()) return;
	if (lever == _leverOrder[_nextLever]) {
		++_nextLever;
		_leverMoving[lever] = true;
		_leverNextTick[lever] = now + _unknownB;
	} else {
		_nextLever = 0;
		_resetting = true;
		_leverFrame[lever] = kDrainStartFrame - 1;
		for (uint i = 0; i < _leverFrame.size(); ++i) {
			_leverMoving[i] = _leverFrame[i] != 0;
			_leverNextTick[i] = now + _unknownA;
		}
	}
	playSoundBlock(_ratchetSound);
}

void ElectroSensorsPuzzle::autoPull(uint32 now) {
	for (uint i = 0; i < _leverMoving.size(); ++i)
		if (_leverMoving[i]) return;
	if (!_resetting && _nextLever < _leverOrder.size())
		pullLever(_leverOrder[_nextLever], now);
}

bool ElectroSensorsPuzzle::isSolved() const {
	if (_resetting || _nextLever != _leverOrder.size() || _leverFrame.empty() || _frameSrcs.empty()) {
		return false;
	}

	// Every meter must finish draining after the full correct sequence.
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

		for (uint i = 0; i < _leverFrame.size(); ++i) {
			if (!_leverMoving[i] || now < _leverNextTick[i]) {
				continue;
			}

			if (_resetting) {
				if (_leverFrame[i]) --_leverFrame[i];
				_leverMoving[i] = _leverFrame[i] != 0;
				_leverNextTick[i] = now + _unknownA;
				dirty = true;
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

		if (_resetting) {
			bool moving = false;
			for (uint i = 0; i < _leverMoving.size(); ++i) moving |= _leverMoving[i];
			if (!moving) _resetting = false;
		}
		if (dirty) {
			redraw();
		}

		switch (_solveState) {
		case kNotSolved:
			if (!isSolved()) {
				break;
			}

			// Raise the solve flag now rather than with the scene change. The sibling
			// records that consume it come earlier in the scene's record list, and the
			// flag is cleared on scene change, so they would otherwise never see it.
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
			// The solve flag was already raised when the puzzle was solved
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

	// Lowered levers remain inert until a wrong pull resets the sequence.
	int lever = leverAtCursor(input.mousePos);
	if (lever != -1) {
		// The data holds a raw Nancy13-style cursor type
		g_nancy->_cursor->setCursorType((CursorManager::CursorType)_leverCursorType, true);

		if (click && !_resetting && !_leverMoving[lever] && _leverFrame[lever] == 0) {
			pullLever(lever, g_nancy->getTotalPlayTime());
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
