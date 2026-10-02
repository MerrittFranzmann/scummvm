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
#include "engines/nancy/graphics.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/util.h"
#include "engines/nancy/input.h"
#include "engines/nancy/cursor.h"

#include "engines/nancy/action/puzzle/whackitpuzzle.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

void WhackItPuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _imageName);
	readRect(stream, _crosshairSrc);
	_unknown31 = stream.readUint16LE();
	readRect(stream, _emptyRect);
	readRect(stream, _playArea);

	for (uint i = 0; i < 10; ++i) {
		_tuning[i] = stream.readUint32LE();
	}

	_unknownMs = stream.readUint16LE();
	_unknownOne = stream.readUint32LE();
	_pad = stream.readByte();

	Animation *anims[3] = { &_puffAnim, &_flyAnim, &_downAnim };
	for (uint i = 0; i < 3; ++i) {
		anims[i]->frameDelay = stream.readUint32LE();
		const uint16 numFrames = stream.readUint16LE();
		anims[i]->frames.resize(numFrames);
		for (uint f = 0; f < numFrames; ++f) {
			readRect(stream, anims[i]->frames[f]);
		}
	}

	_spraySound.readData(stream);
	_reviveSound.readData(stream);
	_buzzSound.readData(stream);
	_angrySound.readData(stream);
	_stungSound.readData(stream);

	_unknown437 = stream.readUint32LE();
	// A Nancy16 scene change followed by an unknown byte
	_exitScene.readData(stream);
	stream.skip(1);

	_silenceSound.readData(stream);
	stream.skip(2);
}

void WhackItPuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	g_nancy->_resource->loadImage(_imageName, _image);
	_image.setTransparentColor(_drawSurface.getTransparentColor());

	const uint32 now = g_nancy->getTotalPlayTime();
	_lastStepTime = now;

	// Bee count and speed come from the tuning block; see the header
	const uint32 count = MAX<uint32>(1, randomBetween(_tuning[8], _tuning[9]));
	const int16 beeW = _flyAnim.frames.empty() ? 0 : _flyAnim.frames[0].width();
	const int16 beeH = _flyAnim.frames.empty() ? 0 : _flyAnim.frames[0].height();

	_bees.resize(count);
	for (uint i = 0; i < count; ++i) {
		Bee &bee = _bees[i];
		bee.pos.x = _playArea.left + g_nancy->_randomSource->getRandomNumber(
			MAX<int>(0, _playArea.width() - beeW - 1));
		bee.pos.y = _playArea.top + g_nancy->_randomSource->getRandomNumber(
			MAX<int>(0, _playArea.height() - beeH - 1));

		// The data has no direction, so pick one at random. Speed is scaled down
		// by ten to give a few pixels per step.
		const int speed = MAX<int>(1, (int)randomBetween(_tuning[0], _tuning[1]) / 10);
		bee.dx = g_nancy->_randomSource->getRandomBit() ? speed : -speed;
		bee.dy = g_nancy->_randomSource->getRandomBit() ? speed : -speed;

		bee.frame = _flyAnim.frames.empty() ? 0 :
			g_nancy->_randomSource->getRandomNumber(_flyAnim.frames.size() - 1);
		bee.nextFrameTime = now + _flyAnim.frameDelay;
	}

	playSoundBlock(_buzzSound);

	redraw();
}

uint32 WhackItPuzzle::randomBetween(uint32 lo, uint32 hi) const {
	if (hi <= lo) {
		return lo;
	}

	return lo + g_nancy->_randomSource->getRandomNumber(hi - lo);
}

SoundDescription WhackItPuzzle::playSoundBlock(const RandomSoundBlock &block) {
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

void WhackItPuzzle::stepBees(uint32 now) {
	const int16 beeW = _flyAnim.frames.empty() ? 0 : _flyAnim.frames[0].width();
	const int16 beeH = _flyAnim.frames.empty() ? 0 : _flyAnim.frames[0].height();
	bool dirty = false;

	for (uint i = 0; i < _bees.size(); ++i) {
		Bee &bee = _bees[i];

		if (bee.down) {
			if (now >= bee.reviveTime) {
				bee.down = false;
				bee.frame = 0;
				bee.nextFrameTime = now + _flyAnim.frameDelay;
				playSoundBlock(_reviveSound);
				dirty = true;
			}

			continue;
		}

		// Fly. Bounce off the edges of the play area.
		bee.pos.x += bee.dx;
		bee.pos.y += bee.dy;

		if (bee.pos.x < _playArea.left) {
			bee.pos.x = _playArea.left;
			bee.dx = -bee.dx;
		} else if (bee.pos.x + beeW > _playArea.right) {
			bee.pos.x = _playArea.right - beeW;
			bee.dx = -bee.dx;
		}

		if (bee.pos.y < _playArea.top) {
			bee.pos.y = _playArea.top;
			bee.dy = -bee.dy;
		} else if (bee.pos.y + beeH > _playArea.bottom) {
			bee.pos.y = _playArea.bottom - beeH;
			bee.dy = -bee.dy;
		}

		if (!_flyAnim.frames.empty() && now >= bee.nextFrameTime) {
			bee.frame = (bee.frame + 1) % _flyAnim.frames.size();
			bee.nextFrameTime = now + _flyAnim.frameDelay;
		}

		dirty = true;
	}

	// Advance the spray puff.
	if (_puffActive && !_puffAnim.frames.empty() && now >= _puffNextFrameTime) {
		++_puffFrame;
		if (_puffFrame >= _puffAnim.frames.size()) {
			_puffActive = false;
			playSoundBlock(_silenceSound);
		} else {
			_puffNextFrameTime = now + _puffAnim.frameDelay;
		}

		dirty = true;
	}

	if (dirty) {
		redraw();
	}
}

void WhackItPuzzle::spray(const Common::Point &at, uint32 now) {
	if (_puffAnim.frames.empty()) {
		return;
	}

	// The puff frames grow; the largest one is what decides the reach, and it is
	// centred on the click.
	const Common::Rect &biggest = _puffAnim.frames[_puffAnim.frames.size() - 1];
	_puffPos.x = at.x - biggest.width() / 2;
	_puffPos.y = at.y - biggest.height() / 2;
	_puffActive = true;
	_puffFrame = 0;
	_puffNextFrameTime = now + _puffAnim.frameDelay;

	playSoundBlock(_spraySound);

	Common::Rect cloud(_puffPos.x, _puffPos.y,
		_puffPos.x + biggest.width(), _puffPos.y + biggest.height());

	const int16 beeW = _flyAnim.frames.empty() ? 0 : _flyAnim.frames[0].width();
	const int16 beeH = _flyAnim.frames.empty() ? 0 : _flyAnim.frames[0].height();

	for (uint i = 0; i < _bees.size(); ++i) {
		Bee &bee = _bees[i];
		if (bee.down) {
			continue;
		}

		Common::Rect beeRect(bee.pos.x, bee.pos.y, bee.pos.x + beeW, bee.pos.y + beeH);
		if (!cloud.intersects(beeRect)) {
			continue;
		}

		bee.down = true;
		bee.frame = 0;
		// Revive delay is in seconds in the data; zero means it gets straight back up.
		bee.reviveTime = now + randomBetween(_tuning[6], _tuning[7]) * 1000;
		playSoundBlock(_angrySound);
	}

	redraw();
}

bool WhackItPuzzle::allBeesDown() const {
	if (_bees.empty()) {
		return false;
	}

	for (uint i = 0; i < _bees.size(); ++i) {
		if (!_bees[i].down) {
			return false;
		}
	}

	return true;
}

void WhackItPuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	for (uint i = 0; i < _bees.size(); ++i) {
		const Bee &bee = _bees[i];
		const Animation &anim = bee.down ? _downAnim : _flyAnim;
		if (anim.frames.empty()) {
			continue;
		}

		const Common::Rect &src = anim.frames[MIN<uint>(bee.frame, anim.frames.size() - 1)];
		_drawSurface.blitFrom(_image, src, Common::Point(bee.pos.x, bee.pos.y));
	}

	if (_puffActive && _puffFrame < _puffAnim.frames.size()) {
		const Common::Rect &src = _puffAnim.frames[_puffFrame];
		// The frames grow around a common centre, so each one is drawn centred on
		// the point that was clicked rather than at a shared top-left.
		const Common::Rect &biggest = _puffAnim.frames[_puffAnim.frames.size() - 1];
		const int16 x = _puffPos.x + (biggest.width() - src.width()) / 2;
		const int16 y = _puffPos.y + (biggest.height() - src.height()) / 2;
		_drawSurface.blitFrom(_image, src, Common::Point(x, y));
	}

	setNeedsRedraw(true);
}

void WhackItPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun: {
		const uint32 now = g_nancy->getTotalPlayTime();

		switch (_solveState) {
		case kNotSolved:
			stepBees(now);

			if (!allBeesDown()) {
				break;
			}

			// The record has no victory sound
			_solvedTime = now;
			_solveState = kWaitForPause;
			break;
		case kWaitForPause:
			// Pause briefly so the player sees all the bees grounded before the scene changes
			if (now - _solvedTime >= 1000) {
				_state = kActionTrigger;
			}

			break;
		}

		break;
	}
	case kActionTrigger:
		playSoundBlock(_silenceSound);
		NancySceneState.changeScene(_exitScene);
		finishExecution();
		break;
	}
}

void WhackItPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _solveState != kNotSolved) {
		return;
	}

	if (!NancySceneState.getViewport().convertViewportToScreen(_playArea).contains(input.mousePos)) {
		return;
	}

	// The cursor is a CURS resource rather than the record's crosshair cell, so
	// use the generic hotspot cursor.
	g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

	if (input.input & NancyInput::kLeftMouseButtonUp) {
		// The viewport only converts rects, so go through a degenerate one.
		const Common::Rect asRect = NancySceneState.getViewport().convertScreenToViewport(
			Common::Rect(input.mousePos.x, input.mousePos.y, input.mousePos.x + 1, input.mousePos.y + 1));
		spray(Common::Point(asRect.left, asRect.top), g_nancy->getTotalPlayTime());
		input.eatMouseInput();
	}
}

} // End of namespace Action
} // End of namespace Nancy
