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

#include "engines/nancy/cursor.h"
#include "engines/nancy/graphics.h"
#include "engines/nancy/input.h"
#include "engines/nancy/nancy.h"
#include "engines/nancy/resource.h"
#include "engines/nancy/sound.h"
#include "engines/nancy/util.h"

#include "engines/nancy/state/scene.h"
#include "engines/nancy/action/puzzle/paperdollpuzzle.h"

namespace Nancy {
namespace Action {

void PaperDollPuzzle::readData(Common::SeekableReadStream &stream) {
	const int64 start = stream.pos();

	readFilename(stream, _imageName);

	// Two empty names and the metrics block. Only the doll rect that follows is
	// used, and seeking to it beats naming fields that are the same in all
	// twelve records and so cannot be told apart.
	stream.seek(start + 215);
	readRect(stream, _dollDest);

	stream.seek(start + 256);
	const uint16 numGarments = stream.readUint16LE();

	_garments.resize(numGarments);
	for (uint i = 0; i < numGarments; ++i) {
		Garment &g = _garments[i];
		readRect(stream, g.src);
		stream.skip(2);				// -1 in every garment
		readRect(stream, g.rackDest);
		readRect(stream, g.hotspot);
		stream.skip(16);			// the part of src matching the hotspot
		stream.skip(16);			// src again
		stream.skip(48);			// three zero RECTs
		stream.skip(5);
		g.everWornFlag = stream.readSint16LE();
		g.wornFlag = stream.readSint16LE();
		stream.skip(4);
	}

	_rustleSound.readData(stream);

	// The second copy of the same eight names, on the next channel up, then four
	// more sound blocks that say "NO SOUND" or repeat the rustle. Nothing in the
	// scene distinguishes them, and the whole tail is byte-identical in all
	// twelve records, so only the first is played.
	RandomSoundBlock ignored;
	ignored.readData(stream);
	ignored.readData(stream);
	ignored.readData(stream);
	stream.skip(4);
	stream.skip(7);					// scene 32767 ("no scene"), frame 0, flag -1
	ignored.readData(stream);
	stream.skip(7);
	ignored.readData(stream);

	const uint16 numDrawers = stream.readUint16LE();
	_drawers.resize(numDrawers);
	for (uint i = 0; i < numDrawers; ++i) {
		Drawer &d = _drawers[i];
		readRect(stream, d.hotspot);
		stream.skip(2);
		d.scene.sceneID = stream.readUint16LE();
		if (d.scene.sceneID == kNancy16NoScene) {
			d.scene.sceneID = kNoScene;
		}

		d.flag.label = stream.readSint16LE();
		d.flag.flag = stream.readByte();
	}
}

void PaperDollPuzzle::init() {
	Common::Rect vpBounds = NancySceneState.getViewport().getBounds();
	_drawSurface.create(vpBounds.width(), vpBounds.height(), g_nancy->_graphics->getInputPixelFormat());
	_drawSurface.clear(g_nancy->_graphics->getTransColor());
	setTransparent(true);
	setVisible(true);
	moveTo(vpBounds);

	g_nancy->_resource->loadImage(_imageName, _image);
	_image.setTransparentColor(_drawSurface.getTransparentColor());

	redraw();
}

int PaperDollPuzzle::wornGarment() const {
	for (uint i = 0; i < _garments.size(); ++i) {
		if (_garments[i].wornFlag != kEvNoEvent &&
				NancySceneState.getEventFlag(_garments[i].wornFlag, g_nancy->_true)) {
			return (int)i;
		}
	}

	return -1;
}

int PaperDollPuzzle::garmentAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _garments.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_garments[i].hotspot).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

int PaperDollPuzzle::drawerAtCursor(const Common::Point &mousePos) const {
	for (uint i = 0; i < _drawers.size(); ++i) {
		if (NancySceneState.getViewport().convertViewportToScreen(_drawers[i].hotspot).contains(mousePos)) {
			return (int)i;
		}
	}

	return -1;
}

void PaperDollPuzzle::redraw() {
	_drawSurface.clear(g_nancy->_graphics->getTransColor());

	// The rack. Every garment in the drawer is always on show, worn or not - the
	// original draws the whole drawerful and marks the chosen one by putting it
	// on the doll, not by taking it off the rack.
	for (uint i = 0; i < _garments.size(); ++i) {
		_drawSurface.blitFrom(_image, _garments[i].src,
			Common::Point(_garments[i].rackDest.left, _garments[i].rackDest.top));
	}

	// The doll. One garment per category, at the single destination the record
	// carries - which is why the record has one doll rect and not one per
	// garment: a hat and a pair of shoes are different records.
	const int worn = wornGarment();
	if (worn >= 0) {
		_drawSurface.blitFrom(_image, _garments[worn].src,
			Common::Point(_dollDest.left, _dollDest.top));
	}

	_lastDrawnWorn = worn;
	setNeedsRedraw(true);
}

void PaperDollPuzzle::playRustle() {
	if (_rustleSound.names.empty()) {
		return;
	}

	const uint idx = _rustleSound.names.size() == 1 ?
		0 : g_nancy->_randomSource->getRandomNumber(_rustleSound.names.size() - 1);
	const Common::String &name = _rustleSound.names[idx];
	if (name.empty() || name == "NO SOUND") {
		return;
	}

	SoundDescription desc;
	desc.name = name;
	desc.channelID = _rustleSound.channel;
	desc.numLoops = _rustleSound.numLoops > 0 ? _rustleSound.numLoops : 1;
	desc.volume = _rustleSound.volume;

	g_nancy->_sound->loadSound(desc);
	g_nancy->_sound->playSound(desc);
}

void PaperDollPuzzle::execute() {
	switch (_state) {
	case kBegin:
		init();
		registerGraphics();
		_state = kRun;
		// fall through
	case kRun:
		if (_drawerRequested) {
			_state = kActionTrigger;
			break;
		}

		// The worn flag can be cleared from outside this record - a load, or the
		// scene's own logic - so the drawing follows the flags rather than only
		// being refreshed by our own clicks.
		if (wornGarment() != _lastDrawnWorn) {
			redraw();
		}

		break;
	case kActionTrigger:
		NancySceneState.setEventFlag(_pendingFlag);
		NancySceneState.changeScene(_pendingScene);
		finishExecution();
		break;
	}
}

void PaperDollPuzzle::handleInput(NancyInput &input) {
	if (_state != kRun || _drawerRequested) {
		return;
	}

	const bool click = (input.input & NancyInput::kLeftMouseButtonUp) != 0;

	// -- The rack: put a garment on, or take it off again. --
	const int garment = garmentAtCursor(input.mousePos);
	if (garment != -1) {
		g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

		if (click) {
			const bool wasWorn = _garments[garment].wornFlag != kEvNoEvent &&
				NancySceneState.getEventFlag(_garments[garment].wornFlag, g_nancy->_true);

			// One garment per category. Clearing the others is what keeps two
			// tops from being drawn over each other on the doll, here and in the
			// type 52 overlays of every other scene in the armoire.
			for (uint i = 0; i < _garments.size(); ++i) {
				if (_garments[i].wornFlag != kEvNoEvent) {
					NancySceneState.setEventFlag(_garments[i].wornFlag, g_nancy->_false);
				}
			}

			if (!wasWorn) {
				NancySceneState.setEventFlag(_garments[garment].wornFlag, g_nancy->_true);

				// The second flag is a latch, never cleared: S3600 turns each one
				// into a flag in the 2715+ range, which is how the game remembers
				// what Nancy has tried on.
				NancySceneState.setEventFlag(_garments[garment].everWornFlag, g_nancy->_true);
			}

			playRustle();
			redraw();
			input.eatMouseInput();
		}

		return;
	}

	// -- The drawer fronts, and the strip along the bottom that closes up. --
	const int drawer = drawerAtCursor(input.mousePos);
	if (drawer != -1) {
		g_nancy->_cursor->setCursorType(CursorManager::kHotspot);

		if (click) {
			_pendingScene = _drawers[drawer].scene;
			_pendingFlag = _drawers[drawer].flag;
			_drawerRequested = true;
			input.eatMouseInput();
		}
	}
}

} // End of namespace Action
} // End of namespace Nancy
