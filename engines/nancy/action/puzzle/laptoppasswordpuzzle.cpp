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

#include "common/random.h"

#include "graphics/font.h"

#include "engines/nancy/cursor.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/input.h"
#include "engines/nancy/nancy.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/ttffont.h"
#include "engines/nancy/util.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/action/puzzle/laptoppasswordpuzzle.h"

namespace Nancy {
namespace Action {

void LaptopPasswordPuzzle::readData(Common::SeekableReadStream &stream) {
	stream.skip(33);	// Empty: this record has no atlas, it only draws text

	_unknownA = stream.readUint32LE();

	readFilename(stream, _fontName);

	_unknownB = stream.readUint16LE();
	_cursorBlinkTime = stream.readUint16LE();
	_unknownC = stream.readUint16LE();
	_unknownD = stream.readByte();
	_unknownE = stream.readByte();

	readFilename(stream, _unknownStrA);
	readFilename(stream, _unknownStrB);

	_boxCursorType = stream.readUint16LE();

	readFilename(stream, _unknownStrC);

	_keypressSound.readData(stream);

	_solveScene.sceneID = stream.readUint16LE();
	_solveScene.frameID = stream.readUint16LE();
	if (_solveScene.sceneID == kNancy16NoScene) {
		_solveScene.sceneID = kNoScene;
	}
	_solveFlag.label = stream.readSint16LE();
	_solveFlag.flag = stream.readByte();

	_solveSound.readData(stream);

	readRect(stream, _exitHotspot);
	_exitCursorType = stream.readUint16LE();
	_exitScene.sceneID = stream.readUint16LE();
	if (_exitScene.sceneID == kNancy16NoScene) {
		_exitScene.sceneID = kNoScene;
	}
	_exitFlag.label = stream.readSint16LE();
	_exitFlag.flag = stream.readByte();

	stream.skip(1);
	_unknownF = stream.readUint16LE();

	readRect(stream, _boxRect);

	const uint16 numAnswers = stream.readUint16LE();
	_answers.resize(numAnswers);
	for (uint i = 0; i < numAnswers; ++i) {
		readFilename(stream, _answers[i]);
	}

	_answerFlag.label = stream.readSint16LE();
	_unknownG = stream.readSint16LE();

	_correctSound.readData(stream);
	_wrongSound.readData(stream);
}

void LaptopPasswordPuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	_nextBlink = g_nancy->getTotalPlayTime() + _cursorBlinkTime;

	redraw();
}

bool LaptopPasswordPuzzle::isAnswerCorrect() const {
	for (uint i = 0; i < _answers.size(); ++i) {
		if (_answers[i].equalsIgnoreCase(_typed)) {
			return true;
		}
	}

	return false;
}

void LaptopPasswordPuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	const Graphics::Font *font = g_nancy->_ttfFonts.get(_fontName, false);
	if (font) {
		Common::String line = _typed;
		if (_cursorVisible && _solveState == kTyping) {
			line += '_';
		}

		// GUESS: the record carries no text colour, and none of its unidentified
		// scalars is a valid FONT registry colour id (those run 0-8). Black reads
		// against the laptop's pale green field.
		const uint32 colour = _drawSurface.format.RGBToColor(0, 0, 0);

		// The authored box is 18px tall; centre the line in it rather than
		// assuming the font matches the box height exactly.
		const int y = _boxRect.top + (_boxRect.height() - font->getFontHeight()) / 2;
		font->drawString(&_drawSurface, line, _boxRect.left, y, _boxRect.width(),
			colour, Graphics::kTextAlignLeft);
	}

	setNeedsRedraw(true);
}

SoundDescription LaptopPasswordPuzzle::playSoundBlock(const RandomSoundBlock &block) {
	SoundDescription desc;
	if (block.names.empty()) {
		return desc;
	}

	uint idx = block.names.size() == 1 ? 0 : g_nancy->_randomSource->getRandomNumber(block.names.size() - 1);
	const Common::String &name = block.names[idx];
	if (name.empty() || name.equalsIgnoreCase("silence") || name == "NO SOUND") {
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

void LaptopPasswordPuzzle::execute() {
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

		const uint32 now = g_nancy->getTotalPlayTime();

		switch (_solveState) {
		case kTyping:
			if (now >= _nextBlink) {
				_cursorVisible = !_cursorVisible;
				_nextBlink = now + _cursorBlinkTime;
				redraw();
			}

			break;
		case kWaitForCorrect:
			if (_playingSound.name.empty() || !g_nancy->_sound->isSoundPlaying(_playingSound)) {
				_playingSound = playSoundBlock(_solveSound);
				_solveState = kWaitForSolve;
			}

			break;
		case kWaitForSolve:
			if (_playingSound.name.empty() || !g_nancy->_sound->isSoundPlaying(_playingSound)) {
				_state = kActionTrigger;
			}

			break;
		}

		break;
	}
	case kActionTrigger:
		if (_solveState == kWaitForSolve) {
			NancySceneState.setEventFlag(_answerFlag);
			NancySceneState.setEventFlag(_solveFlag);
			NancySceneState.changeScene(_solveScene);
		} else {
			NancySceneState.setEventFlag(_exitFlag);
			NancySceneState.changeScene(_exitScene);
		}

		finishExecution();
		break;
	}
}

void LaptopPasswordPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _exitRequested || _solveState != kTyping) {
		return;
	}

	if (!_boxRect.isEmpty() &&
			NancySceneState.getViewport().convertViewportToScreen(_boxRect).contains(input.mousePos)) {
		// The id in the data is a raw Nancy13-style cursor type, which is what the
		// "set from script" path expects.
		g_nancy->_cursor->setCursorType((CursorManager::CursorType)_boxCursorType, true);
	}

	// The record's own exit hotspot is empty in the one record of this type, so
	// this never fires; kept because the field is real and a second record could
	// use it. Scene S2929's own "Scene Change with Hotspot" record owns the real
	// back button.
	if (!_exitHotspot.isEmpty() &&
			NancySceneState.getViewport().convertViewportToScreen(_exitHotspot).contains(input.mousePos)) {
		g_nancy->_cursor->setCursorType((CursorManager::CursorType)_exitCursorType, true);

		if (input.input & NancyInput::kLeftMouseButtonUp) {
			_exitRequested = true;
			input.eatMouseInput();
			return;
		}
	}

	bool dirty = false;

	for (uint i = 0; i < input.otherKbdInput.size(); ++i) {
		const Common::KeyState &key = input.otherKbdInput[i];

		if (key.keycode == Common::KEYCODE_BACKSPACE) {
			if (!_typed.empty()) {
				_typed.deleteLastChar();
				playSoundBlock(_keypressSound);
				dirty = true;
			}

			continue;
		}

		if (key.keycode == Common::KEYCODE_RETURN || key.keycode == Common::KEYCODE_KP_ENTER) {
			// GUESS: the record has a pair of bytes (both 1) where the Nancy9
			// QuizPuzzle keeps "skip empty on enter" and "auto-check"; the mapping
			// is not certain, so this uses the conventional password-box
			// behaviour of checking only on Enter, and ignoring an empty box.
			if (_typed.empty()) {
				continue;
			}

			if (isAnswerCorrect()) {
				_answerFlag.flag = 1;
				_playingSound = playSoundBlock(_correctSound);
				_solveState = kWaitForCorrect;
				_cursorVisible = false;
			} else {
				playSoundBlock(_wrongSound);
				_typed.clear();
			}

			dirty = true;
			break;
		}

		if (key.ascii >= 32 && key.ascii < 127 && _typed.size() < kMaxLength) {
			_typed += (char)key.ascii;
			playSoundBlock(_keypressSound);
			dirty = true;
		}
	}

	if (dirty) {
		_cursorVisible = _solveState == kTyping;
		_nextBlink = g_nancy->getTotalPlayTime() + _cursorBlinkTime;
		redraw();
	}
}

} // End of namespace Action
} // End of namespace Nancy
