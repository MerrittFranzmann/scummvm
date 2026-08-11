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
#include "engines/nancy/action/puzzle/mosaicpuzzle.h"

namespace Nancy {
namespace Action {

// -- debug affordance -----------------------------------------------------------
//
// nancy_mosaic_autoplay drops the gems the record itself names, one hole per
// frame, through exactly the state a click would leave behind: it does not skip
// the win check, the player-table tally, the solve sound, the solve flag or the
// scene change - isSolved() still has to come out true on its own. The tally in
// writeProgressToPlayerTable() is what flag 2571 (and therefore S3251's only
// exit) depends on, so short-circuiting to the solve branch instead would have
// reproduced the S3251 soft-lock; going through the tiles does not.
//
// Placement is silent under autoplay: a board is 84-102 holes and one filled per
// frame would restart the place sound sixty times a second on channel 14. The
// solve sound still plays, so the kWaitForSound handshake is unchanged.
//
// Defaults off, so ordinary play is byte-for-byte what it was.
bool MosaicPuzzle::autoPlay() {
	return ConfMan.hasKey("nancy_mosaic_autoplay") && ConfMan.getBool("nancy_mosaic_autoplay");
}

void MosaicPuzzle::autoStep() {
	for (uint i = 0; i < _tileColour.size(); ++i) {
		if (_tileColour[i] == _tileSolution[i]) {
			continue;
		}

		// Emptying a hole is only legal while carrying the colour already in it,
		// which is the rule handleInput() enforces; mirror it rather than bypass it.
		setHeldColour(_tileSolution[i] == kNoColour ? _tileColour[i] : _tileSolution[i]);
		_tileColour[i] = _tileSolution[i];
		redraw();
		return;
	}
}

// The three sound pools are the shared Nancy16 "counted names, then the settings
// they share" block, which RandomSoundBlock already reads.
void MosaicPuzzle::readData(Common::SeekableReadStream &stream) {
	readFilename(stream, _puzzleName);
	readFilename(stream, _imageName);
	stream.skip(33);	// The atlas name a second time; always identical

	_gridMetricA = stream.readUint16LE();
	_gridMetricB = stream.readUint16LE();
	stream.skip(24);	// Six int32, constant across all four puzzles
	_numWrongTableIndex = stream.readUint16LE();
	_numCorrectTableIndex = stream.readUint16LE();

	const uint16 numColours = stream.readUint16LE();
	stream.skip(1);

	_swatchDests.resize(numColours);
	_swatchSrcs.resize(numColours);
	for (uint i = 0; i < numColours; ++i) {
		stream.skip(3);
		readRect(stream, _swatchDests[i]);
		readRect(stream, _swatchSrcs[i]);
		stream.skip(19);
	}

	const uint16 numTiles = stream.readUint16LE();
	_tileRects.resize(numTiles);
	_tileSolution.resize(numTiles);
	for (uint i = 0; i < numTiles; ++i) {
		readRect(stream, _tileRects[i]);
		stream.skip(2);	// Always -1
		_tileSolution[i] = stream.readSint16LE();
	}

	stream.skip(3);

	_pickUpSound.readData(stream);
	_placeSound.readData(stream);

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

void MosaicPuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	g_nancy->_resource->loadImage(_imageName, _image);
	_image.setTransparentColor(_drawSurface.getTransparentColor());

	// Every hole starts empty; the background art is what shows through.
	_tileColour.resize(_tileRects.size());
	for (uint i = 0; i < _tileColour.size(); ++i) {
		_tileColour[i] = kNoColour;
	}

	// The carried gem is the size of a tray swatch, which is also the size of a hole.
	uint16 gemW = 0, gemH = 0;
	if (!_swatchSrcs.empty()) {
		gemW = _swatchSrcs[0].width();
		gemH = _swatchSrcs[0].height();
	}

	if (gemW && gemH) {
		_heldGem._drawSurface.create(gemW, gemH, g_nancy->_graphics->getInputPixelFormat());
		_heldGem._drawSurface.clear(g_nancy->_graphics->getTransColor());
		_heldGem.setTransparent(true);
	}

	_heldGem.setVisible(false);

	NancySceneState.setNoHeldItem();

	redraw();
}

void MosaicPuzzle::registerGraphics() {
	_heldGem.registerGraphics();
	RenderObject::registerGraphics();
}

int MosaicPuzzle::swatchAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _swatchDests.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_swatchDests[i]).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

int MosaicPuzzle::tileAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _tileRects.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_tileRects[i]).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

// The record does not resolve the puzzle by itself: it keeps a running tally of the
// grid in two player-table slots, and the scene's sibling records watch those slots.
// In S3252 that sibling is record [1], a type-18 SceneChangeWithFlags gated on
// "[11] >= 94 && [10] <= 0", and it raises *two* flags - the panel-solved flag 2519
// and flag 2571. 2571 has no other writer on the solve path, and S3251 (the opened
// mosaic) needs it for its exit and for all five of its UI Control records, so a
// puzzle that never publishes the tally strands the player in S3251 with zero
// hotspots. Publish it every frame; the grid is at most 102 tiles.
void MosaicPuzzle::writeProgressToPlayerTable() {
	TableData *playerTable = (TableData *)NancySceneState.getPuzzleData(TableData::getTag());
	if (!playerTable) {
		return;
	}

	uint16 numCorrect = 0;
	for (uint i = 0; i < _tileColour.size(); ++i) {
		if (_tileColour[i] == _tileSolution[i]) {
			++numCorrect;
		}
	}

	// "Wrong" is every hole that does not yet match, empty ones included. Counting
	// only mis-coloured holes would let "[10] <= 0" pass with holes still empty,
	// firing the solve while the mosaic is unfinished.
	playerTable->setSingleValue(_numWrongTableIndex, (int16)(_tileColour.size() - numCorrect));
	playerTable->setSingleValue(_numCorrectTableIndex, (int16)numCorrect);
}

bool MosaicPuzzle::isSolved() const {
	for (uint i = 0; i < _tileColour.size(); ++i) {
		if (_tileColour[i] != _tileSolution[i]) {
			return false;
		}
	}

	return !_tileColour.empty();
}

void MosaicPuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	// The tray. The background still draws the gems too, but the record carries
	// both a source and a destination rect for each one, so it owns them.
	for (uint i = 0; i < _swatchDests.size(); ++i) {
		_drawSurface.blitFrom(_image, _swatchSrcs[i], Common::Point(_swatchDests[i].left, _swatchDests[i].top));
	}

	// Filled holes. Empty ones draw nothing and show the mosaic underneath.
	for (uint i = 0; i < _tileColour.size(); ++i) {
		int16 colour = _tileColour[i];
		if (colour < 0 || colour >= (int16)_swatchSrcs.size()) {
			continue;
		}

		_drawSurface.blitFrom(_image, _swatchSrcs[colour], Common::Point(_tileRects[i].left, _tileRects[i].top));
	}

	setNeedsRedraw(true);
}

void MosaicPuzzle::setHeldColour(int16 colour) {
	_heldColour = colour;

	if (colour < 0 || colour >= (int16)_swatchSrcs.size() || _heldGem._drawSurface.empty()) {
		_heldGem.setVisible(false);
		_heldGem.putDown();
		return;
	}

	_heldGem._drawSurface.clear(g_nancy->_graphics->getTransColor());
	_heldGem._drawSurface.blitFrom(_image, _swatchSrcs[colour], Common::Point());
	_heldGem.setTransparent(true);
	_heldGem.setVisible(true);
	_heldGem.pickUp();
	_heldGem.setNeedsRedraw(true);
}

SoundDescription MosaicPuzzle::playSoundBlock(const RandomSoundBlock &block) {
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

void MosaicPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun:
		if (autoPlay() && _solveState == kNotSolved && !_exitRequested) {
			autoStep();
		}

		writeProgressToPlayerTable();

		if (_exitRequested) {
			_state = kActionTrigger;
			break;
		}

		switch (_solveState) {
		case kNotSolved:
			if (!isSolved()) {
				break;
			}

			setHeldColour(kNoColour);
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
	case kActionTrigger:
		if (_solveState == kWaitForSound) {
			NancySceneState.setEventFlag(_solveFlag);
			NancySceneState.changeScene(_solveScene);
		} else {
			// Leaving unsolved. In Nancy16 the exit scene is "no scene": the flag the
			// hotspot raises is what starts the leaving chain in the sibling records.
			NancySceneState.setEventFlag(_exitFlag);
			NancySceneState.changeScene(_exitScene);
		}

		setHeldColour(kNoColour);
		finishExecution();
		break;
	}
}

void MosaicPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _exitRequested || _solveState != kNotSolved) {
		return;
	}

	_heldGem.handleInput(input);

	const bool click = (input.input & NancyInput::kLeftMouseButtonUp) != 0;

	// -- The tray: pick a gem up, or swap the one already held. --
	int swatch = swatchAtCursor(input.mousePos);
	if (swatch != -1) {
		g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

		if (click) {
			setHeldColour((int16)swatch);
			playSoundBlock(_pickUpSound);
			input.eatMouseInput();
		}

		return;
	}

	// -- The mosaic: drop the held gem into a hole. Clicking a hole that already
	//    holds the carried colour takes the gem back out again, which is the only
	//    way to empty a hole (Mosaic3 has two that must be left empty). --
	int tile = tileAtCursor(input.mousePos);
	if (tile != -1 && _heldColour != kNoColour) {
		g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

		if (click) {
			_tileColour[tile] = (_tileColour[tile] == _heldColour) ? kNoColour : _heldColour;
			playSoundBlock(_placeSound);
			redraw();
			input.eatMouseInput();
		}

		return;
	}

	// -- Give up and leave. --
	if (!_exitHotspot.isEmpty() &&
			NancySceneState.getViewport().convertViewportToScreen(_exitHotspot).contains(input.mousePos)) {
		// The id in the data is a raw Nancy13-style cursor type, which is what the
		// "set from script" path expects.
		g_nancy->_cursor->setCursorType((CursorManager::CursorType)_exitCursorType, true);

		if (click) {
			_exitRequested = true;
			input.eatMouseInput();
		}
	}
}

} // End of namespace Action
} // End of namespace Nancy
