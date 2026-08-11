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

#include "engines/nancy/nancy.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/util.h"

#include "engines/nancy/action/textscroll16.h"

#include "engines/nancy/state/scene.h"

namespace Nancy {
namespace Action {

void Nancy16TextScroll::readData(Common::SeekableReadStream &stream) {
	if (_hasLeadingField) {
		_leading = stream.readSint32LE();
	}

	_startY = stream.readSint32LE();
	readRect(stream, _dest);

	_unknownFloat = stream.readFloatLE();
	_scrollSpeed = stream.readFloatLE();

	_fontID = stream.readUint32LE();
	_colourID = stream.readUint32LE();

	_useAutotextChunk = stream.readByte();
	_unknownByte = stream.readByte();

	readFilename(stream, _textKey);

	_doneFlag.label = stream.readSint16LE();
	_doneFlag.flag = stream.readByte();
}

Common::String Nancy16TextScroll::getRecordExtraInfo() const {
	return Common::String::format("key %s, font %u, colour %u, box (%d, %d)-(%d, %d), start y %d, speed %g",
		_textKey.c_str(), _fontID, _colourID,
		_dest.left, _dest.top, _dest.right, _dest.bottom, _startY, _scrollSpeed);
}

// The whole string is laid out once into a surface as tall as it needs to be,
// and the visible window is blitted out of it. Re-laying out the credits every
// frame for the two minutes they take to roll would be pure waste.
void Nancy16TextScroll::buildTextSurface() {
	// _useAutotextChunk is 1 in every shipped record; the texts these name are
	// all in AUTOTEXT, never CONVO. An unresolvable key draws nothing rather
	// than drawing its own name.
	Common::String text = _useAutotextChunk ?
		resolveSubtitleText(_textKey, Common::String(), "AUTOTEXT") : _textKey;

	if (text.empty()) {
		_textHeight = 0;
		return;
	}

	// GUESS: the record carries no alignment field, but the strings do - CREDITS
	// opens "<f8><jc>". The shared markup parser only recognises a letter plus
	// digits, so a two-letter code is neither understood nor stripped and would
	// otherwise be printed. Both halves of that are handled here, for this record
	// only, rather than by teaching every text path a new tag.
	Graphics::TextAlign align = Graphics::kTextAlignLeft;
	static const struct { const char *code; Graphics::TextAlign align; } kJustifyCodes[] = {
		{ "<jc>", Graphics::kTextAlignCenter },
		{ "<JC>", Graphics::kTextAlignCenter },
		{ "<jr>", Graphics::kTextAlignRight },
		{ "<JR>", Graphics::kTextAlignRight },
		{ "<jl>", Graphics::kTextAlignLeft },
		{ "<JL>", Graphics::kTextAlignLeft }
	};

	for (uint i = 0; i < ARRAYSIZE(kJustifyCodes); ++i) {
		const char *code = kJustifyCodes[i].code;
		size_t at = text.find(code);
		while (at != Common::String::npos) {
			align = kJustifyCodes[i].align;
			text.erase(at, 4);
			at = text.find(code);
		}
	}

	const int width = MAX<int>(1, _dest.width());
	int height = MAX<int>(64, _clipped.height());

	// drawStyledTextToSurface reports the height the layout needed even when the
	// surface was too short to hold it, so at most one retry is ever needed. The
	// third is only there so a pathological string cannot loop.
	for (uint attempt = 0; attempt < 3; ++attempt) {
		_textSurface.create(width, height, g_nancy->_graphics->getInputPixelFormat());
		_textSurface.clear(g_nancy->_graphics->getTransColor());

		_textHeight = drawStyledTextToSurface(_textSurface, text, (int)_fontID, (int)_colourID,
			0, 0, width, align);

		if (_textHeight <= height) {
			break;
		}

		height = _textHeight + 8;
	}
}

void Nancy16TextScroll::init() {
	_clipped = _dest;
	_clipped.clip(NancySceneState.getViewport().getBounds());

	if (_clipped.isEmpty()) {
		return;
	}

	_drawSurface.create(_clipped.width(), _clipped.height(), g_nancy->_graphics->getInputPixelFormat());
	moveTo(_clipped);
	setTransparent(true);
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	buildTextSurface();

	_curY = (float)_startY;
	_initialised = true;
	setVisible(true);
	redraw();

	RenderObject::init();
}

void Nancy16TextScroll::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	_needsRedraw = true;

	if (!_textHeight || _textSurface.w == 0) {
		return;
	}

	// The box may have been clipped at the top (the credits box starts at -15),
	// so the text's position is tracked in viewport space and converted here.
	int dstY = (int)_curY - _clipped.top;
	Common::Rect src(0, 0, (int16)_textSurface.w, (int16)MIN<int>(_textHeight, _textSurface.h));

	if (dstY < 0) {
		src.top = -dstY;
		dstY = 0;
	}

	if (src.top >= src.bottom || dstY >= (int)_drawSurface.h) {
		return;
	}

	if (dstY + src.height() > (int)_drawSurface.h) {
		src.bottom = src.top + ((int16)_drawSurface.h - dstY);
	}

	const int dstX = _dest.left - _clipped.left;
	if (src.isEmpty() || dstX >= (int)_drawSurface.w) {
		return;
	}

	if (dstX + src.width() > (int)_drawSurface.w) {
		src.right = src.left + ((int16)_drawSurface.w - dstX);
	}

	_drawSurface.blitFrom(_textSurface, src, Common::Point(dstX, dstY));
}

void Nancy16TextScroll::raiseDoneFlag() {
	if (!_scrollDone) {
		_scrollDone = true;
		NancySceneState.setEventFlag(_doneFlag);
	}
}

void Nancy16TextScroll::updateGraphics() {
	if (!_initialised) {
		return;
	}

	// The costume-shop and kiosk price tags are gated on the per-scene rollover
	// flag their neighbouring type 53 record raises, so the label has to come and
	// go with its dependencies rather than latch on the first time they are met.
	if (isVisible() != _isActive) {
		setVisible(_isActive);
	}

	if (!_isActive || _scrollDone || _scrollSpeed == 0.0f) {
		return;
	}

	const uint32 now = g_nancy->getTotalPlayTime();
	if (now <= _lastTick) {
		return;
	}

	_curY += _scrollSpeed * (float)(now - _lastTick) / 1000.0f;
	_lastTick = now;
	redraw();

	// Done once the text has left the box on the side it is travelling towards.
	if (_scrollSpeed < 0.0f) {
		if (_curY + (float)_textHeight <= (float)_clipped.top) {
			raiseDoneFlag();
		}
	} else if (_curY >= (float)_clipped.bottom) {
		raiseDoneFlag();
	}
}

void Nancy16TextScroll::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_lastTick = g_nancy->getTotalPlayTime();
		_state = kRun;

		// Nothing to wait for when the text does not move.
		if (_scrollSpeed == 0.0f) {
			raiseDoneFlag();
		}

		// fall through
	case kRun:
		break;
	case kActionTrigger:
		finishExecution();
		break;
	}
}

void BeginNamedStream::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _sceneName);
	_frameID = stream.readUint16LE();
	_continueSceneSound = stream.readUint16LE();
	readFilename(stream, _streamName);

	for (uint i = 0; i < 4; ++i) {
		_mode[i] = stream.readByte();
	}

	_param = stream.readUint32LE();

	// "s2963" and friends; every one of the 43 resolves to a scene that exists.
	if (_sceneName.size() > 1 && (_sceneName[0] == 's' || _sceneName[0] == 'S')) {
		uint id = 0;
		bool ok = true;
		for (uint i = 1; i < _sceneName.size(); ++i) {
			if (!Common::isDigit(_sceneName[i])) {
				ok = false;
				break;
			}

			id = id * 10 + (uint)(_sceneName[i] - '0');
		}

		if (ok && id < kNoScene) {
			_sceneID = (uint16)id;
		}
	}
}

Common::String BeginNamedStream::getRecordExtraInfo() const {
	return Common::String::format("stream '%s' at scene %u (%s), frame %u, mode %u %u %u %u, param %u",
		_streamName.c_str(), _sceneID, _sceneName.c_str(), _frameID,
		_mode[0], _mode[1], _mode[2], _mode[3], _param);
}

void BeginNamedStream::execute() {
	NancySceneState.getStreams().begin(_streamName, _sceneName, _sceneID, _frameID, _continueSceneSound);
	_isDone = true;
}

void AutoUTurn::readData(Common::SeekableReadStream &stream) {
	readRect(stream, _uTurnHotspot);
	_cursorType = stream.readUint16LE();
	_unknown18 = stream.readByte();

	const uint16 numFrames = stream.readUint16LE();
	_frames.resize(numFrames);
	for (uint i = 0; i < numFrames; ++i) {
		_frames[i] = stream.readUint16LE();
	}
}

void AutoUTurn::execute() {
	switch (_state) {
	case kBegin:
		_hotspot = _uTurnHotspot;
		_state = kRun;
		// fall through
	case kRun: {
		if (_frames.empty()) {
			// No frame list: the strip is live all the way round the node.
			_hasHotspot = true;
			break;
		}

		_hasHotspot = false;
		const uint16 curFrame = NancySceneState.getSceneInfo().frameID;
		for (uint i = 0; i < _frames.size(); ++i) {
			if (_frames[i] == curFrame) {
				_hasHotspot = true;
				break;
			}
		}

		break;
	}
	case kActionTrigger: {
		// GUESS: a U-turn is half a revolution of the panorama. Scenes whose
		// background is a single still have nothing to turn, so they are left
		// alone rather than being sent to frame 0.
		UI::Viewport &viewport = NancySceneState.getViewport();
		const uint frameCount = viewport.getFrameCount();
		if (frameCount > 1) {
			viewport.setFrame((viewport.getCurFrame() + frameCount / 2) % frameCount);
		}

		finishExecution();
		break;
	}
	}
}

} // End of namespace Action
} // End of namespace Nancy
